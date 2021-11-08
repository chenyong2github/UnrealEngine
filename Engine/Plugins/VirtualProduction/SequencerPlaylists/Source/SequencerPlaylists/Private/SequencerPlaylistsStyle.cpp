// Copyright Epic Games, Inc. All Rights Reserved.

#include "SequencerPlaylistsStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Framework/Application/SlateApplication.h"
#include "Slate/SlateGameResources.h"
#include "Interfaces/IPluginManager.h"
#include "Styling/SlateStyleMacros.h"

#define RootToContentDir Style->RootToContentDir

TSharedPtr<FSlateStyleSet> FSequencerPlaylistsStyle::StyleInstance = nullptr;

// FIXME: These should come from IAssetTypeActions::GetTypeColor()
const FSlateColor FSequencerPlaylistsStyle::StyleColors::Animation(FColor::FromHex("143210"));
const FSlateColor FSequencerPlaylistsStyle::StyleColors::Sequence(FColor::FromHex("931414"));


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

TSharedRef< FSlateStyleSet > FSequencerPlaylistsStyle::Create()
{
	static const FVector2D Icon16x16(16.0f, 16.0f);
	static const FVector2D Icon20x20(20.0f, 20.0f);

	TSharedRef< FSlateStyleSet > Style = MakeShareable(new FSlateStyleSet("SequencerPlaylistsStyle"));
	Style->SetContentRoot(IPluginManager::Get().FindPlugin("SequencerPlaylists")->GetBaseDir() / TEXT("Resources"));

	Style->Set("SequencerPlaylists.OpenPluginWindow", new IMAGE_BRUSH_SVG(TEXT("PlaceholderButtonIcon"), Icon20x20));
	Style->Set("SequencerPlaylists.AnimationBorderBrush", new FSlateColorBrush(StyleColors::Animation));
	Style->Set("SequencerPlaylists.SequenceBorderBrush", new FSlateColorBrush(StyleColors::Sequence));

	{
		// These assets live in the editor slate folder, change the root temporarily
		const FString PreviousContentRoot = Style->GetContentRootDir();
		ON_SCOPE_EXIT{ Style->SetContentRoot(PreviousContentRoot); };

		const FString EngineEditorSlateDir = FPaths::EngineContentDir() / TEXT("Editor/Slate");
		Style->SetContentRoot(EngineEditorSlateDir);

		Style->Set("SequencerPlaylists.DropZone.Above", new BOX_BRUSH("Common/DropZoneIndicator_Above", FMargin(10.0f / 16.0f, 10.0f / 16.0f, 0.f, 0.f), Style->GetSlateColor("SelectionColor")));
		Style->Set("SequencerPlaylists.DropZone.Below", new BOX_BRUSH("Common/DropZoneIndicator_Below", FMargin(10.0f / 16.0f, 0.f, 0.f, 10.0f / 16.0f), Style->GetSlateColor("SelectionColor")));
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
