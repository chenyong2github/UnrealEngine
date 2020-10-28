// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "WorldPartition/DataLayer/DataLayer.h"
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

	const UDataLayer* GetDataLayerFromLabel(const FName& InDataLayerLabel) const;
	const UDataLayer* GetDataLayerFromName(const FName& InDataLayerName) const;
	void ActivateDataLayer(const FName& InDataLayerName, bool bActivate);
	bool IsDataLayerActive(const FName& InDataLayerName) const;
	bool IsAnyDataLayerActive(const TArray<FName>& InDataLayerNames) const;

private:
	/** Console command used to toggle activation of a DataLayer */
	static class FAutoConsoleCommand ToggleDataLayerActivation;

	TSet<FName> ActiveDataLayerNames;
};