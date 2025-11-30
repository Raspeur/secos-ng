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

void Init_task_context()
{
   // TO DO : initialize task contexts with suitable stack pointers and CR3 values
   memset(&Task_Context, 0, sizeof(Task_Context));
}

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
void timer_isr()
{
   /* Save general registers, invoke scheduler to pick/prepare next task,
      rebuild the user stack/frame for that task, fix stack pointer, then iret */
   asm volatile(
      "pusha        \n"
      "call timer_handler \n"
      "popa        \n"
      "add $4, %esp   \n"
      "iret        \n"
   );
}

// USERLAND TASKS
// USER TASKS in .user memory area
/* forward declaration of the user-space syscall wrapper (placed in .user) */
__attribute__((section(".user"))) void sys_counter(uint32_t *counter);

__attribute__((section(".user"))) void user0()
{
   /* user0: increment the shared counter at virtual 0x700000 */
   //uint32_t *shared0 = (uint32_t *)0x700000;
   while (1)
   {
      //(*shared0)++;
      //debug("user0 : %d\n", *shared0);
      debug("user0\n");
      for (volatile int i = 0; i < 100000; i++)
      ;
   }
}

__attribute__((section(".user"))) void user1()
{
   /* user1: periodically request kernel to print the shared counter via syscall */
   //uint32_t *shared1 = (uint32_t *)0x701000;
   while (1)
   {
      /* call the user wrapper */
      //sys_counter(shared1);
      /* temporary direct printing */
      debug("user1\n");
      for (volatile int i = 0; i < 100000; i++)
      ;
   }
}

/* user-space syscall wrapper placed in .user so it runs in ring3 */
__attribute__((section(".user"))) void sys_counter(uint32_t *counter)
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
void timer_handler(void)
{
   uint32_t *stack_ptr;
   uint32_t ss, cs;
   uint32_t esp0;

   debug("schedule int");
   /* send EOI to PIC so future timer IRQs are delivered */
   outb(0x20, 0x20);
   /* read EBP into stack_ptr (pointer to the saved stack frame) */
   asm("mov %%ebp, %%eax; mov %%eax, %0" : "=m"(stack_ptr) : );

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
}

