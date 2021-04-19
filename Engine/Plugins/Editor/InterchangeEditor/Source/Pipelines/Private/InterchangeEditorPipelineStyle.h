// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Interfaces/IPluginManager.h"
#include "Brushes/SlateImageBrush.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"

/**
 * Implements the visual style of the text asset editor UI.
 */
class FInterchangeEditorPipelineStyle
	: public FSlateStyleSet
{
public:

	/** Default constructor. */
	FInterchangeEditorPipelineStyle()
		: FSlateStyleSet("InterchangeEditorPipelineStyle")
	{
		const FVector2D Icon16x16(16.0f, 16.0f);

		const FString BaseDir = IPluginManager::Get().FindPlugin("InterchangeEditor")->GetBaseDir();
		SetContentRoot(BaseDir / TEXT("Content"));

		FSlateImageBrush* LodBrush16 = new FSlateImageBrush(RootToContentDir(TEXT("Resources/Interchange_Lod_Icon_16"), TEXT(".png")), Icon16x16);
		Set("SceneGraphIcon.LodGroup", LodBrush16);
		FSlateImageBrush* JointBrush16 = new FSlateImageBrush(RootToContentDir(TEXT("Resources/Interchange_Joint_Icon_16"), TEXT(".png")), Icon16x16);
		Set("SceneGraphIcon.Joint", JointBrush16);
		FSlateStyleRegistry::RegisterSlateStyle(*this);
	}

	/** Destructor. */
	~FInterchangeEditorPipelineStyle()
	{
		FSlateStyleRegistry::UnRegisterSlateStyle(*this);
	}
};
