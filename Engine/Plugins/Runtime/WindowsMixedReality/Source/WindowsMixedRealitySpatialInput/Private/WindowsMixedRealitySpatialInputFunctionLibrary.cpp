// Copyright (c) Microsoft Corporation. All rights reserved.

#include "WindowsMixedRealitySpatialInputFunctionLibrary.h"
#include "WindowsMixedRealitySpatialInput.h"
#include "IWindowsMixedRealitySpatialInputPlugin.h"
#include "Async/Async.h"

#include <functional>
#include "WindowsMixedRealitySpatialInputTypes.h"

UWindowsMixedRealitySpatialInputFunctionLibrary::UWindowsMixedRealitySpatialInputFunctionLibrary(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}


bool UWindowsMixedRealitySpatialInputFunctionLibrary::CaptureGestures(bool Tap, bool Hold, bool Manipulation, bool Navigation, bool NavigationRails)
{
	using namespace WindowsMixedReality;
	TSharedPtr<FWindowsMixedRealitySpatialInput> WindowsMixedRealitySpatialInput = StaticCastSharedPtr<FWindowsMixedRealitySpatialInput>(IWindowsMixedRealitySpatialInputPlugin::Get().GetInputDevice());
	if (!WindowsMixedRealitySpatialInput.IsValid())
	{
		return false;
	}

	uint8 capturingSet = 0;

	if (Tap)
	{
		capturingSet |= (uint8)EGestureType::TapGesture;
	}

	if (Hold)
	{
		capturingSet |= (uint8)EGestureType::HoldGesture;
	}

	if (Manipulation)
	{
		capturingSet |= (uint8)EGestureType::ManipulationGesture;
	}

	if (Navigation)
	{
		capturingSet |= (uint8)EGestureType::NavigationGesture;
	}

	if (NavigationRails)
	{
		capturingSet |= (uint8)EGestureType::NavigationRailsGesture;
	}

	return WindowsMixedRealitySpatialInput->CaptureGestures(capturingSet);
}

