/*
 *  Copyright (C) 2009  Ladislav Klenovic <klenovic@nucleonsoft.com>
 *
 *  This file is part of Nucleos kernel.
 *
 *  Nucleos kernel is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2 of the License.
 */
#ifndef __ASM_X86_SYSCALL_H
#define __ASM_X86_SYSCALL_H

/*
 * @nucleos: This is just a brainstorming proposal (temporaly here).
 *           Took from glibc to test how this could work.
 */

/* Alloc 36 bytes (KIPC_MESSAGE_SIZE) on process's stack. It's actually the message which is
 * used by kernel then.
 *
 * @nucleos: This allocation will be removed once the system call stuff is re-designed
 *           in way that it doesn't use the message from user space.
 */
#define ASM_SYSCALL_ALLOC_MESSAGE	"sub $36, %%esp\t\n"

/* Call the kernel to perform the system call (SYSCALL_VECTOR) */
#define ASM_SYSCALL_CALL_SYSTEM		"int $0x80\t\n"

/* Cleanup stack from message */
#define ASM_SYSCALL_DEALLOC_MESSAGE	"add $36, %%esp\t\n"

/* Get return code which was saved in message */
#define ASM_SYSCALL_GET_MSGRC		"mov 4(%%esp), %%eax\n\t"

/* Nucleos takes system call arguments in registers:

	syscall number	%eax	     call-clobbered
	arg 1		%ebx	     call-saved
	arg 2		%ecx	     call-clobbered
	arg 3		%edx	     call-clobbered
	arg 4		%esi	     call-saved
	arg 5		%edi	     call-saved
	arg 6		%ebp	     call-saved

   The stack layout upon entering the function is:

	24(%esp)	Arg# 6
	20(%esp)	Arg# 5
	16(%esp)	Arg# 4
	12(%esp)	Arg# 3
	 8(%esp)	Arg# 2
	 4(%esp)	Arg# 1
	  (%esp)	Return address

   (Of course a function with say 3 arguments does not have entries for
   arguments 4, 5, and 6.)

   The following code tries hard to be optimal.  A general assumption
   (which is true according to the data books I have) is that

	2 * xchg	is more expensive than	pushl + movl + popl

   Beside this a neat trick is used.  The calling conventions for Nucleos
   tell that among the registers used for parameters %ecx and %edx need
   not be saved.  Beside this we may clobber this registers even when
   they are not used for parameter passing.

   As a result one can see below that we save the content of the %ebx
   register in the %edx register when we have less than 3 arguments
   (2 * movl is less expensive than pushl + popl).

   Second unlike for the other registers we don't save the content of
   %ecx and %edx when we have more than 1 and 2 registers resp.

   The code below might look a bit long but we have to take care for
   the pipelined processors (i586).  Here the `pushl' and `popl'
   instructions are marked as NP (not pairable) but the exception is
   two consecutive of these instruction.  This gives no penalty on
   other processors though.  */

/* Define a macro which expands inline into the wrapper code for a system
   call.  */
#undef INLINE_SYSCALL
#define INLINE_SYSCALL(name, nr, args...)					\
({										\
	unsigned int resultvar = INTERNAL_SYSCALL (name, , nr, args);		\
	if (__builtin_expect (INTERNAL_SYSCALL_ERROR_P (resultvar, ), 0))	\
	{									\
		__set_errno (INTERNAL_SYSCALL_ERRNO (resultvar, ));		\
		resultvar = 0xffffffff;						\
	}									\
	(int) resultvar;							\
})

/* Define a macro which expands inline into the wrapper code for a system
   call.  This use is for internal calls that do not need to handle errors
   normally.  It will never touch errno.  This returns just what the kernel
   gave back.

   The _NCS variant allows non-constant syscall numbers but it is not
   possible to use more than four parameters.  */
