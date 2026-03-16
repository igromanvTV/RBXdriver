#include <ntifs.h>
#include <ntddk.h>
#include <ntstatus.h>
#include "../../../shared/ioctls.h"

typedef struct _Offs {
    ULONG Links, Start, End, Flags, Pid, Active, Root, Hint, Num, VadLock;
} Offs;
Offs g_Offs = { 0 };

#define POOL_TAG_READ 'daER'
#define POOL_TAG_WRITE 'tirW'
#define POOL_TAG_GENERAL 'rbxD'
#define POOL_TAG_VAD 'daVr'

#define SAFE_OB_DEREFERENCE(obj) \
    if (obj) { \
        ObDereferenceObject(obj); \
        obj = NULL; \
    }

NTSYSAPI NTSTATUS NTAPI ZwProtectVirtualMemory(HANDLE p, PVOID* b, PSIZE_T s, ULONG n, PULONG o);
NTSYSAPI NTSTATUS NTAPI ZwFreeVirtualMemory(HANDLE p, PVOID* b, PSIZE_T s, ULONG f);

BOOLEAN Resolve() {
    RTL_OSVERSIONINFOW v = { 0 };
    v.dwOSVersionInfoSize = sizeof(v);
    if (!NT_SUCCESS(RtlGetVersion(&v))) return FALSE;
    ULONG build = v.dwBuildNumber;
    g_Offs.Links = 0x0; g_Offs.Start = 0x18; g_Offs.End = 0x1C; g_Offs.Flags = 0x30;
    if (build >= 26100) {
        g_Offs.Pid = 0x1D0; g_Offs.Active = 0x1D8; g_Offs.Root = 0x558; g_Offs.Hint = 0x560; g_Offs.Num = 0x568; g_Offs.VadLock = 0x5F8;
    }
    else {
        g_Offs.Pid = 0x440; g_Offs.Active = 0x448; g_Offs.Root = 0x7D8; g_Offs.Hint = 0x7E0; g_Offs.Num = 0x7E8; g_Offs.VadLock = 0x478;
    }
    return TRUE;
}

PDEVICE_OBJECT g_DevObj = NULL;
UNICODE_STRING g_Name, g_Sym;
void Unload(PDRIVER_OBJECT d);
NTSTATUS Dispatch(PDEVICE_OBJECT d, PIRP i);
NTSTATUS Control(PDEVICE_OBJECT d, PIRP i);
NTKERNELAPI NTSTATUS IoCreateDriver(PUNICODE_STRING n, PDRIVER_INITIALIZE i);
WCHAR g_BufD[64]; WCHAR g_BufV[64];

NTSTATUS Init(PDRIVER_OBJECT d, PUNICODE_STRING r) {
    UNREFERENCED_PARAMETER(r);
    if (!Resolve()) return STATUS_NOT_SUPPORTED;
    LARGE_INTEGER tick; KeQueryTickCount(&tick);
    g_BufV[0] = L'\\'; g_BufV[1] = L'D'; g_BufV[2] = L'e'; g_BufV[3] = L'v'; g_BufV[4] = L'i'; g_BufV[5] = L'c'; g_BufV[6] = L'e'; g_BufV[7] = L'\\';
    g_BufV[8] = L'r'; g_BufV[9] = L'b'; g_BufV[10] = L'x'; g_BufV[11] = L'd'; g_BufV[12] = L'r'; g_BufV[13] = L'v'; g_BufV[14] = L'_';
    ULONG val = tick.LowPart;
    for (int i = 0; i < 8; i++) {
        ULONG nibble = (val >> (28 - i * 4)) & 0xF;
        g_BufV[15 + i] = (WCHAR)(nibble < 10 ? L'0' + nibble : L'A' + (nibble - 10));
    }
    g_BufV[23] = L'\0';
    RtlInitUnicodeString(&g_Name, g_BufV); RtlInitUnicodeString(&g_Sym, L"\\DosDevices\\rbxdrv");
    IoDeleteSymbolicLink(&g_Sym);
    NTSTATUS status = IoCreateDevice(d, 0, &g_Name, RBX_DEVICE_TYPE, 0, FALSE, &g_DevObj);
    if (!NT_SUCCESS(status)) return status;
    status = IoCreateSymbolicLink(&g_Sym, &g_Name);
    if (!NT_SUCCESS(status)) { IoDeleteDevice(g_DevObj); return status; }
    d->MajorFunction[IRP_MJ_CREATE] = Dispatch;
    d->MajorFunction[IRP_MJ_CLOSE] = Dispatch;
    d->MajorFunction[IRP_MJ_DEVICE_CONTROL] = Control;
    d->DriverUnload = Unload;
    g_DevObj->Flags |= DO_BUFFERED_IO; g_DevObj->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}

