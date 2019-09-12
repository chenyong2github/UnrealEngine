// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "BrushToolIndicator.h"
#include "SceneManagement.h" // for FPrimitiveDrawInterface
#include "VectorUtil.h"


UBrushStampSizeIndicator::UBrushStampSizeIndicator()
{
	ParentTool = nullptr;
	Radius = 1.0;
	Center = FVector::ZeroVector;
	Normal = FVector(0, 1, 0);

	SampleStepCount = 32;
	LineColor = FColor(255, 0, 0);
	LineThickness = 2.0f;
	IsPixelThickness = true;
	DepthLayer = 0;

	SecondaryLineThickness = 0.5f;
	SecondaryLineColor = FColor(128, 128, 128, 128);
	bDrawSecondaryLines = true;
}

UBrushStampSizeIndicator::~UBrushStampSizeIndicator()
{
	check(ParentTool == nullptr);
}

void UBrushStampSizeIndicator::Connect(UInteractiveTool* Tool)
{
	ParentTool = Tool;
}

void UBrushStampSizeIndicator::Disconnect()
{
	ParentTool = nullptr;
}

void UBrushStampSizeIndicator::Render(IToolsContextRenderAPI* RenderAPI)
{
	if (BrushRadius.IsBound())
	{
		Radius = BrushRadius.Get();
	}
	if (BrushPosition.IsBound())
	{
		Center = BrushPosition.Get();
	}
	if (BrushNormal.IsBound())
	{
		Normal = BrushNormal.Get();
	}

	FPrimitiveDrawInterface* PDI = RenderAPI->GetPrimitiveDrawInterface();

	FVector3f Perp1, Perp2;
	VectorUtil::MakePerpVectors( (FVector3f)Normal, Perp1, Perp2);

	DrawCircle(PDI, Perp1, Perp2, Radius, false);
	if (bDrawSecondaryLines)
	{
		DrawCircle(PDI, Perp1, Perp2, Radius/2, true);
		DrawLine(PDI, Center, Center + Radius*Normal, true);
		//DrawCircle(PDI, Normal, Perp1, true);
		//DrawCircle(PDI, Normal, Perp2, true);
	}
}

void UBrushStampSizeIndicator::Tick(float DeltaTime)
{
}

void UBrushStampSizeIndicator::DrawCircle(FPrimitiveDrawInterface* PDI, const FVector3f& AxisX, const FVector3f& AxisY, float UseRadius, bool bIsSecondary)
{
	FVector3f Prev = Center + UseRadius * AxisX;
	for (int k = 1; k <= SampleStepCount; ++k)
	{
		float angle = (float)k / (float)SampleStepCount * FMathd::TwoPi;
		FVector2f Next2D = UseRadius * FVector2f((float)cos(angle), (float)sin(angle));
		FVector3f Next3D = Center + Next2D.X*AxisX + Next2D.Y*AxisY;
		PDI->DrawLine(Prev, Next3D, 
			(bIsSecondary) ? SecondaryLineColor : LineColor,
			DepthLayer, 
			(bIsSecondary)? SecondaryLineThickness :LineThickness,
			0.0f, IsPixelThickness);
		Prev = Next3D;
	}
}


void UBrushStampSizeIndicator::DrawLine(FPrimitiveDrawInterface* PDI, const FVector3f& Start, const FVector3f& End, bool bIsSecondary)
{
	PDI->DrawLine(Start, End,
		(bIsSecondary) ? SecondaryLineColor : LineColor,
		DepthLayer,
		(bIsSecondary) ? SecondaryLineThickness : LineThickness,
		0.0f, IsPixelThickness);
}