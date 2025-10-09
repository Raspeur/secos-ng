/* GPLv2 (c) Airbus */
#include <debug.h>
#include <segmem.h>
#include <string.h>

void userland() {
   asm volatile ("mov %eax, %cr0");
}

void print_gdt_content(gdt_reg_t gdtr_ptr) {
    seg_desc_t* gdt_ptr;
    gdt_ptr = (seg_desc_t*)(gdtr_ptr.addr);
    int i=0;
    while ((uint32_t)gdt_ptr < ((gdtr_ptr.addr) + gdtr_ptr.limit)) {
        uint32_t start = gdt_ptr->base_3<<24 | gdt_ptr->base_2<<16 | gdt_ptr->base_1;
        uint32_t end;
        if (gdt_ptr->g) {
            end = start + ( (gdt_ptr->limit_2<<16 | gdt_ptr->limit_1) <<12) + 4095;
        } else {
            end = start + (gdt_ptr->limit_2<<16 | gdt_ptr->limit_1);
        }
        debug("%d ", i);
        debug("[0x%x ", start);
        debug("- 0x%x] ", end);
        debug("seg_t: 0x%x ", gdt_ptr->type);
        debug("desc_t: %d ", gdt_ptr->s);
        debug("priv: %d ", gdt_ptr->dpl);
        debug("present: %d ", gdt_ptr->p);
        debug("avl: %d ", gdt_ptr->avl);
        debug("longmode: %d ", gdt_ptr->l);
        debug("default: %d ", gdt_ptr->d);
        debug("gran: %d ", gdt_ptr->g);
        debug("\n");
        gdt_ptr++;
        i++;
    }
}


