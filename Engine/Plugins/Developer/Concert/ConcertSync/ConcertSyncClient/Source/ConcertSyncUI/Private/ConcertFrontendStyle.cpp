// Copyright Epic Games, Inc. All Rights Reserved.

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

	// Most icons were designed to be used at 80% opacity.
	const FLinearColor IconColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, 0.8f));

	// 16x16
	StyleSet->Set("Concert.Persist",         new IMAGE_PLUGIN_BRUSH("Icons/icon_ConcertPersist_16x", Icon16x16));
	StyleSet->Set("Concert.LockBackground",  new IMAGE_PLUGIN_BRUSH("Icons/icon_ConcertLockBackground_16x", Icon16x16));
	StyleSet->Set("Concert.MyLock",          new IMAGE_PLUGIN_BRUSH("Icons/icon_ConcertMyLock_16x", Icon16x16));
	StyleSet->Set("Concert.OtherLock",       new IMAGE_PLUGIN_BRUSH("Icons/icon_ConcertOtherLock_16x", Icon16x16));
	StyleSet->Set("Concert.ModifiedByOther", new IMAGE_PLUGIN_BRUSH("Icons/icon_ConcertModifiedByOther_16x", Icon16x16));

	// Multi-user Tab/Menu icons
	StyleSet->Set("Concert.MultiUser", new IMAGE_PLUGIN_BRUSH("Icons/icon_MultiUser_32x", Icon16x16));

	// Maps the UI Command name in Multi-User module. (UI_COMMAND does magic icon mapping when style name and command name matches)
	StyleSet->Set("Concert.OpenBrowser",  new IMAGE_PLUGIN_BRUSH("Icons/icon_MultiUser_32x", Icon16x16));
	StyleSet->Set("Concert.OpenSettings", new IMAGE_PLUGIN_BRUSH("Icons/icon_Settings_32x",  Icon16x16, IconColorAndOpacity));
	StyleSet->Set("Concert.LaunchServer", new IMAGE_PLUGIN_BRUSH("Icons/icon_NewServer_32x", Icon16x16, IconColorAndOpacity));

	// Multi-User Browser
	StyleSet->Set("Concert.ArchiveSession",     new IMAGE_PLUGIN_BRUSH("Icons/icon_ArchiveSession_48x",     Icon24x24, IconColorAndOpacity));
	StyleSet->Set("Concert.CancelAutoJoin",     new IMAGE_PLUGIN_BRUSH("Icons/icon_CancelAutoJoin_48x",     Icon24x24, IconColorAndOpacity));
	StyleSet->Set("Concert.CloseServer",        new IMAGE_PLUGIN_BRUSH("Icons/icon_CloseServer_48x",        Icon24x24, IconColorAndOpacity));
	StyleSet->Set("Concert.DeleteSession",      new IMAGE_PLUGIN_BRUSH("Icons/icon_DeleteSession_48x",      Icon24x24, IconColorAndOpacity));
	StyleSet->Set("Concert.JoinDefaultSession", new IMAGE_PLUGIN_BRUSH("Icons/icon_JoinDefaultSession_48x", Icon24x24, IconColorAndOpacity));
	StyleSet->Set("Concert.JoinSession",        new IMAGE_PLUGIN_BRUSH("Icons/icon_JoinSelectedSession_48x",Icon24x24, IconColorAndOpacity));
	StyleSet->Set("Concert.LeaveSession",       new IMAGE_PLUGIN_BRUSH("Icons/icon_LeaveSession_48x",       Icon24x24, IconColorAndOpacity));
	StyleSet->Set("Concert.NewServer",          new IMAGE_PLUGIN_BRUSH("Icons/icon_NewServer_48x",          Icon24x24, IconColorAndOpacity));
	StyleSet->Set("Concert.NewSession",         new IMAGE_PLUGIN_BRUSH("Icons/icon_NewSession_48x",         Icon24x24, IconColorAndOpacity));
	StyleSet->Set("Concert.PauseSession",       new IMAGE_PLUGIN_BRUSH("Icons/icon_PauseSession_48x",       Icon24x24, IconColorAndOpacity));
	StyleSet->Set("Concert.RestoreSession",     new IMAGE_PLUGIN_BRUSH("Icons/icon_RestoreSession_48x",     Icon24x24, IconColorAndOpacity));
	StyleSet->Set("Concert.ResumeSession",      new IMAGE_PLUGIN_BRUSH("Icons/icon_ResumeSession_48x",      Icon24x24, IconColorAndOpacity));
	StyleSet->Set("Concert.Settings",           new IMAGE_PLUGIN_BRUSH("Icons/icon_Settings_48x",           Icon24x24, IconColorAndOpacity));
	StyleSet->Set("Concert.NewServer.Small",    new IMAGE_PLUGIN_BRUSH("Icons/icon_NewServer_32x",          Icon16x16, IconColorAndOpacity));
	StyleSet->Set("Concert.NewSession.Small",   new IMAGE_PLUGIN_BRUSH("Icons/icon_NewSession_32x",         Icon16x16, IconColorAndOpacity));

	// Multi-user Active session
	StyleSet->Set("Concert.JumpToLocation",     new IMAGE_PLUGIN_BRUSH("Icons/icon_PresenceLocation_32x",   Icon16x16, IconColorAndOpacity));
	StyleSet->Set("Concert.HidePresence",       new IMAGE_PLUGIN_BRUSH("Icons/icon_PresenceEyeOff_32x",     Icon16x16, IconColorAndOpacity));
	StyleSet->Set("Concert.ShowPresence",       new IMAGE_PLUGIN_BRUSH("Icons/icon_PresenceEyeOn_32x",      Icon16x16, IconColorAndOpacity));

	// 24x24/48x48 -> For sequencer toolbar.
	StyleSet->Set("Concert.Sequencer.SyncTimeline",       new IMAGE_PLUGIN_BRUSH("Icons/icon_SequencerSyncTimeline_48x", Icon48x48, IconColorAndOpacity)); // Enable/disable playback and time scrubbing from a remote client.
	StyleSet->Set("Concert.Sequencer.SyncTimeline.Small", new IMAGE_PLUGIN_BRUSH("Icons/icon_SequencerSyncTimeline_48x", Icon24x24, IconColorAndOpacity));
	StyleSet->Set("Concert.Sequencer.SyncSequence",       new IMAGE_PLUGIN_BRUSH("Icons/icon_SequencerSyncSequence_48x", Icon48x48, IconColorAndOpacity)); // Allows or not a remote client to open/close sequencer.
	StyleSet->Set("Concert.Sequencer.SyncSequence.Small", new IMAGE_PLUGIN_BRUSH("Icons/icon_SequencerSyncSequence_48x", Icon24x24, IconColorAndOpacity));
	StyleSet->Set("Concert.Sequencer.SyncUnrelated",      new IMAGE_PLUGIN_BRUSH("Icons/icon_SequencerSyncUnrelated_48x", Icon48x48, IconColorAndOpacity)); // Enable/disable playback and time scrubbing from a remote client event if this user has a different sequence opened.
	StyleSet->Set("Concert.Sequencer.SyncUnrelated.Small",new IMAGE_PLUGIN_BRUSH("Icons/icon_SequencerSyncUnrelated_48x", Icon24x24, IconColorAndOpacity));

	// 40x40 -> Editor toolbar large icons.
	StyleSet->Set("Concert.Browse", new IMAGE_PLUGIN_BRUSH("Icons/icon_MultiUserMenuBrowse_40x", Icon40x40));
	StyleSet->Set("Concert.Join",   new IMAGE_PLUGIN_BRUSH("Icons/icon_MultiUserMenuJoin_40x",   Icon40x40));
	StyleSet->Set("Concert.Leave",  new IMAGE_PLUGIN_BRUSH("Icons/icon_MultiUserMenuLeave_40x",  Icon40x40));
	StyleSet->Set("Concert.Cancel", new IMAGE_PLUGIN_BRUSH("Icons/icon_MultiUserMenuCancel_40x", Icon40x40));

	// 20x20 -> Editor toolbar small icons.
	StyleSet->Set("Concert.Browse.Small", new IMAGE_PLUGIN_BRUSH("Icons/icon_MultiUserMenuBrowse_40x", Icon20x20));
	StyleSet->Set("Concert.Leave.Small",  new IMAGE_PLUGIN_BRUSH("Icons/icon_MultiUserMenuLeave_40x",  Icon20x20));
	StyleSet->Set("Concert.Join.Small",   new IMAGE_PLUGIN_BRUSH("Icons/icon_MultiUserMenuJoin_40x",   Icon20x20));
	StyleSet->Set("Concert.Cancel.Small", new IMAGE_PLUGIN_BRUSH("Icons/icon_MultiUserMenuCancel_40x", Icon20x20));

	// Disaster Recovery
	StyleSet->Set("Concert.RecoveryHub", new IMAGE_PLUGIN_BRUSH("Icons/icon_RecoveryHub_32x", Icon16x16));

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

