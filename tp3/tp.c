/* GPLv2 (c) Airbus */
#include <debug.h>
#include <segmem.h>

seg_desc_t my_new_gdt[8];

#define gdt_flat_dsc(_dSc_,_pVl_,_tYp_)                                 \
   ({                                                                   \
      (_dSc_)->raw     = 0;                                             \
      (_dSc_)->limit_1 = 0xffff;                                        \
      (_dSc_)->limit_2 = 0xf;                                           \
      (_dSc_)->type    = _tYp_;                                         \
      (_dSc_)->dpl     = _pVl_;                                         \
      (_dSc_)->d       = 1;                                             \
      (_dSc_)->g       = 1;                                             \
      (_dSc_)->s       = 1;                                             \
      (_dSc_)->p       = 1;                                             \
   })



#define tss_dsc(_dSc_,_tSs_)                                            \
   ({                                                                   \
      raw32_t addr    = {.raw = _tSs_};                                 \
      (_dSc_)->raw    = sizeof(tss_t);                                  \
      (_dSc_)->base_1 = addr.wlow;                                      \
      (_dSc_)->base_2 = addr._whigh.blow;                               \
      (_dSc_)->base_3 = addr._whigh.bhigh;                              \
      (_dSc_)->type   = SEG_DESC_SYS_TSS_AVL_32;                        \
      (_dSc_)->p      = 1;                                              \
   })

void userland() {
   asm volatile ("mov %eax, %cr0");
}

void initgdt()
{
   gdt_reg_t gdtr;

   //On s'assure que la le premier segment de la gdt est nul
   my_new_gdt[0].raw = 0;

   //On défini dans la gdt 2 segments (code segment et data segment en ring 0)
   my_new_gdt[1].limit_1 = 0xffff;   //:16;     /* bits 00-15 of the segment limit */
   my_new_gdt[1].type = 11; //code,RX //:4;      /* segment type */
   my_new_gdt[1].s = 1;              //:1;      /* descriptor type */
   my_new_gdt[1].dpl = 0; //ring0    //:2;      /* descriptor privilege level */
   my_new_gdt[1].p = 1;              //:1;      /* segment present flag */
   my_new_gdt[1].limit_2 = 0xf;      //:4;      /* bits 16-19 of the segment limit */
   my_new_gdt[1].avl = 1;            //:1;      /* available for fun and profit */
   my_new_gdt[1].l = 0; // 32 bits   //:1;      /* longmode */
   my_new_gdt[1].d = 1;              //:1;      /* default length, depend on seg type */
   my_new_gdt[1].g = 1;              //:1;      /* granularity */
   my_new_gdt[2].limit_1 = 0xffff;   //:16;     /* bits 00-15 of the segment limit */
   my_new_gdt[2].type = 3; //data,RW //:4;      /* segment type */
   my_new_gdt[2].s = 1;              //:1;      /* descriptor type */
   my_new_gdt[2].dpl = 0; //ring0    //:2;      /* descriptor privilege level */
   my_new_gdt[2].p = 1;              //:1;      /* segment present flag */
   my_new_gdt[2].limit_2 = 0xf;      //:4;      /* bits 16-19 of the segment limit */
   my_new_gdt[2].avl = 1;            //:1;      /* available for fun and profit */
   my_new_gdt[2].l = 0; // 32 bits   //:1;      /* longmode */
   my_new_gdt[2].d = 1;              //:1;      /* default length, depend on seg type */
   my_new_gdt[2].g = 1;              //:1;      /* granularity */

   //Et maintenant les 2 autres segments en ring 3
   //On peut passer par des macros pour réduire la réecriture de code
   gdt_flat_dsc(&my_new_gdt[3], 3, 11);
   gdt_flat_dsc(&my_new_gdt[4], 3, 3);

   gdtr.addr = (uint32_t)&my_new_gdt[0]; //ou gdtr.desc = &my_new_gdt[0]
   gdtr.limit = sizeof(my_new_gdt) - 1;

   set_gdtr(gdtr);

   //On set les registres cs, ss, ds, es, fs, et gs, pour qu'ils soient sur les segments en ring 0 de my_new_gdt
   //Comme nous somme ici ring0, nous utilisons la macro gdt_krn_seg_sel
   /*
    * Set the segment registers to appropriate kernel segment selectors.
    *
    * - CS (Code Segment): Points to the segment containing executable code.
    * - SS (Stack Segment): Points to the segment containing the stack.
    * - DS (Data Segment): Points to the segment containing program data.
    * - ES (Extra Segment): Additional data segment, often used for string operations.
    * - FS (FS Segment): General-purpose segment register, often used for thread-local storage or special data.
    * - GS (GS Segment): General-purpose segment register, often used for special data (e.g., per-CPU data).
    *
    * Ici, on configure tous ces registres pour qu'ils pointent vers les segments noyau appropriés via la GDT.
    */
   set_cs(gdt_krn_seg_sel(1));
   set_ss(gdt_krn_seg_sel(2));
   set_ds(gdt_krn_seg_sel(2));
   set_es(gdt_krn_seg_sel(2));
   set_fs(gdt_krn_seg_sel(2));
   set_gs(gdt_krn_seg_sel(2));
}

void tp() {
   /*QUESTION 1*/
   initgdt();
   /*fin question 1*/
   
   //Preparation pour la question 2 et 3
   set_ds(gdt_usr_seg_sel(4));
   set_es(gdt_usr_seg_sel(4));
   set_fs(gdt_usr_seg_sel(4));
   set_gs(gdt_usr_seg_sel(4));

   
   //On prépare en avance une TSS pour les retours Ring 3 → Ring 0
   // Note: TSS is needed for the "kernel stack"
   // when returning from ring 3 to ring 0
   // during a next interrupt occurence
   tss_t      TSS;
   TSS.s0.esp = get_ebp(); // Pile Ring 0
   TSS.s0.ss = gdt_krn_seg_sel(2); //Segment de pile Ring 0
   tss_dsc(&my_new_gdt[5], (offset_t)&TSS);
   set_tr(gdt_krn_seg_sel(5));

   /*QUESTION 2*/
   asm volatile (
      //"push %ss\n\t" // Empile sélecteur Ring 0 (ex: 0x10)
      //"push %esp\n\t"//non ok
      //"pushf\n\t"    //ok
      //"push %cs\n\t" // Empile sélecteur Ring 0 (ex: 0x08)

      "push %0    \n\t" // SS Ring 3 (sélecteur data Ring 3)
      "push $0x200000 \n\t"  // ESP Ring 3 (pile utilisateur fictive)
      "pushf      \n\t"  // EFLAGS
      "push %1    \n\t"  // CS Ring 3
      "push %2    \n\t"  // EIP Ring 3 (adresse userland)
      "iret       \n\t"  // Question 3
      :
      :
      "i"(gdt_usr_seg_sel(4)),    // %0 = SS (data Ring 3)
      "i"(gdt_usr_seg_sel(3)),    // %1 = CS (code Ring 3)  
      "r"((uint32_t)userland)     // %2 = EIP
      :
      "memory"
   );
   /*fin question 2*/

}
