// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Sculpting/MeshBrushOpBase.h"
#include "DynamicMesh3.h"
#include "MeshNormals.h"
#include "MeshWeights.h"
#include "Async/ParallelFor.h"


class FInflateBrushOp : public FMeshSculptBrushOp
{

public:
	double BrushSpeedTuning = 0.05;

	virtual ESculptBrushOpTargetType GetBrushTargetType() const override
	{
		return ESculptBrushOpTargetType::SculptMesh;
	}

	virtual void ApplyStamp(const FDynamicMesh3* Mesh, const FSculptBrushStamp& Stamp, const TArray<int32>& Vertices, TArray<FVector3d>& NewPositionsOut) override
	{
		const FVector3d& StampPos = Stamp.LocalFrame.Origin;

		double UsePower = Stamp.Direction * Stamp.Power * Stamp.Radius * BrushSpeedTuning;

		ParallelFor(Vertices.Num(), [&](int32 k)
		{
			int32 VertIdx = Vertices[k];
			FVector3d OrigPos = Mesh->GetVertex(VertIdx);
			FVector3d Normal = FMeshNormals::ComputeVertexNormal(*Mesh, VertIdx);
			FVector3d MoveVec = UsePower * Normal;

			double Falloff = GetFalloff().Evaluate(Stamp, OrigPos);

			FVector3d NewPos = OrigPos + Falloff * MoveVec;
			NewPositionsOut[k] = NewPos;
		});
	}

};

