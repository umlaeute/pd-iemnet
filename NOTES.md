scratchpad for the development of iemnet
========================================

# speed & syslocks

setting Pd's clocks is not thread-safe, but calling `sys_lock()` will
slow the entire process down to unusability. therefore, we use a
(per-receiver) thread, that get's waits on a `pthread_cond_t` and will
syslock if needed. LATER there should only be one of these clock-threads
(per RTE, in case we finally can run multiple Pd's in a single
applications), that maintains it's own list of callbacks+data.

tests for tcpclient/server: client disconnects -\> server should get
notified server disconnects -\> client should get notified client
crashes -\> server should disconnect server crashes -\> client should
disconnect

# known BUGS

`[tcpclient]` now does all the actual connect/disconnect work in a
helper-thread. unfortunately, `iemnet__receiver_destroy()` assumes that
it is called from the main-thread and will `outlet_list` the remaining
bytes in the dtor. however, `outlet_list` is not threadsafe. possible
solution: see speed&syslocks for a leightweight thread-safe callback
framework.
