#pragma once
// A/B experiment: change only COALESCE between runs; keep diagnostics identical.
#define PQC_COALESCE_SMALL_FRAMES 1
// UART Monitor only. Adds no bytes to the TCP protocol; still costs CPU/UART time.
#define PQC_TRACE_FRAME_TX 0
