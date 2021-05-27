// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "UObject/GCObject.h"


class UMaterial;
class UMaterialExpressionTextureSample;
class UTexture;

struct FGeometry;
struct FPointerEvent;

/** Delegate to be executed when the viewport area is clicked */
DECLARE_DELEGATE_TwoParams(FSimulcamViewportClickedEventHandler, const FGeometry& MyGeometry, const FPointerEvent& MouseEvent);

/**
 * UI to display the provided UTexture and respond to mouse clicks. 
 */
class SSimulcamViewport : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSimulcamViewport) { }
	SLATE_ATTRIBUTE(FVector2D, BrushImageSize);
	SLATE_EVENT(FSimulcamViewportClickedEventHandler, OnSimulcamViewportClicked)
	SLATE_END_ARGS()

public:

	/** Default constructor. */
	SSimulcamViewport();

public:

	/**
	 * Construct this widget
	 *
	 * @param InArgs The declaration data for this widget.
	 * @param InTexture The source texture to render if any
	 */
	void Construct(const FArguments& InArgs, UTexture* InTexture);

	/**
	 * Tick this widget
	 * 
	 * @param InAllottedGeometry Geometry of the widget.
	 * @param InCurrentTime CurrentTime of the engine.
	 * @param InDeltaTime Deltatime since last frame.
	 */
	virtual void Tick(const FGeometry& InAllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:

	class FInternalReferenceCollector : public FGCObject
	{
	public:
		FInternalReferenceCollector(SSimulcamViewport* InObject)
			: Object(InObject)
		{
		}

		//~ FGCObject interface
		virtual void AddReferencedObjects(FReferenceCollector& InCollector) override
		{
			InCollector.AddReferencedObject(Object->Material);
			InCollector.AddReferencedObject(Object->TextureSampler);
		}

	private:
		SSimulcamViewport* Object;
	};

	friend FInternalReferenceCollector;

	/** Collector to keep the UObject alive. */
	FInternalReferenceCollector Collector;

	/** The material that wraps the video texture for display in an SImage. */
	UMaterial* Material;

	/** The Slate brush that renders the material. */
	TSharedPtr<FSlateBrush> MaterialBrush;

	/** The video texture sampler in the wrapper material. */
	UMaterialExpressionTextureSample* TextureSampler;

	/** Brush image size attribute. */
	TAttribute<FVector2D> BrushImageSize;

private:

	/** Delegate to be executed when the viewport area is clicked */
	FSimulcamViewportClickedEventHandler OnSimulcamViewportClicked;
};

