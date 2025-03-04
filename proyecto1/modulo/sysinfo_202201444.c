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

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jose Gongora");
MODULE_DESCRIPTION("Modulo Kernel");
MODULE_VERSION("1.0");

#define PROC_NAME "sysinfo_202201444"

// Funcion para mostrar la informacion en /proc/sysinfo_202201444
static int sysinfo_show(struc seq_file *m, void *v){
    struct task_struct *task;
    
    for_each_process(task){
        if(strstr(task->comm, "container-shim")){
            seq_printf(m, "PID: %d, Nombre: %s\n", task->pid, task->comm);
        }
    }

    return 0;
}

// Funcion que se ejecuta al abrir el archivo
static int sysinfo_open(struct inode *inode, struct file *file){
    return single_open(file, sysinfo_show, NULL);
}

// Estructura para definir el archivo
static const struct file_operations sysinfo_ops = {
    .owner = THIS_MODULE,
    .open = sysinfo_open,
    .read = seq_read,
    .llseek = seq_lseek,
    .release = single_release,
}

static int __init init_sysinfo(void) {
    proc_create(PROC_NAME, 0, NULL, &sysinfo_ops);
    printk(KERN_INFO "Modulo cargado correctamente");
    return 0;
}

static void __exit exit_sysinfo(void) {
    printk(KERN_INFO "Modulo eliminado correctamente");
}

module_init(init_sysinfo);
module_exit(exit_sysinfo);