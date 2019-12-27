// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Windows/WindowsHWrapper.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/AllowWindowsPlatformAtomics.h"
#include <unknwn.h>

#if WITH_VISUALSTUDIO_DTE
	#pragma warning(push)
	#pragma warning(disable: 4278)
	#pragma warning(disable: 4471)
	#pragma warning(disable: 4146)
	#pragma warning(disable: 4191)
	#pragma warning(disable: 6244)

	// import EnvDTE
	#if WITH_VISUALSTUDIO_DTE_OLB
		// Including pregenerated .tlh file to avoid race conditions generating from olb by multiple compilers running in parallel (typically in non-unity builds).
		#include "NotForLicensees/dte80a.tlh" 
		// #import "NotForLicensees/dte80a.olb" version("8.0") lcid("0") raw_interfaces_only named_guids
	#else
		#import "libid:80cc9f66-e7d8-4ddd-85b6-d9e6cd0e93e2" version("8.0") lcid("0") raw_interfaces_only named_guids
	#endif

	#pragma warning(pop)
#endif

#include "Windows/HideWindowsPlatformAtomics.h"
#include "Windows/HideWindowsPlatformTypes.h"
