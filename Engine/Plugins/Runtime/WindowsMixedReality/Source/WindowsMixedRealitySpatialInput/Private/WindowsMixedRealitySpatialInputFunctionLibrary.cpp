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

bool UWindowsMixedRealitySpatialInputFunctionLibrary::CaptureGestures(bool Tap, bool Hold, ESpatialInputAxisGestureType AxisGesture, bool NavigationAxisX, bool NavigationAxisY, bool NavigationAxisZ)
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

	switch (AxisGesture)
	{
	case ESpatialInputAxisGestureType::None:
		break;
	case ESpatialInputAxisGestureType::Manipulation:
		capturingSet |= (uint8)EGestureType::ManipulationGesture;
		break;
	case ESpatialInputAxisGestureType::Navigation:
		capturingSet |= (uint8)EGestureType::NavigationGesture;
		break;
	case ESpatialInputAxisGestureType::NavigationRails:
		capturingSet |= (uint8)EGestureType::NavigationRailsGesture;
		break;
	default:
		check(false);
	}

	if (NavigationAxisX)
	{
		capturingSet |= (uint8)EGestureType::NavigationGestureX;
	}
	if (NavigationAxisY)
	{
		capturingSet |= (uint8)EGestureType::NavigationGestureY;
	}
	if (NavigationAxisZ)
	{
		capturingSet |= (uint8)EGestureType::NavigationGestureZ;
	}

	return WindowsMixedRealitySpatialInput->CaptureGestures(capturingSet);
}