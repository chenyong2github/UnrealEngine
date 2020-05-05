// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Sculpting/MeshBrushOpBase.h"
#include "DynamicMesh3.h"
#include "MeshNormals.h"
#include "MeshWeights.h"
#include "Async/ParallelFor.h"
#include "MeshSculptBrushOps.generated.h"


UCLASS()
class MESHMODELINGTOOLS_API UStandardSculptBrushOpProps : public UMeshSculptBrushOpProps
{
	GENERATED_BODY()
public:
	/** Strength of the Brush */
	UPROPERTY(EditAnywhere, Category = SculptBrush, meta = (DisplayName = "Strength", UIMin = "0.0", UIMax = "1.0", ClampMin = "0.0", ClampMax = "1.0"))
	float Strength = 0.5;

	/** Amount of falloff to apply */
	UPROPERTY(EditAnywhere, Category = SculptBrush, meta = (DisplayName = "Falloff", UIMin = "0.0", UIMax = "1.0", ClampMin = "0.0", ClampMax = "1.0"))
	float Falloff = 1.0;

	virtual float GetStrength() override { return Strength; }
	virtual float GetFalloff() override { return Falloff; }
};

class FSurfaceSculptBrushOp : public FMeshSculptBrushOp
{
public:
	double BrushSpeedTuning = 6.0;

	typedef TUniqueFunction<bool(int32, const FVector3d&, double, FVector3d&, FVector3d&)> NearestQueryFuncType;

	NearestQueryFuncType BaseMeshNearestQueryFunc;

	FSurfaceSculptBrushOp(NearestQueryFuncType QueryFunc)
	{
		BaseMeshNearestQueryFunc = MoveTemp(QueryFunc);
	}

	virtual ESculptBrushOpTargetType GetBrushTargetType() const override
	{
		return ESculptBrushOpTargetType::TargetMesh;
	}

	virtual void ApplyStamp(const FDynamicMesh3* Mesh, const FSculptBrushStamp& Stamp, const TArray<int32>& Vertices, TArray<FVector3d>& NewPositionsOut) override
	{
		double UsePower = Stamp.Direction * Stamp.Power * Stamp.Radius * Stamp.DeltaTime * BrushSpeedTuning;
		double MaxOffset = Stamp.Radius;

		ParallelFor(Vertices.Num(), [&](int32 k)
		{
			int32 VertIdx = Vertices[k];
			FVector3d OrigPos = Mesh->GetVertex(VertIdx);

			FVector3d BasePos, BaseNormal;
			bool bFoundBasePos = BaseMeshNearestQueryFunc(VertIdx, OrigPos, 4.0 * Stamp.Radius, BasePos, BaseNormal);
			if (bFoundBasePos == false)
			{
				NewPositionsOut[k] = OrigPos;
			}
			else
			{
				FVector3d MoveVec = UsePower * BaseNormal;
				double Falloff = GetFalloff().Evaluate(Stamp, OrigPos);
				FVector3d NewPos = OrigPos + Falloff * MoveVec;
				NewPositionsOut[k] = NewPos;
			}
		});
	}

};





UCLASS()
class MESHMODELINGTOOLS_API UViewAlignedSculptBrushOpProps : public UMeshSculptBrushOpProps
{
	GENERATED_BODY()
public:
	/** Strength of the Brush */
	UPROPERTY(EditAnywhere, Category = SculptToViewBrush, meta = (DisplayName = "Strength", UIMin = "0.0", UIMax = "1.0", ClampMin = "0.0", ClampMax = "1.0"))
	float Strength = 0.5;

	/** Amount of falloff to apply */
	UPROPERTY(EditAnywhere, Category = SculptToViewBrush, meta = (DisplayName = "Falloff", UIMin = "0.0", UIMax = "1.0", ClampMin = "0.0", ClampMax = "1.0"))
	float Falloff = 1.0;

	virtual float GetStrength() override { return Strength; }
	virtual float GetFalloff() override { return Falloff; }
};


class FViewAlignedSculptBrushOp : public FMeshSculptBrushOp
{
public:
	double BrushSpeedTuning = 6.0;

	typedef TUniqueFunction<bool(int32, const FVector3d&, double, FVector3d&, FVector3d&)> NearestQueryFuncType;

	NearestQueryFuncType BaseMeshNearestQueryFunc;

	FViewAlignedSculptBrushOp(NearestQueryFuncType QueryFunc)
	{
		BaseMeshNearestQueryFunc = MoveTemp(QueryFunc);
	}

	virtual ESculptBrushOpTargetType GetBrushTargetType() const override
	{
		return ESculptBrushOpTargetType::TargetMesh;
	}

	virtual bool GetAlignStampToView() const
	{
		return true;
	}

