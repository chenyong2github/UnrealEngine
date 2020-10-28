// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Object.h"
#include "WorldDataLayers.h"

#include "DataLayer.generated.h"

UCLASS(Config = Engine, PerObjectConfig, Within = WorldDataLayers)
class ENGINE_API UDataLayer : public UObject
{
	GENERATED_UCLASS_BODY()

public:
#if WITH_EDITOR
	void SetDataLayerLabel(FName InDataLayerLabel);
	void SetVisible(bool bInIsVisible);
	void SetIsDynamicallyLoaded(bool bInIsDynamicallyLoaded);
#endif
	
	FName GetDataLayerLabel() const  { return DataLayerLabel; }
	bool IsVisible() const { return bIsVisible; }
	bool IsDynamicallyLoaded() const { return bIsDynamicallyLoaded; }

private:
	/** The display name of the DataLayer */
	UPROPERTY()
	FName DataLayerLabel;

	/** Whether actors associated with the DataLayer are visible in the viewport */
	UPROPERTY()
	uint32 bIsVisible : 1;

	/** Whether the layer can affect actor loading or not when layer is activated */
	UPROPERTY()
	uint32 bIsDynamicallyLoaded : 1;
};