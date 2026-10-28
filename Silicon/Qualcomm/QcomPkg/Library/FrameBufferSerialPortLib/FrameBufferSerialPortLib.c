#include <PiDxe.h>

#include <Library/ArmLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/HobLib.h>
#include <Library/SerialPortLib.h>

#include <Resources/FbColor.h>
#include <Resources/font5x12.h>

#include "Library/FrameBufferSerialPortLib.h"
#include <Library/TimerLib.h>


FBCON_POSITION m_Position;
FBCON_POSITION m_MaxPosition;
FBCON_COLOR    m_Color;
BOOLEAN        m_Initialized = FALSE;

UINTN gWidth = FixedPcdGet32(PcdMipiFrameBufferWidth);
// Reserve half screen for output
UINTN gHeight = FixedPcdGet32(PcdMipiFrameBufferHeight);
UINTN gBpp    = FixedPcdGet32(PcdMipiFrameBufferPixelBpp);
UINTN delay = FixedPcdGet32(PcdMipiFrameBufferDelay);

STATIC inline VOID PetAndDisableWatchdog(VOID)
{
  *(volatile UINT32 *)(UINTN)0x0B017010 = 0xFFFFFFFF; // Max bark time
  *(volatile UINT32 *)(UINTN)0x0B017014 = 0xFFFFFFFF; // Max bite time (36 hours)
  *(volatile UINT32 *)(UINTN)0x0B017008 = 0;          // WDT0_EN = 0 (DISABLE)
  *(volatile UINT32 *)(UINTN)0x0B017004 = 1;          // WDT0_RST = 1 (PET)
  *(volatile UINT32 *)(UINTN)0x0B017000 = 1;          // Pet offset 0
}

STATIC VOID SafeDelayMs(UINTN Milliseconds)
{
  UINTN Count = Milliseconds / 50;
  if (Count == 0) Count = 1;
  for (UINTN i = 0; i < Count; i++) {
    PetAndDisableWatchdog();
    MicroSecondDelay(50000);
  }
}

// Module-used internal routine
void FbConPutCharWithFactor(char c, int type, unsigned scale_factor);

void FbConDrawglyph(
    char *pixels, unsigned stride, unsigned bpp, unsigned *glyph,
    unsigned scale_factor);

void FbConReset(void);
void FbConScrollUp(void);
void FbConFlush(void);

RETURN_STATUS
EFIAPI
SerialPortInitialize(VOID)
{
  UINTN InterruptState = 0;

  // Prevent dup initialization
  if (m_Initialized)
    return RETURN_SUCCESS;

  // Interrupt Disable
  InterruptState = ArmGetInterruptState();
  ArmDisableInterrupts();

  // Reset console
  FbConReset();

  // Set flag
  m_Initialized = TRUE;

  if (InterruptState)
    ArmEnableInterrupts();
  return RETURN_SUCCESS;
}

void ResetFb(void)
{
  // Clear current screen.
  char *Pixels  = (void *)FixedPcdGet32(PcdMipiFrameBufferAddress);
  UINTN BgColor = FB_BGRA8888_BLACK;

  // Set to black color.
  for (UINTN i = 0; i < gWidth; i++) {
    for (UINTN j = 0; j < gHeight; j++) {
      BgColor = FB_BGRA8888_BLACK;
      // Set pixel bit
      for (UINTN p = 0; p < (gBpp / 8); p++) {
        *Pixels = (unsigned char)BgColor;
        BgColor = BgColor >> 8;
        Pixels++;
      }
    }
  }
}

STATIC inline INTN* GetSharedPosition(VOID)
{
  return (INTN*)(FixedPcdGet32(PcdMipiFrameBufferAddress) + 
                 (FixedPcdGet32(PcdMipiFrameBufferWidth) * 
                  FixedPcdGet32(PcdMipiFrameBufferHeight) * 
                  FixedPcdGet32(PcdMipiFrameBufferPixelBpp) / 8));
}

