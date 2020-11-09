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
	void SetIsDynamicallyLoadedInEditor(bool bInIsDynamicallyLoadedInEditor);
	bool IsDynamicallyLoadedInEditor() const { return !IsDynamicallyLoaded() || bIsDynamicallyLoadedInEditor; }
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

	/** Whether the DataLayer affects actor runtime loading */
	UPROPERTY()
	uint32 bIsDynamicallyLoaded : 1;

#if WITH_EDITORONLY_DATA
	/** Whether the DataLayer affects actor editor loading */
	UPROPERTY(Transient)
	uint32 bIsDynamicallyLoadedInEditor : 1;
#endif
};