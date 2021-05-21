// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

// Interface class for views of recorded debug data
// a delegate which creates implementations of this interface can be registered with a UObject type name to IGameplayInsightsDebugViewCreator
// and they will be shown in the Rewind Debugger when that type of object is selected
class IGameplayInsightsDebugView : public SCompoundWidget
{
	public:
		virtual void SetTimeMarker(double InTimeMarker) = 0;
};
