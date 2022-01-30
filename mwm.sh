#! /bin/sh

while true; do
    # Log stderror to a file 
    # mwm.bin 2> /tmp/mwm.log
    # No error logging
    mwm.bin >/dev/null 2>&1
done

exit 0
