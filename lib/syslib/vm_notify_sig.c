#include <nucleos/syslib.h>
#include <nucleos/vm.h>

/*===========================================================================*
 *                                vm_notify_sig				     *
 *===========================================================================*/
int vm_notify_sig(endpoint_t ep, endpoint_t ipc_ep)
{
    kipc_msg_t m;
    int result;

    m.VM_NOTIFY_SIG_ENDPOINT = ep;
    m.VM_NOTIFY_SIG_IPC = ipc_ep;

    result = ktaskcall(VM_PROC_NR, VM_NOTIFY_SIG, &m);
    return(result);
}

