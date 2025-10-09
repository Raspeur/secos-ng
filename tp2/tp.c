/* GPLv2 (c) Airbus */
#include <debug.h>
#include <intr.h>

void bp_handler() {
	/*QUESTION 2*/
	//debug("Debut handler Q2\n");
	//debug("#BP handling\n");
	//debug("Fin handler Q2\n");

	/*QUESTION 7*/
	// debug("Debut handler Q7\n");
	// uint32_t val;
	// asm volatile ("mov 4(%%ebp), %0":"=r"(val));
	// debug("EIP = 0x%x\n", val);
	// debug("Debut handler Q7\n");

	/*QUESTION 8*/
	// asm volatile (
  	// "pusha\n\t"		// Sauvegarde tous les registres généraux sur la pile
	// );
	// debug("#BP handling\n");

	// // Récupère la valeur de EIP (adresse de retour) depuis la pile via EBP
	// // "mov 4(%%ebp), %0" : lit la valeur à l'adresse EBP+4 (EIP sauvegardé par l'interruption)
	// uint32_t eip;
	// asm volatile ("mov 4(%%ebp), %0":"=r"(eip));
	// // Affiche la valeur de EIP
	// debug("EIP = 0x%x\n", eip);

	/*QUESTION 9*/
	//La routine doit se terminer par l'instruction IRET
	asm volatile ("pusha");
	debug("#BP handling\n");
	uint32_t eip;
	asm volatile ("mov 4(%%ebp), %0":"=r"(eip));
	debug("EIP = %x\n", (unsigned int) eip);
	asm volatile ("popa");
	asm volatile ("leave; iret");
	//presence de code mort à la suite (leave et ret généré par le compilateur)
	//pour éviter ça, on peut ajouter __attribute__((naked)) devant la définition de bp_handler.
}

void bp_trigger() {
	/*QUESTION 4*/
	debug("Debut trigger Q4\n");
	asm volatile ("int3");
	debug("Fin trigger Q4\n");
}

void tp() {
	
	/*QUESTION 1*/
	debug("Debut Q1\n");
	// TODO print idtr
	idt_reg_t idtr_value;
	get_idtr(idtr_value);
	debug("IDTR base adress : 0x%x\n",(unsigned int)idtr_value.addr);
	debug("Fin Q1\n");

	/*QUESTION 3*/
	debug("Debut Q3\n");
	
	// On récupère un pointeur vers le descripteur d'interruption pour l'interruption #3 (Breakpoint)
	int_desc_t *base_interrupt_pointeur_descriptor = &idtr_value.desc[3];

	// On récupère l'adresse de la fonction bp_handler (notre handler personnalisé)
	uint32_t addr = (uint32_t)bp_handler;

	// On place la partie basse de l'adresse du handler dans offset_1 du descripteur
	base_interrupt_pointeur_descriptor->offset_1 = ((uint16_t)addr);

	// On place la partie haute de l'adresse du handler dans offset_2 du descripteur
	base_interrupt_pointeur_descriptor->offset_2 = (uint16_t)(addr>>16);

	debug("Fin Q3\n");

	// TODO call bp_trigger
    bp_trigger();
}
