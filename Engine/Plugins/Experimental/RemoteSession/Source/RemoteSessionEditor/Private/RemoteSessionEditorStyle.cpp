// Copyright Epic Games, Inc. All Rights Reserved.

#include "RemoteSessionEditorStyle.h"

#include "Brushes/SlateImageBrush.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"

namespace RemoteSessionStyle
{
	const FVector2D Icon16x16(16.0f, 16.0f);
	const FVector2D Icon20x20(20.0f, 20.0f);
	const FVector2D Icon40x40(40.0f, 40.0f);
	const FVector2D Icon64x64(64.0f, 64.0f);

	const FName NAME_StyleName(TEXT("RemoteSessionStyle"));

	static TUniquePtr<FSlateStyleSet> StyleInstance;
}

#define IMAGE_BRUSH(RelativePath, ...) FSlateImageBrush(RemoteSessionStyle::StyleInstance->RootToContentDir(RelativePath, TEXT(".png")), __VA_ARGS__)

void FRemoteSessionEditorStyle::Register()
{
	RemoteSessionStyle::StyleInstance = MakeUnique<FSlateStyleSet>(RemoteSessionStyle::NAME_StyleName);
	RemoteSessionStyle::StyleInstance->SetContentRoot(FPaths::EnginePluginsDir() / TEXT("Experimental/RemoteSession/Content/Editor/Icons/"));


	RemoteSessionStyle::StyleInstance->Set("TabIcons.RemoteSession.Small", new IMAGE_BRUSH("RemoteSession_Stream_16x", RemoteSessionStyle::Icon16x16));
	RemoteSessionStyle::StyleInstance->Set("RemoteSessionStream.Stream", new IMAGE_BRUSH("RemoteSession_Stream_40x", RemoteSessionStyle::Icon40x40));
	RemoteSessionStyle::StyleInstance->Set("RemoteSessionStream.Stop", new IMAGE_BRUSH("RemoteSession_Stop_40x", RemoteSessionStyle::Icon40x40));
	RemoteSessionStyle::StyleInstance->Set("RemoteSessionStream.Settings", new IMAGE_BRUSH("RemoteSession_Settings_40x", RemoteSessionStyle::Icon40x40));

	FSlateStyleRegistry::RegisterSlateStyle(*RemoteSessionStyle::StyleInstance.Get());
}

void FRemoteSessionEditorStyle::Unregister()
{
	FSlateStyleRegistry::UnRegisterSlateStyle(*RemoteSessionStyle::StyleInstance.Get());
	RemoteSessionStyle::StyleInstance.Reset();
}

FName FRemoteSessionEditorStyle::GetStyleSetName()
{
	return RemoteSessionStyle::NAME_StyleName;
}

const ISlateStyle& FRemoteSessionEditorStyle::Get()
{
	check(RemoteSessionStyle::StyleInstance.IsValid());
	return *RemoteSessionStyle::StyleInstance.Get();
}

#undef IMAGE_BRUSH
