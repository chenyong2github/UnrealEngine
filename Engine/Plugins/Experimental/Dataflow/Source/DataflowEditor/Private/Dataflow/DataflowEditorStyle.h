// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleMacros.h"
#include "Styling/SlateStyleRegistry.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"

class FDataflowEditorStyle final : public FSlateStyleSet
{
public:
	FDataflowEditorStyle() : FSlateStyleSet("DataflowEditorStyle")
	{
		const FVector2D Icon16x16(16.f, 16.f);
		const FVector2D Icon64x64(64.f, 64.f);
		const FVector2D Icon56x28(56.f, 28.f);
		const FVector2D Icon28x14(28.f, 14.f);
		const FVector2D Icon40x40(28.f, 14.f);

		SetContentRoot(IPluginManager::Get().FindPlugin("Dataflow")->GetBaseDir() / TEXT("Resources"));
		Set("ClassIcon.Dataflow", new FSlateVectorImageBrush(RootToContentDir(TEXT("DataflowAsset_16.svg")), Icon16x16));
		Set("ClassThumbnail.Dataflow", new FSlateVectorImageBrush(RootToContentDir(TEXT("DataflowAsset_64.svg")), Icon64x64));

		Set("Dataflow.Render.Unknown", new FSlateImageBrush(RootToContentDir(TEXT("Slate/Switch_Undetermined_56x_28x.png")), Icon28x14));
		Set("Dataflow.Render.Disabled", new FSlateImageBrush(RootToContentDir(TEXT("Slate/Switch_OFF_56x_28x.png")), Icon28x14));
		Set("Dataflow.Render.Enabled", new FSlateImageBrush(RootToContentDir(TEXT("Slate/Switch_ON_56x_28x.png")), Icon28x14));

		Set("Dataflow.Cached.False", new FSlateImageBrush(RootToContentDir(TEXT("Slate/status_grey.png")), Icon16x16));
		Set("Dataflow.Cached.True", new FSlateImageBrush(RootToContentDir(TEXT("Slate/status_green.png")), Icon16x16));

		FSlateStyleRegistry::RegisterSlateStyle(*this);
	}

	~FDataflowEditorStyle()
	{
		FSlateStyleRegistry::UnRegisterSlateStyle(*this);
	}

public:

	static FDataflowEditorStyle& Get()
	{
		static FDataflowEditorStyle Inst;
		return Inst;
	}

};

