// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VCamComponent.h"
#include "UI/VCamConnectionStructs.h"
#include "Blueprint/UserWidget.h"

#include "VCamWidget.generated.h"

class UInputAction;
class UVCamModifier;

/*
 * A wrapper widget class that contains a set of VCam Connections
 * 
 * If you add a widget deriving from UVCamWidget to an Overlay Widget for a VCam Output Provider then when the
 * Overlay is created by the Provider it will also call InitializeConnections with the owning VCam Component.
 */
UCLASS(Abstract)
class VCAMCORE_API UVCamWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	/*
	 * The VCam Connections associated with this Widget
	 * 
	 * Each Connection has a unique name associated with it and any connection related event
	 * will provide this name as one of its arguments.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VCam Connections")
	TMap<FName, FVCamConnection> Connections;

	/*
	 * Event called when a specific connection has been updated
	 * 
	 * The connection is not guaranteed to succeed so "Did Connect Successfully" should be checked before using
	 * the connected modifier or action
	 */
	UFUNCTION(BlueprintImplementableEvent, Category="VCam Connections")
	void OnConnectionUpdated(FName ConnectionName, bool bDidConnectSuccessfully, FName ModifierConnectionPointName, UVCamModifier* ConnectedModifier, UInputAction* ConnectedAction);

	/*
	 * Iterate all VCam Connections within the widget and attempt to connect them using the provided VCam Component
	 */
	void InitializeConnections(UVCamComponent* VCam);
};