NTSTATUS DriverEntry(PDRIVER_OBJECT d, PUNICODE_STRING r) {
    UNREFERENCED_PARAMETER(d); UNREFERENCED_PARAMETER(r);
    LARGE_INTEGER tick; KeQueryTickCount(&tick);
    g_BufD[0] = L'\\'; g_BufD[1] = L'D'; g_BufD[2] = L'r'; g_BufD[3] = L'i'; g_BufD[4] = L'v'; g_BufD[5] = L'e'; g_BufD[6] = L'r'; g_BufD[7] = L'\\';
    g_BufD[8] = L'r'; g_BufD[9] = L'b'; g_BufD[10] = L'x'; g_BufD[11] = L'd'; g_BufD[12] = L'r'; g_BufD[13] = L'v'; g_BufD[14] = L'_';
    ULONG val = tick.LowPart ^ 0xDEADBEEF;
    for (int i = 0; i < 8; i++) {
        ULONG nibble = (val >> (28 - i * 4)) & 0xF;
        g_BufD[15 + i] = (WCHAR)(nibble < 10 ? L'0' + nibble : L'A' + (nibble - 10));
    }
    g_BufD[23] = L'\0';
    UNICODE_STRING dn; RtlInitUnicodeString(&dn, g_BufD);
    return IoCreateDriver(&dn, &Init);
}

void Unload(PDRIVER_OBJECT d) {
    UNREFERENCED_PARAMETER(d);
    IoDeleteSymbolicLink(&g_Sym);
    IoDeleteDevice(g_DevObj);
}

