// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EDITOR

#include "CoreMinimal.h"
#include "GLTFExporterModule.h"
#include "Brushes/SlateImageBrush.h"
#include "Interfaces/IPluginManager.h"
#include "Styling/SlateStyleRegistry.h"
#include "Styling/SlateStyle.h"

class FGLTFEditorStyle : public FSlateStyleSet
{
public:

	 FGLTFEditorStyle()
		 : FSlateStyleSet("GLTFEditorStyle")
	 {
	 	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(GLTFEXPORTER_MODULE_NAME);
	 	check(Plugin.IsValid());

	 	FSlateStyleSet::SetContentRoot(Plugin->GetContentDir());

	 	const FVector2D Icon16x16(16.0f, 16.0f);
	 	Set("Icon16x16", new FSlateImageBrush(FSlateStyleSet::RootToContentDir(TEXT("Icon16x16.png")), Icon16x16));

		FSlateStyleRegistry::RegisterSlateStyle(*this);
	 }

	 virtual ~FGLTFEditorStyle() override
	 {
		FSlateStyleRegistry::UnRegisterSlateStyle(*this);
	 }

	static FGLTFEditorStyle& Get()
	{
		static FGLTFEditorStyle Singleton;
		return Singleton;
	}
};

#endif
