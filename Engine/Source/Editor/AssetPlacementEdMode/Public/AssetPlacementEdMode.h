// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/UEdMode.h"

#include "AssetPlacementEdMode.generated.h"

UCLASS()
class UAssetPlacementEdMode : public UEdMode
{
	GENERATED_BODY()

public:
	constexpr static const TCHAR AssetPlacementEdModeID[] = TEXT("EM_AssetPlacementEdMode");

	UAssetPlacementEdMode();
	virtual ~UAssetPlacementEdMode();

	////////////////
	// UEdMode interface
	virtual void Enter() override;
	virtual void CreateToolkit() override;
	virtual TMap<FName, TArray<TSharedPtr<FUICommandInfo>>> GetModeCommands() const override;
	//////////////////
	// End of UEdMode interface
	//////////////////
};