NTSTATUS Dispatch(PDEVICE_OBJECT d, PIRP i) {
    UNREFERENCED_PARAMETER(d);
    i->IoStatus.Status = STATUS_SUCCESS;
    i->IoStatus.Information = 0;
    IoCompleteRequest(i, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

LONG GetNodeHeight(PRTL_BALANCED_NODE node) {
    if (!node || !MmIsAddressValid(node)) return 0;
    return (LONG)(node->ParentValue & 3);
}

PRTL_BALANCED_NODE GetParent(PRTL_BALANCED_NODE node) {
    if (!node || !MmIsAddressValid(node)) return NULL;
    return (PRTL_BALANCED_NODE)(node->ParentValue & ~3);
}

VOID UpdateNodeHeight(PRTL_BALANCED_NODE node) {
    if (!node || !MmIsAddressValid(node)) return;
    LONG leftH = GetNodeHeight(node->Left);
    LONG rightH = GetNodeHeight(node->Right);
    LONG newH = (leftH > rightH ? leftH : rightH) + 1;
    node->ParentValue = ((ULONG_PTR)GetParent(node) & ~3) | (newH & 3);
}

VOID RotateLeft(PRTL_BALANCED_NODE* root, PRTL_BALANCED_NODE x) {
    if (!root || !x || !MmIsAddressValid(x)) return;

    PRTL_BALANCED_NODE y = x->Right;
    if (!y || !MmIsAddressValid(y)) return;

    PRTL_BALANCED_NODE parent = GetParent(x);

    x->Right = y->Left;
    if (y->Left && MmIsAddressValid(y->Left))
        y->Left->ParentValue = ((ULONG_PTR)x & ~3) | (GetNodeHeight(y->Left) & 3);

    y->Left = x;
    y->ParentValue = ((ULONG_PTR)parent & ~3) | (GetNodeHeight(y) & 3);
    x->ParentValue = ((ULONG_PTR)y & ~3) | (GetNodeHeight(x) & 3);

    if (!parent) *root = y;
    else if (parent->Left == x) parent->Left = y;
    else parent->Right = y;

    UpdateNodeHeight(x);
    UpdateNodeHeight(y);
}

VOID RotateRight(PRTL_BALANCED_NODE* root, PRTL_BALANCED_NODE y) {
    if (!root || !y || !MmIsAddressValid(y)) return;

    PRTL_BALANCED_NODE x = y->Left;
    if (!x || !MmIsAddressValid(x)) return;

    PRTL_BALANCED_NODE parent = GetParent(y);

    y->Left = x->Right;
    if (x->Right && MmIsAddressValid(x->Right))
        x->Right->ParentValue = ((ULONG_PTR)y & ~3) | (GetNodeHeight(x->Right) & 3);

    x->Right = y;
    x->ParentValue = ((ULONG_PTR)parent & ~3) | (GetNodeHeight(x) & 3);
    y->ParentValue = ((ULONG_PTR)x & ~3) | (GetNodeHeight(y) & 3);

    if (!parent) *root = x;
    else if (parent->Left == y) parent->Left = x;
    else parent->Right = x;

    UpdateNodeHeight(y);
    UpdateNodeHeight(x);
}

VOID RebalanceFrom(PRTL_BALANCED_NODE* root, PRTL_BALANCED_NODE start) {
    if (!root || !start || !MmIsAddressValid(start)) return;

    PRTL_BALANCED_NODE node = start;
    int maxIterations = 2000;
    int iter = 0;
    while (node && MmIsAddressValid(node) && iter++ < maxIterations) {
        LONG leftH = GetNodeHeight(node->Left);
        LONG rightH = GetNodeHeight(node->Right);
        LONG balance = rightH - leftH;

        if (balance > 1) {
            if (!node->Right || !MmIsAddressValid(node->Right)) break;
            if (GetNodeHeight(node->Right->Right) >= GetNodeHeight(node->Right->Left))
                RotateLeft(root, node);
            else {
                RotateRight(&node->Right, node->Right);
                RotateLeft(root, node);
            }
        }
        else if (balance < -1) {
            if (!node->Left || !MmIsAddressValid(node->Left)) break;
            if (GetNodeHeight(node->Left->Left) >= GetNodeHeight(node->Left->Right))
                RotateRight(root, node);
            else {
                RotateLeft(&node->Left, node->Left);
                RotateRight(root, node);
            }
        }
        else {
            UpdateNodeHeight(node);
        }

        node = GetParent(node);
    }
}

BOOLEAN UnlinkVad(PEPROCESS p, ULONG_PTR a) {
    if (KeGetCurrentIrql() > APC_LEVEL) return FALSE;
    if (!p || !MmIsAddressValid(p)) return FALSE;
    if (PsGetProcessExitStatus(p) != STATUS_PENDING) return FALSE;

    ULONG_PTR vpn = a >> PAGE_SHIFT;
    PVOID vadLock = (PUCHAR)p + g_Offs.VadLock;
    if (!MmIsAddressValid(vadLock)) return FALSE;

    PRTL_BALANCED_NODE* root = (PRTL_BALANCED_NODE*)((PUCHAR)p + g_Offs.Root);
    if (!MmIsAddressValid(root)) return FALSE;

    BOOLEAN lockAcquired = FALSE;
    BOOLEAN result = FALSE;
    PRTL_BALANCED_NODE successor = NULL;

    __try {
        ExAcquirePushLockExclusive(vadLock);
        lockAcquired = TRUE;

        PRTL_BALANCED_NODE node = *root;
        PRTL_BALANCED_NODE parent = NULL;
        PRTL_BALANCED_NODE nodeToDelete = NULL;

        int depth = 0;
        while (node && depth++ < 1000) {
            if (!MmIsAddressValid(node)) __leave;

            ULONG start = *(ULONG*)((PUCHAR)node + g_Offs.Start);
            ULONG end = *(ULONG*)((PUCHAR)node + g_Offs.End);

            if (vpn >= start && vpn <= end) {
                nodeToDelete = node;
                break;
            }

            parent = node;
            node = (vpn < start) ? node->Left : node->Right;
        }

        if (!nodeToDelete) __leave;

        PRTL_BALANCED_NODE rebalanceStart = parent;

        if (!nodeToDelete->Left && !nodeToDelete->Right) {
            if (!parent) *root = NULL;
            else if (parent->Left == nodeToDelete) parent->Left = NULL;
            else parent->Right = NULL;
        }
        else if (!nodeToDelete->Left || !nodeToDelete->Right) {
            PRTL_BALANCED_NODE child = nodeToDelete->Left ? nodeToDelete->Left : nodeToDelete->Right;
            if (MmIsAddressValid(child)) {
                if (!parent) *root = child;
                else if (parent->Left == nodeToDelete) parent->Left = child;
                else parent->Right = child;
                child->ParentValue = ((ULONG_PTR)parent & ~3) | (GetNodeHeight(child) & 3);
            }
        }
        else {
            successor = nodeToDelete->Right;
            PRTL_BALANCED_NODE successorParent = nodeToDelete;

            if (!MmIsAddressValid(successor)) __leave;

            while (successor->Left && MmIsAddressValid(successor->Left)) {
                successorParent = successor;
                successor = successor->Left;
            }

            *(ULONG*)((PUCHAR)nodeToDelete + g_Offs.Start) = *(ULONG*)((PUCHAR)successor + g_Offs.Start);
            *(ULONG*)((PUCHAR)nodeToDelete + g_Offs.End) = *(ULONG*)((PUCHAR)successor + g_Offs.End);
            *(ULONG*)((PUCHAR)nodeToDelete + g_Offs.Flags) = *(ULONG*)((PUCHAR)successor + g_Offs.Flags);

            if (successorParent->Left == successor) {
                successorParent->Left = successor->Right;
                if (successor->Right && MmIsAddressValid(successor->Right)) {
                    successor->Right->ParentValue = ((ULONG_PTR)successorParent & ~3) | (GetNodeHeight(successor->Right) & 3);
                }
            }
            else {
                successorParent->Right = successor->Right;
                if (successor->Right && MmIsAddressValid(successor->Right)) {
                    successor->Right->ParentValue = ((ULONG_PTR)successorParent & ~3) | (GetNodeHeight(successor->Right) & 3);
                }
            }

            nodeToDelete = successor;
            parent = successorParent;
            rebalanceStart = successorParent;
        }

        if (nodeToDelete && MmIsAddressValid(nodeToDelete)) {
            RtlZeroMemory((PUCHAR)nodeToDelete + g_Offs.Start, sizeof(ULONG) * 3);
            nodeToDelete->Left = NULL;
            nodeToDelete->Right = NULL;
            nodeToDelete->ParentValue = 0;
        }

        PVOID hintField = (PUCHAR)p + g_Offs.Hint;
        if (MmIsAddressValid(hintField)) {
            PRTL_BALANCED_NODE hint = *(PRTL_BALANCED_NODE*)hintField;
            if (hint == nodeToDelete || hint == successor) {
                *(PRTL_BALANCED_NODE*)hintField = NULL;
            }
        }

        PVOID numField = (PUCHAR)p + g_Offs.Num;
        if (MmIsAddressValid(numField)) {
            if (g_Offs.Num == 0x568) {
                ULONGLONG count = *(PULONGLONG)numField;
                if (count > 0) *(PULONGLONG)numField = count - 1;
            }
            else {
                ULONG_PTR count = *(PULONG_PTR)numField;
                if (count > 0) *(PULONG_PTR)numField = count - 1;
            }
        }

        if (*root && rebalanceStart && MmIsAddressValid(rebalanceStart)) {
            RebalanceFrom(root, rebalanceStart);
        }

        result = TRUE;
    }
    __finally {
        if (lockAcquired) {
            ExReleasePushLockExclusive(vadLock);
        }
    }

    return result;
}

NTSTATUS AllocMem(PALLOCATE_MEMORY_REQ req) {
    if (KeGetCurrentIrql() > APC_LEVEL) return STATUS_UNSUCCESSFUL;
    if (!req || !req->TargetPid || req->Size == 0 || req->Size > MAX_ALLOCATION_SIZE)
        return STATUS_INVALID_PARAMETER;

    PEPROCESS proc = NULL;
    NTSTATUS status = PsLookupProcessByProcessId(req->TargetPid, &proc);
    if (!NT_SUCCESS(status)) return status;
    if (!proc || !MmIsAddressValid(proc)) {
        SAFE_OB_DEREFERENCE(proc);
        return STATUS_INVALID_PARAMETER;
    }

    KAPC_STATE apc;
    BOOLEAN attached = FALSE;
    __try {
        KeStackAttachProcess(proc, &apc);
        attached = TRUE;

        PVOID base = NULL;
        SIZE_T zero = 0;
        SIZE_T size = req->Size;

        status = ZwAllocateVirtualMemory(ZwCurrentProcess(), &base, zero, &size,
            MEM_COMMIT | MEM_RESERVE, req->Protect);
        if (NT_SUCCESS(status)) {
            req->RemoteBase = base;
            req->Size = size;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        status = GetExceptionCode();
    }
    if (attached) KeUnstackDetachProcess(&apc);

    SAFE_OB_DEREFERENCE(proc);
    return status;
}

NTSTATUS MmCopyVirtualMemory(PEPROCESS f, PVOID fa, PEPROCESS t, PVOID ta, SIZE_T s, KPROCESSOR_MODE m, PSIZE_T bs);

NTSTATUS ReadMem(PREAD_MEMORY_REQ req) {
    if (KeGetCurrentIrql() > APC_LEVEL) return STATUS_UNSUCCESSFUL;
    if (!req || !req->TargetPid || !req->RemoteBase || !req->Buffer ||
        req->Size == 0 || req->Size > MAX_MEMORY_OPERATION_SIZE)
        return STATUS_INVALID_PARAMETER;

    PEPROCESS proc;
    NTSTATUS status = PsLookupProcessByProcessId(req->TargetPid, &proc);
    if (!NT_SUCCESS(status)) return status;
    if (!proc || !MmIsAddressValid(proc)) {
        SAFE_OB_DEREFERENCE(proc);
        return STATUS_INVALID_PARAMETER;
    }

    PVOID buf = ExAllocatePoolWithTag(NonPagedPoolNx, req->Size, POOL_TAG_READ);
    if (!buf) {
        SAFE_OB_DEREFERENCE(proc);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(buf, req->Size);

    SIZE_T copied = 0;
    __try {
        status = MmCopyVirtualMemory(proc, req->RemoteBase, PsGetCurrentProcess(), buf,
            req->Size, KernelMode, &copied);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        status = GetExceptionCode();
    }

    if (NT_SUCCESS(status) && copied == req->Size) {
        __try {
            ProbeForWrite(req->Buffer, req->Size, 1);
            RtlCopyMemory(req->Buffer, buf, req->Size);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            status = GetExceptionCode();
        }
    }

    ExFreePoolWithTag(buf, POOL_TAG_READ);
    SAFE_OB_DEREFERENCE(proc);
    return status;
}

NTSTATUS WriteMem(PWRITE_MEMORY_REQ req) {
    if (KeGetCurrentIrql() > APC_LEVEL) return STATUS_UNSUCCESSFUL;
    if (!req || !req->TargetPid || !req->RemoteBase || !req->Buffer ||
        req->Size == 0 || req->Size > MAX_MEMORY_OPERATION_SIZE)
        return STATUS_INVALID_PARAMETER;

    PEPROCESS proc = NULL;
    NTSTATUS status = PsLookupProcessByProcessId(req->TargetPid, &proc);
    if (!NT_SUCCESS(status)) return status;
    if (!proc || !MmIsAddressValid(proc)) {
        SAFE_OB_DEREFERENCE(proc);
        return STATUS_INVALID_PARAMETER;
    }

    PVOID buf = ExAllocatePoolWithTag(NonPagedPoolNx, req->Size, POOL_TAG_WRITE);
    if (!buf) {
        SAFE_OB_DEREFERENCE(proc);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    __try {
        ProbeForRead(req->Buffer, req->Size, 1);
        RtlCopyMemory(buf, req->Buffer, req->Size);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        status = GetExceptionCode();
        ExFreePoolWithTag(buf, POOL_TAG_WRITE);
        SAFE_OB_DEREFERENCE(proc);
        return status;
    }

    SIZE_T copied = 0;
    __try {
        status = MmCopyVirtualMemory(PsGetCurrentProcess(), buf, proc, req->RemoteBase,
            req->Size, KernelMode, &copied);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        status = GetExceptionCode();
    }

    ExFreePoolWithTag(buf, POOL_TAG_WRITE);
    SAFE_OB_DEREFERENCE(proc);
    return status;
}

NTSTATUS ProtMem(PPROTECT_MEMORY_REQ req) {
    if (KeGetCurrentIrql() > APC_LEVEL) return STATUS_UNSUCCESSFUL;
    if (!req || !req->TargetPid || !req->RemoteBase || req->Size == 0)
        return STATUS_INVALID_PARAMETER;

    PEPROCESS proc = NULL;
    NTSTATUS status = PsLookupProcessByProcessId(req->TargetPid, &proc);
    if (!NT_SUCCESS(status)) return status;
    if (!proc || !MmIsAddressValid(proc)) {
        SAFE_OB_DEREFERENCE(proc);
        return STATUS_INVALID_PARAMETER;
    }

    KAPC_STATE apc;
    BOOLEAN attached = FALSE;
    __try {
        KeStackAttachProcess(proc, &apc);
        attached = TRUE;

        PVOID base = req->RemoteBase;
        SIZE_T size = req->Size;
        ULONG old = 0;
        status = ZwProtectVirtualMemory(ZwCurrentProcess(), &base, &size, req->NewProtect, &old);
        if (NT_SUCCESS(status)) req->OldProtect = old;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        status = GetExceptionCode();
    }
    if (attached) KeUnstackDetachProcess(&apc);

    SAFE_OB_DEREFERENCE(proc);
    return status;
}

NTSTATUS FreeMem(PFREE_MEMORY_REQ req) {
    if (KeGetCurrentIrql() > APC_LEVEL) return STATUS_UNSUCCESSFUL;
    if (!req || !req->TargetPid || !req->RemoteBase) return STATUS_INVALID_PARAMETER;

    PEPROCESS proc = NULL;
    NTSTATUS status = PsLookupProcessByProcessId(req->TargetPid, &proc);
    if (!NT_SUCCESS(status)) return status;
    if (!proc || !MmIsAddressValid(proc)) {
        SAFE_OB_DEREFERENCE(proc);
        return STATUS_INVALID_PARAMETER;
    }

    KAPC_STATE apc;
    BOOLEAN attached = FALSE;
    __try {
        KeStackAttachProcess(proc, &apc);
        attached = TRUE;

        PVOID base = req->RemoteBase;
        SIZE_T size = req->Size;
        status = ZwFreeVirtualMemory(ZwCurrentProcess(), &base, &size, MEM_RELEASE);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        status = GetExceptionCode();
    }
    if (attached) KeUnstackDetachProcess(&apc);

    SAFE_OB_DEREFERENCE(proc);
    return status;
}

NTSTATUS UnlinkReq(PUNLINK_VAD_REQ req) {
    if (KeGetCurrentIrql() > APC_LEVEL) return STATUS_UNSUCCESSFUL;
    if (!req || !req->TargetPid || !req->RemoteBase) return STATUS_INVALID_PARAMETER;

    PEPROCESS proc = NULL;
    NTSTATUS status = PsLookupProcessByProcessId(req->TargetPid, &proc);
    if (!NT_SUCCESS(status)) return status;
    if (!proc || !MmIsAddressValid(proc)) {
        SAFE_OB_DEREFERENCE(proc);
        return STATUS_INVALID_PARAMETER;
    }

    status = UnlinkVad(proc, (ULONG_PTR)req->RemoteBase) ? STATUS_SUCCESS : STATUS_NOT_FOUND;
    SAFE_OB_DEREFERENCE(proc);
    return status;
}

NTSTATUS Control(PDEVICE_OBJECT d, PIRP i) {
    UNREFERENCED_PARAMETER(d);

    if (KeGetCurrentIrql() > APC_LEVEL) {
        i->IoStatus.Status = STATUS_UNSUCCESSFUL;
        i->IoStatus.Information = 0;
        IoCompleteRequest(i, IO_NO_INCREMENT);
        return STATUS_UNSUCCESSFUL;
    }

    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(i);
    if (!stack) {
        i->IoStatus.Status = STATUS_INVALID_PARAMETER;
        i->IoStatus.Information = 0;
        IoCompleteRequest(i, IO_NO_INCREMENT);
        return STATUS_INVALID_PARAMETER;
    }

    ULONG code = stack->Parameters.DeviceIoControl.IoControlCode;
    ULONG inLen = stack->Parameters.DeviceIoControl.InputBufferLength;
    PVOID buf = i->AssociatedIrp.SystemBuffer;
    if (!buf) {
        i->IoStatus.Status = STATUS_INVALID_PARAMETER;
        i->IoStatus.Information = 0;
        IoCompleteRequest(i, IO_NO_INCREMENT);
        return STATUS_INVALID_PARAMETER;
    }

    NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;
    ULONG info = 0;

    __try {
        switch (code) {
        case IOCTL_ALLOCATE_MEMORY:
            if (inLen >= sizeof(ALLOCATE_MEMORY_REQ)) {
                status = AllocMem((PALLOCATE_MEMORY_REQ)buf);
                if (NT_SUCCESS(status)) info = sizeof(ALLOCATE_MEMORY_REQ);
            }
            break;
        case IOCTL_WRITE_MEMORY:
            if (inLen >= sizeof(WRITE_MEMORY_REQ)) {
                status = WriteMem((PWRITE_MEMORY_REQ)buf);
                if (NT_SUCCESS(status)) info = sizeof(WRITE_MEMORY_REQ);
            }
            break;
        case IOCTL_READ_MEMORY:
            if (inLen >= sizeof(READ_MEMORY_REQ)) {
                status = ReadMem((PREAD_MEMORY_REQ)buf);
                if (NT_SUCCESS(status)) info = sizeof(READ_MEMORY_REQ);
            }
            break;
        case IOCTL_PROTECT_MEMORY:
            if (inLen >= sizeof(PROTECT_MEMORY_REQ)) {
                status = ProtMem((PPROTECT_MEMORY_REQ)buf);
                if (NT_SUCCESS(status)) info = sizeof(PROTECT_MEMORY_REQ);
            }
            break;
        case IOCTL_FREE_MEMORY:
            if (inLen >= sizeof(FREE_MEMORY_REQ)) {
                status = FreeMem((PFREE_MEMORY_REQ)buf);
                if (NT_SUCCESS(status)) info = sizeof(FREE_MEMORY_REQ);
            }
            break;
        case IOCTL_UNLINK_VAD:
            if (inLen >= sizeof(UNLINK_VAD_REQ)) {
                status = UnlinkReq((PUNLINK_VAD_REQ)buf);
                if (NT_SUCCESS(status)) info = sizeof(UNLINK_VAD_REQ);
            }
            break;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        status = GetExceptionCode();
        info = 0;
    }

    i->IoStatus.Status = status;
    i->IoStatus.Information = info;
    IoCompleteRequest(i, IO_NO_INCREMENT);
    return status;
}