void FbConReset(void)
{
  INTN *pShared = GetSharedPosition();

  // Calc max position.
  m_MaxPosition.x = gWidth / (FONT_WIDTH + 1);
  m_MaxPosition.y = (gHeight - 1) / FONT_HEIGHT;

  // If shared cursor position exists from previous stage, continue seamlessly
  if (pShared[1] > 0 && pShared[1] < (INTN)m_MaxPosition.y) {
    m_Position.x = pShared[0];
    m_Position.y = pShared[1];
  } else {
    m_Position.x = 0;
    m_Position.y = 0;
    pShared[0] = 0;
    pShared[1] = 0;
  }

  // Reset color.
  m_Color.Foreground = FB_BGRA8888_WHITE;
  m_Color.Background = FB_BGRA8888_BLACK;
}

void FbConPutCharWithFactor(char c, int type, unsigned scale_factor)
{
  char *Pixels;

  if (!m_Initialized)
    SerialPortInitialize();

  if ((unsigned char)c > 127)
    return;

  if ((unsigned char)c < 32) {
    if (c == '\n') {
      goto newline;
    }
    else if (c == '\r') {
      m_Position.x = 0;
      return;
    }
    else {
      return;
    }
  }

  // Save some space
  if (m_Position.x == 0 && (unsigned char)c == ' ' &&
      type != FBCON_SUBTITLE_MSG && type != FBCON_TITLE_MSG)
    return;

  BOOLEAN intstate = ArmGetInterruptState();
  ArmDisableInterrupts();

  Pixels = (void *)FixedPcdGet32(PcdMipiFrameBufferAddress);
  Pixels += m_Position.y * ((gBpp / 8) * FONT_HEIGHT * gWidth);
  Pixels += m_Position.x * scale_factor * ((gBpp / 8) * (FONT_WIDTH + 1));

  FbConDrawglyph(
      Pixels, gWidth, (gBpp / 8), font5x12 + (c - 32) * 2, scale_factor);

  m_Position.x++;

  {
    INTN *pShared = GetSharedPosition();
    pShared[0] = m_Position.x;
    pShared[1] = m_Position.y;
  }

  if (m_Position.x >= (int)(m_MaxPosition.x / scale_factor))
    goto newline;

  if (intstate)
    ArmEnableInterrupts();
  return;

newline:
  PetAndDisableWatchdog();
  SafeDelayMs( 30 );

  m_Position.y += scale_factor;
  m_Position.x = 0;
  {
    INTN *pShared = GetSharedPosition();
    pShared[0] = m_Position.x;
    pShared[1] = m_Position.y;
  }
  if (m_Position.y >= m_MaxPosition.y - scale_factor) {
    // Screen is full! Pause 1.5 seconds so user/camera can comfortably see the full page
    SafeDelayMs( 1500 );
    m_Position.x = 0;
    m_Position.y = 0;
    {
      INTN *pShared = GetSharedPosition();
      pShared[0] = 0;
      pShared[1] = 0;
    }
    ResetFb();
    FbConFlush();
    if (intstate)
      ArmEnableInterrupts();
    return;
  }
  else {
    Pixels = (void *)FixedPcdGet32(PcdMipiFrameBufferAddress);
    Pixels += m_Position.y * ((gBpp / 8) * FONT_HEIGHT * gWidth);
    ZeroMem(Pixels, ((gBpp / 8) * FONT_HEIGHT * gWidth) * scale_factor);
    WriteBackInvalidateDataCacheRange(Pixels, ((gBpp / 8) * FONT_HEIGHT * gWidth) * scale_factor);
    if (intstate)
      ArmEnableInterrupts();
  }
}

