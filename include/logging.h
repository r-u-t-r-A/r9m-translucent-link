#pragma once
// The reused ExpressLRS libraries log through these macros. The UART is the
// data link, so logging is compiled out.
#define DBG(...)    do {} while (0)
#define DBGLN(...)  do {} while (0)
#define DBGCR       do {} while (0)
#define ERRLN(...)  do {} while (0)
