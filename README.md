# clearram - clear system RAM and reboot on demand (for Linux and FreeBSD) (WIP)
clearram is an LKM for (at least) Linux v4.x+ and FreeBSD v11.x+ that clears system
RAM on demand, respecting NUMA configurations if present, and then resets the system
through an implicit triple fault. A character device, /dev/clearram, is provided on
both platforms, which will trigger the above process upon a write(2) of any size
greater than or equal to zero (0.)

# Building
* On either Linux or FreeBSD:<br />
$ make [DEBUG=1]
* On Linux using old versions of GNU make lacking the `!=' shell assignment operator:<br />
$ make -f Makefile.Linux [DEBUG=1]
* For development purposes:<br />
$ ./build.sh [-b[uild]] [-c[lean]] [-d[ebug] [`<breakpoint>`]] [-h[elp]] [-r[un]] [-v[nc]]

# Caveats
* Neither Linux nor FreeBSD implement a realloc() operation for memory returned
by {k[mz],vm}alloc() and malloc(9), respectively, that doesn't copy the set of pages
corresponding to the original allocation instead of merely reducing it in size.
Therefore, memory allocated in excess of the (variable) amount required for the
page tables is presently not released back to the OS. This consumes roughly 8 MB
per 4 GB of RAM, regardless of the physical RAM map.

* No synchronisation of cached writes to storage backends is explicitly requested
for by the LKM prior to clearing RAM. Therefore, data loss is generally inevitable.
This can be mitigated by ensuring that sync(1) is invoked prior to writing to
/dev/clearram, which however may block for an arbitrarily long amount of time.

* The default permission bits for the character device on either platforms of 0600
should suffice to prevent an obvious DoS attack vector and should thus normally not
be changed.
