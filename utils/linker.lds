/* GPLv2 (c) Airbus */
OUTPUT_FORMAT("elf32-i386","elf32-i386","elf32-i386");
OUTPUT_ARCH("i386")

ENTRY(entry)

PHDRS
{
   phboot  PT_LOAD FLAGS (7); /*Program Hearder Boot (RWX)*/
   phstack PT_LOAD FLAGS (6); /*Program Hearder Stack (RW)*/
   phsetup PT_LOAD FLAGS (7); /*Program Hearder Setup (RWX)*/
}

SECTIONS
{
   . = 0x300000;
   .mbh      : { KEEP(*(.mbh)) . = ALIGN(4);     } : phboot 	/*mbh fait 12 octets = 0xc*/
   								/*align 0x10 -> +4/*
   								/*0x300010*/
   .stack    : { KEEP(*(.stack))                 } : phstack    /*0x302010 car space 2000*/

   __kernel_start__ = .;

   .idt_jmp  : { KEEP(*(.idt_jmp))               } : phsetup 	/*localisé dans idt.s, et fait 4k soit 0x1000*/
   								/*handler générique - Certaines interuption/Peach fault n'ont pas d'erreur, on doit donc ajouter une erreur générique*/
   								/*0x303010 */
   								
   .text     : { *(.text)                        } : phsetup
   .rodata   : { *(.rodata)                      } : phsetup
   .data     : { *(.data)                        } : phsetup
   .bss      : { *(.bss COMMON)                  } : phsetup
   /DISCARD/ : { *(.note* .indent .comment)      } : phsetup
   

   __kernel_end__ = .;

   
}
