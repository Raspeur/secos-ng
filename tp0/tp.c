/* GPLv2 (c) Airbus */
#include <debug.h>
#include <info.h>

extern info_t   *info;
extern uint32_t __kernel_start__;
extern uint32_t __kernel_end__;

void tp() {
   debug("kernel mem [0x%p - 0x%p]\n", &__kernel_start__, &__kernel_end__);
   debug("MBI flags 0x%x\n", info->mbi->flags);

   // Affiche la carte mémoire fournie par le BIOS via GRUB (via la structure multiboot_info)
   multiboot_memory_map_t* entry = (multiboot_memory_map_t*)info->mbi->mmap_addr;
   
   while((uint32_t)entry < (info->mbi->mmap_addr + info->mbi->mmap_length)) {
      /*Q2*/
      // TODO print "[start - end] type" for each entry
      // On veut les formats :
      // [0x0 - 0x9fbff] MULTIBOOT_MEMORY_AVAILABLE
      // [0x9fc00 - 0x9ffff] MULTIBOOT_MEMORY_RESERVED
      
      // Affiche la plage mémoire [début - fin] et le type (available ou reserved)
      debug("[0x%lx - 0x%lx] %s\n",
         (unsigned long)entry->addr,
         (unsigned long)(entry->addr + entry->len - 1),
         (entry->type == MULTIBOOT_MEMORY_AVAILABLE) ? "MULTIBOOT_MEMORY_AVAILABLE" : "MULTIBOOT_MEMORY_RESERVED");

      // Passe à l'entrée suivante
      entry++;
   }

   /*Q3*/
   int *ptr_in_available_mem;
   ptr_in_available_mem = (int*)0x0;
   debug("Available mem (0x0): before: 0x%x ", *ptr_in_available_mem); // read
   *ptr_in_available_mem = 0xaaaaaaaa;                           // write
   debug("after: 0x%x\n", *ptr_in_available_mem);                // check

   int *ptr_in_reserved_mem;
   ptr_in_reserved_mem = (int*)0xf0000;
   debug("Reserved mem (at: 0xf0000):  before: 0x%x ", *ptr_in_reserved_mem); // read
   *ptr_in_reserved_mem = 0xaaaaaaaa;                           // write
   debug("after: 0x%x\n", *ptr_in_reserved_mem);                // check


   /*Q4*/
   // Tente d'accéder à une adresse au-delà de la mémoire physique (ex: 0x8000000 pour 128MB)
   int *ptr_out_of_physical_mem;
   ptr_out_of_physical_mem = (int*)0x8000000; // 128MB
   debug("Out of bounds mem (at: 0x8000000): before: 0x%x ", *ptr_out_of_physical_mem); // read
   *ptr_out_of_physical_mem = 0xaaaaaaaa; // write
   debug("after: 0x%x\n", *ptr_out_of_physical_mem); // check
}
