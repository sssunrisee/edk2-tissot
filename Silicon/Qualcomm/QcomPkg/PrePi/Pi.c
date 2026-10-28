// Pi.c: Entry point for SEC(Security).

#include <PiPei.h>
#include <Pi/PiBootMode.h>

#include <Library/PrePiLib.h>
#include <Library/PrintLib.h>
#include <Library/PrePiHobListPointerLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/PlatformPrePiLib.h>

#include <Ppi/GuidedSectionExtraction.h>

#include "Pi.h"

#define IS_XIP()  (((UINT64)FixedPcdGet64 (PcdFdBaseAddress) > mSystemMemoryEnd) ||\
                  ((FixedPcdGet64 (PcdFdBaseAddress) + FixedPcdGet32 (PcdFdSize)) <= FixedPcdGet64 (PcdSystemMemoryBase)))

UINT64  mSystemMemoryEnd = FixedPcdGet64 (PcdSystemMemoryBase) +
                           FixedPcdGet64 (PcdSystemMemorySize) - 1;

VOID EFIAPI ProcessLibraryConstructorList(VOID);

VOID
PrePiMain(
  IN VOID *StackBase,
  IN UINTN StackSize
  )
{

  EFI_HOB_HANDOFF_INFO_TABLE *HobList;
  EFI_STATUS                  Status;

  UINTN MemoryBase     = 0;
  UINTN MemorySize     = 0;
  UINTN UefiMemoryBase = 0;
  UINTN UefiMemorySize = 0;

  // Architecture-specific initialization
  // Enable Floating Point
  ArmEnableVFP();

  if (ArmReadCurrentEL() == AARCH64_EL2) {
    // Trap General Exceptions. All exceptions that would be routed to EL1 are routed to EL2
    ArmWriteHcr(ARM_HCR_TGE);

    /* Enable Timer access for non-secure EL1 and EL0
       The cnthctl_el2 register bits are architecturally
       UNKNOWN on reset.
       Disable event stream as it is not in use at this stage
    */
    ArmWriteCntHctl(CNTHCTL_EL2_EL1PCTEN | CNTHCTL_EL2_EL1PCEN);
  }

  /* Enable program flow prediction, if supported */
  ArmEnableBranchPrediction();
  DEBUG((EFI_D_ERROR, "PrePiMain Started\n"));

  // Declare UEFI region
  MemoryBase     = FixedPcdGet32(PcdSystemMemoryBase);
  MemorySize     = FixedPcdGet32(PcdSystemMemorySize);
  UefiMemoryBase = FixedPcdGet32(PcdUefiMemPoolBase);
  UefiMemorySize = FixedPcdGet32(PcdUefiMemPoolSize);
  StackBase      = (VOID *)(UefiMemoryBase + UefiMemorySize - StackSize);

  DEBUG(
      (EFI_D_INFO | EFI_D_LOAD,
       "UEFI Memory Base = 0x%llx, Size = 0x%llx \n"
       "Stack Base = 0x%llx, Stack Size = 0x%llx \n",
       UefiMemoryBase, UefiMemorySize, StackBase, StackSize));

  // Set up HOB
  HobList = HobConstructor(
      (VOID *)UefiMemoryBase, UefiMemorySize, (VOID *)UefiMemoryBase,
      StackBase);

  PrePeiSetHobList (HobList);

  // Invalidate cache
  InvalidateDataCacheRange(
      (VOID *)(UINTN)PcdGet64(PcdFdBaseAddress), PcdGet32(PcdFdSize));

  // Initialize MMU
  DEBUG((EFI_D_ERROR, "Configuring MMU...\n"));
  Status = MemoryPeim(UefiMemoryBase, UefiMemorySize);
  DEBUG((EFI_D_ERROR, "Back in PrePiMain! Status: %r\n", Status));
  ASSERT_EFI_ERROR (Status);

  // Add HOBs
  DEBUG((EFI_D_ERROR, "Calling BuildStackHob...\n"));
  BuildStackHob ((UINTN)StackBase, StackSize);

  // TODO: Call CpuPei as a library
  DEBUG((EFI_D_ERROR, "Calling BuildCpuHob...\n"));
  BuildCpuHob (ArmGetPhysicalAddressBits (), PcdGet8 (PcdPrePiCpuIoSize));

  // Set the Boot Mode
  DEBUG((EFI_D_ERROR, "Calling SetBootMode...\n"));
  SetBootMode (BOOT_WITH_DEFAULT_SETTINGS);

  // Initialize Platform HOBs (CpuHob and FvHob)
  DEBUG((EFI_D_ERROR, "PlatformPeim In \n"));
  Status = PlatformPeim();
  DEBUG((EFI_D_ERROR, "PlatformPeim Out: %r\n", Status));
  ASSERT_EFI_ERROR (Status);

  // Now, the HOB List has been initialized, we can register performance information
  // PERF_START (NULL, "PEI", NULL, StartTimeStamp);

  // SEC phase needs to run library constructors by hand.
  DEBUG((EFI_D_ERROR, "ProcessLibraryConstructorList In\n"));
  ProcessLibraryConstructorList();
  DEBUG((EFI_D_ERROR, "ProcessLibraryConstructorList Out\n"));

  // Checkpoint 6: MMU and HOBs ready, decompressing DXE FV (Orange indicator at rows 4-8)
  EarlyPaintCheckpoint(4, 4, 0x00FF8000);

  // Assume the FV that contains the SEC (our code) also contains a compressed FV.
  DEBUG((EFI_D_ERROR, "DecompressFirstFv In \n"));
  Status = DecompressFirstFv();
  DEBUG((EFI_D_ERROR, "DecompressFirstFv Out: %r\n", Status));
  ASSERT_EFI_ERROR (Status);

  // Invalidate the raw FVMAIN_COMPACT FV HOB (0x9C000000) so DXE Core does not attempt
  // to mount and dispatch the compressed container. DXE Core will ONLY consume the decompressed FVMAIN.
  {
    EFI_PEI_HOB_POINTERS FvHobPtr;
    FvHobPtr.Raw = GetHobList ();
    while ((FvHobPtr.Raw = GetNextHob (EFI_HOB_TYPE_FV, FvHobPtr.Raw)) != NULL) {
      if (FvHobPtr.FirmwareVolume->BaseAddress == PcdGet64 (PcdFdBaseAddress)) {
        DEBUG ((EFI_D_ERROR, "Invalidating raw FV HOB at 0x%llx (FVMAIN_COMPACT) for DXE Core\n", FvHobPtr.FirmwareVolume->BaseAddress));
        FvHobPtr.Header->HobType = EFI_HOB_TYPE_UNUSED;
        break;
      }
      FvHobPtr.Raw = GET_NEXT_HOB (FvHobPtr);
    }
  }

  // Checkpoint 7: DXE Decompressed, jumping to DXE Core (White indicator at rows 8-12)
  EarlyPaintCheckpoint(8, 4, 0x00FFFFFF);

  // Load the DXE Core and transfer control to it
  DEBUG((EFI_D_ERROR, "LoadDxeCoreFromFv In \n"));
  Status = LoadDxeCoreFromFv(NULL, 0);
  DEBUG((EFI_D_ERROR, "LoadDxeCoreFromFv Out: %r\n", Status));
  ASSERT_EFI_ERROR (Status);
}

VOID
CEntryPoint(
  IN VOID *StackBase,
  IN UINTN StackSize
  )
{
  // Do platform specific initialization here
  PlatformInitialize();

  // Goto primary Main.
  PrePiMain(StackBase, StackSize);

  // DXE Core should always load and never return
  ASSERT(FALSE);
}

VOID
SecondaryCEntryPoint(
  IN  UINTN  MpId
  )
{
  // We must never get into this function on UniCore system
  ASSERT(FALSE);
}