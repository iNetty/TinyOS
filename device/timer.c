#include "timer.h"
#include "io.h"
#include "print.h"
#include "interrupt.h"
#include "debug.h"
#include "thread.h"

// IRQ0_FREQUENCY : IRQ0 (clock intr) frequency 
// notice COUNTER0_VALUE<65535.  When COUNTER0_VALUE==0,means 65536
#define IRQ0_FREQUENCY	   100          
#define INPUT_FREQUENCY	   1193180
#define COUNTER0_VALUE	   (INPUT_FREQUENCY / IRQ0_FREQUENCY)
#define CONTRER0_PORT	   0x40
#define COUNTER0_NO	   0
#define COUNTER_MODE	   2
#define READ_WRITE_LATCH   3
#define PIT_CONTROL_PORT   0x43

#define mil_seconds_per_intr (1000 / IRQ0_FREQUENCY)

// TODO : canary
//extern uint32_t canary;
//uint32_t cannary=0x23198408;

uint32_t ticks;   //total tick since interrupt enable

/* init CONTROL reg with counter_no,read write lock:rwl,counter_mode,counter_value */
static void frequency_set(uint8_t counter_port, 
			  uint8_t counter_no, 
			  uint8_t rwl, 
			  uint8_t counter_mode, 
			  uint16_t counter_value) {
/* write to control port 0x43 */
   outb(PIT_CONTROL_PORT, (uint8_t)(counter_no << 6 | rwl << 4 | counter_mode << 1));
/* counter_value low 8bit */
   outb(counter_port, (uint8_t)counter_value);
/* counter_value high 8bit */
   outb(counter_port, (uint8_t)counter_value >> 8);
}

static void intr_timer_handler(void){
   PTASK_STRUCT cur_thread=get_running_thread();

   // TODO ----------------------------- 
   //verify canary.
   //ASSERT(cur_thread->canary==canary);      //cannary stack check
   cur_thread->elapsed_ticks++;
   ticks++;
   if(cur_thread->ticks==0){
      schedule();
   }else{
      cur_thread->ticks--;
   }
}

// sleep ticks
static void ticks_to_sleep(uint32_t sleep_ticks) {
   uint32_t start_tick = ticks;

   // yield until ticks is large enough
   while (ticks - start_tick < sleep_ticks) {
      thread_yield();
   }
}

// sleep millisecond
void sleep(uint32_t m_seconds) {
  uint32_t sleep_ticks = DIV_ROUND_UP(m_seconds, mil_seconds_per_intr);
  ASSERT(sleep_ticks > 0);
  ticks_to_sleep(sleep_ticks); 
}

/* 初始化PIT8253 */
void timer_init() {
   put_str("timer init start\n");
   /* set 8253 intrrupt interval */
   frequency_set(CONTRER0_PORT, COUNTER0_NO, READ_WRITE_LATCH, COUNTER_MODE, COUNTER0_VALUE);
   register_handler(0x20,intr_timer_handler);
   put_str("timer init done\n");
}