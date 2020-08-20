// Copyright Epic Games, Inc. All Rights Reserved.

#include "ColorCorrectRegionsStyle.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Styling/SlateTypes.h"
#include "Interfaces/IPluginManager.h"

TSharedPtr<FSlateStyleSet> FColorCorrectRegionsStyle::CCRStyle;

void FColorCorrectRegionsStyle::Initialize()
{
	if (CCRStyle.IsValid())
	{
		return;
	}

	CCRStyle = MakeShared<FSlateStyleSet>(FName("ColorCorrectRegionsStyle"));

	FString IconPath = IPluginManager::Get().FindPlugin(TEXT("ColorCorrectRegions"))->GetBaseDir() + TEXT("/Resources/PlaceActorPreview.png");
	CCRStyle->Set("CCR.PlaceActorIcon", new FSlateImageBrush(IconPath, FVector2D(40.0f, 40.0f)));

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