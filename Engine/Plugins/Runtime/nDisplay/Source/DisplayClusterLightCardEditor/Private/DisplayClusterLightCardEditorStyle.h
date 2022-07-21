// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateStyleRegistry.h"
#include "Styling/SlateStyleMacros.h"
#include "Styling/SlateStyle.h"


/**
 * Implements the visual style of the operator panel
 */
class FDisplayClusterLightCardEditorStyle final : public FSlateStyleSet
{
public:

	FDisplayClusterLightCardEditorStyle()
		: FSlateStyleSet("DisplayClusterLightCardEditorStyle")
	{
		const FVector2D Icon40x40(40.0f, 40.0f);
		const FVector2D Icon16x16(16.0f, 16.0f);

		// Set miscellaneous icons
		SetContentRoot(FPaths::EnginePluginsDir() / TEXT("Runtime/nDisplay/Content/Icons/OperatorPanel/"));
		SetCoreContentRoot(FPaths::EngineContentDir() / TEXT("Editor/Slate"));

		Set("DrawPoly", new IMAGE_BRUSH("PolyPath_40x", Icon40x40));

		Set("DisplayClusterLightCardEditor.PasteHere", new CORE_IMAGE_BRUSH_SVG("Starship/Actors/paste-here", Icon16x16));

		Set("DisplayClusterLightCardEditor.ViewOrientationTop", new CORE_IMAGE_BRUSH_SVG("Starship/Common/ViewTop", Icon16x16));
		Set("DisplayClusterLightCardEditor.ViewOrientationBottom", new CORE_IMAGE_BRUSH_SVG("Starship/Common/ViewBottom", Icon16x16));
		Set("DisplayClusterLightCardEditor.ViewOrientationLeft", new CORE_IMAGE_BRUSH_SVG("Starship/Common/ViewLeft", Icon16x16));
		Set("DisplayClusterLightCardEditor.ViewOrientationRight", new CORE_IMAGE_BRUSH_SVG("Starship/Common/ViewRight", Icon16x16));
		Set("DisplayClusterLightCardEditor.ViewOrientationFront", new CORE_IMAGE_BRUSH_SVG("Starship/Common/ViewFront", Icon16x16));
		Set("DisplayClusterLightCardEditor.ViewOrientationBack", new CORE_IMAGE_BRUSH_SVG("Starship/Common/ViewBack", Icon16x16));

		Set("UVLightCard", new CORE_IMAGE_BRUSH_SVG("Starship/Common/SetDrawUVs", Icon16x16));

		Set("DisplayClusterLightCardEditor.Labels", new CORE_IMAGE_BRUSH_SVG("Starship/AssetIcons/Font_16", Icon16x16));
		
		FSlateStyleRegistry::RegisterSlateStyle(*this);
	}

	virtual ~FDisplayClusterLightCardEditorStyle()
	{
		FSlateStyleRegistry::UnRegisterSlateStyle(*this);
	}

	static FDisplayClusterLightCardEditorStyle& Get()
	{
		static FDisplayClusterLightCardEditorStyle Inst;
		return Inst;
	}
};
