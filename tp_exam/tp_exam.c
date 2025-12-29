/* GPLv2 (c) Airbus */
#include <debug.h>
#include <segmem.h>
#include <string.h>
#include <intr.h>
#include <pagemem.h>
#include <cr.h>
#include <gpr.h>
#include <io.h>

#define c0_idx 1
#define d0_idx 2
#define c3_idx 3
#define d3_idx 4
#define ts_idx 5

#define c0_sel gdt_krn_seg_sel(c0_idx)
#define d0_sel gdt_krn_seg_sel(d0_idx)
#define c3_sel gdt_usr_seg_sel(c3_idx)
#define d3_sel gdt_usr_seg_sel(d3_idx)
#define ts_sel gdt_krn_seg_sel(ts_idx)

seg_desc_t GDT[6];
tss_t TSS;

#define gdt_flat_dsc(_dSc_, _pVl_, _tYp_) \
   ({                                     \
      (_dSc_)->raw = 0;                   \
      (_dSc_)->limit_1 = 0xffff;          \
      (_dSc_)->limit_2 = 0xf;             \
      (_dSc_)->type = _tYp_;              \
      (_dSc_)->dpl = _pVl_;               \
      (_dSc_)->d = 1;                     \
      (_dSc_)->g = 1;                     \
      (_dSc_)->s = 1;                     \
      (_dSc_)->p = 1;                     \
   })

#define tss_dsc(_dSc_, _tSs_)                  \
   ({                                          \
      raw32_t addr = {.raw = _tSs_};           \
      (_dSc_)->raw = sizeof(tss_t);            \
      (_dSc_)->base_1 = addr.wlow;             \
      (_dSc_)->base_2 = addr._whigh.blow;      \
      (_dSc_)->base_3 = addr._whigh.bhigh;     \
      (_dSc_)->type = SEG_DESC_SYS_TSS_AVL_32; \
      (_dSc_)->p = 1;                          \
   })

#define c0_dsc(_d) gdt_flat_dsc(_d, 0, SEG_DESC_CODE_XR)
#define d0_dsc(_d) gdt_flat_dsc(_d, 0, SEG_DESC_DATA_RW)
#define c3_dsc(_d) gdt_flat_dsc(_d, 3, SEG_DESC_CODE_XR)
#define d3_dsc(_d) gdt_flat_dsc(_d, 3, SEG_DESC_DATA_RW)
#define NUMBER_OF_TASKS 2

typedef struct task_context
{
   uint32_t cr3;
   uint32_t eflags;
   gpr_t gpr;
   /* saved instruction pointer and segment selectors for task resume */
   uint32_t eip;
   uint32_t cs;
   uint32_t ss;

} __attribute__((packed)) task_ctx_t;

// Structure to save task context inside kernel space
task_ctx_t Task_Context[NUMBER_OF_TASKS];

// current running task id
int task_id = 0;

void init_gdt()
{
   gdt_reg_t gdtr;

   GDT[0].raw = 0ULL;

   c0_dsc(&GDT[c0_idx]);
   d0_dsc(&GDT[d0_idx]);
   c3_dsc(&GDT[c3_idx]);
   d3_dsc(&GDT[d3_idx]);

   gdtr.desc = GDT;
   gdtr.limit = sizeof(GDT) - 1;
   set_gdtr(gdtr);

   set_cs(c0_sel);

   set_ss(d0_sel);
   set_ds(d0_sel);
   set_es(d0_sel);
   set_fs(d0_sel);
   set_gs(d0_sel);
}

// SYSCALL MANAGEMENT
void syscall_isr()
{
   /* push general regs, pass pointer to saved regs in eax, call handler */
   asm volatile(
   "leave        \n"
   "pusha        \n"
   "mov %esp, %eax      \n"
   "call syscall_handler \n"
   "popa ; iret");
}

void __regparm__(1) syscall_handler(int_ctx_t *ctx)
{
   /* Expect user pointer in EAX (saved in ctx->gpr.eax) */
   uint32_t user_ptr = ctx->gpr.eax.raw;
   uint32_t val = 0;
   /* read the user-space counter (identity-mapped or accessible) */
   val = *((uint32_t *)user_ptr);

   debug("SYSCALL: counter = %u\n", val);
}

