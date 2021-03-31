// Copyright Epic Games, Inc. All Rights Reserved.

#include "NormalFlowRemesher.h"
#include "Async/ParallelFor.h"
#include "InfoTypes.h"

void FNormalFlowRemesher::RemeshWithFaceProjection()
{
	if (Mesh->TriangleCount() == 0)
	{
		return;
	}

	ModifiedEdgesLastPass = 0;

	ResetQueue();

	// First we do fast splits to hit edge length target

	for (int k = 0; k < MaxFastSplitIterations; ++k)
	{
		if (Cancelled())
		{
			return;
		}

		int nSplits = FastSplitIteration();

		if ((double)nSplits / (double)Mesh->EdgeCount() < 0.01)
		{
			// Call it converged
			break;
		}
	}
	ResetQueue();

	// Now do queued remesh iterations. As we proceed we slowly step
	// down the smoothing factor, this helps us get triangles closer
	// to where they will ultimately want to go

	const double OriginalSmoothSpeed = SmoothSpeedT;
	
	int Iterations = 0;
	const double ProjectionDistanceThreshold = 0.1 * MinEdgeLength;

	bool bContinue = true;
	while (bContinue)
	{
		if (Cancelled())
		{
			break;
		}

		RemeshIteration();

		if (Iterations > MaxRemeshIterations / 2)
		{
			SmoothSpeedT *= 0.9;
		}

		double MaxProjectionDistance = 0.0;
		TrackedFaceProjectionPass(MaxProjectionDistance);

		// Stop if we've hit max iterations, or both:
		// - queue is empty and
		// - projection isn't moving anything
		bContinue = (Iterations++ < MaxRemeshIterations) && ((ModifiedEdges->Num() > 0) || (MaxProjectionDistance > ProjectionDistanceThreshold));
	}

	SmoothSpeedT = OriginalSmoothSpeed;

	// Now just face projections and edge flips
	if (ProjTarget != nullptr)
	{
		for (int k = 0; k < NumExtraProjectionIterations; ++k)
		{
			if (Cancelled())
			{
				break;
			}

			double MaxProjectionDistance = 0.0;
			TrackedFaceProjectionPass(MaxProjectionDistance);

			if (MaxProjectionDistance == 0.0)
			{
				break;
			}

			// See if we can flip edges to improve normal fit
			TrackedEdgeFlipPass();
		}
	}

}


