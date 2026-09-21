#pragma once
// A/B experiment: change only COALESCE between runs; keep diagnostics identical.
#define PQC_COALESCE_SMALL_FRAMES 1
// UART Monitor only. Adds no bytes to the TCP protocol; still costs CPU/UART time.
#define PQC_TRACE_FRAME_TX 0
// Set to 1 and re-upload to restore per-command begin/end UART diagnostics.
// Connection, retry, idle timeout and error logs are always retained.
#ifndef PQC_TRACE_COMMANDS
#define PQC_TRACE_COMMANDS 0
#endif
