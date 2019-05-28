// Copyright (c) Microsoft Corporation. All rights reserved.

#pragma once

#include "CoreMinimal.h"
//#include "Windows/WindowsHWrapper.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UObject/Object.h"

#if WITH_WINDOWS_MIXED_REALITY
#include "MixedRealityInterop.h"
#endif

#include "WindowsMixedRealitySpatialInputFunctionLibrary.Generated.h"


/**
* Windows Mixed Reality Spatial Input Extensions Function Library
*/
UCLASS()
class WINDOWSMIXEDREALITYSPATIALINPUT_API UWindowsMixedRealitySpatialInputFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_UCLASS_BODY()

public:

	/**
	* Beginning and ending of the gestures capturing. 
	*/
	UFUNCTION(BlueprintCallable, Category = "WindowsMixedRealitySpatialInput")
	static bool CaptureGestures(bool Tap = false, bool Hold = false, bool Manipulation = false, bool Navigation = false, bool NavigationRails = false);

};
