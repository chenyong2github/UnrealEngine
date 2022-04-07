cmake_minimum_required(VERSION 3.00)
PROJECT(BinkAudioDecoder)

SET(ENCODER_SRCS
	Src/popmal.c
    	Src/binkace.c
    	Src/binka_ue_encode.cpp
    	Src/radfft.cpp
    	Src/varbits.c)

SET(DECODER_SRCS
    Src/binkacd.cpp
    Src/binka_ue_decode.cpp
    Src/radfft.cpp
    Src/x86_cpu.c)

include_directories(Include/)

ADD_DEFINITIONS(-D__RADINSTATICLIB__=1)
ADD_LIBRARY(libbinka_ue_encode ${ENCODER_SRCS})

ADD_DEFINITIONS(-DWRAP_PUBLICS=UEBA)
ADD_LIBRARY(libbinka_ue_decode ${DECODER_SRCS})
