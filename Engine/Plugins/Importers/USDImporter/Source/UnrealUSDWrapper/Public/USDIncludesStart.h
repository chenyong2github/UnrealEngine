// Copyright Epic Games, Inc. All Rights Reserved.

#if USE_USD_SDK

#include "CoreMinimal.h"

THIRD_PARTY_INCLUDES_START

#pragma warning(push)
#pragma warning(disable: 4193) /* #pragma warning(pop): no matching '#pragma warning(push)', the pop is in USDIncludesEnd.h */
#pragma warning(disable: 4582) /* Workaround union in pxr/usd/pcp/mapFunction.h */
#pragma warning(disable: 4583)
#pragma warning(disable: 4003) /* pxr/usd/sdf/fileFormat.h BOOST_PP_SEQ_DETAIL_IS_NOT_EMPTY during static analysis */
#pragma warning(disable: 6319)

#pragma push_macro("check")
#undef check // Boost function is named 'check' in boost\python\detail\convertible.hpp

// Boost needs _DEBUG defined when /RTCs build flag is enabled (Run Time Checks)
#if PLATFORM_WINDOWS && UE_BUILD_DEBUG
	#ifndef _DEBUG
		#define _DEBUG
	#endif
#endif

#if PLATFORM_WINDOWS
	#include "Windows/WindowsHWrapper.h"
#endif

#include "pxr/pxr.h"

#endif // #if USE_USD_SDK
