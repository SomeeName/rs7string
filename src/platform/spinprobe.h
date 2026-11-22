#pragma once
// [debug] spin_probe=1 (diagnostic): a watchdog thread that finds busy loops. Every 10 s it reads the CPU time
// of every thread of the process; a thread that used >= 8 s of CPU in that window is sampled 20 times
// (SuspendThread / GetThreadContext / ResumeThread, 25 ms apart): EIP and the first return addresses found
// above ESP are logged as module+offset (game addresses absolute). At most 2 reports per thread, 8 in total.
void RsxrSpinProbe_Start();