void FbConDrawglyph(
    char *pixels, unsigned stride, unsigned bpp, unsigned *glyph,
    unsigned scale_factor)
{
  char *       bg_pixels = pixels;
  unsigned     x, y, i, j, k;
  unsigned     data, temp;
  unsigned int fg_color = m_Color.Foreground;
  unsigned int bg_color = m_Color.Background;
  stride -= FONT_WIDTH * scale_factor;

  for (y = 0; y < FONT_HEIGHT / 2; ++y) {
    for (i = 0; i < scale_factor; i++) {
      for (x = 0; x < FONT_WIDTH; ++x) {
        for (j = 0; j < scale_factor; j++) {
          bg_color = m_Color.Background;
          for (k = 0; k < bpp; k++) {
            *bg_pixels = (unsigned char)bg_color;
            bg_color   = bg_color >> 8;
            bg_pixels++;
          }
        }
      }
      bg_pixels += (stride * bpp);
    }
  }

  for (y = 0; y < FONT_HEIGHT / 2; ++y) {
    for (i = 0; i < scale_factor; i++) {
      for (x = 0; x < FONT_WIDTH; ++x) {
        for (j = 0; j < scale_factor; j++) {
          bg_color = m_Color.Background;
          for (k = 0; k < bpp; k++) {
            *bg_pixels = (unsigned char)bg_color;
            bg_color   = bg_color >> 8;
            bg_pixels++;
          }
        }
      }
      bg_pixels += (stride * bpp);
    }
  }

  data = glyph[0];
  for (y = 0; y < FONT_HEIGHT / 2; ++y) {
    temp = data;
    for (i = 0; i < scale_factor; i++) {
      data = temp;
      for (x = 0; x < FONT_WIDTH; ++x) {
        if (data & 1) {
          for (j = 0; j < scale_factor; j++) {
            fg_color = m_Color.Foreground;
            for (k = 0; k < bpp; k++) {
              *pixels  = (unsigned char)fg_color;
              fg_color = fg_color >> 8;
              pixels++;
            }
          }
        }
        else {
          for (j = 0; j < scale_factor; j++) {
            pixels = pixels + bpp;
          }
        }
        data >>= 1;
      }
      pixels += (stride * bpp);
    }
  }

  data = glyph[1];
  for (y = 0; y < FONT_HEIGHT / 2; ++y) {
    temp = data;
    for (i = 0; i < scale_factor; i++) {
      data = temp;
      for (x = 0; x < FONT_WIDTH; ++x) {
        if (data & 1) {
          for (j = 0; j < scale_factor; j++) {
            fg_color = m_Color.Foreground;
            for (k = 0; k < bpp; k++) {
              *pixels  = (unsigned char)fg_color;
              fg_color = fg_color >> 8;
              pixels++;
            }
          }
        }
        else {
          for (j = 0; j < scale_factor; j++) {
            pixels = pixels + bpp;
          }
        }
        data >>= 1;
      }
      pixels += (stride * bpp);
    }
  }
}

/* TODO: Take stride into account */
void FbConScrollUp(void)
{
  unsigned short *dst   = (void *)FixedPcdGet32(PcdMipiFrameBufferAddress);
  unsigned short *src   = dst + (gWidth * FONT_HEIGHT);
  unsigned        count = gWidth * (gHeight - FONT_HEIGHT);

  while (count--) {
    *dst++ = *src++;
  }

  count = gWidth * FONT_HEIGHT;
  while (count--) {
    *dst++ = m_Color.Background;
  }

  FbConFlush();
}

void FbConFlush(void)
{
  unsigned total_x, total_y;
  unsigned bytes_per_bpp;

  total_x       = gWidth;
  total_y       = gHeight;
  bytes_per_bpp = (gBpp / 8);

  WriteBackInvalidateDataCacheRange(
      (void *)FixedPcdGet32(PcdMipiFrameBufferAddress),
      (total_x * total_y * bytes_per_bpp));
}

