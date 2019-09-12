// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Attribute.h"
#include "VectorTypes.h"
#include "ToolIndicatorSet.h"
#include "BrushToolIndicator.generated.h"

/*
 * Circle indicator
 */
UCLASS()
class UBrushStampSizeIndicator : public UObject, public IToolIndicator
{
	GENERATED_BODY()

public:
	UBrushStampSizeIndicator();
	virtual ~UBrushStampSizeIndicator();

	virtual void Connect(UInteractiveTool* Tool) override;
	virtual void Disconnect() override;

	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;
	virtual void Tick(float DeltaTime) override;

	UInteractiveTool* ParentTool;
	float Radius;
	FVector Center;
	FVector Normal;
	int SampleStepCount;
	FColor LineColor;
	float LineThickness;
	bool IsPixelThickness;
	int DepthLayer;

	bool bDrawSecondaryLines;
	FColor SecondaryLineColor;
	float SecondaryLineThickness;

	TAttribute<float> BrushRadius;
	TAttribute<FVector> BrushPosition;
	TAttribute<FVector> BrushNormal;


protected:
	void DrawCircle(FPrimitiveDrawInterface* PDI, const FVector3f& AxisX, const FVector3f& AxisY, float UseRadius, bool bIsSecondary);
	void DrawLine(FPrimitiveDrawInterface* PDI, const FVector3f& Start, const FVector3f& End, bool bIsSecondary);
};