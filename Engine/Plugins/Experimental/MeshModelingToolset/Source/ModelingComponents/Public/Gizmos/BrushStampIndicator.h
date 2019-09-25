// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "InteractiveGizmo.h"
#include "InteractiveGizmoBuilder.h"
#include "BrushStampIndicator.generated.h"

class UPrimitiveComponent;
class UPreviewMesh;

UCLASS()
class MODELINGCOMPONENTS_API UBrushStampIndicatorBuilder : public UInteractiveGizmoBuilder
{
	GENERATED_BODY()
public:
	virtual UInteractiveGizmo* BuildGizmo(const FToolBuilderState& SceneState) const override;
};



/*
 * UBrushStampIndicator is a simple 3D brush indicator. 
 */
UCLASS(Transient)
class MODELINGCOMPONENTS_API UBrushStampIndicator : public UInteractiveGizmo
{
	GENERATED_BODY()

public:

	// UInteractiveGizmo interface/implementation

	virtual void Setup() override;
	virtual void Shutdown() override;
	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;
	virtual void Tick(float DeltaTime) override;


	/**
	 * Update the Radius, Position, and Normal of the stamp indicator
	 */
	virtual void Update(float Radius, const FVector& Position, const FVector& Normal);


	/**
	 * Generate a mesh that is intended to be set as the AttachedComponent of a UBrushStampIndicator
	 * Material is set to a default transparent material.
	 * @warning Calling code must manage the returned UPreviewMesh! (keep it alive, Disconnect it, etc)
	 */
	static UPreviewMesh* MakeDefaultSphereMesh(UObject* Parent, UWorld* World, int Resolution = 32);


public:

	UPROPERTY()
	float BrushRadius = 1.0f;

	UPROPERTY()
	FVector BrushPosition = FVector::ZeroVector;

	UPROPERTY()
	FVector BrushNormal = FVector(0, 0, 1);;



	UPROPERTY()
	bool bDrawIndicatorLines = true;


	UPROPERTY()
	int SampleStepCount = 32;

	UPROPERTY()
	FLinearColor LineColor = FLinearColor(0.96f, 0.06f, 0.06f);

	UPROPERTY()
	float LineThickness = 2.0f;

	UPROPERTY()
	bool bDepthTested = false;



	UPROPERTY()
	bool bDrawSecondaryLines = true;

	UPROPERTY()
	float SecondaryLineThickness = 0.5f;

	UPROPERTY()
	FLinearColor SecondaryLineColor = FLinearColor(0.5f, 0.5f, 0.5f, 0.5f);


	/**
	 * Optional Component that will be transformed such that it tracks the Radius/Position/Normal
	 */
	UPROPERTY()
	UPrimitiveComponent* AttachedComponent;

protected:
	UPrimitiveComponent* ScaleInitializedComponent = nullptr;		// we are just using this as a key, never calling any functions on it
	FVector InitialComponentScale;
};