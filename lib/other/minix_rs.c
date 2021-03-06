#include <nucleos/unistd.h>
#include <nucleos/com.h>
#include <nucleos/ipc.h>
#include <nucleos/endpoint.h>
#include <nucleos/sysutil.h>
#include <nucleos/syslib.h>
#include <nucleos/const.h>
#include <nucleos/type.h>
#include <servers/ds/ds.h>
#include <nucleos/errno.h>
#include <nucleos/string.h>

#include <stdio.h>
#include <stdlib.h>

int minix_rs_lookup(const char *name, endpoint_t *value)
{
	int r;
	kipc_msg_t m;
	size_t len_key;

	len_key = strlen(name)+1;

	m.RS_NAME = (char *) name;
	m.RS_NAME_LEN = len_key;

	r = ktaskcall(RS_PROC_NR, RS_LOOKUP, &m);

	if(r == 0) {
		*value = m.RS_ENDPOINT;
	}

	return r;
}
