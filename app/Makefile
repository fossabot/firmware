#############################################################
# Required variables for each makefile
# Discard this section from all parent makefiles
# Expected variables (with automatic defaults):
#   CSRCS (all "C" files in the dir)
#   SUBDIRS (all subdirs with a Makefile)
#   GEN_LIBS - list of libs to be generated ()
#   GEN_IMAGES - list of object file images to be generated ()
#   GEN_BINS - list of binaries to be generated ()
#   COMPONENTS_xxx - a list of libs/objs in the form
#     subdir/lib to be extracted and rolled up into
#     a generated lib/image xxx.a ()
#
TARGET = eagle

#EXTRA_CCFLAGS += -u

ifndef PDIR # {
GEN_IMAGES= eagle.app.v6.out
GEN_BINS= eagle.app.v6.bin
SPECIAL_MKTARGETS=$(APP_MKTARGETS)
SUBDIRS=    \
	App/				\
	App/ButtonReq_Mng   \
	App/Fota_Mng		\
	App/NetworkReq_Mng  \
	App/AccessPoint_Mng \
	Services			\
	Services/Mqtt_Mng	\
	Services/Nvm_Mng	\
	Services/Wifi_Mng   \
	Services/Misc		\
	Driver/				\
	Driver/Gpio_Mng


endif # } PDIR

APPDIR = .
LDDIR = ../ld

CCFLAGS += -Os

TARGET_LDFLAGS =		\
	-nostdlib		\
	-Wl,-EL \
	--longcalls \
	--text-section-literals

ifeq ($(FLAVOR),debug)
    TARGET_LDFLAGS += -g -O2 
	CONFIGURATION_DEFINES += -DGLOBAL_DEBUG_ON
endif

ifeq ($(FLAVOR),release)
    TARGET_LDFLAGS += -g -O0
endif

COMPONENTS_eagle.app.v6 = \
	App/libapp.a  				   \
	App/ButtonReq_Mng/libbtnreq.a  \
	App/Fota_Mng/libfota.a         \
	App/NetworkReq_Mng/libnetreq.a \
	App/AccessPoint_Mng/libap.a    \
	Services/libservices.a  	   \
	Services/Mqtt_Mng/libmqtt.a    \
	Services/Nvm_Mng/libnvm.a	   \
	Services/Wifi_Mng/libwifi.a    \
	Services/Misc/libmisc.a		   \
	Driver/libdrv.a				   \
	Driver/Gpio_Mng/libgpio.a	   
	
LINKFLAGS_eagle.app.v6 = \
	-L../lib        \
	-nostdlib	\
    -T$(LD_FILE)   \
	-Wl,--no-check-sections	\
	-Wl,--gc-sections	\
    -u call_user_start	\
	-Wl,-static						\
	-Wl,--start-group					\
	-lc					\
	-lgcc					\
	-lhal					\
	-lphy	\
	-lpp	\
	-lnet80211	\
	-llwip	\
	-lwpa	\
	-lcrypto	\
	-lmain	\
	-ljson	\
	-lupgrade\
	-lmbedtls	\
	-lpwm	\
	-ldriver \
	-lsmartconfig \
	$(DEP_LIBS_eagle.app.v6)					\
	-Wl,--end-group

DEPENDS_eagle.app.v6 = \
                $(LD_FILE) \
                $(LDDIR)/eagle.rom.addr.v6.ld

#############################################################
# Configuration i.e. compile options etc.
# Target specific stuff (defines etc.) goes in here!
# Generally values applying to a tree are captured in the
#   makefile at its root level - these are then overridden
#   for a subtree within the makefile rooted therein
#

#UNIVERSAL_TARGET_DEFINES =		\

# Other potential configuration flags include:
#	-DTXRX_TXBUF_DEBUG
#	-DTXRX_RXBUF_DEBUG
#	-DWLAN_CONFIG_CCX
CONFIGURATION_DEFINES += -DICACHE_FLASH \

DEFINES +=				\
	$(UNIVERSAL_TARGET_DEFINES)	\
	$(CONFIGURATION_DEFINES)

DDEFINES +=				\
	$(UNIVERSAL_TARGET_DEFINES)	\
	$(CONFIGURATION_DEFINES)


#############################################################
# Recursion Magic - Don't touch this!!
#
# Each subtree potentially has an include directory
#   corresponding to the common APIs applicable to modules
#   rooted at that subtree. Accordingly, the INCLUDE PATH
#   of a module can only contain the include directories up
#   its parent path, and not its siblings
#
# Required for each makefile to inherit from the parent
#

INCLUDES := $(INCLUDES) -I $(PDIR)Driver/Gpio_Mng -I $(PDIR)Driver/Uart_Mng
INCLUDES := $(INCLUDES) -I $(PDIR)Services/Mqtt_Mng -I $(PDIR)Services/Nvm_Mng -I $(PDIR)Services/Wifi_Mng -I $(PDIR)Services/Misc
INCLUDES := $(INCLUDES) -I $(PDIR)App -I $(PDIR)App/ButtonReq_Mng -I $(PDIR)App/NetworkReq_Mng -I $(PDIR)App/AccessPoint_Mng -I $(PDIR)App/Fota_Mng 
PDIR := ../$(PDIR)
sinclude $(PDIR)Makefile

.PHONY: FORCE
FORCE:

