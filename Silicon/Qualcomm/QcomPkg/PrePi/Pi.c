// Pi.c: Entry point for SEC(Security).

#include <PiPei.h>
#include <Pi/PiBootMode.h>

#include <Library/PrePiLib.h>
#include <Library/PrintLib.h>
#include <Library/PrePiHobListPointerLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/PlatformPrePiLib.h>
#include <Library/ArmMmuLib.h>
#include <Library/PlatformMemoryMapLib.h>

#include <Ppi/GuidedSectionExtraction.h>

#include "Pi.h"

#define IS_XIP()  (((UINT64)FixedPcdGet64 (PcdFdBaseAddress) > mSystemMemoryEnd) ||\
                  ((FixedPcdGet64 (PcdFdBaseAddress) + FixedPcdGet32 (PcdFdSize)) <= FixedPcdGet64 (PcdSystemMemoryBase)))

UINT64  mSystemMemoryEnd = FixedPcdGet64 (PcdSystemMemoryBase) +
                           FixedPcdGet64 (PcdSystemMemorySize) - 1;

VOID EFIAPI ProcessLibraryConstructorList(VOID);

STATIC
VOID
AddHob (
  PARM_MEMORY_REGION_DESCRIPTOR_EX Desc
  )
{
  BuildResourceDescriptorHob (
      Desc->ResourceType, Desc->ResourceAttribute, Desc->Address, Desc->Length);

  if (Desc->ResourceType == EFI_RESOURCE_SYSTEM_MEMORY &&
      Desc->MemoryType != EfiConventionalMemory)
  {
    BuildMemoryAllocationHob (Desc->Address, Desc->Length, Desc->MemoryType);
  }
}

