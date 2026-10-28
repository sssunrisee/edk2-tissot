#include <Library/BaseLib.h>
#include <Library/PlatformMemoryMapLib.h>

static ARM_MEMORY_REGION_DESCRIPTOR_EX gDeviceMemoryDescriptorEx[] = {
    /* Name               Address     Length      HobOption        ResourceAttribute    ArmAttributes
                                                          ResourceType          MemoryType */

    /* DDR Regions starting at 0x80000000 (Snapdragon 625 / MSM8953 true DDR base) */
    {"HLOS Low RAM",      0x80000000, 0x04000000, AddMem, SYS_MEM, SYS_MEM_CAP, Conv,   WRITE_BACK_XN},
    /* PIL Subsystems & SMEM (protects Venus, other_ext, SMEM, Modem, ADSP, WCNSS firmware) */
    {"PIL Subsys & SMEM", 0x84000000, 0x0B600000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, UNCACHED_UNBUFFERED_XN},
    {"ABOOT / lk2nd",     0x8F600000, 0x00A00000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_BACK_XN},
    {"Display Reserved",  0x90000000, 0x00800000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_THROUGH_XN},
    {"HLOS Free 2",       0x90800000, 0x0B800000, AddMem, SYS_MEM, SYS_MEM_CAP, Conv,   WRITE_BACK_XN},

    /* UEFI FD (Firmware Device: 32MB at 0x9C000000) */
    {"UEFI FD",           0x9C000000, 0x02000000, AddMem, SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK},
    {"HLOS Free 3",       0x9E000000, 0x01F8C000, AddMem, SYS_MEM, SYS_MEM_CAP, Conv,   WRITE_BACK_XN},

    {"CPU Vectors",       0x9FF8C000, 0x00001000, AddMem, SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK},
    {"MMU PageTables",    0x9FF8D000, 0x00003000, AddMem, SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK_XN},
    {"UEFI Stack",        0x9FF90000, 0x00040000, AddMem, SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK_XN},
    {"Log Buffer",        0x9FFF7000, 0x00008000, AddMem, SYS_MEM, SYS_MEM_CAP, Reserv, WRITE_BACK_XN},
    {"Info Blk",          0x9FFFF000, 0x00001000, AddMem, SYS_MEM, SYS_MEM_CAP, Reserv, WRITE_BACK_XN},

    /* DXE Heap (480MB from 0xA0000000 to 0xBE000000) - MUST be executable (WRITE_BACK) for DxeCore */
    {"DXE Heap",          0xA0000000, 0x1E000000, AddMem, SYS_MEM, SYS_MEM_CAP, Conv,   WRITE_BACK},

    /* Upper Bank 1 memory: Free RAM up to 0xF2000000 */
    {"RAM Partition 1A",  0xBE000000, 0x34000000, AddMem, SYS_MEM, SYS_MEM_CAP, Conv,   WRITE_BACK_XN},
    /* Protect TrustZone Secure Region, QSEECOM, and ADSP shared memory */
    {"TZ & Sec Subsys",   0xF2000000, 0x0E000000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, UNCACHED_UNBUFFERED_XN},

    /* Register regions for MSM8953 */
    {"MPM2_MPM_CTRL",     0x004A0000, 0x00010000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
    {"SECURITY CONTROL",  0x00058000, 0x00010000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
    {"TLMM CSR",          0x01000000, 0x00300000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
    {"GCC CLK CTL",       0x01800000, 0x000B0000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
    {"MDSS",              0x01A00000, 0x00200000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
    {"PMIC ARB SPMI",     0x02000000, 0x00A00000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
    {"PERIPH_SS",         0x07800000, 0x00200000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
    {"MSM_SDC1",          0x07824000, 0x00001000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
    {"MSM_SDC2",          0x07864000, 0x00001000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
    {"IMEM Base",         0x08600000, 0x00010000, NoHob,  MMAP_IO, INITIALIZED, Conv,   NS_DEVICE},
    {"APCS Subsystem",    0x0B000000, 0x00100000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},

    /* Terminator for MMU */
    {"Terminator", 0, 0, 0, 0, 0, 0, 0}
};

ARM_MEMORY_REGION_DESCRIPTOR_EX *GetPlatformMemoryMap()
{
  return gDeviceMemoryDescriptorEx;
}
