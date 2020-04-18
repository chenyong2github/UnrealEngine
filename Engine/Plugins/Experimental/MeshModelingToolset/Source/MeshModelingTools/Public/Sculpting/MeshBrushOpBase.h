// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VectorTypes.h"
#include "FrameTypes.h"

class FDynamicMesh3;

enum class ESculptBrushOpTargetType : uint8
{
	SculptMesh,
	TargetMesh,
	ActivePlane
};


struct FSculptBrushStamp
{
	FFrame3d WorldFrame;
	FFrame3d LocalFrame;
	double Radius;
	double Power;
	double Direction;
	double Depth;

	FFrame3d PrevWorldFrame;
	FFrame3d PrevLocalFrame;

	// only initialized if current op requires it
	FFrame3d RegionPlane;
};


struct FSculptBrushOptions
{
	bool bPreserveUVFlow = false;

	double MaxHeight = 0.5f;

	FFrame3d ConstantReferencePlane;

	int32 WhichPlaneSideIndex = 0;		// 	BothSides = 0,	PushDown = 1, PullTowards = 2
};


class FMeshSculptFallofFunc
{
public:
	TUniqueFunction<double(const FSculptBrushStamp& StampInfo, const FVector3d& Position)> FalloffFunc;

	inline double Evaluate(const FSculptBrushStamp& StampInfo, const FVector3d& Position) const
	{
		return FalloffFunc(StampInfo, Position);
	}
};


class FMeshSculptBrushOp
{
public:
	virtual ~FMeshSculptBrushOp() {}

	TSharedPtr<FMeshSculptFallofFunc> Falloff;
	FSculptBrushOptions CurrentOptions;

	const FMeshSculptFallofFunc& GetFalloff() const { return *Falloff; }

	virtual void ConfigureOptions(const FSculptBrushOptions& Options)
	{
		CurrentOptions = Options;
	}

	virtual void BeginStroke(const FDynamicMesh3* Mesh, const FSculptBrushStamp& Stamp, const TArray<int32>& InitialVertices) {}
	virtual void EndStroke(const FDynamicMesh3* Mesh, const FSculptBrushStamp& Stamp, const TArray<int32>& FinalVertices) {}
	virtual void ApplyStamp(const FDynamicMesh3* Mesh, const FSculptBrushStamp& Stamp, const TArray<int32>& Vertices, TArray<FVector3d>& NewPositionsOut) = 0;



	//
	// overrideable Brush Op configuration things
	//

	virtual ESculptBrushOpTargetType GetBrushTargetType() const
	{
		return ESculptBrushOpTargetType::SculptMesh;
	}

	virtual bool GetAlignStampToView() const
	{
		return false;
	}

	virtual bool IgnoreZeroMovements() const
	{
		return false;
	}


	virtual bool WantsStampRegionPlane() const
	{
		return false;
	}
};




