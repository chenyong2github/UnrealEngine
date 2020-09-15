// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "DMXProtocolBlueprintLibrary.generated.h"


UCLASS(meta = (ScriptName = "DMXRuntimeLibrary"))
class DMXPROTOCOL_API UDMXProtocolBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Sets if DMX is sent to the network
	 * @param bReceiveDMXEnabled	If true, sends DMX packets to the network, else ignores all send calls globally.
	 * @param bAffectEditor			If true, affects the editor.
	 */
	UFUNCTION(BlueprintCallable, Category = "DMX")
	static void SetSendDMXEnabled(bool bReceiveDMXEnabled = true, bool bAffectEditor = false);

	/**
	 * Returns whether send DMX to the network is enabled globally.
	 * @return		If true, DMX is sent to the Network
	 */
	UFUNCTION(BlueprintCallable, Category = "DMX")
	static bool IsSendDMXEnabled();

	/**
	 * Sets if DMX is received from the network
	 * @param bReceiveDMXEnabled	If true, receives inbound DMX packets, else ignores them, globally.
	 * @param bAffectEditor			If true, affects the editor. 
	 */
	UFUNCTION(BlueprintCallable, Category = "DMX")
	static void SetReceiveDMXEnabled(bool bReceiveDMXEnabled = true, bool bAffectEditor = false);

	/**
	 * Returns whether Receive DMX from the network is enabled globally.
	 * @return		If true, DMX is received from the Network
	 */
	UFUNCTION(BlueprintCallable, Category = "DMX")
	static bool IsReceiveDMXEnabled();
};
