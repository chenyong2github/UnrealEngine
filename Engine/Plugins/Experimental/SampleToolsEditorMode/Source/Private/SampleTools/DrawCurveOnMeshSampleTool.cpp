// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DrawCurveOnMeshSampleTool.h"
#include "ToolBuilderUtil.h"
#include "SceneManagement.h"   // FPrimitiveDrawInterface

// localization namespace
#define LOCTEXT_NAMESPACE "UDrawCurveOnMeshSampleTool"

/*
 * ToolBuilder
 */


UMeshSurfacePointTool* UDrawCurveOnMeshSampleToolBuilder::CreateNewTool(const FToolBuilderState & SceneState) const
{
	UDrawCurveOnMeshSampleTool* NewTool = NewObject<UDrawCurveOnMeshSampleTool>();
	return NewTool;
}



/*
 * Tool
 */


UDrawCurveOnMeshSampleTool::UDrawCurveOnMeshSampleTool()
{
	Thickness = 4.0f;
	DepthBias = 0.0f;
	MinSpacing = 1.0;
	NormalOffset = 0.25;
	Color = FLinearColor(255, 0, 0);
	bScreenSpace = true;
}


void UDrawCurveOnMeshSampleTool::Setup()
{
	UMeshSurfacePointTool::Setup();

	// provide self as property object
	ToolPropertyObjects.Add(this);
}



void UDrawCurveOnMeshSampleTool::Render(IToolsContextRenderAPI* RenderAPI)
{
	FPrimitiveDrawInterface* PDI = RenderAPI->GetPrimitiveDrawInterface();
	
	int NumPts = Positions.Num();
	for (int i = 0; i < NumPts - 1; ++i)
	{
		FVector A = Positions[i] + NormalOffset*Normals[i];
		FVector B = Positions[i+1] + NormalOffset*Normals[i+1];
		PDI->DrawLine(A, B, Color, 0, Thickness, DepthBias, bScreenSpace);
	}
}




void UDrawCurveOnMeshSampleTool::OnBeginDrag(const FRay& Ray)
{
	Positions.Reset();
	Normals.Reset();
	
	FHitResult OutHit;
	if (HitTest(Ray, OutHit))
	{
		Positions.Add(OutHit.ImpactPoint);
		Normals.Add(OutHit.ImpactNormal);
	}

}

void UDrawCurveOnMeshSampleTool::OnUpdateDrag(const FRay& Ray)
{
	FHitResult OutHit;
	if (HitTest(Ray, OutHit))
	{
		if ( FVector::Dist(OutHit.ImpactPoint, Positions[Positions.Num()-1]) > MinSpacing)
		{
			Positions.Add(OutHit.ImpactPoint);
			Normals.Add(OutHit.ImpactNormal);
		}
	}
}

void UDrawCurveOnMeshSampleTool::OnEndDrag(const FRay& Ray)
{

}






#undef LOCTEXT_NAMESPACE