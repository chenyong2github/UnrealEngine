// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "SceneView.h"

#include "GizmoViewContext.generated.h"

/** 
 * A context object that is meant to hold the scene information for the hovered viewport
 * on a game thread, to be used by a gizmo later for hit testing. The organization mirrors
 * FSceneView so that functions could be written in a templated way to use either FSceneView
 * or UGizmoViewContext, though UGizmoViewContext only keeps the needed data.
 */
UCLASS()
class INTERACTIVETOOLSFRAMEWORK_API UGizmoViewContext : public UObject
{
	GENERATED_BODY()
public:
	
	// Wrapping class for the matrices so that they can be accessed in the same way
	// that they are accessed in FSceneView.
	class FMatrices
	{
	public:
		void ResetFromSceneView(const FSceneView& SceneView)
		{
			ViewMatrix = SceneView.ViewMatrices.GetViewMatrix();
			ViewProjectionMatrix = SceneView.ViewMatrices.GetViewProjectionMatrix();
		}

		const FMatrix& GetViewMatrix() const { return ViewMatrix; }
		const FMatrix& GetViewProjectionMatrix() const { return ViewProjectionMatrix; }

	protected:
		FMatrix ViewMatrix;
		FMatrix ViewProjectionMatrix;
	};

	// Use this to reinitialize the object each frame for the hovered viewport.
	void ResetFromSceneView(const FSceneView& SceneView)
	{
		UnscaledViewRect = SceneView.UnscaledViewRect;
		ViewMatrices.ResetFromSceneView(SceneView);
		bIsPerspectiveProjection = SceneView.IsPerspectiveProjection();
		ViewLocation = SceneView.ViewLocation;
	}

	// FSceneView-like functions/properties:
	FVector GetViewRight() const { return ViewMatrices.GetViewMatrix().GetColumn(0); }
	FVector GetViewUp() const { return ViewMatrices.GetViewMatrix().GetColumn(1); }
	FVector GetViewDirection() const { return ViewMatrices.GetViewMatrix().GetColumn(2); }

	// As a function just for similarity with FSceneView
	bool IsPerspectiveProjection() const { return bIsPerspectiveProjection; }

	FVector4 WorldToScreen(const FVector& WorldPoint) const
	{
		return ViewMatrices.GetViewProjectionMatrix().TransformFVector4(FVector4(WorldPoint, 1));
	}

	FMatrices ViewMatrices;
	FIntRect UnscaledViewRect;
	FVector	ViewLocation;

protected:
	bool bIsPerspectiveProjection;
};