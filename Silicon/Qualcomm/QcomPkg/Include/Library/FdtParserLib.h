/** @file
 *
 * Device Tree Parse Library
 *
 * Copyright (c) 2021, BigfootACA <bigfoot@classfun.cn>
 *
 **/

#ifndef _FDT_PARSER_LIB
#define _FDT_PARSER_LIB
#include <Uefi.h>
#include <Library/DebugLib.h>
#include <Library/PrintLib.h>
#include <Library/BaseMemoryLib.h>
#include <KernelFdt.h>
#include <fdtparser.h>
#include <param.h>
#include <keyval.h>

#include "onclite_dtb.h"

STATIC
inline
fdt*
EFIAPI
GetFdt(VOID)
{
  EFI_PHYSICAL_ADDRESS FdtAddress;
  #ifndef FDT_DIRECT
  EFI_STATUS          Status;
  KERNEL_FDT_PROTOCOL *Fdt;

  Status = gBS->LocateProtocol (
    &gKernelFdtProtocolGuid,
    NULL,
    (VOID**)&Fdt
    );
  if (EFI_ERROR (Status)) {
    DEBUG((EFI_D_WARN, "Locate Kernel Fdt Protocol failed, using built-in FDT\n"));
    return get_fdt_from_pointer ((VOID*)_tmp_edk2_msm_Platform_Xiaomi_msm8953_FdtBlob_compat_onclite_dtb);
  }
  if (Fdt == NULL || Fdt->Fdt == NULL) {
    DEBUG((EFI_D_WARN, "Invalid Device Tree, using built-in FDT\n"));
    return get_fdt_from_pointer ((VOID*)_tmp_edk2_msm_Platform_Xiaomi_msm8953_FdtBlob_compat_onclite_dtb);
  }
  FdtAddress = (EFI_PHYSICAL_ADDRESS)Fdt->Fdt;
  #else
  FdtAddress = *(EFI_PHYSICAL_ADDRESS*)FixedPcdGet64 (PcdDeviceTreeStore);
  DEBUG((EFI_D_ERROR, "DTB store at 0x%016lx contains 0x%016lx\n",
         (UINT64)FixedPcdGet64(PcdDeviceTreeStore), (UINT64)FdtAddress));

  // Check if FdtAddress is a valid pointer in DDR (>= 0x80000000) and contains FDT magic
  if (FdtAddress >= 0x80000000 && FdtAddress < 0xF2000000 && *(volatile UINT32 *)(UINTN)FdtAddress == 0xedfe0dd0) {
    DEBUG((EFI_D_ERROR, "Valid FDT magic found at DTB store address 0x%016lx\n", (UINT64)FdtAddress));
  } else {
    DEBUG((EFI_D_ERROR, "Using built-in fallback FDT (2 GiB DDR)\n"));
    FdtAddress = (EFI_PHYSICAL_ADDRESS)(UINTN)_tmp_edk2_msm_Platform_Xiaomi_msm8953_FdtBlob_compat_onclite_dtb;
  }
  #endif

  DEBUG((EFI_D_ERROR, "Device Tree Address: 0x%016lx\n", FdtAddress));
  return get_fdt_from_pointer ((VOID*)FdtAddress);
}

STATIC
inline
VOID
EFIAPI
CmdlineDumpItemDebug(IN UINTN Level, IN CHAR8 *Prefix, IN keyval *Item)
{
	if (Item == NULL) return;
	DEBUG((
		Level,
		"%aKey = \"%a\", Value = \"%a\"\n",
		(Prefix != NULL ? Prefix : ""),
		Item->key, Item->value
	));
}

STATIC
inline
VOID
EFIAPI
CmdlineDumpItemsDebug(IN UINTN Level, IN CHAR8 *Prefix, IN keyval **Items)
{
	CHAR8 String[256];
	if (Items == NULL) return;
	KVARR_FOREACH (Items, Item, Index) {
		ZeroMem(String, sizeof (String));
		AsciiSPrint(
			String, sizeof (String), "%aItem %d: ",
			(Prefix != NULL ? Prefix : ""), Index + 1
		);
		CmdlineDumpItemDebug(Level, String, Item);
	}
}

#endif