VOID
EFIAPI
PeiExceptionHandler (
  IN UINT64 Esr,
  IN UINT64 Far,
  IN UINT64 Elr
  )
{
  // 1. Force screen checkpoint: Paint row 1800-1900 RED at bottom of screen
  EarlyPaintCheckpoint(1800, 100, 0x00FF0000);

  // 2. Pet and keep watchdog disabled
  *(volatile UINT32 *)(UINTN)0x0B017010 = 0xFFFFFFFF;
  *(volatile UINT32 *)(UINTN)0x0B017014 = 0xFFFFFFFF;
  *(volatile UINT32 *)(UINTN)0x0B017004 = 1;
  *(volatile UINT32 *)(UINTN)0x0B017008 = 0;

  // 3. Print detailed exception registers to screen
  DEBUG ((EFI_D_ERROR, "\n*** CPU EXCEPTION AT EL1/EL2 ***\n"));
  DEBUG ((EFI_D_ERROR, "ESR: 0x%016lx\n", Esr));
  DEBUG ((EFI_D_ERROR, "FAR: 0x%016lx\n", Far));
  DEBUG ((EFI_D_ERROR, "ELR: 0x%016lx\n", Elr));

  // 4. Trap and freeze forever petting watchdog so phone never reboots
  while (1) {
    *(volatile UINT32 *)(UINTN)0x0B017004 = 1;
    *(volatile UINT32 *)(UINTN)0x0B017008 = 0;
  }
}

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
    // Disable TGE so non-secure EL1 can execute freely without trapping to EL2
    ArmWriteHcr(0);

    /* Enable Timer access for non-secure EL1 and EL0
       The cnthctl_el2 register bits are architecturally
       UNKNOWN on reset.
       Disable event stream as it is not in use at this stage
    */
    ArmWriteCntHctl(CNTHCTL_EL2_EL1PCTEN | CNTHCTL_EL2_EL1PCEN);
  }

  /* Enable program flow prediction, if supported */
  ArmEnableBranchPrediction();

  // Checkpoint 1: PrePiMain Entry (Green indicator at rows 0-20)
  EarlyPaintCheckpoint(0, 20, 0x0000FF00);
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

  // Initialize MMU directly inside PrePiMain (prevents any function return across MMU boundary)
  {
    PARM_MEMORY_REGION_DESCRIPTOR_EX MemoryDescriptorEx = GetPlatformMemoryMap();
    ARM_MEMORY_REGION_DESCRIPTOR     MemoryDescriptor[MAX_ARM_MEMORY_REGION_DESCRIPTOR_COUNT];
    UINTN                            Index = 0;
    VOID                            *TranslationTableBase;
    UINTN                            TranslationTableSize;

    DEBUG((EFI_D_ERROR, "Configuring MMU in PrePiMain...\n"));

    while (MemoryDescriptorEx->Length != 0) {
      AddHob(MemoryDescriptorEx);

      ASSERT(Index < MAX_ARM_MEMORY_REGION_DESCRIPTOR_COUNT);
      MemoryDescriptor[Index].PhysicalBase = MemoryDescriptorEx->Address;
      MemoryDescriptor[Index].VirtualBase  = MemoryDescriptorEx->Address;
      MemoryDescriptor[Index].Length       = MemoryDescriptorEx->Length;
      MemoryDescriptor[Index].Attributes   = MemoryDescriptorEx->ArmAttributes;
      Index++;
      MemoryDescriptorEx++;
    }

    // Terminator
    ASSERT(Index < MAX_ARM_MEMORY_REGION_DESCRIPTOR_COUNT);
    MemoryDescriptor[Index].PhysicalBase = 0;
    MemoryDescriptor[Index].VirtualBase  = 0;
    MemoryDescriptor[Index].Length       = 0;
    MemoryDescriptor[Index].Attributes   = 0;

    DEBUG((EFI_D_ERROR, "Calling ArmConfigureMmu (%d descriptors)...\n", Index));
    Status = ArmConfigureMmu(MemoryDescriptor, &TranslationTableBase, &TranslationTableSize);
    DEBUG((EFI_D_ERROR, "MMU Configured: %r! TTBR0=%p\n", Status, TranslationTableBase));
    if (EFI_ERROR(Status)) {
      EarlyPaintCheckpoint(120, 60, 0x00FF0000);
      while (1) { *(volatile UINT32 *)(UINTN)0x0B017004 = 1; *(volatile UINT32 *)(UINTN)0x0B017008 = 0; }
    }

    // Checkpoint 2: MMU Configured and Enabled (Cyan indicator at rows 20-40)
    EarlyPaintCheckpoint(20, 20, 0x0000FFFF);

    if (FeaturePcdGet(PcdPrePiProduceMemoryTypeInformationHob)) {
      BuildMemoryTypeInformationHob();
    }
  }

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
  if (EFI_ERROR(Status)) {
    EarlyPaintCheckpoint(120, 60, 0x00FF0000);
    while (1) { *(volatile UINT32 *)(UINTN)0x0B017004 = 1; *(volatile UINT32 *)(UINTN)0x0B017008 = 0; }
  }

  // Checkpoint 3: PlatformPeim Done (Yellow indicator at rows 40-50)
  EarlyPaintCheckpoint(40, 10, 0x00FFFF00);

  // SEC phase needs to run library constructors by hand.
  DEBUG((EFI_D_ERROR, "ProcessLibraryConstructorList In\n"));
  ProcessLibraryConstructorList();
  DEBUG((EFI_D_ERROR, "ProcessLibraryConstructorList Out\n"));

  // Checkpoint 3b: Library Constructors Done (Orange indicator at rows 50-60)
  EarlyPaintCheckpoint(50, 10, 0x00FF8000);

  // Pet watchdog and give full timeout right before heavy decompression
  *(volatile UINT32 *)(UINTN)0x0B017010 = 0xFFFFFFFF;
  *(volatile UINT32 *)(UINTN)0x0B017014 = 0xFFFFFFFF;
  *(volatile UINT32 *)(UINTN)0x0B017004 = 1;
  *(volatile UINT32 *)(UINTN)0x0B017008 = 0;

  // Assume the FV that contains the SEC (our code) also contains a compressed FV.
  DEBUG((EFI_D_ERROR, "DecompressFirstFv In \n"));
  Status = DecompressFirstFv();
  DEBUG((EFI_D_ERROR, "DecompressFirstFv Out: %r\n", Status));
  if (EFI_ERROR(Status)) {
    EarlyPaintCheckpoint(120, 60, 0x00FF0000);
    while (1) { *(volatile UINT32 *)(UINTN)0x0B017004 = 1; *(volatile UINT32 *)(UINTN)0x0B017008 = 0; }
  }

  // Pet watchdog immediately after decompression
  *(volatile UINT32 *)(UINTN)0x0B017004 = 1;

  // Checkpoint 4: DXE FV Decompressed (Blue indicator at rows 60-70)
  EarlyPaintCheckpoint(60, 10, 0x000000FF);

  // Checkpoint 5: Transferring control to DXE Core (White indicator at rows 70-80)
  EarlyPaintCheckpoint(70, 10, 0x00FFFFFF);

  // Pet watchdog with max 3.7 minute timeout right before handing over to DXE Core
  *(volatile UINT32 *)(UINTN)0x0B017010 = 0xFFFFFFFF;
  *(volatile UINT32 *)(UINTN)0x0B017014 = 0xFFFFFFFF;
  *(volatile UINT32 *)(UINTN)0x0B017004 = 1;
  *(volatile UINT32 *)(UINTN)0x0B017008 = 0;

  // Load the DXE Core and transfer control to it
  DEBUG((EFI_D_ERROR, "LoadDxeCoreFromFv In \n"));
  Status = LoadDxeCoreFromFv(NULL, 0);
  DEBUG((EFI_D_ERROR, "LoadDxeCoreFromFv Out: %r\n", Status));

  // Checkpoint Error: LoadDxeCoreFromFv returned! (Magenta indicator at rows 80-100)
  EarlyPaintCheckpoint(80, 20, 0x00FF00FF);
  while (1) { *(volatile UINT32 *)(UINTN)0x0B017004 = 1; *(volatile UINT32 *)(UINTN)0x0B017008 = 0; }
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