UINTN
EFIAPI
SerialPortWrite(IN UINT8 *Buffer, IN UINTN NumberOfBytes)
{
  UINT8 *CONST Final          = &Buffer[NumberOfBytes];
  UINTN        InterruptState;
  BOOLEAN      MajorMilestone = FALSE;

  if (NumberOfBytes >= 3) {
    if (Buffer[0] == '=' && Buffer[1] == '=' && Buffer[2] == '=') MajorMilestone = TRUE;
    else if (NumberOfBytes >= 4 && Buffer[1] == '=' && Buffer[2] == '=' && Buffer[3] == '=') MajorMilestone = TRUE;
    else if (NumberOfBytes >= 8 && Buffer[0] == 'D' && Buffer[1] == 'I' && Buffer[2] == 'S' && Buffer[3] == 'P' && Buffer[4] == 'A' && Buffer[5] == 'T' && Buffer[6] == 'C' && Buffer[7] == 'H') MajorMilestone = TRUE;
    else if (NumberOfBytes >= 4 && Buffer[0] == 'P' && Buffer[1] == 'C' && Buffer[2] == 'D' && Buffer[3] == ':') MajorMilestone = TRUE;
    else if (NumberOfBytes >= 8 && Buffer[0] == 'A' && Buffer[1] == 'P' && Buffer[2] == 'R' && Buffer[3] == 'I' && Buffer[4] == 'O' && Buffer[5] == 'R' && Buffer[6] == 'I' && Buffer[7] == ':') MajorMilestone = TRUE;
    else if (NumberOfBytes >= 7 && Buffer[0] == 'C' && Buffer[1] == 'P' && Buffer[2] == 'U' && Buffer[3] == 'D' && Buffer[4] == 'X' && Buffer[5] == 'E' && Buffer[6] == ':') MajorMilestone = TRUE;
    else if (NumberOfBytes >= 6 && Buffer[0] == 'G' && Buffer[1] == 'I' && Buffer[2] == 'C' && Buffer[3] == 'V' && Buffer[4] == '2' && Buffer[5] == ':') MajorMilestone = TRUE;
  }

  if (!m_Initialized)
    SerialPortInitialize();

  // Continuously pet APCS watchdog (0x0B017004) and keep disabled (0x0B017008)
  PetAndDisableWatchdog();

  InterruptState = ArmGetInterruptState();
  ArmDisableInterrupts();

  INTN start_y = m_Position.y;
  while (Buffer < Final) {
    FbConPutCharWithFactor(*Buffer++, FBCON_COMMON_MSG, SCALE_FACTOR);
  }

  INTN end_y = m_Position.y;
  if (start_y <= end_y) {
    char *p = (void *)FixedPcdGet32(PcdMipiFrameBufferAddress);
    p += start_y * ((gBpp / 8) * FONT_HEIGHT * gWidth);
    UINTN flush_size = (end_y - start_y + SCALE_FACTOR) * ((gBpp / 8) * FONT_HEIGHT * gWidth);
    WriteBackInvalidateDataCacheRange(p, flush_size);
  } else {
    FbConFlush();
  }

  if (MajorMilestone) {
    SafeDelayMs(800);
  }

  if (InterruptState)
    ArmEnableInterrupts();
  return NumberOfBytes;
}

UINTN
EFIAPI
SerialPortWriteCritical(IN UINT8 *Buffer, IN UINTN NumberOfBytes)
{
  UINT8 *CONST Final             = &Buffer[NumberOfBytes];
  UINTN        CurrentForeground = m_Color.Foreground;
  UINTN        InterruptState    = ArmGetInterruptState();

  ArmDisableInterrupts();
  m_Color.Foreground = FB_BGRA8888_YELLOW;

  INTN start_y = m_Position.y;
  while (Buffer < Final) {
    FbConPutCharWithFactor(*Buffer++, FBCON_COMMON_MSG, SCALE_FACTOR);
  }

  INTN end_y = m_Position.y;
  if (start_y <= end_y) {
    char *p = (void *)FixedPcdGet32(PcdMipiFrameBufferAddress);
    p += start_y * ((gBpp / 8) * FONT_HEIGHT * gWidth);
    UINTN flush_size = (end_y - start_y + SCALE_FACTOR) * ((gBpp / 8) * FONT_HEIGHT * gWidth);
    WriteBackInvalidateDataCacheRange(p, flush_size);
  } else {
    FbConFlush();
  }

  m_Color.Foreground = CurrentForeground;

  if (InterruptState)
    ArmEnableInterrupts();
  return NumberOfBytes;
}

UINTN
EFIAPI
SerialPortRead(OUT UINT8 *Buffer, IN UINTN NumberOfBytes) { return 0; }

BOOLEAN
EFIAPI
SerialPortPoll(VOID) { return FALSE; }

RETURN_STATUS
EFIAPI
SerialPortSetControl(IN UINT32 Control) { return RETURN_UNSUPPORTED; }

RETURN_STATUS
EFIAPI
SerialPortGetControl(OUT UINT32 *Control) { return RETURN_UNSUPPORTED; }

RETURN_STATUS
EFIAPI
SerialPortSetAttributes(
    IN OUT UINT64 *BaudRate, IN OUT UINT32 *ReceiveFifoDepth,
    IN OUT UINT32 *Timeout, IN OUT EFI_PARITY_TYPE *Parity,
    IN OUT UINT8 *DataBits, IN OUT EFI_STOP_BITS_TYPE *StopBits)
{
  return RETURN_UNSUPPORTED;
}

UINTN SerialPortFlush(VOID) { return 0; }

VOID EnableSynchronousSerialPortIO(VOID)
{
  // Already synchronous
}
