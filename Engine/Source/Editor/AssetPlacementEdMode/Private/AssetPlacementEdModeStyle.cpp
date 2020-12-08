// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetPlacementEdModeStyle.h"

#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleMacros.h"
#include "Styling/SlateStyleRegistry.h"
#include "Styling/SlateTypes.h"
#include "Styling/SlateStyleMacros.h"
#include "Styling/StarshipCoreStyle.h"
#include "Styling/SlateStyleRegistry.h"

TSharedPtr<FAssetPlacementEdModeStyle> FAssetPlacementEdModeStyle::AssetPlacementEdModeStyle;

void FAssetPlacementEdModeStyle::Initialize()
{
	if (AssetPlacementEdModeStyle.IsValid())
	{
		return;
	}

	AssetPlacementEdModeStyle = MakeShareable(new FAssetPlacementEdModeStyle);
	AssetPlacementEdModeStyle->SetupCustomStyle();
}

void FAssetPlacementEdModeStyle::Shutdown()
{
	AssetPlacementEdModeStyle.Reset();
}

FName FAssetPlacementEdModeStyle::GetStyleSetName()
{
	return "AssetPlacementEdModeStyle";
}

const ISlateStyle& FAssetPlacementEdModeStyle::Get()
{
	check(AssetPlacementEdModeStyle.IsValid());
	return *AssetPlacementEdModeStyle->StyleSet;
}

FAssetPlacementEdModeStyle::FAssetPlacementEdModeStyle()
{
	StyleSet = MakeShareable(new FSlateStyleSet(GetStyleSetName()));

	StyleSet->SetParentStyleName("EditorStyle");
	StyleSet->SetContentRoot(FPaths::EngineContentDir() / TEXT("Editor/Slate"));
	StyleSet->SetCoreContentRoot(FPaths::EngineContentDir() / TEXT("Slate"));
}

void FAssetPlacementEdModeStyle::SetupCustomStyle()
{
	auto MakeImageBrushFn = [this](const FString& RelativePath)
	{
		static const FVector2D Icon20x20(20.0f, 20.0f);
		constexpr const TCHAR ImageExtension[] = TEXT(".png");
		return new FSlateImageBrush(StyleSet->RootToContentDir(RelativePath, ImageExtension), Icon20x20);
	};

	StyleSet->Set("AssetPlacementEdMode.Select", MakeImageBrushFn("Icons/GeneralTools/Select_40x"));
	StyleSet->Set("AssetPlacementEdMode.Select.Small", MakeImageBrushFn("Icons/GeneralTools/Select_40x"));
	StyleSet->Set("AssetPlacementEdMode.SelectAll", MakeImageBrushFn("Icons/GeneralTools/SelectAll_40x"));
	StyleSet->Set("AssetPlacementEdMode.SelectAll.Small", MakeImageBrushFn("Icons/GeneralTools/SelectAll_40x"));
	StyleSet->Set("AssetPlacementEdMode.Deselect", MakeImageBrushFn("Icons/GeneralTools/Deselect_40x"));
	StyleSet->Set("AssetPlacementEdMode.Deselect.Small", MakeImageBrushFn("Icons/GeneralTools/Deselect_40x"));
	StyleSet->Set("AssetPlacementEdMode.SelectInvalid", MakeImageBrushFn("Icons/GeneralTools/SelectInvalid_40x"));
	StyleSet->Set("AssetPlacementEdMode.SelectInvalid.Small", MakeImageBrushFn("Icons/GeneralTools/SelectInvalid_40x"));
	StyleSet->Set("AssetPlacementEdMode.LassoSelect", MakeImageBrushFn("Icons/GeneralTools/Lasso_40x"));
	StyleSet->Set("AssetPlacementEdMode.LassoSelect.Small", MakeImageBrushFn("Icons/GeneralTools/Lasso_40x"));
	StyleSet->Set("AssetPlacementEdMode.PlaceSingle", MakeImageBrushFn("Icons/GeneralTools/Foliage_40x"));
	StyleSet->Set("AssetPlacementEdMode.PlaceSingle.Small", MakeImageBrushFn("Icons/GeneralTools/Foliage_40x"));
	StyleSet->Set("AssetPlacementEdMode.Place", MakeImageBrushFn("Icons/GeneralTools/Paint_40x"));
	StyleSet->Set("AssetPlacementEdMode.Place.Small", MakeImageBrushFn("Icons/GeneralTools/Paint_40x"));
	StyleSet->Set("AssetPlacementEdMode.ReapplySettings", MakeImageBrushFn("Icons/GeneralTools/Repaint_40x"));
	StyleSet->Set("AssetPlacementEdMode.ReapplySettings.Small", MakeImageBrushFn("Icons/GeneralTools/Repaint_40x"));
	StyleSet->Set("AssetPlacementEdMode.Fill", MakeImageBrushFn("Icons/GeneralTools/PaintBucket_40x"));
	StyleSet->Set("AssetPlacementEdMode.Fill.Small", MakeImageBrushFn("Icons/GeneralTools/PaintBucket_40x"));
	StyleSet->Set("AssetPlacementEdMode.Delete", MakeImageBrushFn("Icons/GeneralTools/Delete_40x"));
	StyleSet->Set("AssetPlacementEdMode.Delete.Small", MakeImageBrushFn("Icons/GeneralTools/Delete_40x"));
	StyleSet->Set("AssetPlacementEdMode.Erase", MakeImageBrushFn("Icons/GeneralTools/Erase_40x"));
	StyleSet->Set("AssetPlacementEdMode.Erase.Small", MakeImageBrushFn("Icons/GeneralTools/Erase_40x"));
	StyleSet->Set("AssetPlacementEdMode.Filter", MakeImageBrushFn("Icons/GeneralTools/Filter_40x"));
	StyleSet->Set("AssetPlacementEdMode.Filter.Small", MakeImageBrushFn("Icons/GeneralTools/Filter_40x"));
	StyleSet->Set("AssetPlacementEdMode.Settings", MakeImageBrushFn("Icons/GeneralTools/Settings_40x"));
	StyleSet->Set("AssetPlacementEdMode.Settings.Small", MakeImageBrushFn("Icons/GeneralTools/Settings_40x"));
	StyleSet->Set("AssetPlacementEdMode.MoveToActivePartition", MakeImageBrushFn("Icons/GeneralTools/MoveToLevel_40x"));
	StyleSet->Set("AssetPlacementEdMode.MoveToActivePartition.Small", MakeImageBrushFn("Icons/GeneralTools/MoveToLevel_40x"));

	StyleSet->Set("AssetPlacementEdMode.AddAssetType.Text", FTextBlockStyle(StyleSet->GetWidgetStyle<FTextBlockStyle>("NormalText"))
		.SetFont(DEFAULT_FONT("Bold", 10))
		.SetColorAndOpacity(FLinearColor(1.0f, 1.0f, 1.0f))
		.SetHighlightColor(FLinearColor(1.0f, 1.0f, 1.0f))
		.SetShadowOffset(FVector2D(1, 1))
		.SetShadowColorAndOpacity(FLinearColor(0, 0, 0, 0.9f)));

	FSlateStyleRegistry::RegisterSlateStyle(*StyleSet.Get());
}

FAssetPlacementEdModeStyle::~FAssetPlacementEdModeStyle()
{
	FSlateStyleRegistry::UnRegisterSlateStyle(*StyleSet.Get());
	StyleSet.Reset();
}