	virtual void ApplyStamp(const FDynamicMesh3* Mesh, const FSculptBrushStamp& Stamp, const TArray<int32>& Vertices, TArray<FVector3d>& NewPositionsOut) override
	{
		FVector3d StampNormal = Stamp.LocalFrame.Z();

		double UsePower = Stamp.Direction * Stamp.Power * Stamp.Radius * Stamp.DeltaTime * BrushSpeedTuning;
		double MaxOffset = Stamp.Radius;

		ParallelFor(Vertices.Num(), [&](int32 k)
		{
			int32 VertIdx = Vertices[k];
			FVector3d OrigPos = Mesh->GetVertex(VertIdx);

			FVector3d BasePos, BaseNormal;
			bool bFoundBasePos = BaseMeshNearestQueryFunc(VertIdx, OrigPos, 4.0 * Stamp.Radius, BasePos, BaseNormal);
			if (bFoundBasePos == false)
			{
				NewPositionsOut[k] = OrigPos;
			}
			else
			{
				FVector3d MoveVec = UsePower * StampNormal;
				double Falloff = GetFalloff().Evaluate(Stamp, OrigPos);
				FVector3d NewPos = OrigPos + Falloff * MoveVec;
				NewPositionsOut[k] = NewPos;
			}
		});
	}

};





UCLASS()
class MESHMODELINGTOOLS_API USculptMaxBrushOpProps : public UMeshSculptBrushOpProps
{
	GENERATED_BODY()
public:
	/** Strength of the Brush */
	UPROPERTY(EditAnywhere, Category = SculptMaxBrush, meta = (DisplayName = "Strength", UIMin = "0.0", UIMax = "1.0", ClampMin = "0.0", ClampMax = "1.0"))
	float Strength = 0.5;

	/** Amount of falloff to apply */
	UPROPERTY(EditAnywhere, Category = SculptMaxBrush, meta = (DisplayName = "Falloff", UIMin = "0.0", UIMax = "1.0", ClampMin = "0.0", ClampMax = "1.0"))
	float Falloff = 0.5;

	/** Maximum height as fraction of brush size */
	UPROPERTY(EditAnywhere, Category = SculptMaxBrush, meta = (UIMin = "0.0", UIMax = "1.0", ClampMin = "0.0", ClampMax = "1.0"))
	float MaxHeight = 0.5;

	/** If true, maximum height is defined using the FixedHeight constant instead of brush-relative size */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = SculptMaxBrush)
	bool bUseFixedHeight = false;

	/** Maximum height in world-space dimension */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = SculptMaxBrush)
	float FixedHeight = 0.0;


	virtual float GetStrength() override { return Strength; }
	virtual float GetFalloff() override { return Falloff; }
};


class FSurfaceMaxSculptBrushOp : public FMeshSculptBrushOp
{
public:
	double BrushSpeedTuning = 6.0;

	typedef TUniqueFunction<bool(int32, const FVector3d&, double, FVector3d&, FVector3d&)> NearestQueryFuncType;

	NearestQueryFuncType BaseMeshNearestQueryFunc;

	FSurfaceMaxSculptBrushOp(NearestQueryFuncType QueryFunc)
	{
		BaseMeshNearestQueryFunc = MoveTemp(QueryFunc);
	}

	virtual ESculptBrushOpTargetType GetBrushTargetType() const override
	{
		return ESculptBrushOpTargetType::TargetMesh;
	}

	virtual void ApplyStamp(const FDynamicMesh3* Mesh, const FSculptBrushStamp& Stamp, const TArray<int32>& Vertices, TArray<FVector3d>& NewPositionsOut) override
	{
		double UsePower = Stamp.Direction * Stamp.Power * Stamp.Radius * Stamp.DeltaTime * BrushSpeedTuning;

		USculptMaxBrushOpProps* Props = GetPropertySetAs<USculptMaxBrushOpProps>();
		double MaxOffset = (Props->bUseFixedHeight) ? Props->FixedHeight : (Props->MaxHeight * Stamp.Radius);

		ParallelFor(Vertices.Num(), [&](int32 k)
		{
			int32 VertIdx = Vertices[k];
			FVector3d OrigPos = Mesh->GetVertex(VertIdx);

			FVector3d BasePos, BaseNormal;
			bool bFoundBasePos = BaseMeshNearestQueryFunc(VertIdx, OrigPos, 4.0 * Stamp.Radius, BasePos, BaseNormal);
			if (bFoundBasePos == false)
			{
				NewPositionsOut[k] = OrigPos;
			}
			else
			{
				FVector3d MoveVec = UsePower * BaseNormal;
				double Falloff = GetFalloff().Evaluate(Stamp, OrigPos);
				FVector3d NewPos = OrigPos + Falloff * MoveVec;

				FVector3d DeltaPos = NewPos - BasePos;
				if (DeltaPos.SquaredLength() > MaxOffset * MaxOffset)
				{
					DeltaPos.Normalize();
					NewPos = BasePos + MaxOffset * DeltaPos;
				}

				NewPositionsOut[k] = NewPos;
			}
		});
	}

};
