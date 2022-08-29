// Copyright Epic Games, Inc. All Rights Reserved.

#include "VPRolesEditorStyle.h"
#include "Interfaces/IPluginManager.h"
#include "Framework/Application/SlateApplication.h"
#include "Slate/SlateGameResources.h"
#include "Styling/SlateStyleRegistry.h"
#include "Styling/StyleColors.h"

TSharedPtr<FSlateStyleSet> FVPRolesEditorStyle::StyleInstance;

void FVPRolesEditorStyle::Initialize()
{
	if (!StyleInstance.IsValid())
	{
		StyleInstance = Create();
		FSlateStyleRegistry::RegisterSlateStyle(*StyleInstance);
	}
}

void FVPRolesEditorStyle::Shutdown()
{
	FSlateStyleRegistry::UnRegisterSlateStyle(*StyleInstance);
	ensure(StyleInstance.IsUnique());
	StyleInstance.Reset();
}

FName FVPRolesEditorStyle::GetStyleSetName()
{
	static FName StyleSetName(TEXT("VPRolesEditorStyle"));
	return StyleSetName;
}

#define IMAGE_BRUSH( RelativePath, ... ) FSlateImageBrush( Style->RootToContentDir( RelativePath, TEXT(".png") ), __VA_ARGS__ )

const FVector2D Icon16x16(16.0f, 16.0f);
const FVector2D Icon40x40(40.0f, 40.0f);

TSharedRef<FSlateStyleSet> FVPRolesEditorStyle::Create()
{
	TSharedRef<FSlateStyleSet> Style = MakeShared<FSlateStyleSet>(FVPRolesEditorStyle::GetStyleSetName());
	Style->SetContentRoot(IPluginManager::Get().FindPlugin("VirtualProductionUtilities")->GetBaseDir() / TEXT("Resources"));

	FSlateBrush AddRoleBrush = *FAppStyle::Get().GetBrush("Icons.Plus");
	AddRoleBrush.TintColor = FStyleColors::AccentGreen;

	Style->Set("VPRolesEditor.TabIcon", new IMAGE_BRUSH(TEXT("VPRolesButtonIcon_40x"), Icon16x16));
	Style->Set("VPRolesEditor.OpenMenu", new IMAGE_BRUSH(TEXT("VPRolesButtonIcon_40x"), Icon40x40));
	Style->Set("VPRolesEditor.AddRole", new FSlateBrush(AddRoleBrush));

	return Style;
}

const ISlateStyle& FVPRolesEditorStyle::Get()
{
	static const FVPRolesEditorStyle Inst;
	return *StyleInstance;
}

#undef IMAGE_BRUSH
#undef BOX_BRUSH
#undef BORDER_BRUSH
#undef TTF_FONT
#undef OTF_FONT
