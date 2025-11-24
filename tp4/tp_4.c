/* GPLv2 (c) Airbus */
#include <debug.h>
#include "cr.h"
#include "pagemem.h"
#include <string.h>

void tp() {
	/*Q1 : A l'aide de la fonction `get_cr3()`, afficher la valeur courante du
  	registre CR3 dans `tp.c`.*/
	int current_CR3_val = get_cr3();
	printf("CR3 = 0x%x\n", current_CR3_val);
	/*Fin Q1*/

	/*Q2 : Allouer un PGD de type `(pde32_t*)` à l'adresse physique `0x600000` et
  	mettre à jour `CR3` avec cette adresse.*/
	cr3_reg_t CR3;
	pde32_t* pgd = (pde32_t*)0x60000;
	/* zero-initialiser PGD */
	memset(pgd, 0, 4096);

	CR3.addr = (uint32_t)pgd >> 12;
	printf("CR3.addr = 0x%x\n", CR3.addr);
	/* Ecrire CR3 */
	set_cr3(CR3);
	/*Fin Q2*/
	
	/*Q3 : Modifier le registre CR0 de sorte à activer la pagination dans `tp.c`. */
	/* Activer le bit PG de CR0 */
	cr0_reg_t CR0;
	uint32_t cr0_val = get_cr0();
	printf("Valeur de cr0_val = 0x%x\n", cr0_val);
	memcpy(&CR0, &cr0_val, sizeof(CR0));
	printf("Valeur de CR0 = 0x%x\n", cr0_val);
	//CR0.pg = 1;
	set_cr0(CR0);
	

	/* Optionnel : lire et afficher pour vérification */
	//cr0_val = get_cr0();
	//memcpy(&CR0, &cr0_val, sizeof(CR0));
	//printf("CR0.pg = %d\n", CR0.pg);
	//printf("CR0.pg = %d\n", CR0.pg);
	/*Fin Q3*/

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

	/* remplir la PT pour identity-mapper la première plage 0..4MB (1024 entrées) */
	for (int i = 0; i < 1024; ++i) {
		ptb[i].raw = (i << 12); /* base_phys | present | rw */
		ptb[i].p = 1;
		ptb[i].rw = 1;
	}

	/* maintenant on peut charger CR3 avec l'adresse physique du PGD */
    set_cr3(CR3);

	/* activer le bit PG de CR0 maintenant que les tables sont en place */
    //CR0.pg = 1;
    //set_cr0(CR0);

	/* Lire et afficher pour vérification */
    //cr0_val = get_cr0();
    //memcpy(&CR0, &cr0_val, sizeof(CR0));
    //printf("CR0.pg = %d\n", CR0.pg);
    /*Fin Q4*/

	/*Q5 : Le but va être maintenant d'initialiser la mémoire virtuelle
  	en "identity mapping" : les adresses virtuelles doivent être identiques aux
  	adresses physiques. Pour cela :*/

	/*Bien étudier les plages d'adresses physiques occupées par le noyau
    (`readelf -e kernel.elf`, regarder les program headers).
	Préparer au moins une entrée dans le PGD pour la PTB.
	Préparer plusieurs entrées dans la PTB.*/

	#define KERNEL_START 0x00300000
	#define KERNEL_END   0x00306000  // page supérieure de 0x00305800
	
	for (uint32_t addr = KERNEL_START; addr < KERNEL_END; addr += 0x1000) {
		uint32_t pde_idx = (addr >> 22) & 0x3FF;
		uint32_t pte_idx = (addr >> 12) & 0x3FF;

		// Allouer la PT si elle n'existe pas
		if (!(pgd[pde_idx].raw & 0x1)) {
			pte32_t* pt = (pte32_t*)(0x601000 + pde_idx * 0x1000);
			memset(pt, 0, 4096);
			pgd[pde_idx].raw = ((uint32_t)pt & 0xFFFFF000) | 0x3;
		}

		// Remplir la PTE pour identity mapping
		pte32_t* pt = (pte32_t*)(pgd[pde_idx].raw & 0xFFFFF000);
		pt[pte_idx].raw = (addr & 0xFFFFF000) | 0x3;
	}

	CR0.pg = 1;
    set_cr0(CR0);

	/* Lire et afficher pour vérification */
    cr0_val = get_cr0();
    memcpy(&CR0, &cr0_val, sizeof(CR0));
    printf("CR0.pg = %d\n", CR0.pg);
	/*Fin Q5*/

	/*
	Q6 : Une fois la pagination activée, essayer d'afficher le contenu d'une
  	entrée de votre PTB. Que se passe-t-il ? Trouver la solution pour être
  	capable de modifier les entrées de votre PTB une fois la pagination
  	activée.**
	*/
	uint32_t idx = 0;
	pte32_t* vptb = (pte32_t*)0x601000; /* adresse virtuelle = adresse physique (identity-mapping) */

	printf("Q6 : avant modification ptb[%u].raw = 0x%x\n", idx, vptb[idx].raw);

	/* Assurer que l'entrée est présente et en RW */
	//vptb[idx].raw = (vptb[idx].raw & 0xFFFFF000) | 0x3;

	/* Recharger CR3 pour invalider le TLB si nécessaire */
	//set_cr3(CR3);

	//printf("Q6 : après modification ptb[%u].raw = 0x%x\n", idx, vptb[idx].raw);

}
