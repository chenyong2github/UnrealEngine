// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UUserWidget;
class UWidget;

namespace UE::VCamCore
{
	/** Calls the callback for each widget, including the children of UUserWidgets. */
	VCAMCORE_API void ForEachWidgetToConsiderForVCam(UUserWidget& Widget, TFunctionRef<void(UWidget*)> Callback);
}