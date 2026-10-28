#include <Library/PcdLib.h>
#include <Library/ArmLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/HobLib.h>
#include <Library/SerialPortLib.h>
#include <Library/PrintLib.h>
#include <Library/BaseLib.h>
#include <Library/MemoryMapHelperLib.h>
#include <Library/PlatformPrePiLib.h>
#include <Library/CacheMaintenanceLib.h>

#include "PlatformUtils.h"

#define MDP_BASE                        0x01A00000
#define MDP_CTL_0_BASE                  0x01A02000
#define CTL_START                       0x00
#define CTL_FLUSH                       0x18

// MSM8953 has only 3 pipes: VIG_0, RGB_0, and DMA_0.
// Do NOT access _1 pipes (e.g. RGB_1, VIG_1) as they do not exist
// and trip an immediate AXI bus DECERR external abort on Cortex-A53!
#define MDP_VP_0_VIG_0_BASE             0x01A05000
#define MDP_VP_0_RGB_0_BASE             0x01A15000
#define MDP_VP_0_DMA_0_BASE             0x01A25000

#define PIPE_SSPP_SRC_SIZE              0x00
#define PIPE_SSPP_SRC_IMG_SIZE          0x04
#define PIPE_SSPP_SRC_XY                0x08
#define PIPE_SSPP_SRC_OUT_SIZE          0x0C
#define PIPE_SSPP_OUT_XY                0x10
#define PIPE_SSPP_SRC0_ADDR             0x14
#define PIPE_SSPP_SRC_YSTRIDE           0x24
#define PIPE_SSPP_SRC_FORMAT            0x30
#define PIPE_SSPP_SRC_UNPACK_PATTERN    0x34

STATIC CONST UINT32 mMdpPipes[] = {
  MDP_VP_0_RGB_0_BASE,
  MDP_VP_0_VIG_0_BASE,
  MDP_VP_0_DMA_0_BASE,
};

STATIC VOID ConfigureDisplayHardware(VOID)
{
  UINT32 FbBase = FixedPcdGet32(PcdMipiFrameBufferAddress); // 0x90000000
  UINT32 Width = FixedPcdGet32(PcdMipiFrameBufferWidth);   // 720
  UINT32 Height = FixedPcdGet32(PcdMipiFrameBufferHeight); // 1520
  UINT32 Stride = Width * 4;                               // 2880 bytes
  UINT32 ImgSize = (Height << 16) | Width;

  // Reconfigure any active pipe discovered from previous bootloader
  for (UINTN i = 0; i < ARRAY_SIZE(mMdpPipes); i++) {
    UINT32 PipeBase = mMdpPipes[i];
    UINT32 CurrentAddr = MmioRead32(PipeBase + PIPE_SSPP_SRC0_ADDR);
    if (CurrentAddr != 0) {
      MmioWrite32(PipeBase + PIPE_SSPP_SRC0_ADDR, FbBase);
      MmioWrite32(PipeBase + PIPE_SSPP_SRC_FORMAT, 0x000236FF); // 32bpp XRGB8888
      MmioWrite32(PipeBase + PIPE_SSPP_SRC_UNPACK_PATTERN, 0x03020001); // XRGB/BGRX
      MmioWrite32(PipeBase + PIPE_SSPP_SRC_YSTRIDE, Stride);
      MmioWrite32(PipeBase + PIPE_SSPP_SRC_IMG_SIZE, ImgSize);
      MmioWrite32(PipeBase + PIPE_SSPP_SRC_SIZE, ImgSize);
      MmioWrite32(PipeBase + PIPE_SSPP_SRC_OUT_SIZE, ImgSize);
    }
  }

  // Flush pipe shadow registers (Bit 0=VIG0, Bit 3=CTL, Bit 6=RGB0, Bit 11=DMA0, Bit 13=INTF, Bit 17=LM0)
  // Commits SSPP_SRC_FORMAT (32bpp) and SSPP_SRC_YSTRIDE (2880) to active scanout registers on next VSYNC
  MmioWrite32(MDP_CTL_0_BASE + CTL_FLUSH, 0x00022849);
}

