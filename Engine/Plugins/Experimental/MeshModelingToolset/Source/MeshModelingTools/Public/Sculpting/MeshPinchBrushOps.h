// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Sculpting/MeshBrushOpBase.h"
#include "DynamicMesh3.h"
#include "Async/ParallelFor.h"
#include "LineTypes.h"
#include "MeshPinchBrushOps.generated.h"


UCLASS()
class MESHMODELINGTOOLS_API UPinchBrushOpProps : public UMeshSculptBrushOpProps
{
	GENERATED_BODY()
public:
	/** Strength of the Brush */
	UPROPERTY(EditAnywhere, Category = PinchBrush, meta = (DisplayName = "Strength", UIMin = "0.0", UIMax = "1.0", ClampMin = "0.0", ClampMax = "1.0"))
	float Strength = 0.5;

	/** Amount of falloff to apply */
	UPROPERTY(EditAnywhere, Category = PinchBrush, meta = (DisplayName = "Falloff", UIMin = "0.0", UIMax = "1.0", ClampMin = "0.0", ClampMax = "1.0"))
	float Falloff = 0.75;

	/** Depth of Brush into surface along surface normal */
	UPROPERTY(EditAnywhere, Category = PinchBrush, meta = (UIMin = "-0.5", UIMax = "0.5", ClampMin = "-1.0", ClampMax = "1.0"))
	float Depth = 0;

	/** When enabled, brush will damp motion of vertices that would move perpendicular to brush stroke direction */
	UPROPERTY(EditAnywhere, Category = PinchBrush)
	bool bPerpDamping = true;

	virtual float GetStrength() override { return Strength; }
	virtual float GetFalloff() override { return Falloff; }
	virtual float GetDepth() override { return Depth; }
};


class FPinchBrushOp : public FMeshSculptBrushOp
{

public:
	double BrushSpeedTuning = 3.0;

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
		double UseSpeed = Stamp.Direction * Stamp.Radius * Stamp.Power * Stamp.DeltaTime * BrushSpeedTuning;

		LastSmoothBrushPosLocal = NewSmoothBrushPosLocal;

		bool bLimitDrag = GetPropertySetAs<UPinchBrushOpProps>()->bPerpDamping;

		ParallelFor(Vertices.Num(), [&](int32 k)
		{
			int32 VertIdx = Vertices[k];
			FVector3d OrigPos = Mesh->GetVertex(VertIdx);

			FVector3d Delta = DepthPosLocal - OrigPos;
			Delta.Normalize();
			FVector3d MoveVec = Delta;

			// pinch uses 1/x falloff
			double Distance = OrigPos.Distance(NewSmoothBrushPosLocal);
			double NormalizedDistance = (Distance / Stamp.Radius) + 0.0001;
			double LinearFalloff = FMathd::Clamp(1.0 - NormalizedDistance, 0.0, 1.0);
			double InvFalloff = LinearFalloff * LinearFalloff * LinearFalloff;
			double UseFalloff = FMathd::Lerp(LinearFalloff, InvFalloff, Stamp.Falloff);

			if (bLimitDrag && bHaveMotion && UseFalloff < 0.7f)
			{
				double AnglePower = 1.0 - FMathd::Abs(MoveVec.Normalized().Dot(MotionVec));
				UseFalloff *= AnglePower;
			}

			FVector3d NewPos = OrigPos + UseFalloff * UseSpeed * MoveVec;
			NewPositionsOut[k] = NewPos;
		});
	}


	virtual ESculptBrushOpTargetType GetBrushTargetType() const
	{
		return ESculptBrushOpTargetType::TargetMesh;
	}
};


