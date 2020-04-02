// Copyright Epic Games, Inc. All Rights Reserved.

#include "NavMesh/RecastInternalDebugData.h"
#include "DebugUtils/DebugDraw.h"

void FRecastInternalDebugData::vertex(const float x, const float y, const float z, unsigned int color, const float u, const float v)
{
	float RecastPos[3] = { x,y,z };
	const FVector Pos = Recast2UnrealPoint(RecastPos);
	switch(CurrentPrim)
	{
	case DU_DRAW_POINTS:
		PointVertices.Push(Pos);
		PointColors.Push(color);
		break;
	case DU_DRAW_LINES:
		LineVertices.Push(Pos);
		LineColors.Push(color);
		break;
	case DU_DRAW_TRIS:
		// Fallthrough
	case DU_DRAW_QUADS:
		TriangleVertices.Push(Pos);
		TriangleColors.Push(color);
		break;
	}
}

void FRecastInternalDebugData::end()
{
	if (CurrentPrim == DU_DRAW_QUADS)
	{
		// Turns quads to triangles
		for (int32 i = FirstVertexIndex; i < TriangleVertices.Num(); i += 4)
		{
			ensure(i + 3 < TriangleVertices.Num());
			TriangleIndices.Push(i + 0);
			TriangleIndices.Push(i + 1);
			TriangleIndices.Push(i + 3);

			TriangleIndices.Push(i + 3);
			TriangleIndices.Push(i + 1);
			TriangleIndices.Push(i + 2);
		}
	}
	else if (CurrentPrim == DU_DRAW_TRIS)
	{
		// Add indices for triangles.
		for (int32 i = FirstVertexIndex; i < TriangleVertices.Num(); i++)
		{
			TriangleIndices.Push(i);
		}
	}
}
