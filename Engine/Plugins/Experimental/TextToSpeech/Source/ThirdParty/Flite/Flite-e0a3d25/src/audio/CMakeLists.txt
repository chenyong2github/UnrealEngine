# Copyright Epic Games, Inc. All Rights Reserved.
cmake_minimum_required(VERSION 3.6)
project(lib_audio C)
# We only need the synthesized wave data from flite 
# Thus we just use Au_None.c for the audio driver to disallow audio output   
add_library(lib_audio OBJECT 
	au_none.c
	auclient.c
	auserver.c
	audio.c
	au_streaming.c
)

target_include_directories(lib_audio 
	PUBLIC ../../include
	PUBLIC .
)

set_target_properties(lib_audio PROPERTIES COMPILE_PDB_NAME "${FLITE_COMPILE_PDB_NAME}")
# To ensure that it's the null driver that's defined
target_compile_definitions(lib_audio PRIVATE CST_AUDIO_NONE)
#this is disabled to enable Windows builds 
target_compile_definitions(lib_audio PUBLIC CST_NO_SOCKETS)