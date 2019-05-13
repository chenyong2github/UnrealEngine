// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "InsightsStyle.h"
#include "Styling/SlateStyleRegistry.h"

#define IMAGE_BRUSH(RelativePath, ...)  FSlateImageBrush (FPaths::EngineContentDir() / "Editor/Slate"/ RelativePath + TEXT(".png"), __VA_ARGS__)
#define BOX_BRUSH(RelativePath, ...)    FSlateBoxBrush   (FPaths::EngineContentDir() / "Editor/Slate"/ RelativePath + TEXT(".png"), __VA_ARGS__)
#define BORDER_BRUSH(RelativePath, ...) FSlateBorderBrush(FPaths::EngineContentDir() / "Editor/Slate"/ RelativePath + TEXT(".png"), __VA_ARGS__)

TSharedPtr< FSlateStyleSet > FInsightsStyle::StyleInstance = nullptr;

void FInsightsStyle::Initialize()
{
	if (!StyleInstance.IsValid())
	{
		StyleInstance = Create();
		FSlateStyleRegistry::RegisterSlateStyle(*StyleInstance);
	}
}

void FInsightsStyle::Shutdown()
{
	FSlateStyleRegistry::UnRegisterSlateStyle(*StyleInstance);
	ensure(StyleInstance.IsUnique());
	StyleInstance.Reset();
}

FName FInsightsStyle::GetStyleSetName()
{
	static FName StyleSetName(TEXT("InsightsStyle"));
	return StyleSetName;
}

TSharedRef< FSlateStyleSet > FInsightsStyle::Create()
{
	TSharedRef<FSlateStyleSet> StyleRef = MakeShareable(new FSlateStyleSet(FInsightsStyle::GetStyleSetName()));
	StyleRef->SetContentRoot(FPaths::EngineContentDir() / TEXT("Editor/Slate"));
	StyleRef->SetCoreContentRoot(FPaths::EngineContentDir() / TEXT("Slate"));

	FSlateStyleSet& Style = StyleRef.Get();

	// Widgets
	Style.Set("Insights.Background", new BOX_BRUSH("Insights/Background", FMargin(5.f / 12.f)));
	Style.Set("Insights.BorderPadding", FVector2D(1,0));

	return StyleRef;
}

#undef IMAGE_BRUSH
#undef BOX_BRUSH
#undef BORDER_BRUSH

const ISlateStyle& FInsightsStyle::Get()
{
	return *StyleInstance;
}
