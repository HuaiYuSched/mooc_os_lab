#include <defs.h>
#include <stdio.h>
#include <string.h>
#include <console.h>
#include <kdebug.h>
#include <picirq.h>
#include <trap.h>
#include <clock.h>
#include <intr.h>
#include <pmm.h>
#include <vmm.h>
#include <ide.h>
#include <swap.h>
#include <proc.h>
#include <fs.h>
#include <kmonitor.h>
#include <mp.h>
#include <lapic.h>
#include <sched.h>
#include <mmu.h>

int kern_init(void) __attribute__((noreturn));
void grade_backtrace(void);
static void lab1_switch_test(void);
static void mpmain(void) __attribute__((noreturn));
static void startothers(void);

int
kern_init(void) {
    extern char edata[], end[];
    memset(edata, 0, end - edata);

    cons_init();                // init the console

    const char *message = "(THU.CST) os is loading ...";
    cprintf("%s\n\n", message);

//    print_kerninfo();

//    grade_backtrace();
	mpinit();
	lapicinit();
    pmm_init();                 // init physical memory management

//	cprintf("\ncpu%d: starting ucore\n\n", current_cpu->id);
    pic_init();                 // init interrupt controller
    idt_init();                 // init interrupt descriptor table
	ioapicinit();

    vmm_init();                 // init virtual memory management
    sched_init();               // init scheduler
    proc_init();                // init process table
    
    ide_init();                 // init ide devices
    swap_init();                // init swap
    fs_init();                  // init fs
    
    clock_init();               // init clock interrupt
	startothers();
    intr_enable();              // enable irq interrupt


	mpmain();
    
 //   cpu_idle();                 // run idle process
}

void __attribute__((noinline))
grade_backtrace2(int arg0, int arg1, int arg2, int arg3) {
    mon_backtrace(0, NULL, NULL);
}

void __attribute__((noinline))
grade_backtrace1(int arg0, int arg1) {
    grade_backtrace2(arg0, (int)&arg0, arg1, (int)&arg1);
}

void __attribute__((noinline))
grade_backtrace0(int arg0, int arg1, int arg2) {
    grade_backtrace1(arg0, arg2);
}

void
grade_backtrace(void) {
    grade_backtrace0(0, (int)kern_init, 0xffff0000);
}

static void
lab1_print_cur_status(void) {
    static int round = 0;
    uint16_t reg1, reg2, reg3, reg4;
    asm volatile (
            "mov %%cs, %0;"
            "mov %%ds, %1;"
            "mov %%es, %2;"
            "mov %%ss, %3;"
            : "=m"(reg1), "=m"(reg2), "=m"(reg3), "=m"(reg4));
    cprintf("%d: @ring %d\n", round, reg1 & 3);
    cprintf("%d:  cs = %x\n", round, reg1);
    cprintf("%d:  ds = %x\n", round, reg2);
    cprintf("%d:  es = %x\n", round, reg3);
    cprintf("%d:  ss = %x\n", round, reg4);
    round ++;
}


static inline void
lgdt_cpu(struct segdesc *p, int size)
{
volatile ushort pd[3];

  pd[0] = size-1;
  pd[1] = (uint)p;
  pd[2] = (uint)p >> 16; 
  
  asm volatile("lgdt (%0)" : : "r" (pd));
}

static void load_gdt()
{
  struct cpu *c; 
  c = &cpus[cpunum()];
  c->gdt[SEG_KTEXT] = SEG(STA_X | STA_R, 0x0, 0xFFFFFFFF, DPL_KERNEL),
  c->gdt[SEG_KDATA] = SEG(STA_W, 0x0, 0xFFFFFFFF, DPL_KERNEL),
  c->gdt[SEG_UTEXT] = SEG(STA_X | STA_R, 0x0, 0xFFFFFFFF, DPL_USER),
  c->gdt[SEG_UDATA] = SEG(STA_W, 0x0, 0xFFFFFFFF, DPL_USER);
  c->gdt[SEG_TSS]   = SEGTSS(STS_T32A, (uintptr_t)&c->ts, sizeof(struct taskstate), DPL_KERNEL);
  load_esp0((uintptr_t)bootstacktop);
  c->ts.ts_ss0 = KERNEL_DS;
  lgdt_cpu(c->gdt,sizeof(c->gdt));

//  current_cpu = c;
  current_proc = 0;
}

// Other CPUs jump here from entryother.S.
static void
mpenter(void)
{
//  switchkvm(); 
  enable_paging();
  idt_init();
  lapicinit();
  mpmain();
}

// Common CPU setup code.
static void
mpmain(void)
{
  load_gdt();	//build a new GDT for each cpu
  cprintf("cpu%d: starting\n", current_cpu->id);
//  loadidt();       // load idt register
  xchg(&current_cpu->started, 1); // tell startothers() we're up
  cpu_idle(); 

}

pde_t entrypgdir[];  // For entry.S

// Start the non-boot (AP) processors.
static void
startothers(void)
{
  extern uchar _binary_entryother_start[], _binary_entryother_size[];
  uchar *code;
  struct cpu *c;
  char *stack;

  // Write entry code to unused memory at 0x7000.
  // The linker has placed the image of entryother.S in
  // _binary_entryother_start.
  code = p2v(0x7000);
  memmove(code, _binary_entryother_start, (uint32_t)_binary_entryother_size);

  for(c = cpus; c < cpus+ncpu; c++){
    if(c == cpus+cpunum())  // We've started already.
      continue;

    // Tell entryother.S what stack to use, where to enter, and what 
    // pgdir to use. We cannot use kpgdir yet, because the AP processor
    // is running in low  memory, so we use entrypgdir for the APs too.
    stack = alloc_page();
    *(void**)(code-4) = stack + KSTACKSIZE;
    *(void**)(code-8) = mpenter;
    *(int**)(code-12) = (void *) v2p(entrypgdir);

    lapicstartap(c->id, v2p(code));

    // wait for cpu to finish mpmain()
    while(c->started == 0)
      ;
  }
}

// Boot page table used in entry.S and entryother.S.
// Page directories (and page tables), must start on a page boundary,
// hence the "__aligned__" attribute.  
// Use PTE_PS in page directory entry to enable 4Mbyte pages.
__attribute__((__aligned__(PGSIZE)))
pde_t entrypgdir[NPDEENTRY] = {
  // Map VA's [0, 4MB) to PA's [0, 4MB)
  [0] = (0) | PTE_P | PTE_W | PTE_PS,
  // Map VA's [KERNBASE, KERNBASE+4MB) to PA's [0, 4MB)
  [KERNBASE>>PDXSHIFT] = (0) | PTE_P | PTE_W | PTE_PS,
};