// TIMER MANAGEMENT FOR TASK SWITCHING
int timer_handler(void);

void timer_isr()
{
   /* Save general registers, call timer_handler; the handler returns in EAX
    * whether it rebuilt the user stack (1) or skipped scheduling (0). The
    * wrapper will only adjust the stack when needed to avoid corrupting
    * the kernel frame when scheduling was skipped.
    */
   asm volatile(
      "pusha        \n"
      "call timer_handler \n"
      "test %eax, %eax\n"
      "jz 1f\n"
      "popa        \n"
      "add $4, %esp   \n"
      "iret        \n"
      "1:\n"
      "popa        \n"
      "iret        \n"
   );
}

// USERLAND TASKS
// USER TASKS in .user1/.user2 memory sections
/* forward declaration of the user-space syscall wrapper (placed in .user) */
__attribute__((section(".user1"))) void sys_counter(uint32_t *counter);

__attribute__((section(".user1"))) void user1()
{
   /* user1: increment the shared counter at virtual 0x700000 */
   uint32_t *shared0 = (uint32_t *)0x700000;
   while (1)
   {
      (*shared0)++;
      //debug("user1 : %d\n", *shared0);

      // delay loop to slow down increments
      for (volatile int i = 0; i < 100000; i++)
      ;
   }
}

__attribute__((section(".user2"))) void user2()
{
   /* user2: periodically request kernel to print the shared counter via syscall */
   uint32_t *shared1 = (uint32_t *)0x701000;
   while (1)
   {
      /* temporary direct printing */
      //debug("user2 : ");
      /* call the user wrapper */
      sys_counter(shared1);
      
      // delay loop to reduce syscall frequency
      for (volatile int i = 0; i < 100000; i++)
      ;
   }
}

/* user-space syscall wrapper placed in .user so it runs in ring3 */
__attribute__((section(".user1"))) void sys_counter(uint32_t *counter)
{
   asm volatile ("int $0x80" :: "a"(counter));
}

/* Timer interrupt is routed through a assembly wrapper that
 * invokes timer_handler().  timer_handler() is responsible for:
 *  - saving the current user registers and resume frame into
 *    Task_Context[cur];
 *  - selecting the next task and loading its CR3 (address space);
 *  - rebuilding the user stack/frame exactly as expected by iret
 *    so execution resumes in ring 3 for the chosen task.
 * The function relies on the precise stack layout produced by the
 * assembly wrapper and on Task_Context entries initialized in init_paging().
 */
