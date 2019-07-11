// Copyright 2011-2019 Molecular Matters GmbH, all rights reserved.

#pragma once

#include "LC_Types.h"
#include "Windows/WindowsHWrapper.h"

#define LC_WITH_VISUAL_STUDIO_DTE 1

#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/AllowWindowsPlatformAtomics.h"
#include <unknwn.h>

#if LC_WITH_VISUAL_STUDIO_DTE
	#pragma warning(push)
	#pragma warning(disable: 4278)
	#pragma warning(disable: 4471)
	#pragma warning(disable: 4146)
	#pragma warning(disable: 4191)
	#pragma warning(disable: 6244)

	// import EnvDTE
	#define VSACCESSOR_HAS_DTE_OLB 0
	#if VSACCESSOR_HAS_DTE_OLB
		#import "NotForLicensees/dte80a.olb" version("8.0") lcid("0") raw_interfaces_only named_guids
	#else
		#import "libid:80cc9f66-e7d8-4ddd-85b6-d9e6cd0e93e2" version("8.0") lcid("0") raw_interfaces_only named_guids
	#endif

	#pragma warning(pop)
#endif

#include "Windows/HideWindowsPlatformAtomics.h"
#include "Windows/HideWindowsPlatformTypes.h"
