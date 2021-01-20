// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/LegacyEdModeWidgetHelpers.h"
#include "Elements/Framework/TypedElementHandle.h"
#include "AssetPlacementSettings.h"

#include "AssetPlacementEdMode.generated.h"

UCLASS()
class UAssetPlacementEdMode : public UBaseLegacyWidgetEdMode
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
	virtual bool IsSelectionAllowed(AActor* InActor, bool bInSelection) const override;
	// End of UEdMode interface
	//////////////////

	////////////////
	// UBaseLegacyWidgetEdMode interface
	virtual bool UsesPropertyWidgets() const override;
	virtual bool ShouldDrawWidget() const override;
	// End of UBaseLegacyWidgetEdMode interface
	//////////////////

	static bool DoesPaletteSupportElement(const FTypedElementHandle& InElementToCheck, const TArray<FPaletteItem>& InPaletteToCheck);

protected:
	enum class EPaletteFilter
	{
		EntirePalette,
		ActivePaletteOnly,
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