#define INTERNAL_SYSCALL(name, err, nr, args...)	\
({							\
	register unsigned int resultvar;		\
	EXTRAVAR_##nr					\
	asm volatile (					\
		LOADARGS_##nr				\
		ASM_SYSCALL_ALLOC_MESSAGE		\
		"movl	%1, %%eax\n\t"			\
		ASM_SYSCALL_CALL_SYSTEM			\
		ASM_SYSCALL_GET_MSGRC			\
		ASM_SYSCALL_DEALLOC_MESSAGE		\
		RESTOREARGS_##nr			\
		: "=a" (resultvar)			\
		: "i" (__NR_##name) ASMFMT_##nr(args)	\
		: "memory", "cc"			\
	);						\
	(int) resultvar;				\
})

#define INTERNAL_SYSCALL_NCS(name, err, nr, args...)	\
({							\
	register unsigned int resultvar;		\
	EXTRAVAR_##nr					\
	asm volatile (					\
		LOADARGS_##nr				\
		ASM_SYSCALL_ALLOC_MESSAGE		\
		ASM_SYSCALL_CALL_SYSTEM			\
		ASM_SYSCALL_GET_MSGRC			\
		ASM_SYSCALL_DEALLOC_MESSAGE		\
		RESTOREARGS_##nr			\
		: "=a" (resultvar)			\
		: "0" (name) ASMFMT_##nr(args)		\
		: "memory", "cc"			\
	);						\
	(int) resultvar;				\
})

#undef INTERNAL_SYSCALL_ERROR_P
#define INTERNAL_SYSCALL_ERROR_P(val, err) \
	((unsigned int) (val) >= 0xfffff001u)

#undef INTERNAL_SYSCALL_ERRNO
#define INTERNAL_SYSCALL_ERRNO(val, err)	(-(val))

#define __set_errno(val)	(errno = (val))

#define LOADARGS_0
#ifdef __PIC__
# if defined I386_USE_SYSENTER && defined SHARED
#  define LOADARGS_1 \
    "bpushl .L__X'%k3, %k3\n\t"
#  define LOADARGS_5 \
    "movl %%ebx, %4\n\t"	\
    "movl %3, %%ebx\n\t"
# else
#  define LOADARGS_1 \
    "bpushl .L__X'%k2, %k2\n\t"
#  define LOADARGS_5 \
    "movl %%ebx, %3\n\t"	\
    "movl %2, %%ebx\n\t"
# endif
# define LOADARGS_2	LOADARGS_1
# define LOADARGS_3 \
    "xchgl %%ebx, %%edi\n\t"
# define LOADARGS_4	LOADARGS_3
#else
# define LOADARGS_1
# define LOADARGS_2
# define LOADARGS_3
# define LOADARGS_4
# define LOADARGS_5
#endif

#define RESTOREARGS_0
#ifdef __PIC__
# if defined I386_USE_SYSENTER && defined SHARED
#  define RESTOREARGS_1 \
    "bpopl .L__X'%k3, %k3\n\t"
#  define RESTOREARGS_5 \
    "movl %4, %%ebx"
# else
#  define RESTOREARGS_1 \
    "bpopl .L__X'%k2, %k2\n\t"
#  define RESTOREARGS_5 \
    "movl %3, %%ebx"
# endif
# define RESTOREARGS_2  RESTOREARGS_1
# define RESTOREARGS_3 \
    "xchgl %%edi, %%ebx\n\t"
# define RESTOREARGS_4  RESTOREARGS_3
#else
# define RESTOREARGS_1
# define RESTOREARGS_2
# define RESTOREARGS_3
# define RESTOREARGS_4
# define RESTOREARGS_5
#endif

#define ASMFMT_0()
#define ASMFMT_1(arg1) \
	, "b" (arg1)
#define ASMFMT_2(arg1, arg2) \
	, "b" (arg1), "c" (arg2)
#define ASMFMT_3(arg1, arg2, arg3) \
	, "b" (arg1), "c" (arg2), "d" (arg3)
#define ASMFMT_4(arg1, arg2, arg3, arg4) \
	, "b" (arg1), "c" (arg2), "d" (arg3), "S" (arg4)
#define ASMFMT_5(arg1, arg2, arg3, arg4, arg5) \
	, "b" (arg1), "c" (arg2), "d" (arg3), "S" (arg4), "D" (arg5)

#define EXTRAVAR_0
#define EXTRAVAR_1
#define EXTRAVAR_2
#define EXTRAVAR_3
#define EXTRAVAR_4
#ifdef __PIC__
# define EXTRAVAR_5 int _xv;
#else
# define EXTRAVAR_5
#endif

#if defined(__KERNEL__) || defined(__UKERNEL__)
/* @nucleos: define some usefull stuffs here (see linux source) */

#include <nucleos/kipc.h>

extern int errno;
static inline int ksyscall(int who, int syscallnr, message *msgptr)
{
	int status;

	msgptr->m_type = syscallnr;
	status = kipc_sendrec(who, msgptr);

	if (status != 0) {
		/* 'sendrec' itself failed. */
		/* XXX - strerror doesn't know all the codes */
		msgptr->m_type = status;
	}

	if (msgptr->m_type < 0) {
		errno = -msgptr->m_type;
		return(-1);
	}

	return(msgptr->m_type);
}

static inline int ktaskcall(endpoint_t who, int syscallnr, register message *msgptr)
{
	int status;

	msgptr->m_type = syscallnr;
	status = kipc_sendrec(who, msgptr);

	if (status != 0)
		return(status);

	return(msgptr->m_type);
}

#endif /* defined(__KERNEL__) || defined(__UKERNEL__) */

#endif /* __ASM_X86_SYSCALL_H */