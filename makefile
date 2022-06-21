#####  DP_MAKE_TARGET autodetection and arch specific variables #####

ifndef DP_MAKE_TARGET

# Win32
ifdef WINDIR
	DP_MAKE_TARGET=mingw
else

# UNIXes
DP_ARCH:=$(shell uname)
ifneq ($(filter %BSD,$(DP_ARCH)),)
	DP_MAKE_TARGET=bsd
else
ifeq ($(DP_ARCH), Darwin)
	DP_MAKE_TARGET=macosx
else
ifeq ($(DP_ARCH), SunOS)
	DP_MAKE_TARGET=sunos
else
	DP_MAKE_TARGET=linux

endif  # ifeq ($(DP_ARCH), SunOS)
endif  # ifeq ($(DP_ARCH), Darwin)
endif  # ifneq ($(filter %BSD,$(DP_ARCH)),)
endif  # ifdef windir
endif  # ifndef DP_MAKE_TARGET

# If we're targeting an x86 CPU we want to enable DP_SSE (CFLAGS_SSE and SSE2)
ifeq ($(DP_MAKE_TARGET), mingw)
	DP_SSE:=1
	ifndef MINGWARCH
		MINGWARCH=i686
	endif
else
	DP_MACHINE:=$(shell uname -m)
	ifeq ($(DP_MACHINE),x86_64)
		DP_SSE:=1
	else
	ifeq ($(DP_MACHINE),i686)
		DP_SSE:=1
	else
	ifeq ($(DP_MACHINE),i386)
		DP_SSE:=1
	else
		DP_SSE:=0
	endif # ifeq ($(DP_MACHINE),i386)
	endif # ifeq ($(DP_MACHINE),i686)
	endif # ifeq ($(DP_MACHINE),x86_64)
endif

# Makefile name
MAKEFILE=makefile

# Commands
ifdef windir
	CMD_RM=del
	CMD_CP=copy /y
	CMD_MKDIR=mkdir
else
	CMD_RM=$(CMD_UNIXRM)
	CMD_CP=$(CMD_UNIXCP)
	CMD_MKDIR=$(CMD_UNIXMKDIR)
endif

# default targets
# we don't build the cl target for Vecxis because its badly outdated
TARGETS_DEBUG=sv-debug sdl-debug
TARGETS_PROFILE=sv-profile sdl-profile
TARGETS_RELEASE=sv-release sdl-release
TARGETS_RELEASE_PROFILE=sv-release-profile sdl-release-profile
TARGETS_NEXUIZ=sv-nexuiz sdl-nexuiz
TARGETS_REXUIZ=sv-rexuiz sdl-rexuiz

###### Optional features #####
OBJ_SDLCD=$(OBJ_CD_COMMON)

DP_VIDEO_CAPTURE?=enabled
ifeq ($(DP_VIDEO_CAPTURE), enabled)
	CFLAGS_VIDEO_CAPTURE=-DCONFIG_VIDEO_CAPTURE
	OBJ_VIDEO_CAPTURE= cap_avi.o cap_ogg.o
else
	CFLAGS_VIDEO_CAPTURE=
	OBJ_VIDEO_CAPTURE=
endif

DP_VOIP?=enabled
ifeq ($(DP_VOIP), enabled)
	CFLAGS_VOIP=-DCONFIG_VOIP
	OBJ_VOIP=snd_voip.o
else
	CFLAGS_VOIP=
	OBJ_VOIP=
endif

DP_LINK_ZLIB?=shared
DP_LINK_JPEG?=shared
DP_LINK_PNG?=shared
DP_LINK_ODE?=dlopen
DP_LINK_CRYPTO?=dlopen
DP_LINK_CRYPTO_RIJNDAEL?=dlopen
DP_LINK_OGGVORBIS?=dlopen
DP_LINK_FREETYPE?=dlopen
DP_LINK_OPUS?=shared

# Linux configuration
ifeq ($(DP_MAKE_TARGET), linux)
	OBJ_ICON=
	OBJ_ICON_NEXUIZ=
	OBJ_ICON_REXUIZ=

	LDFLAGS_SV=$(LDFLAGS_LINUXSV)
	LDFLAGS_SDL=$(LDFLAGS_LINUXSDL)

	EXE_SV=$(EXE_UNIXSV)
	EXE_SDL=$(EXE_UNIXSDL)
	EXE_SVNEXUIZ=$(EXE_UNIXSVNEXUIZ)
	EXE_SDLNEXUIZ=$(EXE_UNIXSDLNEXUIZ)
	EXE_SVREXUIZ=$(EXE_UNIXSVREXUIZ)
	EXE_SDLREXUIZ=$(EXE_UNIXSDLREXUIZ)
