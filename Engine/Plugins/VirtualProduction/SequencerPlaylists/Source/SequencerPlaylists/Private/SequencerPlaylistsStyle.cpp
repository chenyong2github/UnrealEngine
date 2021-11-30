// Copyright Epic Games, Inc. All Rights Reserved.

#include "SequencerPlaylistsStyle.h"
#include "EditorStyleSet.h"
#include "Framework/Application/SlateApplication.h"
#include "Interfaces/IPluginManager.h"
#include "Slate/SlateGameResources.h"
#include "Styling/SlateStyleMacros.h"
#include "Styling/SlateStyleRegistry.h"
#include "Styling/StyleColors.h"


#define RootToContentDir Style->RootToContentDir


TSharedPtr<FSlateStyleSet> FSequencerPlaylistsStyle::StyleInstance = nullptr;


void FSequencerPlaylistsStyle::Initialize()
{
	if (!StyleInstance.IsValid())
	{
		StyleInstance = Create();
		FSlateStyleRegistry::RegisterSlateStyle(*StyleInstance);
	}
}

void FSequencerPlaylistsStyle::Shutdown()
{
	FSlateStyleRegistry::UnRegisterSlateStyle(*StyleInstance);
	ensure(StyleInstance.IsUnique());
	StyleInstance.Reset();
}

FName FSequencerPlaylistsStyle::GetStyleSetName()
{
	static FName StyleSetName(TEXT("SequencerPlaylistsStyle"));
	return StyleSetName;
}

FLinearColor FSequencerPlaylistsStyle::MakeColorVariation(FLinearColor InColor, EColorVariation Variation)
{
	const FLinearColor HsvColor = InColor.LinearRGBToHSV();
	switch (Variation)
	{
		case EColorVariation::Desaturated: return FLinearColor(HsvColor.R, HsvColor.G * .5, HsvColor.B, HsvColor.A).HSVToLinearRGB();
		case EColorVariation::Dimmed: return FLinearColor(HsvColor.R, HsvColor.G, HsvColor.B * .5, HsvColor.A).HSVToLinearRGB();
		default: checkNoEntry();
	}

	return InColor;
};

