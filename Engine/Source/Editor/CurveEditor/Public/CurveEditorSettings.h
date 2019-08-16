// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "CurveEditorSettings.generated.h"

/** Defines visibility states for the tangents in the curve editor. */
UENUM()
enum class ECurveEditorTangentVisibility : uint8
{
	/** All tangents should be visible. */
	AllTangents,
	/** Only tangents from selected keys should be visible. */
	SelectedKeys,
	/** Don't display tangents. */
	NoTangents
};

/** Defines the position to center the zoom about in the curve editor. */
UENUM()
enum class ECurveEditorZoomPosition : uint8
{
	/** Current Time. */
	CurrentTime,

	/** Mouse Position. */
	MousePosition,
};

/** Serializable options for curve editor. */
UCLASS(config=EditorPerProjectUserSettings)
class CURVEEDITOR_API UCurveEditorSettings : public UObject
{
public:
	GENERATED_BODY()

	UCurveEditorSettings();

	/** Gets whether or not the curve editor auto frames the selected curves. */
	bool GetAutoFrameCurveEditor() const;
	/** Sets whether or not the curve editor auto frames the selected curves. */
	void SetAutoFrameCurveEditor(bool InbAutoFrameCurveEditor);

	/** Gets whether or not to show curve tool tips in the curve editor. */
	bool GetShowCurveEditorCurveToolTips() const;
	/** Sets whether or not to show curve tool tips in the curve editor. */
	void SetShowCurveEditorCurveToolTips(bool InbShowCurveEditorCurveToolTips);

	/** Gets the current tangent visibility. */
	ECurveEditorTangentVisibility GetTangentVisibility() const;
	/** Sets the current tangent visibility. */
	void SetTangentVisibility(ECurveEditorTangentVisibility InTangentVisibility);

	/** Get zoom in/out position (mouse position or current time). */
	ECurveEditorZoomPosition GetZoomPosition() const;
	/** Set zoom in/out position (mouse position or current time). */
	void SetZoomPosition(ECurveEditorZoomPosition InZoomPosition);

protected:
	UPROPERTY( config, EditAnywhere, Category="Curve Editor" )
	bool bAutoFrameCurveEditor;

	UPROPERTY( config, EditAnywhere, Category="Curve Editor" )
	bool bShowCurveEditorCurveToolTips;

	UPROPERTY( config, EditAnywhere, Category="Curve Editor" )
	ECurveEditorTangentVisibility TangentVisibility;

	UPROPERTY( config, EditAnywhere, Category="Curve Editor")
	ECurveEditorZoomPosition ZoomPosition;
};
