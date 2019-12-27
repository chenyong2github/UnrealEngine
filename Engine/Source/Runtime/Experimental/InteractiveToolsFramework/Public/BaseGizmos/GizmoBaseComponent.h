// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Components/PrimitiveComponent.h"
#include "GizmoBaseComponent.generated.h"

/**
 * Base class for simple Components intended to be used as part of 3D Gizmos.
 * Contains common properties and utility functions.
 * This class does nothing by itself, use subclasses like UGizmoCircleComponent
 */
UCLASS(ClassGroup = Utility, HideCategories = (Physics, Collision, Mobile))
class INTERACTIVETOOLSFRAMEWORK_API UGizmoBaseComponent : public UPrimitiveComponent
{
	GENERATED_BODY()

public:
	UGizmoBaseComponent()
	{
		bUseEditorCompositing = true;
	}


public:
	UPROPERTY(EditAnywhere, Category = Options)
	FLinearColor Color = FLinearColor::Red;


	UPROPERTY(EditAnywhere, Category = Options)
	float HoverSizeMultiplier = 2.0f;


	UPROPERTY(EditAnywhere, Category = Options)
	float PixelHitDistanceThreshold = 7.0f;


public:
	UFUNCTION()
	void UpdateHoverState(bool bHoveringIn)
	{
		if (bHoveringIn != bHovering)
		{
			bHovering = bHoveringIn;
		}
	}

	UFUNCTION()
	void UpdateWorldLocalState(bool bWorldIn)
	{
		if (bWorldIn != bWorld)
		{
			bWorld = bWorldIn;
		}
	}


protected:
	// scale factor between pixel distances and world distances at Gizmo origin
	float DynamicPixelToWorldScale = 1.0f;

	// hover state
	bool bHovering = false;

	// world/local coordinates state
	bool bWorld = false;
};