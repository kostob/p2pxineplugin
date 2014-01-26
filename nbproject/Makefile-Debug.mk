#
# Generated Makefile - do not edit!
#
# Edit the Makefile in the project folder instead (../Makefile). Each target
# has a -pre and a -post target defined where you can add customized code.
#
# This makefile implements configuration specific macros and targets.


# Environment
MKDIR=mkdir
CP=cp
GREP=grep
NM=nm
CCADMIN=CCadmin
RANLIB=ranlib
CC=gcc
CCC=g++
CXX=g++
FC=gfortran
AS=as

# Macros
CND_PLATFORM=GNU-Generic
CND_DLIB_EXT=so
CND_CONF=Debug
CND_DISTDIR=dist
CND_BUILDDIR=build

# Include project Makefile
include Makefile

# Object Directory
OBJECTDIR=${CND_BUILDDIR}/${CND_CONF}/${CND_PLATFORM}

# Object Files
OBJECTFILES= \
	${OBJECTDIR}/input_p2p.o \
	${OBJECTDIR}/network.o \
	${OBJECTDIR}/output_factory.o \
	${OBJECTDIR}/output_ffmpeg.o \
	${OBJECTDIR}/streamer.o \
	${OBJECTDIR}/threads.o


# C Compiler Flags
CFLAGS=

# CC Compiler Flags
CCFLAGS=
CXXFLAGS=

# Fortran Compiler Flags
FFLAGS=

# Assembler Flags
ASFLAGS=

# Link Libraries and Options
LDLIBSOPTIONS=-L/usr/home/tobias/dev/GRAPES/src -L/usr/lib -L/usr/local/lib -lpthread -lgrapes -lavformat -lavcodec -lavfilter -lavdevice -lavresample -lavutil -lcunit

# Build Targets
.build-conf: ${BUILD_SUBPROJECTS}
	"${MAKE}"  -f nbproject/Makefile-${CND_CONF}.mk ${CND_DISTDIR}/${CND_CONF}/${CND_PLATFORM}/inp_p2p.${CND_DLIB_EXT}

${CND_DISTDIR}/${CND_CONF}/${CND_PLATFORM}/inp_p2p.${CND_DLIB_EXT}: ${OBJECTFILES}
	${MKDIR} -p ${CND_DISTDIR}/${CND_CONF}/${CND_PLATFORM}
	${LINK.c} -o ${CND_DISTDIR}/${CND_CONF}/${CND_PLATFORM}/inp_p2p.${CND_DLIB_EXT} ${OBJECTFILES} ${LDLIBSOPTIONS} -shared -fPIC

${OBJECTDIR}/input_p2p.o: nbproject/Makefile-${CND_CONF}.mk input_p2p.c 
	${MKDIR} -p ${OBJECTDIR}
	${RM} "$@.d"
	$(COMPILE.c) -g -DDEBUG -I/usr/local/include -I/usr/home/tobias/dev/GRAPES/include -I. -fPIC  -MMD -MP -MF "$@.d" -o ${OBJECTDIR}/input_p2p.o input_p2p.c

${OBJECTDIR}/network.o: nbproject/Makefile-${CND_CONF}.mk network.c 
	${MKDIR} -p ${OBJECTDIR}
	${RM} "$@.d"
	$(COMPILE.c) -g -DDEBUG -I/usr/local/include -I/usr/home/tobias/dev/GRAPES/include -I. -fPIC  -MMD -MP -MF "$@.d" -o ${OBJECTDIR}/network.o network.c

${OBJECTDIR}/output_factory.o: nbproject/Makefile-${CND_CONF}.mk output_factory.c 
	${MKDIR} -p ${OBJECTDIR}
	${RM} "$@.d"
	$(COMPILE.c) -g -DDEBUG -I/usr/local/include -I/usr/home/tobias/dev/GRAPES/include -I. -fPIC  -MMD -MP -MF "$@.d" -o ${OBJECTDIR}/output_factory.o output_factory.c

${OBJECTDIR}/output_ffmpeg.o: nbproject/Makefile-${CND_CONF}.mk output_ffmpeg.c 
	${MKDIR} -p ${OBJECTDIR}
	${RM} "$@.d"
	$(COMPILE.c) -g -DDEBUG -I/usr/local/include -I/usr/home/tobias/dev/GRAPES/include -I. -fPIC  -MMD -MP -MF "$@.d" -o ${OBJECTDIR}/output_ffmpeg.o output_ffmpeg.c

${OBJECTDIR}/streamer.o: nbproject/Makefile-${CND_CONF}.mk streamer.c 
	${MKDIR} -p ${OBJECTDIR}
	${RM} "$@.d"
	$(COMPILE.c) -g -DDEBUG -I/usr/local/include -I/usr/home/tobias/dev/GRAPES/include -I. -fPIC  -MMD -MP -MF "$@.d" -o ${OBJECTDIR}/streamer.o streamer.c

${OBJECTDIR}/threads.o: nbproject/Makefile-${CND_CONF}.mk threads.c 
	${MKDIR} -p ${OBJECTDIR}
	${RM} "$@.d"
	$(COMPILE.c) -g -DDEBUG -I/usr/local/include -I/usr/home/tobias/dev/GRAPES/include -I. -fPIC  -MMD -MP -MF "$@.d" -o ${OBJECTDIR}/threads.o threads.c

# Subprojects
.build-subprojects:

# Clean Targets
.clean-conf: ${CLEAN_SUBPROJECTS}
	${RM} -r ${CND_BUILDDIR}/${CND_CONF}
	${RM} ${CND_DISTDIR}/${CND_CONF}/${CND_PLATFORM}/inp_p2p.${CND_DLIB_EXT}

# Subprojects
.clean-subprojects:

# Enable dependency checking
.dep.inc: .depcheck-impl

include .dep.inc
