// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "DataLayerSubsystem.generated.h"

/**
 * UDataLayerSubsystem
 */

UCLASS()
class ENGINE_API UDataLayerSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	UDataLayerSubsystem();

	//~ Begin USubsystem Interface.
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	//~ End USubsystem Interface.

	void ActivateDataLayer(const FName& InDataLayer, bool bActivate);
	bool IsDataLayerActive(const FName& InDataLayer) const;
	bool IsAnyDataLayerActive(const TArray<FName>& InDataLayers) const;
	const TSet<FName>& GetActiveDataLayers() const { return ActiveDataLayers; }

private:
	/** Console command used to toggle activation of a DataLayer */
	static class FAutoConsoleCommand ToggleDataLayerActivation;

	TSet<FName> ActiveDataLayers;
};