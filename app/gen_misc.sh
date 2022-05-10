#!/bin/bash

echo -e "Interactive generation of user<1/2> binary file\n"

echo "STEP 1: choose boot version (0 = boot_v1.1, 1 = boot_v1.2+, 2 = none [default])"
read input
if [ -z "$input" ]; then
    boot=none
elif [ $input == 0 ]; then
	boot=old
elif [ $input == 1 ]; then
    boot=new
else
    boot=none
fi

echo -e "\t> Boot mode: $boot\n"

echo "STEP 2: choose bin to generate (0 = eagle.flash.bin+eagle.irom0text.bin [default], 1 = user1.bin, 2 = user2.bin)"
read input

if [ -z "$input" ]; then
    if [ $boot != none ]; then
    	boot=none
	echo -e "\t> Ignoring boot"
    fi
    app=0
    echo -e "\tGenerating bin: eagle.flash.bin+eagle.irom0text.bin"
elif [ $input == 1 ]; then
    if [ $boot == none ]; then
    	app=0
	echo -e "\t> Choose no boot before"
	echo -e "\t> Generating bin: eagle.flash.bin+eagle.irom0text.bin"
    else
	app=1
        echo -e "\t> Generating bin: user1.bin"
    fi
elif [ $input == 2 ]; then
    if [ $boot == none ]; then
    	app=0
	echo -e "\t> Choose no boot before"
	echo -e "\t> Generate bin: eagle.flash.bin+eagle.irom0text.bin"
    else
    	app=2
        echo -e "\t> Generating bin: user2.bin"
    fi
else
    if [ $boot != none ]; then
    	boot=none
	echo -e "\t> Ignoring boot"
    fi
    app=0
    echo -e "\tGenerating bin: eagle.flash.bin+eagle.irom0text.bin"
fi

echo "STEP 3: choose spi speed (0 = 20MHz, 1 = 26.7MHz, 2 = 40MHz [default], 3 = 80MHz)"
read input

if [ -z "$input" ]; then
    spi_speed=40
elif [ $input == 0 ]; then
    spi_speed=20
elif [ $input == 1 ]; then
    spi_speed=26.7
elif [ $input == 3 ]; then
    spi_speed=80
else
    spi_speed=40
fi

echo -e "\t> Spi speed: $spi_speed MHz\n"

echo "STEP 4: choose spi mode (0 = QIO [default], 1 = QOUT, 2 = DIO, 3 = DOUT)"
read input

if [ -z "$input" ]; then
    spi_mode=QIO
elif [ $input == 1 ]; then
    spi_mode=QOUT
elif [ $input == 2 ]; then
    spi_mode=DIO
elif [ $input == 3 ]; then
    spi_mode=DOUT
else
    spi_mode=QIO
fi

echo -e "\t> Spi mode: $spi_mode\n"

echo "STEP 5: choose spi size and map"
echo "    0 = 512KB     (256KB  +  256KB) [default]"
echo "    2 = 1024KB    (512KB  +  512KB)"
echo "    3 = 2048KB    (512KB  +  512KB)"
echo "    4 = 4096KB    (512KB  +  512KB)"
echo "    5 = 2048KB    (1024KB +  1024KB)"
echo "    6 = 4096KB    (1024KB +  1024KB)"
echo "    7 = 4096KB    (2048KB +  2048KB) not supported, just for compatibility with nodeMCU board"
echo "    8 = 8192KB    (1024KB +  1024KB)"
echo "    9 = 16384KB   (1024KB +  1024KB)"
read input

if [ -z "$input" ]; then
    spi_size_map=0
    echo "spi size: 512KB"
    echo "spi ota map:  256KB + 256KB"
elif [ $input == 2 ]; then
    spi_size_map=2
    echo "spi size: 1024KB"
    echo "spi ota map:  512KB + 512KB"
elif [ $input == 3 ]; then
    spi_size_map=3
    echo "spi size: 2048KB"
    echo "spi ota map:  512KB + 512KB"
elif [ $input == 4 ]; then
    spi_size_map=4
    echo "spi size: 4096KB"
    echo "spi ota map:  512KB + 512KB"
elif [ $input == 5 ]; then
    spi_size_map=5
    echo "spi size: 2048KB"
    echo "spi ota map:  1024KB + 1024KB"
elif [ $input == 6 ]; then
    spi_size_map=6
    echo "spi size: 4096KB"
    echo "spi ota map:  1024KB + 1024KB"
elif [ $input == 7 ]; then
    spi_size_map=7
    echo"not support ,just for compatible with nodeMCU board"
    exit
elif [ $input == 8 ]; then
    spi_size_map=8
    echo "spi size: 8192KB"
    echo "spi ota map:  1024KB + 1024KB"
elif [ $input == 9 ]; then
    spi_size_map=9
    echo "spi size: 16384KB"
    echo "spi ota map:  1024KB + 1024KB"
else
    spi_size_map=0
    echo "spi size: 512KB"
    echo "spi ota map:  256KB + 256KB"
fi

echo -e "\t--- CONFIGURATION ---"
echo -e "\t> Boot mode: $boot"
echo -e "\t> Binary: user$app.bin"
echo -e "\t> SPI speed: $spi_speed"
echo -e "\t> SPI mode: $spi_mode\n\n"

# END OF CONFIGURATION

echo "*****************"
echo "Starting build..."
echo -e "*****************\n"

make clean
make COMPILE=gcc BOOT=$boot APP=$app SPI_SPEED=$spi_speed SPI_MODE=$spi_mode SPI_SIZE_MAP=$spi_size_map

# $? stores the error value (0 = no errors)
if [ $? -eq 0 ]; then
    echo -e "\n[OK] Build successfull\n"
else
    echo -e "\n/!\\ ERROR: build failed. /!\\"
    echo -e "Aborting...\n"
    exit
fi
