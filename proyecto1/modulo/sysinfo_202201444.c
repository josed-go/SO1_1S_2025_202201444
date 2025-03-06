#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/uaccess.h>
#include <linux/tty.h>
#include <linux/sched/signal.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/sched/mm.h>
#include <linux/binfmts.h>
#include <linux/timekeeping.h>
#include <linux/cpumask.h>
#include <linux/cpu.h>
#include <linux/kernel_stat.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jose Gongora");
MODULE_DESCRIPTION("Modulo Kernel");
MODULE_VERSION("1.0");

#define PROC_NAME "sysinfo_202201444"
#define CONTAINER_ID_LEN 64 // Longitud fija del ID del contenedor

// Función para obtener el ID del contenedor desde la línea de comando
static void get_container_id(const char *cmdline, char *container_id, int len) {
    // Buscar el ID del contenedor (64 caracteres hexadecimales)
    const char *id_start = strstr(cmdline, "-id ");
    if (id_start) {
        id_start += strlen("-id "); // Avanzar al inicio del ID
        if (strlen(id_start) >= CONTAINER_ID_LEN) {
            snprintf(container_id, len, "%.*s", CONTAINER_ID_LEN, id_start);
            printk(KERN_INFO "ID del contenedor extraído: %s\n", container_id);
        } else {
            printk(KERN_ERR "El ID del contenedor no tiene la longitud esperada\n");
            snprintf(container_id, len, "N/A");
        }
    } else {
        printk(KERN_ERR "No se encontró la cadena '-id' en la línea de comando\n");
        snprintf(container_id, len, "N/A");
    }
}

// Función para obtener el comando del contenedor desde la línea de comando
static void get_container_cmd(struct task_struct *task, char *cmd, int len) {
    struct mm_struct *mm;
    int res = 0;

    mm = get_task_mm(task);
    if (mm) {
        if (mm->arg_end) {
            res = access_process_vm(task, mm->arg_start, cmd, mm->arg_end - mm->arg_start, 0);
            if (res > 0) {
                cmd[res] = '\0';
                // Reemplazar los caracteres nulos con espacios
                for (int i = 0; i < res; i++) {
                    if (cmd[i] == '\0') cmd[i] = ' ';
                }
                printk(KERN_INFO "Comando del contenedor: %s\n", cmd);
            } else {
                printk(KERN_ERR "Error al leer la línea de comando del proceso\n");
                snprintf(cmd, len, "N/A");
            }
        } else {
            printk(KERN_ERR "No se pudo acceder a la línea de comando del proceso\n");
            snprintf(cmd, len, "N/A");
        }
        mmput(mm);
    } else {
        printk(KERN_ERR "No se pudo obtener la estructura mm del proceso\n");
        snprintf(cmd, len, "N/A");
    }
}

// Función para mostrar la información en /proc/sysinfo_202201444
static int sysinfo_show(struct seq_file *m, void *v) {
    struct sysinfo i;
    unsigned long totalram, freeram, usedram;
    unsigned long totalram_mb, freeram_mb, usedram_mb;
    unsigned long totalram_gb, freeram_gb, usedram_gb;
    unsigned long cpu_user = 0, cpu_system = 0, cpu_total = 0, idle_time = 0, total_time = 0, cpu_percentage = 0;
    int i_cpu;
    struct task_struct *task;
    char container_id[CONTAINER_ID_LEN + 1]; // +1 para el carácter nulo
    char container_cmd[256];

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

    seq_printf(m, "Memoria RAM Total: %lu KB (%lu MB, %lu GB)\n", totalram, totalram_mb, totalram_gb);
    seq_printf(m, "Memoria RAM Libre: %lu KB (%lu MB, %lu GB)\n", freeram, freeram_mb, freeram_gb);
    seq_printf(m, "Memoria RAM en Uso: %lu KB (%lu MB, %lu GB)\n", usedram, usedram_mb, usedram_gb);
    seq_printf(m, "Uso de CPU: %lu%%\n", cpu_percentage);

    seq_printf(m, "Procesos en ejecucion:\n");
    int count = 0;
    for_each_process(task) {
        if (strstr(task->comm, "containerd-shim")) {

            printk(KERN_INFO "Proceso encontrado: PID=%d, Nombre=%s\n", task->pid, task->comm);
            get_container_cmd(task, container_cmd, sizeof(container_cmd));
            get_container_id(container_cmd, container_id, sizeof(container_id));

            seq_printf(m, "PID: %d, Nombre: %s, ID Contenedor: %s, Comando: %s\n",
                    task->pid, task->comm, container_id, container_cmd);
            if (++count >= 10) break; // Limitar a 10 procesos
        }
    }

    return 0;
}

// Funcion que se ejecuta al abrir el archivo
static int sysinfo_open(struct inode *inode, struct file *file){
    return single_open(file, sysinfo_show, NULL);
};

// Estructura para definir el archivo
static const struct proc_ops sysinfo_ops = {
    .proc_open = sysinfo_open,
    .proc_read = seq_read,
    .proc_lseek = seq_lseek,
    .proc_release = single_release,
};

static int __init init_sysinfo(void) {
    // Crear el archivo en /proc
    if (!proc_create(PROC_NAME, 0, NULL, &sysinfo_ops)) {
        printk(KERN_ERR "Error al crear el archivo en /proc\n");
        return -ENOMEM;
    }
    printk(KERN_INFO "Modulo cargado correctamente\n");
    return 0;
};

static void __exit exit_sysinfo(void) {
    remove_proc_entry(PROC_NAME, NULL);
    printk(KERN_INFO "Modulo eliminado correctamente\n");
}


module_init(init_sysinfo);
module_exit(exit_sysinfo);