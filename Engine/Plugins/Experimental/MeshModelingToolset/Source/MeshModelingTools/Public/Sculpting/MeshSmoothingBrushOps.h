// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Sculpting/MeshBrushOpBase.h"
#include "DynamicMesh3.h"
#include "MeshWeights.h"
#include "Async/ParallelFor.h"


class FSmoothBrushOp : public FMeshSculptBrushOp
{

public:

	virtual void ApplyStamp(const FDynamicMesh3* Mesh, const FSculptBrushStamp& Stamp, const TArray<int32>& Vertices, TArray<FVector3d>& NewPositionsOut) override
	{
		const FVector3d& StampPos = Stamp.LocalFrame.Origin;

		ParallelFor(Vertices.Num(), [&](int32 k)
		{
			int32 VertIdx = Vertices[k];

			FVector3d OrigPos = Mesh->GetVertex(VertIdx);

			double Falloff = GetFalloff().Evaluate(Stamp, OrigPos);

			FVector3d SmoothedPos = (CurrentOptions.bPreserveUVFlow) ?
				FMeshWeights::MeanValueCentroid(*Mesh, VertIdx) : FMeshWeights::UniformCentroid(*Mesh, VertIdx);

			FVector3d NewPos = FVector3d::Lerp(OrigPos, SmoothedPos, Falloff * Stamp.Power);

			NewPositionsOut[k] = NewPos;
		});
	}
};










class FFlattenBrushOp : public FMeshSculptBrushOp
{

public:
	double BrushSpeedTuning = 0.05;

	virtual void ApplyStamp(const FDynamicMesh3* Mesh, const FSculptBrushStamp& Stamp, const TArray<int32>& Vertices, TArray<FVector3d>& NewPositionsOut) override
	{
		static const double PlaneSigns[3] = { 0, -1, 1 };
		double PlaneSign = PlaneSigns[CurrentOptions.WhichPlaneSideIndex];

		const FVector3d& StampPos = Stamp.LocalFrame.Origin;

		double UseSpeed = Stamp.Power * Stamp.Radius * BrushSpeedTuning;
		const FFrame3d& FlattenPlane = Stamp.RegionPlane;

		ParallelFor(Vertices.Num(), [&](int32 k)
		{
			int32 VertIdx = Vertices[k];
			FVector3d OrigPos = Mesh->GetVertex(VertIdx);
			FVector3d PlanePos = FlattenPlane.ToPlane(OrigPos, 2);
			FVector3d Delta = PlanePos - OrigPos;

			double Dot = Delta.Dot(FlattenPlane.Z());
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


	virtual bool WantsStampRegionPlane() const
	{
		return true;
	}
};


