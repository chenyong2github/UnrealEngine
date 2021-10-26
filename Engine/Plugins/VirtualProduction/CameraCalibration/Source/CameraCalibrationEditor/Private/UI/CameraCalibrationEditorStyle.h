// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateStyleRegistry.h"
#include "Styling/SlateStyleMacros.h"
#include "Styling/SlateStyle.h"


/**
 * Implements the visual style of the camera calibration tools UI.
 */
class FCameraCalibrationEditorStyle	final : public FSlateStyleSet
{
public:

	/** Default constructor. */
	FCameraCalibrationEditorStyle()	: FSlateStyleSet("CameraCalibrationEditorStyle")
	{
		const FVector2D Icon16x16(16.0f, 16.0f);
		const FVector2D Icon20x20(20.0f, 20.0f);
		const FVector2D Icon64x64(64.0f, 64.0f);

		// Set placement browser icons
		SetContentRoot(FPaths::EngineContentDir() / TEXT("Editor/Slate/"));
		Set("PlacementBrowser.Icons.VirtualProduction", new IMAGE_BRUSH_SVG("Starship/Common/VirtualProduction", Icon16x16));

		// Set miscellaneous icons
		SetContentRoot(FPaths::EnginePluginsDir() / TEXT("VirtualProduction/CameraCalibration/Content/Editor/Icons/"));
		Set("ClassThumbnail.LensFile", new IMAGE_BRUSH("LensFileIcon_64x", Icon64x64));
		Set("ClassIcon.LensFile", new IMAGE_BRUSH("LensFileIcon_20x", Icon20x20));

		FSlateStyleRegistry::RegisterSlateStyle(*this);
	}

	/** Virtual destructor. */
	virtual ~FCameraCalibrationEditorStyle()
	{
		FSlateStyleRegistry::UnRegisterSlateStyle(*this);
	}

	static FCameraCalibrationEditorStyle& Get()
	{
		static FCameraCalibrationEditorStyle Inst;
		return Inst;
	}
};
