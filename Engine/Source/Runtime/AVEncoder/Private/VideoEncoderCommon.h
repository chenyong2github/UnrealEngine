// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/ScopeLock.h"
#include "Logging/LogMacros.h"


#if PLATFORM_WINDOWS

#pragma warning(push)
#pragma warning(disable : 4005)	// macro redefinition

#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/PreWindowsApi.h"
	#include <d3d11.h>
	#include <mftransform.h>
	#include <mfapi.h>
	#include <mferror.h>
	#include <mfidl.h>
	#include <codecapi.h>
	#include <shlwapi.h>
	#include <mfreadwrite.h>
	#include <d3d11_1.h>
	#include <d3d12.h>
	#include <dxgi1_4.h>
#include "Windows/PostWindowsApi.h"
#include "Windows/HideWindowsPlatformTypes.h"
#include "MicrosoftCommon.h"

#pragma warning(pop)

#endif /* PLATFORM_WINDOWS */


DECLARE_LOG_CATEGORY_EXTERN(LogVideoEncoder, Log, All);

namespace AVEncoder
{
} /* namespace AVEncoder */