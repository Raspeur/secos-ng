/* GPLv2 (c) Airbus */
#include <start.h>
#include <debug.h>
#include <pic.h>
#include <uart.h>
#include <intr.h>
#include <info.h>

volatile const uint32_t __mbh__ mbh[] = {
   MBH_MAGIC,	/**/
   MBH_FLAGS,	/*Les services requis*/
   (uint32_t)-(MBH_MAGIC+MBH_FLAGS), /*Checksum pour être sur que l'on a le bon header*/
};

static info_t __info __attribute__ ((aligned(16)));
       info_t *info = &__info;

void __attribute__((regparm(1))) start(mbi_t *mbi) 	/*Multi Boot Info, type defined in mbi.h, and originaly from grub_mbi.h (multi_mmap_entry)*/
							/*regparm(1) = va chercher eax*/
{
   info->mbi = mbi;

   pic_init();
   uart_init();
   intr_init();
   debug("\n" RELEASE " (c) Airbus\n");

   tp();	/*Fonction tp qui est notre entrée*/

   panic("halted !");
}
