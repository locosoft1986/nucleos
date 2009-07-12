/*
 *  Copyright (C) 2009  Ladislav Klenovic <klenovic@nucleonsoft.com>
 *
 *  This file is part of Nucleos kernel.
 *
 *  Nucleos kernel is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2 of the License.
 */
#ifndef __SERVER_VM_SANITYCHECK_H
#define __SERVER_VM_SANITYCHECK_H

#include <servers/vm/vm.h>
#include <servers/vm/glo.h>

#if SANITYCHECKS

/* This macro is used in the sanity check functions, where file and 
 * line are function arguments.
 */
#define MYASSERT(c) do { if(!(c)) { \
        printf("VM:%s:%d: %s failed\n", file, line, #c); \
	vm_panic("sanity check failed", NO_NUM); } } while(0)

#define SANITYCHECK(l) if(!nocheck && ((l) <= vm_sanitychecklevel)) {  \
		int failflag = 0; \
		u32_t *origptr = CHECKADDR;\
		int _sanep; \
		struct vmproc *vmp;	\
					\
                for(_sanep = 0; _sanep < sizeof(data1) / sizeof(*origptr); \
			_sanep++) {    \
                        if(origptr[_sanep] != data1[_sanep]) {    \
                                printf("%d: %08lx != %08lx  ", \
		_sanep, origptr[_sanep], data1[_sanep]); failflag = 1;   \
                        }                       \
                }                               \
        if(failflag) {				\
		printf("%s:%d: memory corruption test failed\n", \
			__FILE__, __LINE__); 		\
		vm_panic("memory corruption", NO_NUM);	\
	}  \
	for(vmp = vmproc; vmp <= &vmproc[NR_PROCS]; vmp++) { \
		if((vmp->vm_flags & (VMF_INUSE | VMF_HASPT)) == \
			(VMF_INUSE | VMF_HASPT)) { \
			pt_sanitycheck(&vmp->vm_pt, __FILE__, __LINE__); \
		} \
	} \
	map_sanitycheck(__FILE__, __LINE__); \
	} 
#else
#define SANITYCHECK 
#endif

#endif /* __SERVER_VM_SANITYCHECK_H */