int timer_handler(void)
{
   uint32_t *stack_ptr;
   uint32_t ss, cs;
   uint32_t esp0;

   debug("SCHEDULE INT\n");
   /* send EOI to PIC so future timer IRQs are delivered */
   outb(0x20, 0x20);
   /* read EBP into stack_ptr (pointer to the saved stack frame) */
   asm("mov %%ebp, %%eax; mov %%eax, %0" : "=m"(stack_ptr) : );

   /* Quick detection: ensure the interrupted frame corresponds to a user->kernel
    * transition (i.e. SS/ESP were pushed). If not, this IRQ interrupted the kernel
    * itself — do not attempt to perform a user-context switch using the user-frame
    * layout (that would read wrong offsets and corrupt state).
    */
   uint32_t ss_cand = stack_ptr[15];
   /* SS should be a small selector like d3_sel (0x1b) or d0_sel (0x10). If it's
    * large (stack pointer or address), the layout is different -> skip scheduling.
    */
   if (ss_cand == 0 || ss_cand > 0x1000 || (ss_cand & 0x3) != 3) {
      debug("TIMER: interrupted kernel or non-user frame (ss=0x%x) - skipping schedule\n", ss_cand);
      return 0;
   }

   /* Save context into Task_Context[cur] */
   int cur = task_id;
   Task_Context[cur].gpr.edi.raw = stack_ptr[2];
   Task_Context[cur].gpr.esi.raw = stack_ptr[3];
   Task_Context[cur].gpr.ebp.raw = stack_ptr[10];
   Task_Context[cur].gpr.ebx.raw = stack_ptr[6];
   Task_Context[cur].gpr.edx.raw = stack_ptr[7];
   Task_Context[cur].gpr.ecx.raw = stack_ptr[8];
   Task_Context[cur].gpr.eax.raw = stack_ptr[9];
   Task_Context[cur].eip = stack_ptr[11];
   Task_Context[cur].cs = stack_ptr[12];
   Task_Context[cur].eflags = stack_ptr[13];
   Task_Context[cur].gpr.esp.raw = stack_ptr[14];
   Task_Context[cur].ss = stack_ptr[15];

   /* Prepare kernel stack (cleanup) */
   TSS.s0.esp = (uint32_t)(stack_ptr + 16);
   esp0 = TSS.s0.esp;

   /* Pick next process (2-task round-robin) */
   if (NUMBER_OF_TASKS > task_id + 1) {
      task_id = task_id + 1;
   } else {
      task_id = 0;
   }
   int nxt = task_id;

   ss = (uint32_t)Task_Context[nxt].ss;
   cs = (uint32_t)Task_Context[nxt].cs;

   /* Build the new user stack and gpr area*/
   asm volatile (
      "mov %0, %%esp\n"
      "push %1      \n"
      "push %2      \n"
      "push %3      \n"
      "push %4      \n"
      "push %5      \n"
      ::
      "r"(esp0),
      "r"(ss),
      "r"(Task_Context[nxt].gpr.esp.raw),
      "r"(Task_Context[nxt].eflags),
      "r"(cs),
      "r"(Task_Context[nxt].eip)
   );

   asm volatile (
      "push %0      \n"
      "push %1      \n"
      "push %2      \n"
      "push %3      \n"
      "push %4      \n"
      "push %5      \n"
      ::
      "r"(Task_Context[nxt].gpr.ebp.raw),
      "r"(Task_Context[nxt].gpr.eax.raw),
      "r"(Task_Context[nxt].gpr.ecx.raw),
      "r"(Task_Context[nxt].gpr.edx.raw),
      "r"(Task_Context[nxt].gpr.ebx.raw),
      "r"(Task_Context[nxt].gpr.esp.raw)
   );

   asm volatile (
      "push %0      \n"
      "push %1      \n"
      "push %2      \n"
      "mov %3, %%eax  \n"
      "mov %%eax, %%cr3  \n"
      ::
      "r"(Task_Context[nxt].gpr.ebp.raw),
      "r"(Task_Context[nxt].gpr.esi.raw),
      "r"(Task_Context[nxt].gpr.edi.raw),
      "r"(Task_Context[nxt].cr3)
   );
   return 1;
}