void FNormalFlowRemesher::TrackedFaceProjectionPass(double& MaxDistanceMoved)
{
	ensure(ProjTarget != nullptr);

	IOrientedProjectionTarget* NormalProjTarget = static_cast<IOrientedProjectionTarget*>(ProjTarget);
	ensure(NormalProjTarget != nullptr);

	InitializeVertexBufferForFacePass();

	// this function computes rotated position of triangle, such that it
	// aligns with face normal on target surface. We accumulate weighted-average
	// of vertex positions, which we will then use further down where possible.

	for (int TriangleIndex : Mesh->TriangleIndicesItr())
	{
		FVector3d TriangleNormal, Centroid;
		double Area;
		Mesh->GetTriInfo(TriangleIndex, TriangleNormal, Area, Centroid);

		FVector3d ProjectedNormal{ 1e30, 1e30, 1e30 };
		FVector3d ProjectedPosition = NormalProjTarget->Project(Centroid, ProjectedNormal);

		check(ProjectedNormal[0] != 1e30);
		check(ProjectedNormal.Length() > 1e-6);

		FVector3d V0, V1, V2;
		Mesh->GetTriVertices(TriangleIndex, V0, V1, V2);

		FFrame3d TriF(Centroid, TriangleNormal);
		V0 = TriF.ToFramePoint(V0);
		V1 = TriF.ToFramePoint(V1);
		V2 = TriF.ToFramePoint(V2);

		TriF.AlignAxis(2, ProjectedNormal);
		TriF.Origin = ProjectedPosition;
		V0 = TriF.FromFramePoint(V0);
		V1 = TriF.FromFramePoint(V1);
		V2 = TriF.FromFramePoint(V2);

		double Dot = TriangleNormal.Dot(ProjectedNormal);
		Dot = FMath::Clamp(Dot, 0.0, 1.0);
		double Weight = Area * (Dot * Dot * Dot);

		FIndex3i TriangleVertices = Mesh->GetTriangle(TriangleIndex);
		TempPosBuffer[TriangleVertices.A] += Weight * V0;
		TempWeightBuffer[TriangleVertices.A] += Weight;
		TempPosBuffer[TriangleVertices.B] += Weight * V1;
		TempWeightBuffer[TriangleVertices.B] += Weight;
		TempPosBuffer[TriangleVertices.C] += Weight * V2;
		TempWeightBuffer[TriangleVertices.C] += Weight;
	}

	// ok now we filter out all the positions we can't change, as well as vertices that
	// did not actually move. We also queue any edges that moved far enough to fall
	// under min/max edge length thresholds

	MaxDistanceMoved = 0.0;

	for (int VertexID : Mesh->VertexIndicesItr())
	{
		TempFlagBuffer[VertexID] = false;

		if (FMath::IsNearlyZero(TempWeightBuffer[VertexID]))
		{
			continue;
		}

		if (IsVertexPositionConstrained(VertexID))
		{
			continue;
		}

		if (VertexControlF != nullptr && ((int)VertexControlF(VertexID) & (int)EVertexControl::NoProject) != 0)
		{
			continue;
		}

		FVector3d CurrentPosition = Mesh->GetVertex(VertexID);
		FVector3d ProjectedPosition = TempPosBuffer[VertexID] / TempWeightBuffer[VertexID];

		if (VectorUtil::EpsilonEqual(CurrentPosition, ProjectedPosition, FMathd::ZeroTolerance))
		{
			continue;
		}

		MaxDistanceMoved = FMath::Max(MaxDistanceMoved, CurrentPosition.Distance(ProjectedPosition));

		TempFlagBuffer[VertexID] = true;
		TempPosBuffer[VertexID] = ProjectedPosition;

		for (int EdgeID : Mesh->VtxEdgesItr(VertexID))
		{
			FIndex2i EdgeVertices = Mesh->GetEdgeV(EdgeID);
			int OtherVertexID = (EdgeVertices.A == VertexID) ? EdgeVertices.B : EdgeVertices.A;
			FVector3d OtherVertexPosition = Mesh->GetVertex(OtherVertexID);

			double NewEdgeLength = ProjectedPosition.Distance(OtherVertexPosition);
			if (NewEdgeLength < MinEdgeLength || NewEdgeLength > MaxEdgeLength)
			{
				QueueEdge(EdgeID);
			}
		}
	}

	// update vertices
	ApplyVertexBuffer(true);
}


namespace
{

	double ComputeNormalError(const FDynamicMesh3* Mesh, IOrientedProjectionTarget* NormalProjTarget, FVector3d TriangleNormal, FVector3d Centroid)
	{
		FVector3d ProjectedNormal{ 1e30, 1e30, 1e30 };
		FVector3d ProjectedPosition = NormalProjTarget->Project(Centroid, ProjectedNormal);

		double Err = 0.5 * (1.0 - TriangleNormal.Dot(ProjectedNormal));
		check(Err > -SMALL_NUMBER);
		check(Err < 1.0 + SMALL_NUMBER);

		return Err;
	}

	double ComputeNormalError(const FDynamicMesh3* Mesh, IOrientedProjectionTarget* NormalProjTarget, FIndex3i Triangle)
	{
		FVector3d v0 = Mesh->GetVertex(Triangle[0]);
		FVector3d v1 = Mesh->GetVertex(Triangle[1]);
		FVector3d v2 = Mesh->GetVertex(Triangle[2]);

		FVector3d Centroid = (v0 + v1 + v2) * (1.0 / 3.0);
		FVector3d Normal = VectorUtil::Normal(v0, v1, v2);

		return ComputeNormalError(Mesh, NormalProjTarget, Normal, Centroid);
	}
}


