#!/bin/bash

echo -e "Automated flash of user<1/2> binary file\n"

BINPATH='../bin/upgrade'

# Check if args passed are less than 1 (i.e. no args) or greater than 2 (i.e. too much users)
if [ $# -lt 1 ] || [ $# -gt 2 ]; then
    echo -e "Usage: $0 <user> [<user>]\n"
    exit
fi

for USER; do
    case "$USER" in
    "1")
        esptool.py --port /dev/ttyUSB0 --baud 1500000 write_flash --flash_size 2MB-c1 0x01000 "$BINPATH"/user1.2048.new.5.bin 
        ;;
    "2")
        esptool.py --port /dev/ttyUSB0 --baud 1500000 write_flash --flash_size 2MB-c1 0x101000 "$BINPATH"/user2.2048.new.5.bin
        ;;
    "a")
        esptool.py --port /dev/ttyUSB0 --baud 1500000 write_flash --flash_size 2MB-c1 0x01000 "$BINPATH"/user1.2048.new.5.bin 0x101000 "$BINPATH"/user2.2048.new.5.bin 
        ;;
    *)
        echo "ERROR: not a valid user (valid: 1, 2, a)"
        echo -e "Aborting.\n"
        exit
        ;;
    esac
done
