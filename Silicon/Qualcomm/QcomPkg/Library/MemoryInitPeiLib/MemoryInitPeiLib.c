/** @file

  Copyright (c) 2011-2015, ARM Limited. All rights reserved.

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#define FDT_DIRECT
#include <PiPei.h>

#include <Library/ArmMmuLib.h>
#include <Library/ArmLib.h>
#include <Library/ArmPlatformLib.h>
#include <Library/DebugLib.h>
#include <Library/HobLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>
#include <Library/FdtParserLib.h>
#include <Library/PlatformMemoryMapLib.h>

#define SIZE_KB ((UINTN)(1024))
#define SIZE_MB ((UINTN)(SIZE_KB * 1024))
#define SIZE_GB ((UINTN)(SIZE_MB * 1024))
#define SIZE_MB_BIG(_Size,_Value) ((_Size) > ((_Value) * SIZE_MB))
#define SIZE_MB_SMALL(_Size,_Value) ((_Size) < ((_Value) * SIZE_MB))
#define SIZE_MB_IN(_Min,_Max,_Size) \
  if (SIZE_MB_BIG((MemoryTotal), (_Min)) && SIZE_MB_SMALL((MemoryTotal), (_Max)))\
    Mem = Mem##_Size##G, MemGB = _Size

extern UINT64 mSystemMemoryEnd;

VOID BuildMemoryTypeInformationHob(VOID);


STATIC
VOID InitMmu(IN ARM_MEMORY_REGION_DESCRIPTOR *MemoryTable)
{

  VOID *        TranslationTableBase;
  UINTN         TranslationTableSize;
  RETURN_STATUS Status;

  DEBUG((EFI_D_ERROR, "InitMmu: Calling ArmConfigureMmu...\n"));
  // Note: Because we called PeiServicesInstallPeiMemory() before
  // to call InitMmu() the MMU Page Table resides in
  // RAM (even at the top of DRAM as it is the first permanent memory
  // allocation)
  Status = ArmConfigureMmu(
      MemoryTable, &TranslationTableBase, &TranslationTableSize);

  ArmInvalidateDataCache();

  if (EFI_ERROR(Status)) {
    DEBUG((EFI_D_ERROR, "Error: Failed to enable MMU: %r\n", Status));
  } else {
    DEBUG((EFI_D_ERROR, "InitMmu: MMU Enabled! TTBR0=%p\n", TranslationTableBase));
  }
}

STATIC
VOID AddHob(PARM_MEMORY_REGION_DESCRIPTOR_EX Desc)
{
  BuildResourceDescriptorHob(
      Desc->ResourceType, Desc->ResourceAttribute, Desc->Address, Desc->Length);

  if (Desc->ResourceType == EFI_RESOURCE_SYSTEM_MEMORY &&
      Desc->MemoryType != EfiConventionalMemory)
  {
    BuildMemoryAllocationHob(Desc->Address, Desc->Length, Desc->MemoryType);
  }
}

/*++

Routine Description:



Arguments:

  FileHandle  - Handle of the file being invoked.
  PeiServices - Describes the list of possible PEI Services.

Returns:

  Status -  EFI_SUCCESS if the boot mode could be set

--*/
EFI_STATUS
EFIAPI
MemoryPeim(IN EFI_PHYSICAL_ADDRESS UefiMemoryBase, IN UINT64 UefiMemorySize)
{

  PARM_MEMORY_REGION_DESCRIPTOR_EX MemoryDescriptorEx =
      GetPlatformMemoryMap();
  ARM_MEMORY_REGION_DESCRIPTOR
        MemoryDescriptor[MAX_ARM_MEMORY_REGION_DESCRIPTOR_COUNT];
  UINTN Index = 0;
  UINTN Node = 0;
  UINTN MemoryBase = 0;
  UINTN MemorySize = 0;
  UINTN MemoryTotal = 0;
  DeviceMemoryAddHob Mem = Mem2G;
  UINT8 MemGB = 2;
  fdt *Fdt;

  DEBUG((EFI_D_ERROR, "MemoryPeim: Step 1 - GetFdt...\n"));
  Fdt = GetFdt();
  DEBUG((EFI_D_ERROR, "MemoryPeim: Step 2 - Fdt=%p\n", Fdt));
  if (Fdt != NULL) {
    while (fdt_get_memory(Fdt, (int)Node, (uint64_t*)&MemoryBase, (uint64_t*)&MemorySize)) {
      MemoryTotal += MemorySize;
      DEBUG((
        EFI_D_ERROR,
        "FDT Memory %d: 0x%llx - 0x%llx\n",
        Node, MemoryBase, (MemoryBase + MemorySize)
      ));
      Node++;
    }
  } else {
    DEBUG((EFI_D_ERROR, "Fdt is NULL, continuing with platform memory map\n"));
  }

  // Memory   Min    Max   Config
  SIZE_MB_IN (1024,  2560, 2);
  SIZE_MB_IN (2560,  3584, 3);
  SIZE_MB_IN (3584,  4608, 4);
  SIZE_MB_IN (5120,  6656, 6);
  SIZE_MB_IN (7168,  8704, 8);
  SIZE_MB_IN (9216, 10752, 10);
  SIZE_MB_IN (11520, 12488, 12);

  DEBUG((EFI_D_ERROR, "MemoryPeim: Step 3 - MemTotal=%d GiB, Config=%d GiB\n", (UINT32)(MemoryTotal / SIZE_GB), MemGB));

  // Run through each memory descriptor
  while (MemoryDescriptorEx->Length != 0) {
    if (MemoryDescriptorEx->MemoryType == EfiConventionalMemory)
      MemoryTotal += MemoryDescriptorEx->Length;
    switch (MemoryDescriptorEx->HobOption) {
    case Mem2G:
    case Mem3G:
    case Mem4G:
    case Mem6G:
    case Mem8G:
    case Mem10G:
    case Mem12G:
      if (MemoryDescriptorEx->HobOption != Mem) {
        MemoryDescriptorEx++;
        continue;
      }
      // fallthrough
    case AddMem:
    case AddDev:
    case HobOnlyNoCacheSetting:
      AddHob(MemoryDescriptorEx);
      break;
    case NoHob:
    default:
      goto update;
    }

    if (MemoryDescriptorEx->HobOption == HobOnlyNoCacheSetting) {
      MemoryDescriptorEx++;
      continue;
    }

  update:
    ASSERT(Index < MAX_ARM_MEMORY_REGION_DESCRIPTOR_COUNT);

    MemoryDescriptor[Index].PhysicalBase = MemoryDescriptorEx->Address;
    MemoryDescriptor[Index].VirtualBase  = MemoryDescriptorEx->Address;
    MemoryDescriptor[Index].Length       = MemoryDescriptorEx->Length;
    MemoryDescriptor[Index].Attributes   = MemoryDescriptorEx->ArmAttributes;

    Index++;
    MemoryDescriptorEx++;
  }

  // Last one (terminator)
  ASSERT(Index < MAX_ARM_MEMORY_REGION_DESCRIPTOR_COUNT);
  MemoryDescriptor[Index].PhysicalBase = 0;
  MemoryDescriptor[Index].VirtualBase  = 0;
  MemoryDescriptor[Index].Length       = 0;
  MemoryDescriptor[Index].Attributes   = 0;

  // Build Memory Allocation Hob
  DEBUG((EFI_D_ERROR, "MemoryPeim: Step 4 - %d Descriptors. Calling InitMmu...\n", Index));
  InitMmu(MemoryDescriptor);
  DEBUG((EFI_D_ERROR, "MemoryPeim: Step 5 - InitMmu returned!\n"));

  if (FeaturePcdGet(PcdPrePiProduceMemoryTypeInformationHob)) {
    // Optional feature that helps prevent EFI memory map fragmentation.
    BuildMemoryTypeInformationHob();
  }

  DEBUG((EFI_D_ERROR, "MemoryPeim: Step 6 - Finished!\n"));
  ArmInvalidateDataCache();
  return EFI_SUCCESS;
}