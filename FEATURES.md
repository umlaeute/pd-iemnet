iemnet - networking for Pd
==========================

# general
iemnet objects provide a low-level interface (OSI-5 transport layer)
to networking from within Pd. iemnet tries to do only one thing
(transmitting data over the internet), but it tries to do it good.

## data
data data passed over the network has to be given as "list of bytes".
in Pd-speak these are "list"s, containing only floating point numbers
whoses values must be integer and in the range 0..255. you have to take
care of that yourself. if you don't (e.g. trying to send symbols,
fractional numbers, out of range numbers,...), you are to blame. in order
to send more complex data, you have to wrap them into an application
layer protocol, such as Open Sound Control (OSC). you can find objects to
convert OSC messages to/from messages understood by iemnet objects in
Martin Peaches great "osc" library.

## threading
iemnet makes heavy use of threading. this means that
sending data (to the internet), receiving data (from the internet) and
processing datat (within Pd) can run in parallel. this means that you
won't get audio dropouts if the network is slow because your your
neighbour is downloading videos. if you have a multi-core (SMP) system,
threads can utitlize this. if you don't have a multi-core (SMP) system,
you still benefit from the threading approach.

# TCP/IP objects

## \[tcpserver\] listens on a port for incoming messages

if the port is already occupied, you are provided feedback so you can
react

the port can be changed at runtime

you can let the system chose an available port for you (and you can
query it)

passing only a list (without the "send <csocket>", "client <id>" or
"broadcast" prefix) specifies only the payload (data). the target can be
set with the "target" message ("target 0" means "broadcast", "target
<id>" will send to "client <id>" if id\>0 and to all but "client <id>"
if id\<0
