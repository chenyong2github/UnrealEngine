// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "DMXRuntimeBlueprintLibrary.generated.h"


UCLASS(meta = (ScriptName = "DMXRuntimeLibrary"))
class DMXRUNTIME_API UDMXRuntimeBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Sets if DMX is received from the network
	 * @param bReceiveDMXEnabled	If true, receives inbound DMX packets, else ignores them, globally.
	 * @param bAffectEditor			If true, affects the editor. Overrides what is set in DMX project settings.
	 */
	UFUNCTION(BlueprintCallable, Category = "DMX")
	static void SetReceiveDMXEnabled(bool bReceiveDMXEnabled = true, bool bAffectEditor = false);

	/**
	 * Returns whether Receive DMX from the network is enabled
	 * @return		If true, DMX is received from the Network
	 */
	UFUNCTION(BlueprintCallable, Category = "DMX")
	static bool IsReceiveDMXEnabled();
};
