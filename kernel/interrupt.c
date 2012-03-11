/*
 *  Copyright (C) 2012  Ladislav Klenovic <klenovic@nucleonsoft.com>
 *
 *  This file is part of Nucleos kernel.
 *
 *  Nucleos kernel is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2 of the License.
 */
/*
 *   The Minix hardware interrupt system.
 *   
 *   This file contains routines for managing the interrupt
 *   controller.
 *  
 *   put_irq_handler: register an interrupt handler.
 *   rm_irq_handler:  deregister an interrupt handler.
 *   irq_handle:     handle a hardware interrupt.
 *                    called by the system dependent part when an
 *                    external interrupt occures.
 *   enable_irq:      enable hook for IRQ.
 *   disable_irq:     disable hook for IRQ.
 */
#include <nucleos/com.h>
#include <kernel/kernel.h>
#include <kernel/proc.h>
#include <asm/kernel/const.h>
#include <asm/kernel/hw_intr.h>

/* number of lists of IRQ hooks, one list per supported line. */
irq_hook_t* irq_handlers[NR_IRQ_VECTORS] = {0};

/* Register an interrupt handler.  */
void put_irq_handler( irq_hook_t* hook, int irq, irq_handler_t handler)
{
	int id;
	irq_hook_t **line;
	unsigned long bitmap;

	if( irq < 0 || irq >= NR_IRQ_VECTORS )
		kernel_panic("invalid call to put_irq_handler", irq);

	line = &irq_handlers[irq];

	bitmap = 0;
	while ( *line != NULL ) {
		if(hook == *line)
			return; /* extra initialization */
		bitmap |= (*line)->id;	/* mark ids in use */
		line = &(*line)->next;
	}

	/* find the lowest id not in use */
	for (id = 1; id != 0; id <<= 1)
		if (!(bitmap & id))
			break;

	if(id == 0)
		kernel_panic("Too many handlers for irq", irq);

	hook->next = NULL;
	hook->handler = handler;
	hook->irq = irq;
	hook->id = id;
	*line = hook;
	irq_use |= 1 << irq;  /* this does not work for irq >= 32 */

	/* And as last enable the irq at the hardware.
	 *
	 * Internal this activates the line or source of the given interrupt.
	 */
	if((irq_actids[hook->irq] &= ~hook->id) == 0)
		hw_intr_unmask(hook->irq);
}

/* Unregister an interrupt handler. */
void rm_irq_handler( irq_hook_t* hook ) {
	int irq = hook->irq; 
	int id = hook->id;
	irq_hook_t **line;

	if( irq < 0 || irq >= NR_IRQ_VECTORS ) 
		kernel_panic("invalid call to rm_irq_handler", irq);

	/* disable the irq.  */
	irq_actids[hook->irq] |= hook->id;
	hw_intr_mask(hook->irq);

	/* remove the hook.  */
	line = &irq_handlers[irq];

	while( (*line) != NULL ) {
		if((*line)->id == id) {
			(*line) = (*line)->next;

			if(!irq_handlers[irq])
				irq_use &= ~(1 << irq);

			if (irq_actids[irq] & id)
				irq_actids[irq] &= ~id;

			return;
		}

		line = &(*line)->next;
	}

	/* When the handler is not found, normally return here. */
}

/*
 * The function first disables interrupt is need be and restores the state at
 * the end. Before returning, it unmasks the IRQ if and only if all active ID
 * bits are cleared, and restart a process.
 */
void irq_handle(int irq)
{
	irq_hook_t * hook;

	IDLE_STOP;

	/* here we need not to get this IRQ until all the handlers had a say */
	hw_intr_mask(irq);
	hook = irq_handlers[irq];

	/* Call list of handlers for an IRQ. */
	while( hook != NULL ) {
		/* For each handler in the list, mark it active by setting its ID bit,
		 * call the function, and unmark it if the function returns true.
		 */
		irq_actids[irq] |= hook->id;

		/* Call the hooked function. */
		if( (*hook->handler)(hook) )
			irq_actids[hook->irq] &= ~hook->id;

		/* Next hooked function. */
		hook = hook->next;
	}

	/* reenable the IRQ only if there is no active handler */
	if (irq_actids[irq] == 0)
		hw_intr_unmask(irq);
}

/* Enable/Disable a interrupt line.  */
void enable_irq(irq_hook_t* hook)
{
	if((irq_actids[hook->irq] &= ~hook->id) == 0)
		hw_intr_unmask(hook->irq);
}

/* Return true if the interrupt was enabled before call.  */
int disable_irq(irq_hook_t* hook)
{
	if(irq_actids[hook->irq] & hook->id)  /* already disabled */
		return 0;

	irq_actids[hook->irq] |= hook->id;
	hw_intr_mask(hook->irq);

	return 1;
}

