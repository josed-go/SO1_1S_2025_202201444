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

// Funcion para mostrar la informacion en /proc/sysinfo_202201444
static int sysinfo_show(struct seq_file *m, void *v){
    struct task_struct *task;

    seq_printf(m, "Procesos en ejecución:\n");
    
    for_each_process(task){
        if(strstr(task->comm, "containerd-shim")){
            seq_printf(m, "PID: %d, Nombre: %s\n", task->pid, task->comm);
        }
    }

    return 0;
};

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
    proc_create(PROC_NAME, 0, NULL, &sysinfo_ops);
    printk(KERN_INFO "Modulo cargado correctamente");
    return 0;
};

static void __exit exit_sysinfo(void) {
    remove_proc_entry(PROC_NAME, NULL);
    printk(KERN_INFO "Modulo eliminado correctamente\n");
}


module_init(init_sysinfo);
module_exit(exit_sysinfo);