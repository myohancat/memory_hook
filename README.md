# memory_hook
simple memory_hook test code using LD_PRELOAD

$> LD_PRELOAD=memory_hook.so [TARGET_EXECUTE_FILE]

Memory leak check with memory hook.
(malloc, calloc, realloc, memalign, free...)


S> telnet localhost 3333

You can dump memory allocation status.

