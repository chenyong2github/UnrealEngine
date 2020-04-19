// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Sculpting/MeshBrushOpBase.h"
#include "DynamicMesh3.h"
#include "Async/ParallelFor.h"
#include "LineTypes.h"


class FPinchBrushOp : public FMeshSculptBrushOp
{

public:
	double BrushSpeedTuning = 0.05;

	FVector3d LastSmoothBrushPosLocal;
	FVector3d LastSmoothBrushNormalLocal;;

	virtual void BeginStroke(const FDynamicMesh3* Mesh, const FSculptBrushStamp& Stamp, const TArray<int32>& InitialVertices) 
	{
		LastSmoothBrushPosLocal = Stamp.LocalFrame.Origin;
		LastSmoothBrushNormalLocal = Stamp.LocalFrame.Z();
	}


	virtual void ApplyStamp(const FDynamicMesh3* Mesh, const FSculptBrushStamp& Stamp, const TArray<int32>& Vertices, TArray<FVector3d>& NewPositionsOut) override
	{
		// hardcoded LazyBrush
		FVector3d NewSmoothBrushPosLocal = FVector3d::Lerp(LastSmoothBrushPosLocal, Stamp.LocalFrame.Origin, 0.75f);
		FVector3d NewSmoothBrushNormal = FVector3d::Lerp(LastSmoothBrushNormalLocal, Stamp.LocalFrame.Z(), 0.75f);
		NewSmoothBrushNormal.Normalize();

		FVector3d MotionVec = NewSmoothBrushPosLocal - LastSmoothBrushPosLocal;
		bool bHaveMotion = (MotionVec.Length() > FMathd::ZeroTolerance);
		MotionVec.Normalize();
		FLine3d MoveLine(LastSmoothBrushPosLocal, MotionVec);

		FVector3d DepthPosLocal = NewSmoothBrushPosLocal - (Stamp.Depth * Stamp.Radius * NewSmoothBrushNormal);
		double UseSpeed = Stamp.Direction * Stamp.Radius * Stamp.Power * BrushSpeedTuning;

		LastSmoothBrushPosLocal = NewSmoothBrushPosLocal;

		ParallelFor(Vertices.Num(), [&](int32 k)
		{
			int32 VertIdx = Vertices[k];
			FVector3d OrigPos = Mesh->GetVertex(VertIdx);

			FVector3d Delta = DepthPosLocal - OrigPos;
			FVector3d MoveVec = UseSpeed * Delta;

			//double Falloff = GetFalloff().Evaluate(Stamp, OrigPos);

			// pinch uses 1/x falloff
			double Distance = OrigPos.Distance(NewSmoothBrushPosLocal);
			double NormalizedDistance = (Distance / Stamp.Radius) + 0.0001;
			double Falloff = FMathd::Clamp(1.0 - NormalizedDistance, 0.0, 1.0);
			Falloff *= Falloff; Falloff *= Falloff;

			if (bHaveMotion && Falloff < 0.8f)
			{
				double AnglePower = 1.0 - FMathd::Abs(MoveVec.Normalized().Dot(MotionVec));
				Falloff *= AnglePower;
			}

			FVector3d NewPos = OrigPos + Falloff * MoveVec;
			NewPositionsOut[k] = NewPos;
		});
	}


	virtual ESculptBrushOpTargetType GetBrushTargetType() const
	{
		return ESculptBrushOpTargetType::TargetMesh;
	}
};