void init_paging()
{
   /*Q1 : A l'aide de la fonction `get_cr3()`, afficher la valeur courante du
   registre CR3 dans `tp.c`.*/
   int current_CR3_val = get_cr3();
   printf("CR3 = 0x%x\n", current_CR3_val);
   /*Fin Q1*/

   /*Q2 : Allouer un PGD de type `(pde32_t*)` à l'adresse physique `0x600000` et
   mettre à jour `CR3` avec cette adresse.*/
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

   /*Q4 : Un certain nombre de choses restent à configurer avant l'activation de
   la pagination. Comme pour le PGD, allouer également une PTB de type `
   (pte32_t*)` à l'adresse `0x601000`.*/
   /* Placement des PT (page table) et initialisation d'un identity mapping pour 0..4MB */
   pte32_t *ptb = (pte32_t *)0x601000;

   /* zero-initialiser PT */
   memset(ptb, 0, 4096);

   /* Dans la PGD (Page Global Directory), préparer une entrée (PTE) qui pointe vers la PTB (Page Table Base) avec les attributs present, rw et user */
   pgd[0].addr = ((uint32_t)ptb >> 12);
   pgd[0].p = 1;
   pgd[0].rw = 1;
   pgd[0].lvl = 1; /* user accessible */

   /* remplir la PT pour identity-mapper la première plage 0..4MB (1024 entrées) */
   for (int i = 0; i < 1024; ++i)
   {
   ptb[i].addr = (i << 0); /* base_phys >>12 stored in addr */
   ptb[i].p = 1;
   ptb[i].rw = 1;
   ptb[i].lvl = 1; /* allow user access */
   }

   /* ----------------------------------------------------------------------
   Mapper également la plage 4MB..8MB pour la stack/user (incl. 0x600000) 
   ---------------------------------------------------------------------- */
   pte32_t *ptb2 = (pte32_t *)0x602000;
   memset(ptb2, 0, 4096);

   /* PGD[1] -> PTB2 (virt/phys 0x400000..0x7FFFFF) */
   pgd[1].addr = ((uint32_t)ptb2 >> 12);
   pgd[1].p = 1;
   pgd[1].rw = 1;
   pgd[1].lvl = 1; /* user accessible */

   /* remplir la PTB2 pour identity-mapper 4..8MB */
   for (int i = 0; i < 1024; ++i)
   {
   uint32_t phys = 0x400000 + (i << 12);
   ptb2[i].addr = (phys >> 12);
   ptb2[i].p = 1;
   ptb2[i].rw = 1;
   ptb2[i].lvl = 1;
   }

   /* sauvegarder le PGD pour la première tâche utilisateur */
   Task_Context[0].cr3 = (uint32_t)pgd;
   /* initialize resume frame for task 0 */
   Task_Context[0].eip = (uint32_t)&user0;
   Task_Context[0].cs = c3_sel;
   Task_Context[0].ss = d3_sel;
   Task_Context[0].eflags = 0x200; /* IF=1 */

   /* remplir une seconde PGD/PTB pour user1 (PGD à 0x1600000, PTB1 à 0x1601000 et PTB2 à 0x1602000) */
   pde32_t *user_pgd = (pde32_t *)0x1600000;
   pte32_t *user_ptb = (pte32_t *)0x1601000;
   pte32_t *user_ptb2 = (pte32_t *)0x1602000;
   memset(user_pgd, 0, 4096);
   memset(user_ptb, 0, 4096);
   memset(user_ptb2, 0, 4096);

   /* user_pgd mappe 0..4MB via user_ptb */
   user_pgd[0].addr = ((uint32_t)user_ptb >> 12);
   user_pgd[0].p = 1;
   user_pgd[0].rw = 1;
   user_pgd[0].lvl = 1;
   for (int i = 0; i < 1024; ++i) {
      user_ptb[i].addr = (i << 0);
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

   /* sauvegarder le PGD pour la seconde tâche utilisateur */
   Task_Context[1].cr3 = (uint32_t)user_pgd;
   /* initialize resume frame for task 1 */
   Task_Context[1].eip = (uint32_t)&user1;
   Task_Context[1].cs = c3_sel;
   Task_Context[1].ss = d3_sel;
   Task_Context[1].eflags = 0x200; /* IF=1 */

   /* -------------------------------------------------------------
   Zones partagées et stacks
   ------------------------------------------------------------- */
   /* page physique partagée (choix arbitraire dans 4..8MB) */
   uint32_t shared_phys = 0x700000; /* physical page used as shared page */
   /* zero-initialize shared physical page */
   memset((void*)shared_phys, 0, 4096);
   /* mappee virtuellement differemment pour chaque tache (dans 4..8MB) */
   uint32_t shared_v0 = 0x700000; /* for user0 */
   uint32_t shared_v1 = 0x701000; /* for user1 */

   /* Remplacer les entrées correspondantes dans chaque PTB2 pour pointer vers shared_phys */
   uint32_t idx0 = (shared_v0 >> 12) & 0x3ff;
   uint32_t idx1 = (shared_v1 >> 12) & 0x3ff;

   /* user0's ptb2 currently at ptb2 */
   ptb2[idx0].addr = (shared_phys >> 12);
   ptb2[idx0].p = 1;
   ptb2[idx0].rw = 1;
   ptb2[idx0].lvl = 1;

   /* user1's ptb2 in user_ptb2 */
   user_ptb2[idx1].addr = (shared_phys >> 12);
   user_ptb2[idx1].p = 1;
   user_ptb2[idx1].rw = 1;
   user_ptb2[idx1].lvl = 1;

   /* Stacks utilisateurs (1 page chacun) : donner une page distincte par tache */
   uint32_t user0_stack_base = 0x700000 + 0x2000; /* pick pages after shared page */
   uint32_t user1_stack_base = 0x700000 + 0x3000;
   Task_Context[0].gpr.esp.raw = (uint32_t)(user0_stack_base + 0x1000); /* top of stack */
   Task_Context[1].gpr.esp.raw = (uint32_t)(user1_stack_base + 0x1000);

   /* Stacks noyau (1 page chacun) */
   static uint32_t kstack[NUMBER_OF_TASKS];
   kstack[0] = 0x900000 + 0x1000; /* top */
   kstack[1] = 0x901000 + 0x1000;
   /* initialiser TSS pour la tache 0 */
   TSS.s0.esp = kstack[0];

   /* charge CR3 avec l'adresse physique du PGD */
   set_cr3(CR3);

   /* active le bit PG de CR0 maintenant que les tables sont en place */
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
   uint32_t ustack = 0x600000;
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
       "r"(&user0));
   // end common
}
