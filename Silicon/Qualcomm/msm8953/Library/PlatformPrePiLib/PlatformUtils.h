#ifndef _PLATFORM_UTILS_H_
#define _PLATFORM_UTILS_H_

#include <Library/PcdLib.h>

#define APCS_WDOG_BASE         0x0B017000
#define APCS_WDOG_RST_REG      0x0B017004
#define APCS_WDOG_CTL_REG      0x0B017008
#define APCS_WDOG_STS_REG      0x0B01700C
#define APCS_WDOG_BARK_VAL_REG 0x0B017010
#define APCS_WDOG_BITE_VAL_REG 0x0B017014

VOID SetWatchdogState(BOOLEAN Enable);
VOID PlatformInitialize();

#endif /* _PLATFORM_UTILS_H_ */
