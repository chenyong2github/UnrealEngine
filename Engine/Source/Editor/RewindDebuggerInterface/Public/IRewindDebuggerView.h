// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

// Interface class for views of recorded debug data
// a delegate which creates implementations of this interface can be registered with a UObject type name to IGameplayInsightsDebugViewCreator
// and they will be shown in the Rewind Debugger when that type of object is selected
class REWINDDEBUGGERINTERFACE_API IRewindDebuggerView : public SCompoundWidget
{
	public:
		// unique name for widget
		virtual FName GetName() const = 0;

		// id of target object
		virtual uint64 GetObjectId() const = 0;

		// called by the debugger when the scrubbing bar position changes
		virtual void SetTimeMarker(double InTimeMarker) = 0;
};
