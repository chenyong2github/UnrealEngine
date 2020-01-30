// Copyright Epic Games, Inc. All Rights Reserved.

#include "Drawing/MeshDebugDrawing.h"
#include "DynamicMesh3.h"
#include "FrameTypes.h"

#include "SceneManagement.h" // FPrimitiveDrawInterface


void MeshDebugDraw::DrawNormals(
	const FDynamicMeshNormalOverlay* Overlay,
	float Length, FColor Color, float Thickness, bool bScreenSpace,
	FPrimitiveDrawInterface* PDI, const FTransform& Transform)
{
	const FDynamicMesh3* Mesh = Overlay->GetParentMesh();
	for (int ElementID : Overlay->ElementIndicesItr())
	{
		FVector3f Normal = Overlay->GetElement(ElementID);
		int ParentVID = Overlay->GetParentVertex(ElementID);
		FVector3f ParentPos = (FVector3f)Mesh->GetVertex(ParentVID);

		FVector A = (FVector)ParentPos, B = (FVector)(ParentPos + Length * Normal);
		PDI->DrawLine(Transform.TransformPosition(A), Transform.TransformPosition(B),
			Color, 0, Thickness, 0, bScreenSpace);
	}
}





void MeshDebugDraw::DrawVertices(
	const FDynamicMesh3* Mesh, const TArray<int>& Indices,
	float PointSize, FColor Color,
	FPrimitiveDrawInterface* PDI, const FTransform& Transform)
{
	for (int VertID : Indices)
	{
		FVector3d Pos = Mesh->GetVertex(VertID);
		PDI->DrawPoint(Transform.TransformPosition((FVector)Pos), Color, PointSize, SDPG_World);
	}
}

void MeshDebugDraw::DrawVertices(
	const FDynamicMesh3* Mesh, const TSet<int>& Indices,
	float PointSize, FColor Color,
	FPrimitiveDrawInterface* PDI, const FTransform& Transform)
{
	for (int VertID : Indices)
	{
		FVector3d Pos = Mesh->GetVertex(VertID);
		PDI->DrawPoint(Transform.TransformPosition((FVector)Pos), Color, PointSize, SDPG_World);
	}
}



void MeshDebugDraw::DrawTriCentroids(
	const FDynamicMesh3* Mesh, const TArray<int>& Indices,
	float PointSize, FColor Color,
	FPrimitiveDrawInterface* PDI, const FTransform& Transform)
{
	for (int TriID : Indices)
	{
		FVector3d Pos = Mesh->GetTriCentroid(TriID);
		PDI->DrawPoint(Transform.TransformPosition((FVector)Pos), Color, PointSize, SDPG_World);
	}
}



void MeshDebugDraw::DrawSimpleGrid(	
	const FFrame3f& LocalFrame, int GridLines, float GridLineSpacing,
	float LineWidth, FColor Color, bool bDepthTested,
	FPrimitiveDrawInterface* PDI, const FTransform& Transform)
{
	ESceneDepthPriorityGroup DepthPriority = (bDepthTested) ? SDPG_World : SDPG_Foreground;

	FFrame3f WorldFrame = LocalFrame;
	WorldFrame.Transform(Transform);

	float Width = (float)(GridLines-1) * GridLineSpacing;
	float Extent = Width * 0.5;

	FVector3f Origin = WorldFrame.Origin;
	FVector3f X = WorldFrame.X();
	FVector3f Y = WorldFrame.Y();
	FVector3f A, B;

	int LineSteps = GridLines / 2;
	for (int i = 0; i < LineSteps; i++)
	{
		float dx = (float)i * GridLineSpacing;
		A = Origin - Extent * Y - dx * X;
		B = Origin + Extent * Y - dx * X;
		PDI->DrawLine((FVector)A, (FVector)B, Color, DepthPriority, LineWidth, 0, true);
		A = Origin - Extent * Y + dx * X;
		B = Origin + Extent * Y + dx * X;
		PDI->DrawLine((FVector)A, (FVector)B, Color, DepthPriority, LineWidth, 0, true);

		A = Origin - Extent * X - dx * Y;
		B = Origin + Extent * X - dx * Y;
		PDI->DrawLine((FVector)A, (FVector)B, Color, DepthPriority, LineWidth, 0, true);
		A = Origin - Extent * X + dx * Y;
		B = Origin + Extent * X + dx * Y;
		PDI->DrawLine((FVector)A, (FVector)B, Color, DepthPriority, LineWidth, 0, true);
	}

}
