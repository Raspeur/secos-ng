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


// switch between the NUMBER_OF_TASKS tasks, manages the global Task_Context array and task_id
void switch_task()
{
 /* Simple-os header 
   Source: https://github.com/cfenollosa/os-tutorial/blob/master/23-fixes/cpu/interrupt.asm
    Ref: xv6/trapasm.S
    Defined in isr.c
    [extern int_handler]

   kernel data segment descriptor in flat mode
   KERNEL_DATA_SEG equ 0000000000010_0_00b

   global int_ret

   ; Common code
common_stub:
    ; 1. Save CPU state
    
    ; save the (user-space/ring 3) segment descriptors
    ; CS is saved by int instruction already

    */

// STACK CONTENT ON ENTRY TO THIS FUNCTION
// ASSUMING AN INTERRUPT FROM RING 3
// +-----------------+
// |      SS         | <- esp + 16 (only if ring change)
// +-----------------+
// |      ESP        | <- esp + 12 (only if ring change)
// +-----------------+              
// |    EFLAGS       | <- esp + 8
// +-----------------+
// |      CS         | <- esp + 4
// +-----------------+
// |      EIP        | <- esp + 0
// +-----------------+

// check if ring changed
  /*  uint32_t esp;
   asm volatile ("movl  %%esp, %0":"=r"(esp)); // get current esp
   int ring_changed = ((esp & 0x4) != 0); // SS

 */


/* 
   asm volatile (
    "cli                     \n" // disable interrupts during switch 
    "push ds                 \n"
    "push es                 \n"
    "push fs                 \n"
    "push gs                 \n"
    "pusha                   \n" // Pushes edi,esi,ebp,esp,ebx,edx,ecx,eax
   ::);
 */
   // STACK CONTENT AFTER PUSHES
   // +-----------------+
   // |      EDI        | <- esp + 32 (+16 if ring change)
   // +-----------------+
   // |      ESI        | <- esp + 28 (+16 if ring change)
   // +-----------------+        
   // |      EBP        | <- esp + 24 (+16 if ring change)
   // +-----------------+
   // |      ESP        | <- esp + 20 (+16 if ring change)
   // +-----------------+
   // |      EBX        | <- esp + 16 (+16 if ring change)
   // +-----------------+
   // |      EDX        | <- esp + 12 (+16 if ring change)
   // +-----------------+
   // |      ECX        | <- esp + 8  (+16 if ring change)
   // +-----------------+
   // |      EAX        | <- esp + 4  (+16 if ring change)
   // +-----------------+     
   // |      GS         | <- esp + 0  (+16 if ring change)
   // +-----------------+
   // |      FS         | <- esp -4  (+16 if ring change)
   // +-----------------+
   // |      ES         | <- esp -8  (+16 if ring change)
   // +-----------------+
   // |      DS         | <- esp -12 (+16 if ring change)
   // +-----------------+     

   
/*
    ; switch to kernel data segment
    ; cs has been switched by int instruction already
    */
  /*  asm volatile ("
    mov ax, %0
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
   "::"i"(KERNEL_DATA_SEG));   */
   
/*	
   //  Move current stack content, representative of interrupted task context,
   // into the Task_Context[task_id] structure, from SS to EAX

   task_ctx_t *ctx = &Task_Context[task_id];
   uint32_t *stack_ptr;
   asm volatile ("movl  %%esp, %0":"=r"(stack_ptr));
   // adjust stack_ptr to point to saved EAX

    ; 2. Call C handler
    push esp ; registers_t *r
    cld ; C code following the sysV ABI requires DF to be clear on function entry
	call int_handler
    add esp, 4 ; reverse 'push esp'

int_ret:

*/


    // 3. Restore state (also be used independently to enter user space)
    /* asm volatile (
    "popa                    \n"
    "pop gs                  \n"
    "pop fs                  \n"
    "pop es                  \n"
    "pop ds                  \n"
	 "add esp, 8              \n" // Cleans up the pushed error code and pushed ISR/IRQ number
	 "iret"                       // pops 5 things at once: EIP, CS, EFLAGS, ESP and SS (from low to high memory address)
::); */
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
   asm volatile(
       "leave ; pusha        \n"
       "mov %esp, %eax      \n"
       "call syscall_handler \n"
       "popa ; iret");
}

void __regparm__(1) syscall_handler(int_ctx_t *ctx)
{
   debug("SYSCALL eax = %s%d\n", "TIMER IT \n",(int)(ctx));
}


void __regparm__(1) timer_handler(void)
{
   outb(0x20, 0x20);  // send EOI to master PIC at port 0x20
   debug("TIMER IT \n");
}


// TIMER MANAGEMENT FOR TASK SWITCHING
void timer_isr()
{
   asm volatile(
       "leave ; pusha        \n"
       "mov %esp, %eax      \n"
       "call timer_handler \n"
       "popa ; iret");
}


// USERLAND TASKS
// USER TASKS in .user memory area
__attribute__((section(".user"))) void user0()
{
   // TODO à compléter
   while (1)
   {
      debug("%s", "task1\n");
      for (volatile int i = 0; i < 10000000; i++)
         ;
   }
}