void init_paging()
{
   /* Print current CR3 value using `get_cr3()` */
   int current_CR3_val = get_cr3();
   printf("CR3 = 0x%x\n", current_CR3_val);

   /* Allocate a PGD at physical address 0x600000 and prepare CR3 */
   cr3_reg_t CR3;
   pde32_t *pgd = (pde32_t *)0x600000;
   /* zero-initialiser PGD */
   memset(pgd, 0, 4096);

   CR3.addr = (uint32_t)pgd >> 12;
   printf("CR3.addr = 0x%x\n", CR3.addr);
   /* Ne pas charger CR3 maintenant : on configure d'abord toutes les PT */

   cr0_reg_t CR0;
   uint32_t cr0_val = get_cr0();
   printf("Valeur de cr0_val = 0x%x\n", cr0_val);
   memcpy(&CR0, &cr0_val, sizeof(CR0));

   /* Allocate a PTB at physical address 0x601000 and set up page tables */
   /* Place page tables and set up an identity mapping for 0..4MB */
   pte32_t *ptb = (pte32_t *)0x601000;

   /* zero-initialiser PT */
   memset(ptb, 0, 4096);

   /* In the PGD, prepare an entry pointing to the PTB with present, rw and user attributes */
   pgd[0].addr = ((uint32_t)ptb >> 12);
   pgd[0].p = 1;
   pgd[0].rw = 1;
   pgd[0].lvl = 1; /* user accessible */

   /* Fill the PT to identity-map the first 0..4MB range (1024 entries) */
   for (int i = 0; i < 1024; ++i)
   {
   ptb[i].addr = (i << 0); /* 'addr' stores (physical_address >> 12). For identity mapping phys = i << 12, so we store i */
   ptb[i].p = 1;
   ptb[i].rw = 1;
   ptb[i].lvl = 1; /* allow user access */
   }

   /* ----------------------------------------------------------------------
   Also map the 4MB..8MB range for stack/user (includes 0x600000)
   ---------------------------------------------------------------------- */
   pte32_t *ptb2 = (pte32_t *)0x602000;
   memset(ptb2, 0, 4096);

   /* PGD[1] -> PTB2 (virt/phys 0x400000..0x7FFFFF) */
   pgd[1].addr = ((uint32_t)ptb2 >> 12);
   pgd[1].p = 1;
   pgd[1].rw = 1;
   pgd[1].lvl = 1; /* user accessible */

   /* Fill PTB2 to identity-map 4..8MB */
   for (int i = 0; i < 1024; ++i)
   {
   uint32_t phys = 0x400000 + (i << 12);
   ptb2[i].addr = (phys >> 12);
   ptb2[i].p = 1;
   ptb2[i].rw = 1;
   ptb2[i].lvl = 1;
   }

   /* Save the PGD for the first user task */
   Task_Context[0].cr3 = (uint32_t)pgd;
   /* initialize resume frame for task 0 */
   Task_Context[0].eip = (uint32_t)&user1;
   Task_Context[0].cs = c3_sel;
   Task_Context[0].ss = d3_sel;
   Task_Context[0].eflags = 0x200; /* IF=1 */

   /* Fill a second PGD/PTB for user2 (PGD at 0x603000, PTB1 at 0x604000 and PTB2 at 0x605000) */
   pde32_t *user_pgd = (pde32_t *)0x603000;
   pte32_t *user_ptb = (pte32_t *)0x604000;
   pte32_t *user_ptb2 = (pte32_t *)0x605000;
   memset(user_pgd, 0, 4096);
   memset(user_ptb, 0, 4096);
   memset(user_ptb2, 0, 4096);

   /* user_pgd maps 0..4MB via user_ptb */
   user_pgd[0].addr = ((uint32_t)user_ptb >> 12);
   user_pgd[0].p = 1;
   user_pgd[0].rw = 1;
   user_pgd[0].lvl = 1;
   for (int i = 0; i < 1024; ++i) {
      user_ptb[i].addr = (i << 0); /* 'addr' stores (physical_address >> 12). For identity mapping phys = i << 12, so we store i */
      user_ptb[i].p = 1;
      user_ptb[i].rw = 1;
      user_ptb[i].lvl = 1;
   }

   /* user_pgd maps 4..8MB via user_ptb2 */
   user_pgd[1].addr = ((uint32_t)user_ptb2 >> 12);
   user_pgd[1].p = 1;
   user_pgd[1].rw = 1;
   user_pgd[1].lvl = 1;
   for (int i = 0; i < 1024; ++i) {
      uint32_t phys = 0x400000 + (i << 12);
      user_ptb2[i].addr = (phys >> 12);
      user_ptb2[i].p = 1;
      user_ptb2[i].rw = 1;
      user_ptb2[i].lvl = 1;
   }

   /* Save the PGD for the second user task */
   Task_Context[1].cr3 = (uint32_t)user_pgd;
   /* initialize resume frame for task 1 */
   Task_Context[1].eip = (uint32_t)&user2;
   Task_Context[1].cs = c3_sel;
   Task_Context[1].ss = d3_sel;
   Task_Context[1].eflags = 0x200; /* IF=1 */

   /* -------------------------------------------------------------
   Shared zones and stacks
   ------------------------------------------------------------- */
   /* shared physical page (arbitrarily chosen in 4..8MB) */
   uint32_t shared_phys = 0x700000; /* physical page used as shared page */
   /* zero-initialize shared physical page */
   memset((void*)shared_phys, 0, 4096);
   /* mapped differently in virtual space for each task (in 4..8MB) */
   uint32_t shared_v0 = 0x700000; /* for user1 */
   uint32_t shared_v1 = 0x701000; /* for user2 */

   /* Replace the corresponding PTB2 entries to point to shared_phys */
   uint32_t idx0 = (shared_v0 >> 12) & 0x3ff;
   uint32_t idx1 = (shared_v1 >> 12) & 0x3ff;

   /* user1's ptb2 currently at ptb2 */
   ptb2[idx0].addr = (shared_phys >> 12);
   ptb2[idx0].p = 1;
   ptb2[idx0].rw = 1;
   ptb2[idx0].lvl = 1;

   /* user2's ptb2 in user_ptb2 */
   user_ptb2[idx1].addr = (shared_phys >> 12);
   user_ptb2[idx1].p = 1;
   user_ptb2[idx1].rw = 1;
   user_ptb2[idx1].lvl = 1;

   /* User stacks (1 page each): give each task a distinct page */
   uint32_t user1_stack_base = 0x401000 ; /* pick pages after shared page */
   uint32_t user2_stack_base = 0x501000 ;
   Task_Context[0].gpr.esp.raw = (uint32_t)(user1_stack_base + 0x1000); /* top of stack */
   Task_Context[1].gpr.esp.raw = (uint32_t)(user2_stack_base + 0x1000);

   /* Kernel stacks (1 page each) */
   static uint32_t kstack[NUMBER_OF_TASKS];
   kstack[0] = 0x402000 + 0x1000; /* base stack for USER1 */
   kstack[1] = 0x502000 + 0x1000; /* base stack for USER2 */
   /* Initialize TSS for task 0 */
   TSS.s0.esp = kstack[0];

   /* Load CR3 with the physical address of the PGD */
   set_cr3(CR3);

   debug("activation Pagination \n");

   /* Enable the PG bit in CR0 now that the tables are in place */
   CR0.pg = 1;
   set_cr0(CR0);
}

