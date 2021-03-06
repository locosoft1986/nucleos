#include <nucleos/unistd.h>
#include <nucleos/com.h>
#include <nucleos/kipc.h>
#include <nucleos/endpoint.h>
#include <nucleos/sysutil.h>
#include <nucleos/const.h>
#include <nucleos/type.h>
#include <nucleos/syslib.h>

#include <nucleos/types.h>
#include <nucleos/ipc.h>
#include <nucleos/shm.h>
#include <nucleos/sem.h>
#include <nucleos/mman.h>
#include <asm/servers/vm/vm.h>
#include <nucleos/vm.h>

#include <nucleos/time.h>
#include <stdio.h>
#include <nucleos/string.h>
#include <stdlib.h>
#include <nucleos/unistd.h>
#include <nucleos/errno.h>

int do_shmget(kipc_msg_t *);
int do_shmat(kipc_msg_t *);
int do_shmdt(kipc_msg_t *);
int do_shmctl(kipc_msg_t *);
int check_perm(struct ipc_perm *, endpoint_t, int);
void list_shm_ds(void);
void update_refcount_and_destroy(void);
int do_semget(kipc_msg_t *);
int do_semctl(kipc_msg_t *);
int do_semop(kipc_msg_t *);
int is_sem_nil(void);
int is_shm_nil(void);
void sem_process_vm_notify(void);

extern int identifier;
extern endpoint_t who_e;
extern int call_type;
extern endpoint_t SELF_E;
