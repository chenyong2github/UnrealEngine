// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassDebuggerStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Interfaces/IPluginManager.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleMacros.h"


TSharedPtr<FSlateStyleSet> FMassDebuggerStyle::StyleSet = nullptr;

FString FMassDebuggerStyle::InContent(const FString& RelativePath, const ANSICHAR* Extension)
{
	static FString ContentDir = IPluginManager::Get().FindPlugin(TEXT("MassEntity"))->GetContentDir() / TEXT("Slate");
	return (ContentDir / RelativePath) + Extension;
}

void FMassDebuggerStyle::Initialize()
{
	if (StyleSet.IsValid())
	{
		return;
	}

	const FVector2D Icon16x16(16.0f, 16.0f);

	StyleSet = MakeShared<FSlateStyleSet>(GetStyleSetName());

	StyleSet->SetContentRoot(FPaths::EngineContentDir() / TEXT("Editor/Slate"));
	StyleSet->SetCoreContentRoot(FPaths::EngineContentDir() / TEXT("Slate"));

	FSlateStyleSet& Style = *StyleSet.Get();
	StyleSet->Set("MassDebuggerApp.TabIcon", new FSlateVectorImageBrush(InContent("MassDebugger", ".svg"), Icon16x16));
	
	FSlateStyleRegistry::RegisterSlateStyle(*StyleSet.Get());
}

void FMassDebuggerStyle::Shutdown()
{
	if (StyleSet.IsValid())
	{
		FSlateStyleRegistry::UnRegisterSlateStyle(*StyleSet.Get());
		ensure(StyleSet.IsUnique());
		StyleSet.Reset();
	}
}

FName FMassDebuggerStyle::GetStyleSetName()
{
	static FName StyleName("MassDebuggerStyle");
	return StyleName;
}