void tp()
{
   // disable interruptions
   asm volatile("cli");

   // init paging for kernel and user tasks
   debug("Init Paging");
   init_paging();

   // TP5 Q1 : FLAT MODEL FOR SEGMENTS
   debug("Init GDT");
   init_gdt();

   debug("Set user data segments to ring 3, init TSS (esp/ss) and load TR");
   set_ds(d3_sel);
   set_es(d3_sel);
   set_fs(d3_sel);
   set_gs(d3_sel);
   TSS.s0.esp = get_ebp();
   TSS.s0.ss = d0_sel;
   tss_dsc(&GDT[ts_idx], (offset_t)&TSS);
   set_tr(ts_sel);
   // end Q1

   debug("Init de l'IDTR\n");
   // start init
   // TP5 Q2 : install syscall at IRG 0x80
   int_desc_t *dsc;
   idt_reg_t idtr;
   get_idtr(idtr);
   dsc = &idtr.desc[0x80];
   dsc->offset_1 = (uint16_t)((uint32_t)syscall_isr); // 3 install kernel syscall handler
   dsc->offset_2 = (uint16_t)(((uint32_t)syscall_isr) >> 16);
   dsc->dpl = 3;

   // TP5 Q2 : install Timer IRQ at 0x20
   dsc = &idtr.desc[0x20];
   dsc->offset_1 = (uint16_t)((uint32_t)timer_isr); // 3 install kernel timer handler
   dsc->offset_2 = (uint16_t)(((uint32_t)timer_isr) >> 16);

   // enable interruptions
   asm volatile("sti");

   // START first user task in ring 3
   // uint32_t   ustack = Task_Context[0].gpr.esp;
   uint32_t ustack = 0x401000;
   asm volatile(
       "push %0 \n" // ss
       "push %1 \n" // esp pour du ring 3 !
       "pushf   \n" // eflags
       "push %2 \n" // cs
       "push %3 \n" // eip
       "iret" ::
           "i"(d3_sel),
       "m"(ustack),
       "i"(c3_sel),
       "r"(&user1));
   // end common
}
