// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Sculpting/MeshBrushOpBase.h"
#include "DynamicMesh3.h"
#include "MeshNormals.h"
#include "MeshWeights.h"
#include "Async/ParallelFor.h"
#include "MeshMoveBrushOps.generated.h"


UCLASS()
class MESHMODELINGTOOLS_API UMoveBrushOpProps : public UMeshSculptBrushOpProps
{
	GENERATED_BODY()
public:
	/** Strength of the Brush */
	UPROPERTY(EditAnywhere, Category = MoveBrush, meta = (DisplayName = "Strength", UIMin = "0.0", UIMax = "1.0", ClampMin = "0.0", ClampMax = "1.0"))
	float Strength = 1.0;

	/** Amount of falloff to apply */
	UPROPERTY(EditAnywhere, Category = MoveBrush, meta = (DisplayName = "Falloff", UIMin = "0.0", UIMax = "1.0", ClampMin = "0.0", ClampMax = "1.0"))
	float Falloff = 0.5;

	/** Depth of Brush into surface along view ray */
	UPROPERTY(EditAnywhere, Category = MoveBrush, meta = (UIMin = "-0.5", UIMax = "0.5", ClampMin = "-1.0", ClampMax = "1.0"))
	float Depth = 0;

	virtual float GetStrength() override { return Strength; }
	virtual float GetFalloff() override { return Falloff; }
	virtual float GetDepth() override { return Depth; }
};


class FMoveBrushOp : public FMeshSculptBrushOp
{
public:

	virtual void ApplyStamp(const FDynamicMesh3* Mesh, const FSculptBrushStamp& Stamp, const TArray<int32>& Vertices, TArray<FVector3d>& NewPositionsOut) override
	{
		const FVector3d& StampPos = Stamp.LocalFrame.Origin;

		double UsePower = Stamp.Power;
		FVector3d MoveVec = Stamp.LocalFrame.Origin - Stamp.PrevLocalFrame.Origin;

		ParallelFor(Vertices.Num(), [&](int32 k)
		{
			int32 VertIdx = Vertices[k];
			FVector3d OrigPos = Mesh->GetVertex(VertIdx);

			double Falloff = GetFalloff().Evaluate(Stamp, OrigPos);

			FVector3d NewPos = OrigPos + Falloff * MoveVec;
			NewPositionsOut[k] = NewPos;
		});
	}


	virtual ESculptBrushOpTargetType GetBrushTargetType() const override
	{
		return ESculptBrushOpTargetType::ActivePlane;
	}

	virtual bool IgnoreZeroMovements() const override
	{
		return true;
	}
};