endif

# Android configuration
ifeq ($(DP_MAKE_TARGET), android)
	LDFLAGS_SDL=$(LDFLAGS_ANDROIDSDL)
endif

# Mac OS X configuration
ifeq ($(DP_MAKE_TARGET), macosx)
	OBJ_ICON=
	OBJ_ICON_NEXUIZ=
	OBJ_ICON_REXUIZ=

	LDFLAGS_SV=$(LDFLAGS_MACOSXSV)
	LDFLAGS_SDL=$(LDFLAGS_MACOSXSDL)

	EXE_SV=$(EXE_UNIXSV)
	EXE_SDL=$(EXE_UNIXSDL)
	EXE_SVNEXUIZ=$(EXE_UNIXSVNEXUIZ)
	EXE_SDLNEXUIZ=$(EXE_UNIXSDLNEXUIZ)
	EXE_SVREXUIZ=$(EXE_UNIXSVREXUIZ)
	EXE_SDLREXUIZ=$(EXE_UNIXSDLREXUIZ)

	ifeq ($(word 2, $(filter -arch, $(CC))), -arch)
		CFLAGS_MAKEDEP=
	endif

	# we don't build the CL by default because it uses deprecated
	# and not-implemented-in-64bit Carbon
	TARGETS_DEBUG=sv-debug sdl-debug
	TARGETS_PROFILE=sv-profile sdl-profile
	TARGETS_RELEASE=sv-release sdl-release
	TARGETS_RELEASE_PROFILE=sv-release-profile sdl-release-profile
	TARGETS_NEXUIZ=sv-nexuiz sdl-nexuiz
	TARGETS_REXUIZ=sv-rexuiz sdl-rexuiz
endif

# SunOS configuration (Solaris)
ifeq ($(DP_MAKE_TARGET), sunos)
	OBJ_ICON=
	OBJ_ICON_NEXUIZ=
	OBJ_ICON_REXUIZ=

	CFLAGS_EXTRA=$(CFLAGS_SUNOS)

	LDFLAGS_SV=$(LDFLAGS_SUNOSSV)
	LDFLAGS_SDL=$(LDFLAGS_SUNOSSDL)

	EXE_SV=$(EXE_UNIXSV)
	EXE_SDL=$(EXE_UNIXSDL)
	EXE_SVNEXUIZ=$(EXE_UNIXSVNEXUIZ)
	EXE_SDLNEXUIZ=$(EXE_UNIXSDLNEXUIZ)
	EXE_SVREXUIZ=$(EXE_UNIXSVREXUIZ)
	EXE_SDLREXUIZ=$(EXE_UNIXSDLREXUIZ)
endif

# BSD configuration
ifeq ($(DP_MAKE_TARGET), bsd)
	OBJ_ICON=
	OBJ_ICON_NEXUIZ=
	OBJ_ICON_REXUIZ=

	LDFLAGS_SV=$(LDFLAGS_BSDSV)
	LDFLAGS_SDL=$(LDFLAGS_BSDSDL)

	EXE_SV=$(EXE_UNIXSV)
	EXE_SDL=$(EXE_UNIXSDL)
	EXE_SVNEXUIZ=$(EXE_UNIXSVNEXUIZ)
	EXE_SDLNEXUIZ=$(EXE_UNIXSDLNEXUIZ)
	EXE_SVREXUIZ=$(EXE_UNIXSVREXUIZ)
	EXE_SDLREXUIZ=$(EXE_UNIXSDLREXUIZ)
endif

CFLAGS_WARNINGS=-Wall -Wno-missing-field-initializers -Wold-style-definition -Wstrict-prototypes -Wsign-compare -Wdeclaration-after-statement -Wmissing-prototypes

