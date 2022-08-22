// Copyright Epic Games, Inc. All Rights Reserved.

#include "ColorCorrectRegionsStyle.h"
#include "Interfaces/IPluginManager.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Styling/SlateTypes.h"

TSharedPtr<FSlateStyleSet> FColorCorrectRegionsStyle::CCRStyle;

void FColorCorrectRegionsStyle::Initialize()
{
	if (CCRStyle.IsValid())
	{
		return;
	}

	CCRStyle = MakeShared<FSlateStyleSet>(FName("ColorCorrectRegionsStyle"));

	FString CCR_IconPath = IPluginManager::Get().FindPlugin(TEXT("ColorCorrectRegions"))->GetBaseDir() + TEXT("/Resources/PlaceActorPreview.png");
	FString CCW_IconPath = IPluginManager::Get().FindPlugin(TEXT("ColorCorrectRegions"))->GetBaseDir() + TEXT("/Resources/Icon40_CCW.png");
	CCRStyle->Set("CCR.PlaceActorThumbnail", new FSlateImageBrush(CCR_IconPath, FVector2D(40.0f, 40.0f)));
	CCRStyle->Set("CCW.PlaceActorThumbnail", new FSlateImageBrush(CCW_IconPath, FVector2D(40.0f, 40.0f)));
	CCRStyle->Set("CCR.PlaceActorIcon", new FSlateImageBrush(CCR_IconPath, FVector2D(16.0f, 16.0f)));

	FSlateStyleRegistry::RegisterSlateStyle(*CCRStyle.Get());
}

void FColorCorrectRegionsStyle::Shutdown()
{
	if (CCRStyle.IsValid())
	{
		FSlateStyleRegistry::UnRegisterSlateStyle(*CCRStyle.Get());
		ensure(CCRStyle.IsUnique());
		CCRStyle.Reset();
	}
}