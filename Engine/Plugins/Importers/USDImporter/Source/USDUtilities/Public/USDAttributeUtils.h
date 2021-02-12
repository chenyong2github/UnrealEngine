// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#if USE_USD_SDK

namespace UE
{
	class FSdfLayer;
	class FUsdAttribute;
	class FUsdStage;
}

namespace UsdUtils
{
	/** Adds a "Muted" CustomData entry to the attribute at the stage's UE state sublayer, which will prevent it from being animated when loaded into UE */
	USDUTILITIES_API bool MuteAttribute( UE::FUsdAttribute& Attribute, const UE::FUsdStage& Stage );

	/** Removes the "Muted" CustomData entry from the attribute at the stage's UE state sublayer, letting it be animated when loaded into UE */
	USDUTILITIES_API bool UnmuteAttribute( UE::FUsdAttribute& Attribute, const UE::FUsdStage& Stage );

	/** Returns whether this attribute has the "Muted" CustomData on the stage's UE state sublayer, meaning it shouldn't be animated when loaded into UE */
	USDUTILITIES_API bool IsAttributeMuted( const UE::FUsdAttribute& Attribute, const UE::FUsdStage& Stage );
}

#endif // #if USE_USD_SDK