ifeq ($(DP_MAKE_TARGET), mingw)
	TARGET=$(MINGWARCH)-w64-mingw32
	CC=$(TARGET)-gcc
	WINDRES=$(TARGET)-windres

	OBJ_ICON=darkplaces.o
	OBJ_ICON_NEXUIZ=nexuiz.o
	OBJ_ICON_REXUIZ=rexuiz.o

	LDFLAGS_SV=$(LDFLAGS_WINSV)
	LDFLAGS_SDL=$(LDFLAGS_WINSDL)

	EXE_SV=$(EXE_WINSV)-$(MINGWARCH).exe
	EXE_SDL=$(EXE_WINSDL)-$(MINGWARCH).exe
	EXE_SVNEXUIZ=$(EXE_WINSVNEXUIZ)-$(MINGWARCH).exe
	EXE_SDLNEXUIZ=$(EXE_WINSDLNEXUIZ)-$(MINGWARCH).exe
	EXE_SVREXUIZ=$(EXE_WINSVREXUIZ)-$(MINGWARCH).exe
	EXE_SDLREXUIZ=$(EXE_WINSDLREXUIZ)-$(MINGWARCH).exe

	ifeq ($(MINGWARCH), i686)
		CPUOPTIMIZATIONS=-march=i686 -fno-math-errno -ffinite-math-only -fno-rounding-math -fno-signaling-nans -fno-trapping-math
		LDFLAGS_WINCOMMON=-Wl,--large-address-aware
	else
		CPUOPTIMIZATIONS=
		LDFLAGS_WINCOMMON=
	endif
endif

ifeq ($(DP_LIBMICROHTTPD),static)
	CFLAGS_LIBMICROHTTPD=-DUSE_LIBMICROHTTPD `pkg-config --cflags libmicrohttpd`
	LIB_LIBMICROHTTPD=`pkg-config --static --libs libmicrohttpd`
endif
ifeq ($(DP_LIBMICROHTTPD),shared)
	CFLAGS_LIBMICROHTTPD=-DUSE_LIBMICROHTTPD `pkg-config --cflags libmicrohttpd`
	LIB_LIBMICROHTTPD=`pkg-config --libs libmicrohttpd`
endif

ifdef DP_GLES2
	CFLAGS_GL=-DUSE_GLES2
	LIB_GL=-lGLESv2
endif

# set these to "" if you want to use dynamic loading instead
# zlib
ifeq ($(DP_LINK_ZLIB), static)
	CFLAGS_LIBZ=`pkg-config --cflags zlib`
	LIB_Z=-lz `pkg-config --static --libs zlib`
endif
ifeq ($(DP_LINK_ZLIB), shared)
	CFLAGS_LIBZ=`pkg-config --cflags zlib`
	LIB_Z=-lz `pkg-config --libs zlib`
endif

# jpeg
ifeq ($(DP_LINK_JPEG), static)
	CFLAGS_LIBJPEG=`pkg-config --cflags libjpeg`
	LIB_JPEG= `pkg-config --static --libs libjpeg`
endif
ifeq ($(DP_LINK_JPEG), shared)
	CFLAGS_LIBJPEG=`pkg-config --cflags libjpeg`
	LIB_JPEG= `pkg-config --libs libjpeg`
endif

# png
ifeq ($(DP_LINK_PNG), shared)
	CFLAGS_LIBPNG=`pkg-config --cflags libpng`
	LIB_PNG=`pkg-config --libs libpng`
endif
ifeq ($(DP_LINK_PNG), static)
	CFLAGS_LIBPNG=`pkg-config --cflags libpng`
	LIB_PNG=`pkg-config --static --libs libpng`
endif

# ode
ifeq ($(DP_LINK_ODE), shared)
	ODE_CONFIG?=ode-config
	LIB_ODE=`$(ODE_CONFIG) --libs`
	CFLAGS_ODE=`$(ODE_CONFIG) --cflags` -DUSEODE -DLINK_TO_LIBODE
endif
ifeq ($(DP_LINK_ODE), static)
	ODE_CONFIG?=ode-config
	LIB_ODE=`$(ODE_CONFIG) --static --libs`
	CFLAGS_ODE=`$(ODE_CONFIG) --cflags` -DUSEODE -DLINK_TO_LIBODE
endif
ifeq ($(DP_LINK_ODE), dlopen)
	CFLAGS_ODE= -DUSEODE
endif

# ogg and vorbis
ifeq ($(DP_LINK_OGGVORBIS), static)
ifeq ($(DP_VIDEO_CAPTURE), enabled)
	LIB_OGGVORBIS=`pkg-config --static --libs ogg vorbis vorbisfile theora vorbisenc theoraenc`
	CFLAGS_OGGVORBIS=`pkg-config --cflags ogg vorbis vorbisfile theora vorbisenc theoraenc` -DLINK_TO_LIBVORBIS
else
	LIB_OGGVORBIS=`pkg-config --static --libs ogg vorbis vorbisfile`
	CFLAGS_OGGVORBIS=`pkg-config --cflags ogg vorbis vorbisfile` -DLINK_TO_LIBVORBIS