bool FNormalFlowRemesher::EdgeFlipWouldReduceNormalError(int EdgeID, double BadEdgeErrorThreshold, double ImprovementRatioThreshold) const
{
	IOrientedProjectionTarget* NormalProjTarget = static_cast<IOrientedProjectionTarget*>(ProjTarget);
	if (NormalProjTarget == nullptr)
	{
		return false;
	}

	FDynamicMesh3::FEdge Edge = Mesh->GetEdge(EdgeID);
	if (Edge.Tri[1] == FDynamicMesh3::InvalidID)
	{
		return false;
	}

	double CurrErr = 0.0;
	CurrErr += ComputeNormalError(Mesh, NormalProjTarget, Mesh->GetTriangle(Edge.Tri[0]));
	CurrErr += ComputeNormalError(Mesh, NormalProjTarget, Mesh->GetTriangle(Edge.Tri[1]));

	if (CurrErr > BadEdgeErrorThreshold)	// only consider edges having a certain error already
	{
		FIndex3i TriangleC = Mesh->GetTriangle(Edge.Tri[0]);
		FIndex3i TriangleD = Mesh->GetTriangle(Edge.Tri[1]);
		int VertexInTriangleC = IndexUtil::OrientTriEdgeAndFindOtherVtx(Edge.Vert[0], Edge.Vert[1], TriangleC);
		int VertexInTriangleD = IndexUtil::FindTriOtherVtx(Edge.Vert[0], Edge.Vert[1], TriangleD);

		int OtherEdge = Mesh->FindEdge(VertexInTriangleC, VertexInTriangleD);
		if (OtherEdge != FDynamicMesh3::InvalidID)
		{
			return false;
		}

		double OtherErr = 0.0;
		OtherErr += ComputeNormalError(Mesh, NormalProjTarget, FIndex3i{ VertexInTriangleC, VertexInTriangleD, Edge.Vert[1] });
		OtherErr += ComputeNormalError(Mesh, NormalProjTarget, FIndex3i{ VertexInTriangleD, VertexInTriangleC, Edge.Vert[0] });

		return (OtherErr < ImprovementRatioThreshold * CurrErr);	// return true if we improve error by enough
	}

	return false;
}


void FNormalFlowRemesher::TrackedEdgeFlipPass()
{
	check(ModifiedEdges);

	IOrientedProjectionTarget* NormalProjTarget = static_cast<IOrientedProjectionTarget*>(ProjTarget);
	check(NormalProjTarget != nullptr);

	for (auto EdgeID : Mesh->EdgeIndicesItr())
	{
		check(Mesh->IsEdge(EdgeID));

		FEdgeConstraint Constraint =
			(!Constraints) ? FEdgeConstraint::Unconstrained() : Constraints->GetEdgeConstraint(EdgeID);

		if (!Constraint.CanFlip())
		{
			continue;
		}

		if (EdgeFlipWouldReduceNormalError(EdgeID))
		{
			DynamicMeshInfo::FEdgeFlipInfo FlipInfo;
			auto Result = Mesh->FlipEdge(EdgeID, FlipInfo);

			if (Result == EMeshResult::Ok)
			{
				FIndex2i EdgeVertices = Mesh->GetEdgeV(EdgeID);
				FIndex2i OpposingEdgeVertices = Mesh->GetEdgeOpposingV(EdgeID);

				QueueOneRing(EdgeVertices.A);
				QueueOneRing(EdgeVertices.B);
				QueueOneRing(OpposingEdgeVertices.A);
				QueueOneRing(OpposingEdgeVertices.B);
				OnEdgeFlip(EdgeID, FlipInfo);
			}
		}
	}

}
