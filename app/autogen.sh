#!/bin/bash

# Check if args passed are less than 1 (i.e. no args) or greater than 2 (i.e. too much users)
if [ $# -lt 1 ] || [ $# -gt 3 ]; then
    echo -e "Usage: $0 <flavor> <user> [<user>]\n"
    exit
fi

echo -e "Automated generation of user<1/2> binary file\n"

# START OF CONFIGURATION

# This is equal to "for every arguments that you passed"
for USER; do
    if [ -z $flavor ]; then
        # STEP 0: decide the build's flavor
        if [[ $1 == "release" ]]; then
            flavor=release
        elif [[ $1 == "debug" ]]; then
            flavor=debug
        else
            echo "ERROR: FLAVOR is empty. Setting it to debug";
            flavor=debug
        fi
    fi

    # STEP 1: boot version (0 = boot_v1.1, 1 = boot_v1.2+, 2 = none)
    boot=new;

    # STEP 2: binary to generate (0 = eagle.flash.bin+eagle.irom0text.bin, 1 = user1.bin, 2 = user2.bin)"
    # assign the variable "input" to the current arg analyzed
    input=$USER

    if [ $input != 1 ] && [ $input != 2 ]; then
        if [[ $input == $1 ]]; then
            continue
        else
            echo "ERROR: not a valid user (valid: 1, 2)"
            echo -e "Skipping.\n"
            continue
        fi
    else
        app=$input
    fi

    # STEP 3: SPI speed (0 = 20MHz, 1 = 26.7MHz, 2 = 40MHz, 3 = 80MHz)"
    spi_speed=40

    #STEP 4: SPI mode (0 = QIO, 1 = QOUT, 2 = DIO, 3 = DOUT)"
    spi_mode=QIO

    # STEP 5: SPI size and map"
    spi_size_map=5

    # RECAP

    echo -e "\n\t--- CONFIGURATION ---"
    echo -e "\t> Boot mode: $boot"
    echo -e "\t> Binary: user$app.bin"
    echo -e "\t> SPI speed: $spi_speed"
    echo -e "\t> SPI mode: $spi_mode"
    echo -e "\t> FLAVOR: $flavor\n\n"

    # END OF CONFIGURATION

    # seems kinda useless, given that folder "user" does not exist (i.e. will always fail)
    #touch user/user_main.c

    echo "*****************"
    echo "Starting build..."
    echo -e "*****************\n"
    make clean
    make COMPILE=gcc BOOT=$boot APP=$app SPI_SPEED=$spi_speed SPI_MODE=$spi_mode SPI_SIZE_MAP=$spi_size_map FLAVOR=$flavor

    # $? stores the error value (0 = no errors)
    if [ $? -eq 0 ]; then
        echo -e "\n[OK] Build successfull | DATE: $(date)"
        echo -e "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n"
    else
        echo -e "\n/!\\ ERROR: build failed. /!\\"
        echo -e "Aborting...\n"
        exit
    fi
done
