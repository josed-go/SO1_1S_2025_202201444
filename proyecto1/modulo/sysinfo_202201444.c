#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/jiffies.h>
#include <linux/uaccess.h>
#include <linux/sched/signal.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/sched/mm.h>
#include <linux/timekeeping.h>
#include <linux/cpumask.h>
#include <linux/cpu.h>
#include <linux/kernel_stat.h>
#include <linux/cgroup.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jose Gongora");
MODULE_DESCRIPTION("Modulo Kernel");
MODULE_VERSION("1.0");

#define PROC_NAME "sysinfo_202201444"
#define CONTAINER_ID_LEN 64

struct cpu_usage_data {
    pid_t pid;
    unsigned long prev_usage; // Uso previo en microsegundos
    unsigned long prev_time;  // Tiempo previo en microsegundos
};

struct io_stats {
    unsigned long rbytes; // Bytes leídos
    unsigned long wbytes; // Bytes escritos
};

static struct cpu_usage_data *cpu_data = NULL;
static int cpu_data_count = 0;
static DEFINE_SPINLOCK(cpu_data_lock); // Para proteger cpu_data

static void get_container_cmd(struct task_struct *task, char *cmd, int len) {
    struct mm_struct *mm = get_task_mm(task);
    if (mm) {
        if (mm->arg_end > mm->arg_start) {
            int res = access_process_vm(task, mm->arg_start, cmd, mm->arg_end - mm->arg_start, 0);
            if (res > 0) {
                cmd[res] = '\0';
                for (int i = 0; i < res; i++) {
                    if (cmd[i] == '\0') cmd[i] = ' ';
                }
            } else {
                snprintf(cmd, len, "N/A");
            }
        } else {
            snprintf(cmd, len, "N/A");
        }
        mmput(mm);
    } else {
        snprintf(cmd, len, "N/A");
    }
}

static char* get_cgroup_path(struct task_struct *task) {
    struct cgroup *cgrp;
    char *path;

    rcu_read_lock();
    cgrp = task_cgroup(task, cpu_cgrp_id);
    if (!cgrp || !cgrp->kn) {
        rcu_read_unlock();
        return NULL;
    }

    path = kmalloc(PATH_MAX, GFP_KERNEL);
    if (!path) {
        rcu_read_unlock();
        return NULL;
    }

    snprintf(path, PATH_MAX, "/sys/fs/cgroup");
    if (!kernfs_path(cgrp->kn, path + strlen("/sys/fs/cgroup"), PATH_MAX - strlen("/sys/fs/cgroup"))) {
        kfree(path);
        rcu_read_unlock();
        return NULL;
    }

    rcu_read_unlock();
    return path;
}

static void get_container_id(struct task_struct *task, char *container_id, int len) {
    char *cgroup_path = get_cgroup_path(task);
    if (!cgroup_path) {
        snprintf(container_id, len, "N/A");
        return;
    }

    // Buscar el último componente de la ruta del cgroup, que suele ser el ID del contenedor
    char *id_start = strrchr(cgroup_path, '/');
    if (id_start && id_start != cgroup_path) {
        id_start++; // Saltar el '/'
        // Algunos sistemas prependen un prefijo como "docker-" o "podman-", lo quitamos si existe
        char *id_real_start = strstr(id_start, "docker-");
        if (id_real_start) {
            id_start = id_real_start + strlen("docker-");
        } else {
            id_real_start = strstr(id_start, "podman-");
            if (id_real_start) {
                id_start = id_real_start + strlen("podman-");
            }
        }
        snprintf(container_id, len, "%.*s", min(CONTAINER_ID_LEN, (int)strlen(id_start)), id_start);
    } else {
        snprintf(container_id, len, "N/A");
    }

    kfree(cgroup_path);
}

