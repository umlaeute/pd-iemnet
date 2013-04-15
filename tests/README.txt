things that need testing
========================

all tests should be done with audio on (e.g. testtone)

senders
-------
- delete object
- connect/disconnect
- send 100MB of data
-- disconnect
-- delete object
- send 100MB of data
-- delete object

receivers
---------
- delete object
- recv 100MB of data
-- disconnect
-- delete object
- recv 100MB of data
-- delete object


data integrity
--------------
repeat data back to sender (or to another receiver, for unidirectional objects)
is the data consistent?
(simple test: nc a large file)
