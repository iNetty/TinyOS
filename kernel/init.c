#include "print.h"
#include "init.h"
#include "interrupt.h"
#include "timer.h"
#include "memory.h"

//INIT all 
void init_all(){
    put_str("start init all\n");
    idt_init();
    timer_init();
    mem_init();
    put_str("finish init all\n");
}