// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdMode.h"

struct FUVGenerationSettings;
enum class EGenerateUVProjectionType : uint8;

class FUVGenerationTool : public FEdMode
{
public:
	static const FEditorModeID EM_UVGeneration;

	// FEdMode interface
	virtual FVector GetWidgetLocation() const override { return ShapePosition; }
	virtual bool ShouldDrawWidget() const override { return true; }
	virtual bool AllowWidgetMove() override { return true; }
	virtual bool GetCustomDrawingCoordinateSystem(FMatrix& InMatrix, void* InData) override;
	virtual bool InputDelta(FEditorViewportClient* InViewportClient, FViewport* InViewport, FVector& InDrag, FRotator& InRot, FVector& InScale) override;
	virtual bool StartTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport) override;
	virtual bool EndTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport) override;

	virtual void Enter() override;
	virtual void Exit() override;
	// End FEdMode interface

	// FEditorCommonDrawHelper interface
	virtual void Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI) override;
	// End FEditorCommonDrawHelper interface

	/** Updates the settings needed to display the shape */
	void SetShapeSettings(const FUVGenerationSettings& GenerationSettings);
	
	DECLARE_EVENT_ThreeParams(FUVGenerationTool, FOnShapeSettingsChanged, const FVector& /*Position*/, const FVector& /*Size*/, const FRotator& /*Rotation*/);
	FOnShapeSettingsChanged& OnShapeSettingsChanged(){ return OnShapeSettingsChangedEvent; };

private:

	bool IsHandlingInputs() const;

	/** Indicate the type of shape we need to display. */
	EGenerateUVProjectionType ShapeType;

	/** Indicate the shape's center position */
	FVector ShapePosition;
	
	/** Indicate the shape's size, behaves like applying scaling on the shape. */
	FVector ShapeSize;
	
	/** The shape's rotation. */
	FRotator ShapeRotation;

	FWidget::EWidgetMode PreviousWidgetMode;

	FOnShapeSettingsChanged OnShapeSettingsChangedEvent;

	bool bIsTrackingWidgetDrag = false;
};