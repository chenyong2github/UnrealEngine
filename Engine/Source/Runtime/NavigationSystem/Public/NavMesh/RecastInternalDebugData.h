// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NavMesh/RecastHelpers.h"
#include "DebugUtils/DebugDraw.h"

struct FRecastInternalDebugData : public duDebugDraw
{
	struct Command
	{
		duDebugDrawPrimitives Prim;
		int Offset; // Index offset
		int Count;  // Index count
	};

	TArray<Command> Commands;
	TArray<uint32> Indices;
	TArray<FVector> Vertices;
	TArray<uint32>  VertexColors;

	FRecastInternalDebugData() {}
	virtual ~FRecastInternalDebugData() override {}

	virtual void depthMask(bool state) override { /*unused*/ };
	virtual void texture(bool state) override { /*unused*/ };

	virtual void begin(duDebugDrawPrimitives prim, float size = 1.0f) override
	{
		Command Cmd;
		Cmd.Prim = prim;
		Cmd.Offset = Vertices.Num();    // Misuse to store initial state.
		Cmd.Count = 0;
		Commands.Push(Cmd);
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

