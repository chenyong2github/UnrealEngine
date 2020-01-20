// Copyright Epic Games, Inc. All Rights Reserved.

#include "EventFilterStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Styling/SlateTypes.h"
#include "Styling/CoreStyle.h"

TSharedPtr< FSlateStyleSet > FEventFilterStyle::StyleSet = nullptr;

#define IMAGE_BRUSH( RelativePath, ... ) FSlateImageBrush( StyleSet->RootToContentDir( RelativePath, TEXT(".png") ), __VA_ARGS__ )
#define BOX_BRUSH( RelativePath, ... ) FSlateBoxBrush( StyleSet->RootToContentDir( RelativePath, TEXT(".png") ), __VA_ARGS__ )
#define BORDER_BRUSH( RelativePath, ... ) FSlateBorderBrush( StyleSet->RootToContentDir( RelativePath, TEXT(".png") ), __VA_ARGS__ )
#define DEFAULT_FONT(...) FCoreStyle::GetDefaultFontStyle(__VA_ARGS__)

// Const icon sizes
static const FVector2D Icon8x8(8.0f, 8.0f);
static const FVector2D Icon9x19(9.0f, 19.0f);
static const FVector2D Icon14x14(14.0f, 14.0f);
static const FVector2D Icon16x16(16.0f, 16.0f);
static const FVector2D Icon20x20(20.0f, 20.0f);
static const FVector2D Icon22x22(22.0f, 22.0f);
static const FVector2D Icon24x24(24.0f, 24.0f);
static const FVector2D Icon28x28(28.0f, 28.0f);
static const FVector2D Icon27x31(27.0f, 31.0f);
static const FVector2D Icon26x26(26.0f, 26.0f);
static const FVector2D Icon32x32(32.0f, 32.0f);
static const FVector2D Icon40x40(40.0f, 40.0f);
static const FVector2D Icon48x48(48.0f, 48.0f);
static const FVector2D Icon75x82(75.0f, 82.0f);
static const FVector2D Icon360x32(360.0f, 32.0f);
static const FVector2D Icon171x39(171.0f, 39.0f);
static const FVector2D Icon170x50(170.0f, 50.0f);
static const FVector2D Icon267x140(170.0f, 50.0f);

void FEventFilterStyle::Initialize()
{
	// Only register once
	if( StyleSet.IsValid() )
	{
		return;
	}

	StyleSet = MakeShareable(new FSlateStyleSet("EventFilter") );
	
	StyleSet->SetContentRoot(FPaths::EngineContentDir() / TEXT("Editor/Slate"));
	StyleSet->SetCoreContentRoot(FPaths::EngineContentDir() / TEXT("Slate"));

	// Colors
	{
		StyleSet->Set("EventFilter.EnginePreset", FLinearColor(0.728f, 0.364f, 0.003f));
		StyleSet->Set("EventFilter.SharedPreset", FLinearColor(0.003f, 0.364f, 0.728f));
		StyleSet->Set("EventFilter.LocalPreset", FLinearColor(0.003f, 0.728f, 0.364f));
	}
	
	// Icons
	{
		StyleSet->Set("EventFilter.State.Enabled", new IMAGE_BRUSH("Common/CheckBox_Checked", Icon16x16));
		StyleSet->Set("EventFilter.State.Enabled_Hovered", new IMAGE_BRUSH("Common/CheckBox_Checked_Hovered", Icon16x16));

		StyleSet->Set("EventFilter.State.Disabled", new IMAGE_BRUSH("Common/CheckBox", Icon16x16));
		StyleSet->Set("EventFilter.State.Disabled_Hovered", new IMAGE_BRUSH("Common/CheckBox_Hovered", Icon16x16));

		StyleSet->Set("EventFilter.State.Pending", new IMAGE_BRUSH("Common/CheckBox_Undetermined", Icon16x16));
		StyleSet->Set("EventFilter.State.Pending_Hovered", new IMAGE_BRUSH("Common/CheckBox_Undetermined_Hovered", Icon16x16));

		StyleSet->Set("EventFilter.TabIcon", new IMAGE_BRUSH("/Icons/icon_Genericfinder_16x", Icon16x16));
	}


	// Filter list
	/* Set images for various SCheckBox states associated with "ContentBrowser.FilterButton" ... */
	const FCheckBoxStyle FilterButtonCheckBoxStyle = FCheckBoxStyle()
		.SetUncheckedImage(IMAGE_BRUSH("ContentBrowser/FilterUnchecked", FVector2D(10.0f, 20.0f)))
		.SetUncheckedHoveredImage(IMAGE_BRUSH("ContentBrowser/FilterUnchecked", FVector2D(10.0f, 20.0f), FLinearColor(0.5f, 0.5f, 0.5f, 1.0f)))
		.SetUncheckedPressedImage(IMAGE_BRUSH("ContentBrowser/FilterUnchecked", FVector2D(10.0f, 20.0f), FLinearColor(0.5f, 0.5f, 0.5f, 1.0f)))
		.SetCheckedImage(IMAGE_BRUSH("ContentBrowser/FilterChecked", FVector2D(10.0f, 20.0f)))
		.SetCheckedHoveredImage(IMAGE_BRUSH("ContentBrowser/FilterChecked", FVector2D(10.0f, 20.0f), FLinearColor(0.5f, 0.5f, 0.5f, 1.0f)))
		.SetCheckedPressedImage(IMAGE_BRUSH("ContentBrowser/FilterChecked", FVector2D(10.0f, 20.0f), FLinearColor(0.5f, 0.5f, 0.5f, 1.0f)));
	/* ... and add the new style */
	StyleSet->Set("FilterPresets.FilterButton", FilterButtonCheckBoxStyle);

	StyleSet->Set("FilterPresets.FilterNameFont", DEFAULT_FONT("Regular", 10));
	StyleSet->Set("FilterPresets.FilterButtonBorder", new BOX_BRUSH("Common/RoundedSelection_16x", FMargin(4.0f / 16.0f)));
	
	FSlateStyleRegistry::RegisterSlateStyle( *StyleSet.Get() );
}

#undef IMAGE_BRUSH
#undef BOX_BRUSH
#undef BORDER_BRUSH
#undef DEFAULT_FONT

void FEventFilterStyle::Shutdown()
{
	if( StyleSet.IsValid() )
	{
		FSlateStyleRegistry::UnRegisterSlateStyle( *StyleSet.Get() );
		ensure( StyleSet.IsUnique() );
		StyleSet.Reset();
	}
}

const ISlateStyle& FEventFilterStyle::Get()
{
	return *( StyleSet.Get() );
}

const FName& FEventFilterStyle::GetStyleSetName()
{
	return StyleSet->GetStyleSetName();
}
