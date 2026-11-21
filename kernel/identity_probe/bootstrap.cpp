// Build-only WDM bootstrap. No device, IOCTL, process access, or memory reads.
#include <ntddk.h>

extern "C" DRIVER_INITIALIZE DriverEntry;
extern "C" DRIVER_UNLOAD BootstrapUnload;

extern "C" VOID BootstrapUnload(_In_ PDRIVER_OBJECT driver_object)
{
    UNREFERENCED_PARAMETER(driver_object);
}

extern "C" NTSTATUS DriverEntry(_In_ PDRIVER_OBJECT driver_object,
                                _In_ PUNICODE_STRING registry_path)
{
    UNREFERENCED_PARAMETER(registry_path);
    driver_object->DriverUnload = BootstrapUnload;
    return STATUS_SUCCESS;
}
