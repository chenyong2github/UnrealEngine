// Copyright Epic Games, Inc. All Rights Reserved.

#include "NavMesh/RecastInternalDebugData.h"
#include "DebugUtils/DebugDraw.h"

void FRecastInternalDebugData::vertex(const float x, const float y, const float z, unsigned int color, const float u, const float v)
{
	float pos[3] = { x,y,z };
	Vertices.Push(Recast2UnrealPoint(pos));
	VertexColors.Push(color);
}

void FRecastInternalDebugData::end()
{
	if (ensure(Commands.Num() > 0))
	{
		Command& Cmd = Commands.Last();
		const int FirstVertex = Cmd.Offset;

		Cmd.Offset = Indices.Num();

		// Turns quads to triangles
		if (Cmd.Prim == DU_DRAW_QUADS)
		{
			Cmd.Prim = DU_DRAW_TRIS;
			for (int i = FirstVertex; i < Vertices.Num(); i += 4)
			{
				ensure(i + 3 < Vertices.Num());
				Indices.Push(i + 0);
				Indices.Push(i + 1);
				Indices.Push(i + 3);

				Indices.Push(i + 3);
				Indices.Push(i + 1);
				Indices.Push(i + 2);
			}
		}
		else
		{
			for (int i = FirstVertex; i < Vertices.Num(); i++)
			{
				Indices.Push(i);
			}
		}
	}
}
