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

		// Set miscellaneous icons
		SetContentRoot(FPaths::EnginePluginsDir() / TEXT("Runtime/nDisplay/Content/Icons/OperatorPanel/"));
		Set("DrawPoly", new IMAGE_BRUSH("PolyPath_40x", Icon40x40));

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
