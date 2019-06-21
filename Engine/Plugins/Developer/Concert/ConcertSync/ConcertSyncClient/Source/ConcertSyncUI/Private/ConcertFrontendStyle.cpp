// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ConcertFrontendStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Styling/SlateTypes.h"
#include "EditorStyleSet.h"
#include "Interfaces/IPluginManager.h"
#include "SlateOptMacros.h"

#define IMAGE_PLUGIN_BRUSH( RelativePath, ... ) FSlateImageBrush( FConcertFrontendStyle::InContent( RelativePath, ".png" ), __VA_ARGS__ )
#define IMAGE_BRUSH(RelativePath, ...) FSlateImageBrush(StyleSet->RootToContentDir(RelativePath, TEXT(".png")), __VA_ARGS__)

FString FConcertFrontendStyle::InContent(const FString& RelativePath, const ANSICHAR* Extension)
{
	static FString ContentDir = IPluginManager::Get().FindPlugin(TEXT("ConcertSyncClient"))->GetContentDir();
	return (ContentDir / RelativePath) + Extension;
}

TSharedPtr< class FSlateStyleSet > FConcertFrontendStyle::StyleSet;

FName FConcertFrontendStyle::GetStyleSetName()
{
	return FName(TEXT("ConcertFrontendStyle"));
}

void FConcertFrontendStyle::Initialize()
{
	// Only register once
	if (StyleSet.IsValid())
	{
		return;
	}

	StyleSet = MakeShared<FSlateStyleSet>(GetStyleSetName());
	StyleSet->SetContentRoot(FPaths::EngineContentDir() / TEXT("Editor/Slate"));
	StyleSet->SetCoreContentRoot(FPaths::EngineContentDir() / TEXT("Slate"));

	// Const icon sizes
	const FVector2D Icon16x16(16.0f, 16.0f);
	const FVector2D Icon20x20(20.0f, 20.0f);
	const FVector2D Icon24x24(24.0f, 24.0f);
	const FVector2D Icon40x40(40.0f, 40.0f);
	const FVector2D Icon48x48(48.0f, 48.0f);

	// 16x16
	StyleSet->Set("Concert.Concert", new IMAGE_PLUGIN_BRUSH("Icons/icon_Concert_16x", Icon16x16));
	StyleSet->Set("Concert.Persist", new IMAGE_PLUGIN_BRUSH("Icons/icon_ConcertPersist_16x", Icon16x16));
	StyleSet->Set("Concert.MyLock", new IMAGE_PLUGIN_BRUSH("Icons/icon_ConcertMyLock_16x", Icon16x16));
	StyleSet->Set("Concert.OtherLock", new IMAGE_PLUGIN_BRUSH("Icons/icon_ConcertOtherLock_16x", Icon16x16));
	StyleSet->Set("Concert.ModifiedByOther", new IMAGE_PLUGIN_BRUSH("Icons/icon_ConcertModifiedByOther_16x", Icon16x16));
	StyleSet->Set("Concert.Stateless", new IMAGE_PLUGIN_BRUSH("Icons/icon_ConcertStateless_16x", Icon16x16)); // Used as icon aside on multi-user tabs title.
	StyleSet->Set("Concert.OnlineServer", new IMAGE_PLUGIN_BRUSH("Icons/icon_ConcertServerOnline_16x", Icon16x16)); // Used to make a list of online server more attractive.
	StyleSet->Set("Concert.DefaultServer", new IMAGE_PLUGIN_BRUSH("Icons/icon_ConcertServerDefault_16x", Icon16x16)); // Used to quickly spot the configured 'default server' in a list.

	// 20x20 -> For toolbar small icons.
	StyleSet->Set("Concert.Online.Small", new IMAGE_PLUGIN_BRUSH("Icons/icon_ConcertOnline_40x", Icon20x20));
	StyleSet->Set("Concert.Offline.Small", new IMAGE_PLUGIN_BRUSH("Icons/icon_ConcertOffline_40x", Icon20x20));

	// 24x24/48x48 -> For sequencer toolbar.
	StyleSet->Set("Concert.Sequencer.SyncTimeline",       new IMAGE_PLUGIN_BRUSH("Icons/icon_SequencerSyncTimeline_48x", Icon48x48)); // Enable/disable playback and time scrubbing from a remote client.
	StyleSet->Set("Concert.Sequencer.SyncTimeline.Small", new IMAGE_PLUGIN_BRUSH("Icons/icon_SequencerSyncTimeline_48x", Icon24x24));
	StyleSet->Set("Concert.Sequencer.SyncSequence",       new IMAGE_PLUGIN_BRUSH("Icons/icon_SequencerSyncSequence_48x", Icon48x48)); // Allows or not a remote client to open/close sequencer.
	StyleSet->Set("Concert.Sequencer.SyncSequence.Small", new IMAGE_PLUGIN_BRUSH("Icons/icon_SequencerSyncSequence_48x", Icon24x24));

	// 40x40
	StyleSet->Set("Concert.Online", new IMAGE_PLUGIN_BRUSH("Icons/icon_ConcertOnline_40x", Icon40x40));
	StyleSet->Set("Concert.Offline", new IMAGE_PLUGIN_BRUSH("Icons/icon_ConcertOffline_40x", Icon40x40));

	// Activity Text
	{
		FTextBlockStyle BoldText = FEditorStyle::Get().GetWidgetStyle<FTextBlockStyle>("RichTextBlock.Bold");
		StyleSet->Set("ActivityText.Bold", FTextBlockStyle(BoldText));
	}

	// Colors
	{
		StyleSet->Set("Concert.Color.LocalUser", FLinearColor(0.31f, 0.749f, 0.333f));
		StyleSet->Set("Concert.Color.OtherUser", FLinearColor(0.93f, 0.608f, 0.169f));
	}

	// Colors
	StyleSet->Set("Concert.DisconnectedColor", FLinearColor(0.672f, 0.672f, 0.672f));

	FSlateStyleRegistry::RegisterSlateStyle(*StyleSet.Get());
};

void FConcertFrontendStyle::Shutdown()
{
	if (StyleSet.IsValid())
	{
		FSlateStyleRegistry::UnRegisterSlateStyle(*StyleSet.Get());
		ensure(StyleSet.IsUnique());
		StyleSet.Reset();
	}
}

TSharedPtr<class ISlateStyle> FConcertFrontendStyle::Get()
{
	return StyleSet;
}

#undef IMAGE_PLUGIN_BRUSH
#undef IMAGE_BRUSH

