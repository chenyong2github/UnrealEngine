// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SteamVREditorStyle.h"
#include "SteamVREditor.h"
#include "Runtime/Projects/Public/Interfaces/IPluginManager.h"

TSharedPtr< FSlateStyleSet > FSteamVREditorStyle::StyleInstance = NULL;

void FSteamVREditorStyle::Initialize()
{
	if (!StyleInstance.IsValid())
	{
		StyleInstance = Create();
		FSlateStyleRegistry::RegisterSlateStyle(*StyleInstance);
	}
}

void FSteamVREditorStyle::Shutdown()
{
	FSlateStyleRegistry::UnRegisterSlateStyle(*StyleInstance);
	ensure(StyleInstance.IsUnique());
	StyleInstance.Reset();
}

FName FSteamVREditorStyle::GetStyleSetName()
{
	static FName StyleSetName(TEXT("SteamVREditorStyle"));
	return StyleSetName;
}

#define IMAGE_BRUSH( RelativePath, ... ) FSlateImageBrush( Style->RootToContentDir( RelativePath, TEXT(".png") ), __VA_ARGS__ )
#define BOX_BRUSH( RelativePath, ... ) FSlateBoxBrush( Style->RootToContentDir( RelativePath, TEXT(".png") ), __VA_ARGS__ )
#define BORDER_BRUSH( RelativePath, ... ) FSlateBorderBrush( Style->RootToContentDir( RelativePath, TEXT(".png") ), __VA_ARGS__ )
#define TTF_FONT( RelativePath, ... ) FSlateFontInfo( Style->RootToContentDir( RelativePath, TEXT(".ttf") ), __VA_ARGS__ )
#define OTF_FONT( RelativePath, ... ) FSlateFontInfo( Style->RootToContentDir( RelativePath, TEXT(".otf") ), __VA_ARGS__ )

const FVector2D Icon16x16(16.0f, 16.0f);
const FVector2D Icon20x20(20.0f, 20.0f);
const FVector2D Icon40x40(40.0f, 40.0f);

TSharedRef< FSlateStyleSet > FSteamVREditorStyle::Create()
{
	TSharedRef< FSlateStyleSet > Style = MakeShareable(new FSlateStyleSet("SteamVREditorStyle"));
	Style->SetContentRoot(IPluginManager::Get().FindPlugin("SteamVR")->GetBaseDir() / TEXT("Resources"));

	Style->Set("SteamVREditor.PluginAction", new IMAGE_BRUSH(TEXT("Icon_PluginAction"), Icon40x40));
	Style->Set("SteamVREditor.JsonActionManifest", new IMAGE_BRUSH(TEXT("Icon_regenerate_action_manifest"), Icon16x16));
	Style->Set("SteamVREditor.JsonControllerBindings", new IMAGE_BRUSH(TEXT("Icon_regenerate_controller_bindings"), Icon16x16));
	Style->Set("SteamVREditor.ReloadActionManifest", new IMAGE_BRUSH(TEXT("Icon_reload_action_manifest"), Icon16x16));
	Style->Set("SteamVREditor.LaunchBindingsURL", new IMAGE_BRUSH(TEXT("Icon_launch_bindings_url"), Icon16x16));
	//Style->Set("SteamVREditor.AddSampleInputs", new IMAGE_BRUSH(TEXT("Icon_add_sample_inputs"), Icon16x16));

	return Style;
}

#undef IMAGE_BRUSH
#undef BOX_BRUSH
#undef BORDER_BRUSH
#undef TTF_FONT
#undef OTF_FONT

void FSteamVREditorStyle::ReloadTextures()
{
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().GetRenderer()->ReloadTextureResources();
	}
}

const ISlateStyle& FSteamVREditorStyle::Get()
{
	return *StyleInstance;
}
