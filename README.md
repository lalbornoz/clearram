# clearram - clear system RAM and reboot on demand (for Linux and FreeBSD) (WIP)
clearram is an LKM for (at least) Linux v4.x+ and FreeBSD v11.x+ that clears system
RAM on demand, respecting NUMA configurations if present, and then resets the system
through an implicit triple fault. A character device, /dev/clearram, is provided on
both platforms, which will trigger the above process upon a write(2) of any size
greater than or equal to zero (0.)

# Building
* On either Linux or FreeBSD:<br />
$ make [DEBUG=1]
* On Linux using old versions of GNU make lacking the != shell assignment operator:<br />
$ make -f Makefile.Linux [DEBUG=1]
* For development purposes:<br />
$ ./build.sh [-b[uild]] [-c[lean]] [-d[ebug] [`<breakpoint>`]] [-h[elp]] [-r[un]] [-v[nc]]

# Caveats
* No synchronisation of cached writes to storage backends is explicitly requested
for by the LKM prior to clearing RAM. Therefore, data loss is generally inevitable.
This can be mitigated by ensuring that sync(1) is invoked prior to writing to
/dev/clearram, which however may block for an arbitrarily long amount of time.

* The default permission bits for the character device on either platforms of 0600
should suffice to prevent an obvious DoS attack vector and should thus normally not
be changed.

# Troubleshooting
When built with -DDEBUG, the LKM will print the RAM sections encountered and the
PFN<->VA range mappings created to the kernel ring buffer at loading time.
Additionally, zero-filling will take place in units of 1 GB and a light green-on-black
single period (.) is printed to the framebuffer starting at linear offset zero (00)
after each successful iteration. Any exceptions generated as part of the zero-filling
loop will be caught and printed, along with a CPU state dump, to the framebuffer as well.
