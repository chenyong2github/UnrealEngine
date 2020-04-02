// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NavMesh/RecastHelpers.h"
#include "DebugUtils/DebugDraw.h"

struct FRecastInternalDebugData : public duDebugDraw
{
	duDebugDrawPrimitives CurrentPrim;
	int32 FirstVertexIndex;

	TArray<uint32> TriangleIndices;
	TArray<FVector> TriangleVertices;
	TArray<uint32> TriangleColors;

	TArray<FVector> LineVertices;
	TArray<uint32>  LineColors;

	TArray<FVector> PointVertices;
	TArray<uint32>  PointColors;

	FRecastInternalDebugData() {}
	virtual ~FRecastInternalDebugData() override {}

	virtual void depthMask(bool state) override { /*unused*/ };
	virtual void texture(bool state) override { /*unused*/ };

	virtual void begin(duDebugDrawPrimitives prim, float size = 1.0f) override
	{
		CurrentPrim = prim;
		FirstVertexIndex = TriangleVertices.Num();
	}

	virtual void vertex(const float* pos, unsigned int color) override
	{
		vertex(pos[0], pos[1], pos[2], color, 0.0f, 0.0f);
	}

	virtual void vertex(const float x, const float y, const float z, unsigned int color) override
	{
		vertex(x, y, z, color, 0.0f, 0.0f);
	}

	virtual void vertex(const float* pos, unsigned int color, const float* uv) override
	{
		vertex(pos[0], pos[1], pos[2], color, uv[0], uv[1]);
	}

	virtual void vertex(const float x, const float y, const float z, unsigned int color, const float u, const float v) override;

	virtual void end() override;
};

