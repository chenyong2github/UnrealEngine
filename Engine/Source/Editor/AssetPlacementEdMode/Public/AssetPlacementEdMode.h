// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/UEdMode.h"
#include "Elements/Framework/TypedElementHandle.h"

#include "AssetPlacementEdMode.generated.h"

USTRUCT()
struct FPaletteItem
{
	GENERATED_BODY()
	FTypedHandleTypeId ElementId;
	bool bIsEnabled;
};

UCLASS(config = EditorPerProjectUserSettings)
class UAssetPlacementSettings : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(config, EditAnywhere, Category = "Level Partition Settings")
	bool bPlaceInCurrentLevelPartition = true;

	UPROPERTY(config, EditAnywhere, Category = "Filters")
	bool bLandscape = true;

	UPROPERTY(config, EditAnywhere, Category = "Filters")
	bool bStaticMeshes = true;
	
	UPROPERTY(config, EditAnywhere, Category = "Filters")
	bool bBSP = true;
	
	UPROPERTY(config, EditAnywhere, Category = "Filters")
	bool bFoliage = false;
	
	UPROPERTY(config, EditAnywhere, Category = "Filters")
	bool bTranslucent = false;

	UPROPERTY(config, EditAnywhere, Category = "Asset Palette")
	TArray<FPaletteItem> PaletteItems;
};

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
	virtual void BindCommands() override;
	//////////////////
	// End of UEdMode interface
	//////////////////

protected:
	enum class EPaletteFilter
	{
		EntirePalette,
		ActivePaletteOnly,
		InvalidInstances,
	};

	enum class ESelectMode
	{
		Select,
		Deselect,
	};

	void SelectAssets(EPaletteFilter InSelectAllType, ESelectMode InSelectMode);
	void DeleteAssets();
	void MoveAssetToActivePartition();
	bool HasAnyAssetsInPalette(EPaletteFilter InSelectAllType);
};
