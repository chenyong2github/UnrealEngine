// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "InteractiveGizmo.h"
#include "InteractiveGizmoBuilder.h"
#include "BrushStampIndicator.generated.h"



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

	virtual void Setup() override;
	virtual void Shutdown() override;

	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;

	virtual void Tick(float DeltaTime) override;


	virtual void Update(float Radius, const FVector& Position, const FVector& Normal);


public:

	UPROPERTY()
	float BrushRadius = 1.0f;

	UPROPERTY()
	FVector BrushPosition = FVector::ZeroVector;

	UPROPERTY()
	FVector BrushNormal = FVector(0, 0, 1);;


	UPROPERTY()
	int SampleStepCount = 32;

	UPROPERTY()
	FLinearColor LineColor = FLinearColor(0.96f, 0.06f, 0.06f);

	UPROPERTY()
	float LineThickness = 2.0f;

	UPROPERTY()
	bool bDepthTested = false;

	UPROPERTY()
	bool IsPixelThickness = true;

	UPROPERTY()
	int DepthLayer = 0;


	UPROPERTY()
	bool bDrawSecondaryLines = true;

	UPROPERTY()
	float SecondaryLineThickness = 0.5f;

	UPROPERTY()
	FLinearColor SecondaryLineColor = FLinearColor(0.5f, 0.5f, 0.5f, 0.5f);



};