void tp() {
	// TODO
	gdt_reg_t gdt_read_out; /*On déclare la structure qui va recevoir le contenu de gdt*/
	//get_gdt_ptr(&_out); /*On recupère le contenu de la gdt*/
	get_gdtr(gdt_read_out);
	print_gdt_content(gdt_read_out); /*On l'affiche*/
	
	debug("Gdt_Adress 0x%x \n", (unsigned int)gdt_read_out.addr);
	debug("Gdt_limit 0x%x \n", (unsigned int)gdt_read_out.limit);
	/*QUESTION 3*/
	
	debug("SS : 0x%x \n", get_ss());	
	debug("DS : 0x%x \n", get_ds());
	debug("ES : 0x%x \n", get_es());
	debug("FS : 0x%x \n", get_fs());
	debug("GS : 0x%x \n", get_gs());
	debug("CS : 0x%x \n", get_cs()); //Affichable que en ring 0
	
	
	/* QUESTION 5 : reconfiguration de la GDT */
	debug("Début Q5:\n");
	// nous choisissons une adresse a la fin de la DRAM disponible pour la nv GDT
	seg_desc_t* our_gdt = (seg_desc_t*) 0x7fd0000;
	//seg_desc_t our_gdt[5] = (seg_desc_t*) 0x7fd0000;
	
	gdt_reg_t gdt_read_new;
	gdt_read_new.addr = (offset_t) our_gdt;
	gdt_read_new.limit = gdt_read_out.limit;
	
	
	
	
	
	//uint8_t gdt_size = (gdt_read_out.limit)+1;
	//debug("gdt_size : 0x%x \n", gdt_size);
	//On copie la GDT grub vers l'espace mémoire de la nouvelle GDT
	_memcpy8((uint32_t)gdt_read_new.addr, (uint32_t)gdt_read_out.addr, (uint16_t)gdt_read_out.limit+1);
	print_gdt_content(gdt_read_new);
	
	
	/*QUESTION 6*/
	debug("Début Q6:\n");
	// GDTR update
	set_gdtr(gdt_read_new);

	// selector update
	set_ss(0x10);
	set_ds(0x10);
	set_es(0x10);
	set_fs(0x10);
	set_gs(0x10);
	set_cs(0x8);
	
	/*QUESTION 7*/
	debug("Début Q7:\n");
	get_gdtr(gdt_read_out);
	print_gdt_content(gdt_read_out);
	
	/*QUESTION 8*/
	
	
	
	/*QUESTION 9*/
	debug("Début Q9:\n");
    char  src[64];
    char *dst = 0;
    memset(src, 0xff, 64);

    our_gdt[3].limit_1 = 0x1f;   //:16;     /* bits 00-15 of the segment limit */
    our_gdt[3].base_1 = 0x0000;    //:16;     /* bits 00-15 of the base address */
    our_gdt[3].base_2 = 0x60;      //:8;      /* bits 16-23 of the base address */
    our_gdt[3].type = 3; //data,RW //:4;      /* segment type */
    our_gdt[3].s = 1;              //:1;      /* descriptor type */
    our_gdt[3].dpl = 0; //ring0    //:2;      /* descriptor privilege level */
    our_gdt[3].p = 1;              //:1;      /* segment present flag */
    our_gdt[3].limit_2 = 0x0;      //:4;      /* bits 16-19 of the segment limit */
    our_gdt[3].avl = 1;            //:1;      /* available for fun and profit */
    our_gdt[3].l = 0; // 32 bits   //:1;      /* longmode */
    our_gdt[3].d = 1;              //:1;      /* default length, depend on seg type */
    our_gdt[3].g = 0;              //:1;      /* granularity */
    our_gdt[3].base_3 = 0x00;      //:8;      /* bits 24-31 of the base address */
    print_gdt_content(gdt_read_out);
    // end Q9
    
    /*QUESTION 10*/
    debug("Début Q10:\n");
    //Attention movsb <-> mem[%edi++] = mem[%esi++]
    //				cs		ds
    //On veut changer la limit pour être sur que l'on ne copie pas plus que 32
    seg_sel_t my_es;
    my_es.index = 3;
    my_es.ti = 0;
    my_es.rpl = 0;
    set_es(my_es);
    //Comme dst = 0, les octets seront copiés à es:[dst] (via mov es:[dst]), soit 0x600000
    _memcpy8(dst, src, 32);
    
    /*QUESTION 11*/
    debug("Début Q11:\n");
    //_memcpy8(dst, src, 33);
    // end Q11
    
    /*QUESTION 12*/
    debug("Début Q12:\n");
    our_gdt[4].limit_1 = 0xffff;   //:16;     /* bits 00-15 of the segment limit */
    our_gdt[4].base_1 = 0x0000;    //:16;     /* bits 00-15 of the base address */
    our_gdt[4].base_2 = 0x00;      //:8;      /* bits 16-23 of the base address */
    our_gdt[4].type = 11;//Code,RX //:4;      /* segment type */
    our_gdt[4].s = 1;              //:1;      /* descriptor type */
    our_gdt[4].dpl = 3; //ring3    //:2;      /* descriptor privilege level */
    our_gdt[4].p = 1;              //:1;      /* segment present flag */
    our_gdt[4].limit_2 = 0xf;      //:4;      /* bits 16-19 of the segment limit */
    our_gdt[4].avl = 1;            //:1;      /* available for fun and profit */
    our_gdt[4].l = 0; //32bits     //:1;      /* longmode */
    our_gdt[4].d = 1;              //:1;      /* default length, depend on seg type */
    our_gdt[4].g = 1;              //:1;      /* granularity */
    our_gdt[4].base_3 = 0x00;      //:8;      /* bits 24-31 of the base address */
    our_gdt[5].limit_1 = 0xffff;   //:16;     /* bits 00-15 of the segment limit */
    our_gdt[5].base_1 = 0x0000;    //:16;     /* bits 00-15 of the base address */
    our_gdt[5].base_2 = 0x00;      //:8;      /* bits 16-23 of the base address */
    our_gdt[5].type = 3; //data,RW //:4;      /* segment type */
    our_gdt[5].s = 1;              //:1;      /* descriptor type */
    our_gdt[5].dpl = 3; //ring3    //:2;      /* descriptor privilege level */
    our_gdt[5].p = 1;              //:1;      /* segment present flag */
    our_gdt[5].limit_2 = 0xf;      //:4;      /* bits 16-19 of the segment limit */
    our_gdt[5].avl = 1;            //:1;      /* available for fun and profit */
    our_gdt[5].l = 0; // 32 bits   //:1;      /* longmode */
    our_gdt[5].d = 1;              //:1;      /* default length, depend on seg type */
    our_gdt[5].g = 1;              //:1;      /* granularity */
    our_gdt[5].base_3 = 0x00;      //:8;      /* bits 24-31 of the base address */
    // end Q12
    
    /*QUESTION 13*/
    debug("Début Q113:\n");
    // DS/ES/FS/GS
    set_ds(gdt_usr_seg_sel(5));
    set_es(gdt_usr_seg_sel(5));
    set_fs(gdt_usr_seg_sel(5));
    set_gs(gdt_usr_seg_sel(5));
    
    // SS
    //set_ss(gdt_usr_seg_sel(5)); // plante, #GP
    //tss_t TSS;
    //TSS.s0.esp = get_ebp();
    //TSS.s0.ss  = gdt_krn_seg_sel(2);
    //tss_dsc(&my_gdt[6], (offset_t)&TSS);
    //set_tr(gdt_krn_seg_sel(6));
    
    // CS via farjump
    // fptr32_t fptr = {.segment = gdt_usr_seg_sel(4), .offset = (uint32_t)userland}; 
    // farjump(fptr);  // plante, #GP
    // interdit, un moyen de démarrer une tâche ring 3 depuis le ring 0 est 
    // de détourner l'usage principal de iret pour profiter du changement 
    // de contexte que le CPU sait effectuer à ce moment-là... cf. TP3 pour l'implem.
    // end Q13
	
}