static unsigned long get_task_memory_usage(struct task_struct *task) {
    char *cgroup_path;
    char *mem_file_path;
    struct file *filp = NULL;
    char buffer[32];
    unsigned long memory_usage = 0;
    loff_t pos = 0;
    ssize_t ret;

    cgroup_path = get_cgroup_path(task);
    if (!cgroup_path) {
        printk(KERN_ERR "Failed to get cgroup path\n");
        return 0;
    }

    mem_file_path = kmalloc(PATH_MAX, GFP_KERNEL);
    if (!mem_file_path) {
        kfree(cgroup_path);
        printk(KERN_ERR "Failed to allocate memory for mem_file_path\n");
        return 0;
    }

    snprintf(mem_file_path, PATH_MAX, "%s/memory.current", cgroup_path);
    kfree(cgroup_path);

    filp = filp_open(mem_file_path, O_RDONLY, 0);
    if (IS_ERR(filp)) {
        printk(KERN_ERR "Failed to open %s: %ld\n", mem_file_path, PTR_ERR(filp));
        kfree(mem_file_path);
        return 0;
    }

    ret = kernel_read(filp, buffer, sizeof(buffer) - 1, &pos);
    if (ret > 0) {
        buffer[ret] = '\0';
        if (kstrtoul(buffer, 10, &memory_usage)) {
            printk(KERN_ERR "Failed to parse memory usage\n");
            memory_usage = 0;
        }
    } else {
        printk(KERN_ERR "Failed to read memory.current: %ld\n", ret);
    }

    filp_close(filp, NULL);
    kfree(mem_file_path);

    return memory_usage;
}

static unsigned long get_task_disk_usage(struct task_struct *task) {
    char *cgroup_path;
    char *io_file_path;
    struct file *filp = NULL;
    char buffer[256];
    unsigned long disk_usage = 0;
    loff_t pos = 0;
    ssize_t ret;

    cgroup_path = get_cgroup_path(task);
    if (!cgroup_path) {
        printk(KERN_ERR "Failed to get cgroup path for disk usage\n");
        return 0;
    }

    io_file_path = kmalloc(PATH_MAX, GFP_KERNEL);
    if (!io_file_path) {
        kfree(cgroup_path);
        printk(KERN_ERR "Failed to allocate memory for io_file_path\n");
        return 0;
    }

    snprintf(io_file_path, PATH_MAX, "%s/io.stat", cgroup_path);
    kfree(cgroup_path);

    filp = filp_open(io_file_path, O_RDONLY, 0);
    if (IS_ERR(filp)) {
        printk(KERN_ERR "Failed to open %s: %ld\n", io_file_path, PTR_ERR(filp));
        kfree(io_file_path);
        return 0;
    }

    ret = kernel_read(filp, buffer, sizeof(buffer) - 1, &pos);
    if (ret > 0) {
        buffer[ret] = '\0';
        char *line = buffer;
        while (line) {
            unsigned long rbytes = 0, wbytes = 0;
            sscanf(line, "%*s rbytes=%lu wbytes=%lu", &rbytes, &wbytes);
            disk_usage += rbytes + wbytes;
            line = strchr(line, '\n');
            if (line) line++;
        }
    } else {
        printk(KERN_ERR "Failed to read io.stat: %ld\n", ret);
    }

    filp_close(filp, NULL);
    kfree(io_file_path);

    return disk_usage;
}

