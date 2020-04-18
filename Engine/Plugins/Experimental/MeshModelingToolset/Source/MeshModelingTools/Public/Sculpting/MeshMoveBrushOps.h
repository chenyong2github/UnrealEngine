// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Sculpting/MeshBrushOpBase.h"
#include "DynamicMesh3.h"
#include "MeshNormals.h"
#include "MeshWeights.h"
#include "Async/ParallelFor.h"


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





//void UMeshVertexSculptTool::ApplyMoveBrush(const FRay& WorldRay)
//{
//	FVector3d NewBrushPosLocal = CurTargetTransform.InverseTransformPosition(LastBrushPosWorld);
//	FVector3d MoveVec = NewBrushPosLocal - LastBrushPosLocal;
//
//	if (MoveVec.SquaredLength() <= 0)
//	{
//		LastBrushPosLocal = NewBrushPosLocal;
//		return;
//	}
//
//	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();
//	int NumV = VertexROI.Num();
//	ROIPositionBuffer.SetNum(NumV, false);
//
//	ParallelFor(NumV, [this, Mesh, NewBrushPosLocal, MoveVec](int k)
//	{
//		int VertIdx = VertexROI[k];
//		FVector3d OrigPos = Mesh->GetVertex(VertIdx);
//
//		double PrevDist = (OrigPos - LastBrushPosLocal).Length();
//		double NewDist = (OrigPos - NewBrushPosLocal).Length();
//		double UseDist = FMath::Min(PrevDist, NewDist);
//
//		double Falloff = CalculateBrushFalloff(UseDist) * ActivePressure;
//
//		FVector3d NewPos = OrigPos + Falloff * MoveVec;
//		ROIPositionBuffer[k] = NewPos;
//	});
//
//	// Update the mesh positions to match those in the position buffer
//	SyncMeshWithPositionBuffer(Mesh);
//
//	LastBrushPosLocal = NewBrushPosLocal;
//}