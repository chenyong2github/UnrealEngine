// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Sculpting/MeshBrushOpBase.h"
#include "DynamicMesh3.h"
#include "Async/ParallelFor.h"


class FPlaneBrushOp : public FMeshSculptBrushOp
{

public:
	double BrushSpeedTuning = 0.05;

	FFrame3d StrokePlane;

	virtual void BeginStroke(const FDynamicMesh3* Mesh, const FSculptBrushStamp& Stamp, const TArray<int32>& InitialVertices) override
	{
		StrokePlane = CurrentOptions.ConstantReferencePlane;
	}

	virtual void ApplyStamp(const FDynamicMesh3* Mesh, const FSculptBrushStamp& Stamp, const TArray<int32>& Vertices, TArray<FVector3d>& NewPositionsOut) override
	{
		static const double PlaneSigns[3] = { 0, -1, 1 };
		double PlaneSign = PlaneSigns[CurrentOptions.WhichPlaneSideIndex];

		const FVector3d& StampPos = Stamp.LocalFrame.Origin;

		double UseSpeed = Stamp.Power * Stamp.Radius * BrushSpeedTuning;

		ParallelFor(Vertices.Num(), [&](int32 k)
		{
			int32 VertIdx = Vertices[k];
			FVector3d OrigPos = Mesh->GetVertex(VertIdx);
			FVector3d PlanePos = StrokePlane.ToPlane(OrigPos, 2);
			FVector3d Delta = PlanePos - OrigPos;

			double Dot = Delta.Dot(StrokePlane.Z());
			FVector3d NewPos = OrigPos;
			if (Dot * PlaneSign >= 0)
			{
				double Falloff = GetFalloff().Evaluate(Stamp, OrigPos);
				FVector3d MoveVec = Falloff * UseSpeed * Delta;
				double MaxDist = Delta.Normalize();
				NewPos = (MoveVec.SquaredLength() > MaxDist * MaxDist) ?
					PlanePos : (OrigPos + Falloff * MoveVec);
			}

			NewPositionsOut[k] = NewPos;
		});
	}

};