TSharedRef<FSlateStyleSet> FSequencerPlaylistsStyle::Create()
{
	static const FVector2D Icon12x12(12.0f, 12.0f);
	static const FVector2D Icon16x16(16.0f, 16.0f);
	static const FVector2D Icon20x20(20.0f, 20.0f);
	static const FVector2D Icon24x24(24.0f, 24.0f);

	TSharedRef<FSlateStyleSet> Style = MakeShareable(new FSlateStyleSet("SequencerPlaylistsStyle"));
	Style->SetContentRoot(IPluginManager::Get().FindPlugin("SequencerPlaylists")->GetBaseDir() / TEXT("Resources"));

	Style->Set("SequencerPlaylists.TabIcon", new IMAGE_BRUSH_SVG("PlaylistIcon", Icon16x16));
	Style->Set("SequencerPlaylists.OpenPluginWindow", new IMAGE_BRUSH_SVG("PlaceholderButtonIcon", Icon20x20));
	Style->Set("SequencerPlaylists.Panel.Background", new FSlateColorBrush(FStyleColors::Panel));

	FLinearColor DimColor(FSlateColor(EStyleColor::Background).GetSpecifiedColor());
	DimColor.A = 0.5f;
	Style->Set("SequencerPlaylists.Item.Dim", new FSlateColorBrush(DimColor));

	Style->Set("SequencerPlaylists.NewPlaylist.Background", new IMAGE_BRUSH_SVG("PlaylistIcon", Icon20x20));
	// NewPlaylist.Overlay, SavePlaylistAs, OpenPlaylist are registered under a different root below.

	FButtonStyle TransportButtonStyle = FButtonStyle()
		.SetNormalForeground(FStyleColors::Foreground)
		.SetDisabledForeground(FStyleColors::Foreground);

	const FLinearColor PlayColor = FStyleColors::AccentGreen.GetSpecifiedColor();
	const FLinearColor StopColor = FStyleColors::AccentRed.GetSpecifiedColor();
	const FLinearColor ResetColor = FStyleColors::AccentBlue.GetSpecifiedColor();
	const FLinearColor PlayPressedColor = MakeColorVariation(PlayColor, EColorVariation::Dimmed);
	const FLinearColor StopPressedColor = MakeColorVariation(StopColor, EColorVariation::Dimmed);
	const FLinearColor ResetPressedColor = MakeColorVariation(ResetColor, EColorVariation::Dimmed);

	const float CornerRadius = 4.0f;
	const FVector4 LeftCorners(CornerRadius, 0.0f, 0.0f, CornerRadius);
	const FVector4 MiddleCorners(0.0f, 0.0f, 0.0f, 0.0f);
	const FVector4 RightCorners(0.0f, CornerRadius, CornerRadius, 0.0f);

	const FSlateRoundedBoxBrush LeftBrush(FStyleColors::Dropdown, LeftCorners);
	const FSlateRoundedBoxBrush MiddleBrush(FStyleColors::Dropdown, MiddleCorners);
	const FSlateRoundedBoxBrush RightBrush(FStyleColors::Dropdown, RightCorners);

	const FButtonStyle LeftTransportButton = FButtonStyle(TransportButtonStyle)
		.SetNormal(LeftBrush)
		.SetHovered(LeftBrush)
		.SetPressed(LeftBrush)
		.SetDisabled(LeftBrush)
		.SetNormalPadding(FMargin(8.f, 4.f, 6.f, 4.f))
		.SetPressedPadding(FMargin(8.f, 4.f, 6.f, 4.f));

	const FButtonStyle MiddleTransportButton = FButtonStyle(TransportButtonStyle)
		.SetNormal(MiddleBrush)
		.SetHovered(MiddleBrush)
		.SetPressed(MiddleBrush)
		.SetDisabled(MiddleBrush)
		.SetNormalPadding(FMargin(6.f, 4.f, 6.f, 4.f))
		.SetPressedPadding(FMargin(6.f, 4.f, 6.f, 4.f));

	const FButtonStyle RightTransportButton = FButtonStyle(TransportButtonStyle)
		.SetNormal(RightBrush)
		.SetHovered(RightBrush)
		.SetPressed(RightBrush)
		.SetDisabled(RightBrush)
		.SetNormalPadding(FMargin(6.f, 4.f, 8.f, 4.f))
		.SetPressedPadding(FMargin(6.f, 4.f, 8.f, 4.f));

	const FButtonStyle PlayTransportButton = FButtonStyle(LeftTransportButton)
		.SetHoveredForeground(PlayColor)
		.SetPressedForeground(PlayPressedColor);

	const FButtonStyle StopTransportButton = FButtonStyle(MiddleTransportButton)
		.SetHoveredForeground(StopColor)
		.SetPressedForeground(StopPressedColor);

	const FButtonStyle ResetTransportButton = FButtonStyle(RightTransportButton)
		.SetHoveredForeground(ResetColor)
		.SetPressedForeground(ResetPressedColor);

	Style->Set("SequencerPlaylists.TransportButton.Play", PlayTransportButton);
	Style->Set("SequencerPlaylists.TransportButton.Stop", StopTransportButton);
	Style->Set("SequencerPlaylists.TransportButton.Reset", ResetTransportButton);

	const FButtonStyle HoverTransportButtonStyle = FButtonStyle()
		.SetNormal(FSlateNoResource())
		.SetHovered(FSlateNoResource())
		.SetPressed(FSlateNoResource())
		.SetNormalForeground(FStyleColors::Foreground)
		.SetNormalPadding(FMargin(0, 0, 0, 0))
		.SetPressedPadding(FMargin(0, 0, 0, 0));

	const FButtonStyle PlayHoverTransportButton = FButtonStyle(HoverTransportButtonStyle)
		.SetHoveredForeground(PlayColor)
		.SetPressedForeground(PlayPressedColor);

	const FButtonStyle StopHoverTransportButton = FButtonStyle(HoverTransportButtonStyle)
		.SetHoveredForeground(StopColor)
		.SetPressedForeground(StopPressedColor);

	const FButtonStyle ResetHoverTransportButton = FButtonStyle(HoverTransportButtonStyle)
		.SetHoveredForeground(ResetColor)
		.SetPressedForeground(ResetPressedColor);

	Style->Set("SequencerPlaylists.HoverTransport.Play", PlayHoverTransportButton);
	Style->Set("SequencerPlaylists.HoverTransport.Stop", StopHoverTransportButton);
	Style->Set("SequencerPlaylists.HoverTransport.Reset", ResetHoverTransportButton);

	FEditableTextBoxStyle EditableTextStyle = FEditableTextBoxStyle()
		.SetFont(FCoreStyle::GetDefaultFontStyle("Regular", 9))
		.SetBackgroundImageNormal(FSlateNoResource())
		.SetBackgroundImageHovered(FSlateNoResource())
		.SetBackgroundImageFocused(FSlateNoResource())
		.SetBackgroundImageReadOnly(FSlateNoResource())
		.SetBackgroundColor(FLinearColor::Transparent)
		.SetForegroundColor(FSlateColor::UseForeground());

	Style->Set("SequencerPlaylists.EditableTextBox", EditableTextStyle);
	Style->Set("SequencerPlaylists.TitleFont", FCoreStyle::GetDefaultFontStyle("Bold", 18));
	Style->Set("SequencerPlaylists.DescriptionFont", FCoreStyle::GetDefaultFontStyle("Regular", 8));

	{
		// These assets live in engine slate folders; change the root temporarily
		const FString PreviousContentRoot = Style->GetContentRootDir();
		ON_SCOPE_EXIT{ Style->SetContentRoot(PreviousContentRoot); };

		// Engine/Content/Slate/...
		{
			const FString EngineSlateDir = FPaths::EngineContentDir() / TEXT("Slate");
			Style->SetContentRoot(EngineSlateDir);

			Style->Set("SequencerPlaylists.Ellipsis", new IMAGE_BRUSH_SVG("Starship/Common/ellipsis-vertical-narrow", FVector2D(6, 24)));
		}

		// Engine/Content/Editor/Slate/...
		{
			const FString EngineEditorSlateDir = FPaths::EngineContentDir() / TEXT("Editor") / TEXT("Slate");
			Style->SetContentRoot(EngineEditorSlateDir);

			Style->Set("SequencerPlaylists.NewPlaylist.Overlay", new IMAGE_BRUSH_SVG("Starship/MainToolbar/ToolBadgePlus", Icon20x20, FStyleColors::AccentGreen));
			Style->Set("SequencerPlaylists.SavePlaylistAs", new IMAGE_BRUSH_SVG("Starship/Common/SaveCurrentAs", Icon16x16));
			Style->Set("SequencerPlaylists.OpenPlaylist", new IMAGE_BRUSH_SVG("Starship/Common/OpenAsset", Icon16x16));

			Style->Set("SequencerPlaylists.Play", new IMAGE_BRUSH_SVG("Starship/Common/play", Icon20x20));
			Style->Set("SequencerPlaylists.Play.Small", new IMAGE_BRUSH_SVG("Starship/Common/play", Icon12x12));
			Style->Set("SequencerPlaylists.Stop", new IMAGE_BRUSH_SVG("Starship/MainToolbar/stop", Icon20x20));
			Style->Set("SequencerPlaylists.Stop.Small", new IMAGE_BRUSH_SVG("Starship/MainToolbar/stop", Icon12x12));
		}
	}

	return Style;
}

void FSequencerPlaylistsStyle::ReloadTextures()
{
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().GetRenderer()->ReloadTextureResources();
	}
}

const ISlateStyle& FSequencerPlaylistsStyle::Get()
{
	return *StyleInstance;
}
