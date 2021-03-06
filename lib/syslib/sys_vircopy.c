/*
 *  Copyright (C) 2012  Ladislav Klenovic <klenovic@nucleonsoft.com>
 *
 *  This file is part of Nucleos kernel.
 *
 *  Nucleos kernel is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2 of the License.
 */
#include <nucleos/syslib.h>

int sys_vircopy(src_proc, src_seg, src_vir, 
	dst_proc, dst_seg, dst_vir, bytes)
endpoint_t src_proc;		/* source process */
int src_seg;			/* source memory segment */
vir_bytes src_vir;		/* source virtual address */
endpoint_t dst_proc;		/* destination process */
int dst_seg;			/* destination memory segment */
vir_bytes dst_vir;		/* destination virtual address */
phys_bytes bytes;		/* how many bytes */
{
/* Transfer a block of data.  The source and destination can each either be a
 * process number or ENDPT_SELF (to indicate own process number). Virtual addresses 
 * are offsets within LOCAL_SEG (text, stack, data), REMOTE_SEG, or BIOS_SEG. 
 */

  kipc_msg_t copy_mess;

  if (bytes == 0L) return 0;
  copy_mess.CP_SRC_ENDPT = src_proc;
  copy_mess.CP_SRC_SPACE = src_seg;
  copy_mess.CP_SRC_ADDR = (long) src_vir;
  copy_mess.CP_DST_ENDPT = dst_proc;
  copy_mess.CP_DST_SPACE = dst_seg;
  copy_mess.CP_DST_ADDR = (long) dst_vir;
  copy_mess.CP_NR_BYTES = (long) bytes;
  return(ktaskcall(SYSTASK, SYS_VIRCOPY, &copy_mess));
}