static unsigned long get_task_cpu_usage(struct task_struct *task, unsigned long *prev_usage, unsigned long *prev_time) {
    char *cgroup_path = get_cgroup_path(task);
    char *cpu_file_path;
    struct file *filp;
    char buffer[256];
    unsigned long usage_usec = 0;
    loff_t pos = 0;
    unsigned long current_time = ktime_to_us(ktime_get());

    if (!cgroup_path) return 0;

    cpu_file_path = kmalloc(PATH_MAX, GFP_KERNEL);
    if (!cpu_file_path) {
        kfree(cgroup_path);
        return 0;
    }

    snprintf(cpu_file_path, PATH_MAX, "%s/cpu.stat", cgroup_path);
    kfree(cgroup_path);

    filp = filp_open(cpu_file_path, O_RDONLY, 0);
    if (IS_ERR(filp)) {
        printk(KERN_ERR "Failed to open %s: %ld\n", cpu_file_path, PTR_ERR(filp));
        kfree(cpu_file_path);
        return 0;
    }

    if (kernel_read(filp, buffer, sizeof(buffer) - 1, &pos) > 0) {
        buffer[pos] = '\0';
        char *line = buffer;
        while (line) {
            if (strncmp(line, "usage_usec", 10) == 0) {
                sscanf(line, "usage_usec %lu", &usage_usec);
                break;
            }
            line = strchr(line, '\n');
            if (line) line++;
        }
    }

    filp_close(filp, NULL);
    kfree(cpu_file_path);

    if (*prev_time == 0) {
        *prev_time = current_time;
        *prev_usage = usage_usec;
        return 0; // Primera medición
    }

    unsigned long time_diff = current_time - *prev_time;
    unsigned long usage_diff = usage_usec - *prev_usage;
    unsigned long cpu_percentage = 0;

    if (time_diff > 0) {
        // Multiplicamos por 10000 para mantener 2 decimales (100 = porcentaje, 100 = 2 decimales)
        cpu_percentage = (usage_diff * 10000) / time_diff;
        cpu_percentage = min(cpu_percentage, 10000UL * num_online_cpus()); // Límite: 100.00% por CPU
    }

    *prev_time = current_time;
    *prev_usage = usage_usec;

    return cpu_percentage; // Ahora en formato 9379 para 93.79%
}

static struct io_stats get_task_io_stats(struct task_struct *task) {
    struct io_stats stats = {0, 0};
    char *cgroup_path = get_cgroup_path(task);
    char *io_file_path;
    struct file *filp;
    char buffer[256];
    loff_t pos = 0;

    if (!cgroup_path) {
        printk(KERN_ERR "Failed to get cgroup path for I/O stats\n");
        return stats;
    }

    io_file_path = kmalloc(PATH_MAX, GFP_KERNEL);
    if (!io_file_path) {
        kfree(cgroup_path);
        printk(KERN_ERR "Failed to allocate memory for io_file_path\n");
        return stats;
    }

    snprintf(io_file_path, PATH_MAX, "%s/io.stat", cgroup_path);
    kfree(cgroup_path);

    filp = filp_open(io_file_path, O_RDONLY, 0);
    if (IS_ERR(filp)) {
        printk(KERN_ERR "Failed to open %s: %ld\n", io_file_path, PTR_ERR(filp));
        kfree(io_file_path);
        return stats;
    }

    if (kernel_read(filp, buffer, sizeof(buffer) - 1, &pos) > 0) {
        buffer[pos] = '\0';
        char *line = buffer;
        while (line) {
            unsigned long rbytes = 0, wbytes = 0;
            sscanf(line, "%*s rbytes=%lu wbytes=%lu", &rbytes, &wbytes);
            stats.rbytes += rbytes;
            stats.wbytes += wbytes;
            line = strchr(line, '\n');
            if (line) line++;
        }
    } else {
        printk(KERN_ERR "Failed to read io.stat: %lld\n", pos); // Corregido a %lld
    }

    filp_close(filp, NULL);
    kfree(io_file_path);
    return stats;
}