endif
endif
ifeq ($(DP_LINK_OGGVORBIS), shared)
ifeq ($(DP_VIDEO_CAPTURE), enabled)
	LIB_OGGVORBIS=`pkg-config --libs ogg vorbis vorbisfile theora vorbisenc theoraenc`
	CFLAGS_OGGVORBIS=`pkg-config --cflags ogg vorbis vorbisfile theora vorbisenc theoraenc` -DLINK_TO_LIBVORBIS
else
	LIB_OGGVORBIS=`pkg-config --libs ogg vorbis vorbisfile`
	CFLAGS_OGGVORBIS=`pkg-config --cflags ogg vorbis vorbisfile` -DLINK_TO_LIBVORBIS
endif
endif

# opus
ifeq ($(DP_VOIP), enabled)
CFLAGS_OPUS=`pkg-config --cflags opus`
ifeq ($(DP_LINK_OPUS), static)
LIB_OPUS=`pkg-config --static --libs opus`
else
LIB_OPUS=`pkg-config --libs opus`
endif
else
LIB_OPUS=
CFLAGS_OPUS=
endif

# d0_blind_id
ifeq ($(DP_LINK_CRYPTO), shared)
	LIB_CRYPTO=-ld0_blind_id
	CFLAGS_CRYPTO=-DLINK_TO_CRYPTO
endif
ifeq ($(DP_LINK_CRYPTO), static)
	LIB_CRYPTO=-ld0_blind_id
	CFLAGS_CRYPTO=-DLINK_TO_CRYPTO
endif

ifeq ($(DP_LINK_CRYPTO_RIJNDAEL), shared)
	LIB_CRYPTO_RIJNDAEL=-ld0_rijndael
	CFLAGS_CRYPTO_RIJNDAEL=-DLINK_TO_CRYPTO_RIJNDAEL
endif
ifeq ($(DP_LINK_CRYPTO_RIJNDAEL), static)
	LIB_CRYPTO_RIJNDAEL=-ld0_rijndael
	CFLAGS_CRYPTO_RIJNDAEL=-DLINK_TO_CRYPTO_RIJNDAEL
endif

#freetype
ifeq ($(DP_LINK_FREETYPE), shared)
	LIB_FREETYPE=`pkg-config --libs freetype2`
	CFLAGS_FREETYPE=-DLINK_TO_FREETYPE `pkg-config --cflags freetype2`
endif
ifeq ($(DP_LINK_FREETYPE), static)
	LIB_FREETYPE=`pkg-config --static --libs freetype2`
	CFLAGS_FREETYPE=-DLINK_TO_FREETYPE `pkg-config --cflags freetype2`
endif


##### Extra CFLAGS #####

DP_FS_USERDIRMODE?=USERDIRMODE_SAVEDGAMES
DP_FS_BASEDIR_NEXUIZ?=/usr/share/games/nexuiz
CFLAGS_MAKEDEP?=-MMD

ifeq ($(DP_MAKE_TARGET), linux)
ifndef DP_FS_BASEDIR
ifeq ($(ISNEXUIZ), 1)
	DP_FS_BASEDIR=$(DP_FS_BASEDIR_NEXUIZ)
endif
endif
endif

ifdef DP_FS_BASEDIR
	CFLAGS_FS=-DDP_FS_BASEDIR=\"$(DP_FS_BASEDIR)\"
else
	CFLAGS_FS=
endif

CFLAGS_FS+=-DUSERDIRMODE_PREFERED=$(DP_FS_USERDIRMODE)

ifdef DP_FS_FORCE_NOHOME
	CFLAGS_FS+=-DFS_FORCE_NOHOME
endif

ifndef DP_JPEG_VERSION
ifeq ($(DP_MAKE_TARGET), mingw)
	DP_JPEG_VERSION=62
else
ifeq ($(wildcard /usr/lib/libjpeg.so.8),)
ifeq ($(wildcard /usr/lib/*/libjpeg.so.8),)
	DP_JPEG_VERSION=62
endif
endif
	DP_JPEG_VERSION?=80
endif
endif

CFLAGS_LIBJPEG+=-DJPEG_LIB_VERSION="${DP_JPEG_VERSION}"
CFLAGS_NET=
# Systems without IPv6 support should uncomment this:
#CFLAGS_NET+=-DNOSUPPORTIPV6

##### GNU Make specific definitions #####

DO_LD=$(CC) -o ../../../$@ $^ $(LDFLAGS)


##### Definitions shared by all makefiles #####
include makefile.inc


##### Dependency files #####

-include *.d

# hack to deal with no-longer-needed .h files
%.h:
	@echo
	@echo "NOTE: file $@ mentioned in dependencies missing, continuing..."
	@echo "HINT: consider 'make clean'"
	@echo
