// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/DirectLinkExtensionStyle.h"

#include "Styling/SlateStyleRegistry.h"
#include "Brushes/SlateImageBrush.h"
#include "EditorStyleSet.h"
#include "Interfaces/IPluginManager.h"

#define IMAGE_PLUGIN_BRUSH( RelativePath, ... ) FSlateImageBrush( FDirectLinkExtensionStyle::InContent( RelativePath, TEXT(".png") ), __VA_ARGS__ )

namespace UE::DatasmithImporter
{
	TUniquePtr<FSlateStyleSet> FDirectLinkExtensionStyle::StyleSet;

	FString FDirectLinkExtensionStyle::InContent(const FString& RelativePath, const TCHAR* Extension)
	{
		static FString ContentDir = IPluginManager::Get().FindPlugin(TEXT("DatasmithImporter"))->GetContentDir();
		return (ContentDir / RelativePath) + Extension;
	}

	FName FDirectLinkExtensionStyle::GetStyleSetName()
	{
		return FName(TEXT("DirectLinkExtensionStyle"));
	}

	void FDirectLinkExtensionStyle::Initialize()
	{
		// Only register once
		if (StyleSet.IsValid())
		{
			return;
		}

		StyleSet = MakeUnique<FSlateStyleSet>(GetStyleSetName());
		StyleSet->SetContentRoot(FPaths::EngineContentDir() / TEXT("Editor/Slate"));
		StyleSet->SetCoreContentRoot(FPaths::EngineContentDir() / TEXT("Slate"));


		TUniquePtr< FSlateStyleSet > Style = MakeUnique<FSlateStyleSet>(GetStyleSetName());
		const FString ContentDir = IPluginManager::Get().FindPlugin(TEXT("DatasmithImporter"))->GetContentDir();
		Style->SetContentRoot(ContentDir);

		// Const icon sizes
		const FVector2D Icon16x16(16.0f, 16.0f);

		// 16x16
		StyleSet->Set("DirectLinkExtension.NotAvailable", new IMAGE_PLUGIN_BRUSH(TEXT("Icons/SourceNotAvailable16"), Icon16x16));
		StyleSet->Set("DirectLinkExtension.OutOfSync", new IMAGE_PLUGIN_BRUSH(TEXT("Icons/SourceOutOfSync16"), Icon16x16));
		StyleSet->Set("DirectLinkExtension.UpToDate", new IMAGE_PLUGIN_BRUSH(TEXT("Icons/SourceUpToDate16"), Icon16x16));
		StyleSet->Set("DirectLinkExtension.AutoReimport", new IMAGE_PLUGIN_BRUSH(TEXT("Icons/SourceAutoReimport16"), Icon16x16));

		FSlateStyleRegistry::RegisterSlateStyle(*StyleSet.Get());
	};

	void FDirectLinkExtensionStyle::Shutdown()
	{
		if (StyleSet.IsValid())
		{
			FSlateStyleRegistry::UnRegisterSlateStyle(*StyleSet.Get());
			StyleSet.Reset();
		}
	}

	const ISlateStyle& FDirectLinkExtensionStyle::Get()
	{
		return *StyleSet.Get();
	}
}

#undef IMAGE_PLUGIN_BRUSH