// __attribute__((section(".user"))) void user1()
// {
//    // TODO à compléter
//    while (1)
//    {
//       debug("%s", "task2\n");
//       for (volatile int i = 0; i < 10000000; i++)
//          ;
//    }
// }

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
	pde32_t* pgd = (pde32_t*)0x600000;
	/* zero-initialiser PGD */
	memset(pgd, 0, 4096);

	CR3.addr = (uint32_t)pgd >> 12;
	printf("CR3.addr = 0x%x\n", CR3.addr);
   /* Ne pas charger CR3 maintenant : on configure d'abord toutes les PT */
   /* Ecrire CR3 plus bas une fois les PT en place */
	/*Fin Q2*/
   
   cr0_reg_t CR0;
	uint32_t cr0_val = get_cr0();
	printf("Valeur de cr0_val = 0x%x\n", cr0_val);
	memcpy(&CR0, &cr0_val, sizeof(CR0));


   /*Q4 : Un certain nombre de choses restent à configurer avant l'activation de
  	la pagination. Comme pour le PGD, allouer également une PTB de type `
  	(pte32_t*)` à l'adresse `0x601000`.*/
	/* Placement des PT (page table) et initialisation d'un identity mapping pour 0..4MB */
    pte32_t* ptb = (pte32_t*)0x601000;

	/* zero-initialiser PT */
    memset(ptb,  0, 4096);

	/* Dans la PGD (Page Global Directory), préparer une entrée (PTE) qui pointe vers la PTB (Page Table Base) avec les attributs present et rw */
   pgd[0].raw = ((uint32_t)ptb & 0xFFFFF000);
   pgd[0].p = 1;
   pgd[0].rw = 1;
   pgd[0].lvl = 1; /* user accessible */

	/* remplir la PT pour identity-mapper la première plage 0..4MB (1024 entrées) */
	for (int i = 0; i < 1024; ++i) {
      ptb[i].raw = (i << 12); /* base_phys | present | rw */
      ptb[i].p = 1;
      ptb[i].rw = 1;
      ptb[i].lvl = 1; /* allow user access */
	}

   /* --- Mapper également la plage 4MB..8MB pour la stack/user (incl. 0x600000) --- */
   pte32_t* ptb2 = (pte32_t*)0x602000;
   memset(ptb2, 0, 4096);

   /* PGD[1] -> PTB2 (virt/phys 0x400000..0x7FFFFF) */
   pgd[1].raw = ((uint32_t)ptb2 & 0xFFFFF000);
   pgd[1].p = 1;
   pgd[1].rw = 1;
   pgd[1].lvl = 1; /* user accessible */

   /* remplir la PTB2 pour identity-mapper 4..8MB */
   for (int i = 0; i < 1024; ++i) {
      uint32_t phys = 0x400000 + (i << 12);
      ptb2[i].raw = (phys & 0xFFFFF000);
      ptb2[i].p = 1;
      ptb2[i].rw = 1;
      ptb2[i].lvl = 1;
   }

   /* sauvegarder le PGD pour la première tâche utilisateur */
   Task_Context[0].cr3 = (uint32_t)pgd;

	/* maintenant on peut charger CR3 avec l'adresse physique du PGD */
    set_cr3(CR3);

	/* activer le bit PG de CR0 maintenant que les tables sont en place */

    CR0.pg = 1;
    set_cr0(CR0);
}

void tp() {
   // disable interruptions
    asm volatile("cli");   

    // init paging for kernel and user tasks
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
    TSS.s0.ss  = d0_sel;
    tss_dsc(&GDT[ts_idx], (offset_t)&TSS);
    set_tr(ts_sel);
    // end Q1

    
    debug("Init de l'IDTR");
    // start init
    // TP5 Q2 : install syscall at IRG 0x80
    int_desc_t *dsc;
    idt_reg_t  idtr;
    get_idtr(idtr);
    dsc = &idtr.desc[0x80];
    dsc->offset_1 = (uint16_t)((uint32_t)syscall_isr); // 3 install kernel syscall handler
    dsc->offset_2 = (uint16_t)(((uint32_t)syscall_isr)>>16);
    dsc->dpl = 3;
    

    // TP5 Q2 : install Timer IRQ at 0x20
    dsc = &idtr.desc[0x20];
    dsc->offset_1 = (uint16_t)((uint32_t)timer_isr); // 3 install kernel timer handler
    dsc->offset_2 = (uint16_t)(((uint32_t)timer_isr)>>16);
    
    // enable interruptions
    asm volatile("sti");

    // START first user task in ring 3
    // uint32_t   ustack = Task_Context[0].gpr.esp;
    uint32_t   ustack = 0x600000;
    asm volatile (
      "push %0 \n" // ss
      "push %1 \n" // esp pour du ring 3 !
      "pushf   \n" // eflags
      "push %2 \n" // cs
      "push %3 \n" // eip
      "iret"
      ::
       "i"(d3_sel),
       "m"(ustack),
       "i"(c3_sel),
       "r"(&user0)
      );
    // end common
}