static int sysinfo_show(struct seq_file *m, void *v) {
    struct sysinfo i;
    unsigned long totalram, freeram, usedram;
    unsigned long totalram_mb, freeram_mb, usedram_mb;
    unsigned long totalram_gb, freeram_gb, usedram_gb;
    unsigned long cpu_user = 0, cpu_system = 0, cpu_total = 0, idle_time = 0, total_time = 0, cpu_percentage = 0;
    int i_cpu;
    struct task_struct *task;
    int count = 0;

    // Asignar memoria dinámica para las variables grandes
    char (*processed_container_ids)[CONTAINER_ID_LEN + 1] = kmalloc_array(10, CONTAINER_ID_LEN + 1, GFP_KERNEL);
    if (!processed_container_ids) {
        return -ENOMEM;
    }
    int processed_count = 0;

    char *container_cmd = kmalloc(256, GFP_KERNEL);
    if (!container_cmd) {
        kfree(processed_container_ids);
        return -ENOMEM;
    }

    char *container_id = kmalloc(CONTAINER_ID_LEN + 1, GFP_KERNEL);
    if (!container_id) {
        kfree(container_cmd);
        kfree(processed_container_ids);
        return -ENOMEM;
    }

    si_meminfo(&i);
    totalram = i.totalram * i.mem_unit / 1024;
    freeram = i.freeram * i.mem_unit / 1024;
    usedram = totalram - freeram;

    totalram_mb = totalram / 1024;
    freeram_mb = freeram / 1024;
    usedram_mb = usedram / 1024;

    totalram_gb = totalram_mb / 1024;
    freeram_gb = freeram_mb / 1024;
    usedram_gb = usedram_mb / 1024;

    for_each_online_cpu(i_cpu) {
        cpu_user += kcpustat_cpu(i_cpu).cpustat[CPUTIME_USER];
        cpu_system += kcpustat_cpu(i_cpu).cpustat[CPUTIME_SYSTEM];
        idle_time += kcpustat_cpu(i_cpu).cpustat[CPUTIME_IDLE];
    }
    cpu_total = cpu_user + cpu_system;
    total_time = cpu_total + idle_time;
    if (total_time > 0) {
        cpu_percentage = (cpu_total * 100) / total_time;
    }

    seq_printf(m, "{\n");

    seq_printf(m, "  \"memory\": {\n");
    seq_printf(m, "    \"total\": {\"kb\": %lu, \"mb\": %lu, \"gb\": %lu},\n", totalram, totalram_mb, totalram_gb);
    seq_printf(m, "    \"free\": {\"kb\": %lu, \"mb\": %lu, \"gb\": %lu},\n", freeram, freeram_mb, freeram_gb);
    seq_printf(m, "    \"used\": {\"kb\": %lu, \"mb\": %lu, \"gb\": %lu}\n", usedram, usedram_mb, usedram_gb);
    seq_printf(m, "  },\n");

    seq_printf(m, "  \"cpu_usage_percent\": %lu,\n", cpu_percentage);

    seq_printf(m, "  \"processes\": [\n");

    spin_lock(&cpu_data_lock);
    for_each_process(task) {
        if (strstr(task->comm, "stress")) {
            get_container_id(task, container_id, CONTAINER_ID_LEN + 1);

            // Verificar si este container_id ya fue procesado
            int i;
            for (i = 0; i < processed_count; i++) {
                if (strcmp(processed_container_ids[i], container_id) == 0) {
                    goto skip_process;
                }
            }

            unsigned long memory_bytes = get_task_memory_usage(task);
            get_container_cmd(task, container_cmd, 256);

            unsigned long memory_kb = memory_bytes / 1024;
            unsigned long memory_percentage_100 = (memory_kb * 10000) / totalram;

            struct io_stats io = get_task_io_stats(task);
            unsigned long disk_mb = (io.rbytes + io.wbytes) / (1024 * 1024);
            unsigned long rbytes_mb = io.rbytes / (1024 * 1024);
            unsigned long wbytes_mb = io.wbytes / (1024 * 1024);

            unsigned long cpu_usage = 0;
            for (i = 0; i < cpu_data_count; i++) {
                if (cpu_data[i].pid == task->pid) {
                    cpu_usage = get_task_cpu_usage(task, &cpu_data[i].prev_usage, &cpu_data[i].prev_time);
                    break;
                }
            }
            if (i == cpu_data_count && count < 10) {
                cpu_data = krealloc(cpu_data, (cpu_data_count + 1) * sizeof(struct cpu_usage_data), GFP_ATOMIC);
                if (!cpu_data) {
                    spin_unlock(&cpu_data_lock);
                    kfree(container_id);
                    kfree(container_cmd);
                    kfree(processed_container_ids);
                    seq_printf(m, "  ]\n}\n");
                    return -ENOMEM;
                }
                cpu_data[cpu_data_count].pid = task->pid;
                cpu_data[cpu_data_count].prev_usage = 0;
                cpu_data[cpu_data_count].prev_time = 0;
                cpu_usage = get_task_cpu_usage(task, &cpu_data[cpu_data_count].prev_usage, &cpu_data[cpu_data_count].prev_time);
                cpu_data_count++;
            }

            if (count > 0) {
                seq_printf(m, ",\n");
            }

            char temp_cmd[256]; // Esta sigue siendo local, pero es temporal y pequeña
            int j, k = 0;
            for (j = 0; container_cmd[j] && k < sizeof(temp_cmd) - 2; j++) {
                if (container_cmd[j] == '"') {
                    temp_cmd[k++] = '\\';
                    temp_cmd[k++] = '"';
                } else {
                    temp_cmd[k++] = container_cmd[j];
                }
            }
            temp_cmd[k] = '\0';
            strncpy(container_cmd, temp_cmd, 256 - 1);
            container_cmd[256 - 1] = '\0';

            seq_printf(m, "    {\n");
            seq_printf(m, "      \"pid\": %d,\n", task->pid);
            seq_printf(m, "      \"name\": \"%s\",\n", task->comm);
            seq_printf(m, "      \"container_id\": \"%s\",\n", container_id);
            seq_printf(m, "      \"command\": \"%s\",\n", container_cmd);
            seq_printf(m, "      \"cpu_usage_percent\": %lu.%02lu,\n", cpu_usage / 100, cpu_usage % 100);
            seq_printf(m, "      \"memory_usage_percent\": %lu.%02lu,\n", memory_percentage_100 / 100, memory_percentage_100 % 100);
            seq_printf(m, "      \"disk_usage_mb\": %lu,\n", disk_mb);
            seq_printf(m, "      \"block_io\": {\"read_mb\": %lu, \"write_mb\": %lu}\n", rbytes_mb, wbytes_mb);
            seq_printf(m, "    }");

            // Agregar el container_id a la lista de procesados
            if (processed_count < 15) {
                strncpy(processed_container_ids[processed_count++], container_id, CONTAINER_ID_LEN + 1);
            }

            if (++count >= 15) break;
        }
skip_process:
        continue;
    }
    spin_unlock(&cpu_data_lock);

    // Liberar la memoria asignada
    kfree(container_id);
    kfree(container_cmd);
    kfree(processed_container_ids);

    seq_printf(m, "\n  ]\n}\n");

    return 0;
}