VOID ClearScreenBlack(VOID)
{
  UINT32 FbBase = FixedPcdGet32(PcdMipiFrameBufferAddress);
  UINT32 Width = FixedPcdGet32(PcdMipiFrameBufferWidth);
  UINT32 Height = FixedPcdGet32(PcdMipiFrameBufferHeight);
  volatile UINT64 *p = (volatile UINT64 *)(UINTN)FbBase;
  // Clear full framebuffer cleanly
  UINTN Count = (Width * Height * 4) / sizeof(UINT64);

  for (UINTN i = 0; i < Count; i++) {
    p[i] = 0;
  }
  WriteBackInvalidateDataCacheRange((VOID *)(UINTN)FbBase, Width * Height * 4);
}

VOID EarlyPaintCheckpoint(UINTN StartRow, UINTN NumRows, UINT32 Color)
{
  volatile UINT32 *pFb = (volatile UINT32 *)(UINTN)FixedPcdGet32(PcdMipiFrameBufferAddress);
  UINT32 Width = FixedPcdGet32(PcdMipiFrameBufferWidth);
  for (UINTN row = StartRow; row < StartRow + NumRows; row++) {
    for (UINTN col = 0; col < Width; col++) {
      pFb[row * Width + col] = Color;
    }
  }
  WriteBackInvalidateDataCacheRange((VOID *)((UINTN)pFb + StartRow * Width * 4), NumRows * Width * 4);
}

VOID InitializeSharedUartBuffers(VOID)
{
  INTN* pFbConPosition = (INTN*)(FixedPcdGet32(PcdMipiFrameBufferAddress) + (FixedPcdGet32(PcdMipiFrameBufferWidth) * 
                                                                              FixedPcdGet32(PcdMipiFrameBufferHeight) * 
                                                                              FixedPcdGet32(PcdMipiFrameBufferPixelBpp) / 8));

  *(pFbConPosition + 0) = 0;
  *(pFbConPosition + 1) = 0;
}

VOID UartInit(VOID)
{
  SerialPortInitialize();

  InitializeSharedUartBuffers();

  DEBUG((EFI_D_ERROR, "\nRenegade Project edk2-msm (AArch64)\n"));
  DEBUG(
      (EFI_D_ERROR, "Firmware tissot (MSM8953) [%a %a - v7]\n\n",
       __TIME__, __DATE__));
}

VOID SetWatchdogState(BOOLEAN Enable)
{
  MmioWrite32(APCS_WDOG_CTL_REG, Enable ? 1 : 0);
  MmioWrite32(APCS_TMR_WDOG_CTL_REG, Enable ? 1 : 0);
}

VOID PlatformInitialize()
{
  // 1. Pet and disable APCS hardware watchdog (both KPSS and APCS TMR layouts)
  MmioWrite32(APCS_WDOG_BARK_VAL_REG, 0xFFFFFFFF);
  MmioWrite32(APCS_WDOG_BITE_VAL_REG, 0xFFFFFFFF);
  MmioWrite32(APCS_TMR_WDOG_BARK_VAL_REG, 0xFFFFFFFF);
  MmioWrite32(APCS_TMR_WDOG_BITE_VAL_REG, 0xFFFFFFFF);

  SetWatchdogState(FALSE);

  MmioWrite32(APCS_WDOG_RST_REG, 1);
  MmioWrite32(APCS_TMR_WDOG_RST_REG, 1);
  MmioWrite32(APCS_WDOG_BASE, 1);

  // 2. Reconfigure MDP5 pipe to 32bpp XRGB8888
  ConfigureDisplayHardware();

  // 3. Clear console area
  ClearScreenBlack();

  // 4. Status indicator: rows 0-20 green (PrePi live)
  EarlyPaintCheckpoint(0, 20, 0x0000FF00);

  // 5. Initialize console
  UartInit();
}
