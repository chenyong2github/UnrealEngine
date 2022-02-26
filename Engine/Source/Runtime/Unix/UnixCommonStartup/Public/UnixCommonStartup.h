// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#ifndef DISABLE_ASAN_LEAK_DETECTOR
	#define DISABLE_ASAN_LEAK_DETECTOR 0
#endif

/**
 * @brief CommonUnixMain - executes common startup code for Unix programs/engine
 * @param argc - number of arguments in argv[]
 * @param argv - array of arguments
 * @param RealMain - the next main routine to call in chain
 * @param AppExitCallback - workaround for Launch module that needs to call FEngineLoop::AppExit() at certain point
 * @return error code to return to the OS
 */
int UNIXCOMMONSTARTUP_API CommonUnixMain(int argc, char *argv[], int (*RealMain)(const TCHAR * CommandLine), void (*AppExitCallback)() = nullptr);

#if DISABLE_ASAN_LEAK_DETECTOR
/*
 * We honestly leak so much this output is not super useful, so lets disable by default but if you want to re-enable disable
 * this DEFINE in LinuxToolchain.cs area
 */
extern "C" const char* UNIXCOMMONSTARTUP_API __asan_default_options();
#endif