static int sysinfo_open(struct inode *inode, struct file *file) {
    return single_open(file, sysinfo_show, NULL);
}

static const struct proc_ops sysinfo_ops = {
    .proc_open = sysinfo_open,
    .proc_read = seq_read,
    .proc_lseek = seq_lseek,
    .proc_release = single_release,
};

static int __init init_sysinfo(void) {
    if (!proc_create(PROC_NAME, 0, NULL, &sysinfo_ops)) {
        printk(KERN_ERR "Error al crear el archivo en /proc\n");
        return -ENOMEM;
    }
    cpu_data = kmalloc(sizeof(struct cpu_usage_data), GFP_KERNEL);
    if (!cpu_data) {
        remove_proc_entry(PROC_NAME, NULL);
        return -ENOMEM;
    }
    cpu_data_count = 0;
    printk(KERN_INFO "Modulo cargado correctamente\n");
    return 0;
}

static void __exit exit_sysinfo(void) {
    remove_proc_entry(PROC_NAME, NULL);
    spin_lock(&cpu_data_lock);
    kfree(cpu_data);
    cpu_data = NULL;
    cpu_data_count = 0;
    spin_unlock(&cpu_data_lock);
    printk(KERN_INFO "Modulo eliminado correctamente\n");
}

module_init(init_sysinfo);
module_exit(exit_sysinfo);