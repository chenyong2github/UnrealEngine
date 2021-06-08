// Copyright Epic Games, Inc. All Rights Reserved.

#include "CADKernel/Topo/TopologicalLoop.h"

#include "CADKernel/Core/KernelParameters.h"
#include "CADKernel/Geo/Curves/RestrictionCurve.h"
#include "CADKernel/Math/SlopeUtils.h"
#include "CADKernel/Topo/TopologicalEdge.h"
#include "CADKernel/Topo/TopologicalLink.h"
#include "CADKernel/Topo/TopologicalFace.h"
#include "CADKernel/UI/Display.h"
#include "CADKernel/UI/Message.h"
#include "CADKernel/Utils/Util.h"


using namespace CADKernel;

TSharedPtr<FTopologicalLoop> FTopologicalLoop::Make(const TArray<TSharedPtr<FTopologicalEdge>>& InEdges, const TArray<EOrientation>& InEdgeDirections, double GeometricTolerance)
{
	TSharedRef<FTopologicalLoop> Loop = FEntity::MakeShared<FTopologicalLoop>(InEdges, InEdgeDirections);
	Loop->EnsureLogicalClosing(GeometricTolerance);
	for (FOrientedEdge& OrientedEdge : Loop->GetEdges())
	{
		OrientedEdge.Entity->SetLoop(Loop);
	}
	return Loop;
}

FTopologicalLoop::FTopologicalLoop(const TArray<TSharedPtr<FTopologicalEdge>>& InEdges, const TArray<EOrientation>& InEdgeDirections)
	: FTopologicalEntity()
	, Face(TSharedPtr<FTopologicalFace>())
	, bExternalLoop(true)
{
	Edges.Reserve(InEdges.Num());
	for(int32 Index = 0; Index < InEdges.Num(); ++Index)
	{
		TSharedPtr<FTopologicalEdge> Edge = InEdges[Index];
		EOrientation Orientation = InEdgeDirections[Index];
		Edges.Emplace(Edge, Orientation);
	}
}

void FTopologicalLoop::RemoveEdge(TSharedPtr<FTopologicalEdge>& EdgeToRemove)
{
	for (int32 IEdge = 0; IEdge < Edges.Num(); IEdge++)
	{
		if (Edges[IEdge].Entity == EdgeToRemove)
		{
			EdgeToRemove->RemoveLoop();
			Edges.RemoveAt(IEdge);
			return;
		}
	}
	ensureCADKernel(false);
}

EOrientation FTopologicalLoop::GetDirection(TSharedPtr<FTopologicalEdge>& InEdge, bool bAllowLinkedEdge) const
{
	ensureCADKernel(InEdge.IsValid());

	for (const FOrientedEdge& BoundaryEdge : Edges)
	{
		if (BoundaryEdge.Entity == InEdge)
		{
			return BoundaryEdge.Direction;
		}
		else if (bAllowLinkedEdge && BoundaryEdge.Entity->IsLinkedTo(InEdge.ToSharedRef()))
		{
			return BoundaryEdge.Direction;
		}
	}

	FMessage::Printf(Debug, TEXT("Edge %d is not in boundary %d Edges\n"), InEdge->GetId(), GetId());
	ensureCADKernel(false);
	return EOrientation::Front;
}


void FTopologicalLoop::Get2DSampling(TArray<FPoint2D>& LoopSampling)
{
	int32 PointNum = 0;
	for (const FOrientedEdge& Edge : Edges)
	{
		PointNum += Edge.Entity->GetCurve()->GetPolylineSize();
	}

	LoopSampling.Empty(PointNum);

	for (const FOrientedEdge& Edge : Edges)
	{
		Edge.Entity->GetDiscretization2DPoints(Edge.Direction, LoopSampling);
		LoopSampling.Pop();
	}
	LoopSampling.Emplace(LoopSampling[0]);
}

void FTopologicalLoop::Orient()
{
	ensureCADKernel(Edges.Num() > 0);

	TArray<FPoint2D> LoopSampling;
	Get2DSampling(LoopSampling);

	double UMin = HUGE_VAL;
	double UMax = -HUGE_VAL;
	double VMin = HUGE_VAL;
	double VMax = -HUGE_VAL;
	int32 IndexUMin = 0;
	int32 IndexUMax = 0;
	int32 IndexVMin = 0;
	int32 IndexVMax = 0;

	int32 PointCount = LoopSampling.Num();

	for (int32 Index = 0; Index < PointCount; ++Index)
	{
		if (LoopSampling[Index].U > UMax)
		{
			UMax = LoopSampling[Index].U;
			IndexUMax = Index;
		}
		if (LoopSampling[Index].U < UMin)
		{
			UMin = LoopSampling[Index].U;
			IndexUMin = Index;
		}

		if (LoopSampling[Index].V > VMax)
		{
			VMax = LoopSampling[Index].V;
			IndexVMax = Index;
		}
		if (LoopSampling[Index].V < VMin)
		{
			VMin = LoopSampling[Index].V;
			IndexVMin = Index;
		}
	}

	int32 WrongOrientationNum = 0;
	TFunction<void(int32&, double)> CompareOrientation = [&](int32& Index, double ReferenceSlop)
	{
		// if the slop of the selected segment is not close to the BBox side (closed of 0 of 4), so the angle between the neighboring segments of the local extrema is not closed to 4 and allows to defined the orientation
		// Pic case: the slop is compute between previous and nex segment of the extrema 
		int32 Index2 = Index == 0 ? PointCount - 1 : Index - 1;
		double Slop = ComputePositiveSlope(LoopSampling[Index], LoopSampling[Index + 1], LoopSampling[Index2]);
		if (Slop > 4.2)
		{
			WrongOrientationNum++;
		}
		else if (Slop > 3.8)
		{
			// the angle between the neighboring segments of the local extrema is too closed to 4 (PI) so the slop of the segment Index, Index+1 is compared to the AABB side to define the orientation
			Slop = ComputeUnorientedSlope(LoopSampling[Index], LoopSampling[Index + 1], ReferenceSlop);
			// slop should be closed to [0, 0.2] or [3.8, 4]
			if (Slop > 3)
			{
				WrongOrientationNum++;
			}
		}

#ifdef DEBUG_ORIENT
		if (false /*Surface->GetId() == 826*/)
		{
			F3DDebugSession _(*FString::Printf(TEXT("Pic case")));
			{
				F3DDebugSession _(*FString::Printf(TEXT("Loop")));
				DisplayPolyline(LoopSampling);
			}
			{
				F3DDebugSession _(*FString::Printf(TEXT("Next")));
				::DisplaySegment(LoopSampling[Index + 1], LoopSampling[Index], EVisuProperty::NonManifoldEdge);
			}
			{
				F3DDebugSession _(*FString::Printf(TEXT("Previous")));
				::DisplaySegment(LoopSampling[Index2], LoopSampling[Index], EVisuProperty::NonManifoldEdge);
			}
			{
				F3DDebugSession _(*FString::Printf(TEXT("Node %f"), Slop));
				::Display(LoopSampling[Index], EVisuProperty::RedPoint, Index);
			}
			Wait();
		}
#endif
	};

	CompareOrientation(IndexUMin, 6);
	CompareOrientation(IndexUMax, 2);
	CompareOrientation(IndexVMin, 0);
	CompareOrientation(IndexVMax, 4);

#ifdef DEBUG_ORIENT
	if (WrongOrientationNum != 0 && WrongOrientationNum != 4)
	{
		F3DDebugSession GraphicSession(TEXT("Points of evaluation"));
		{
			F3DDebugSession G(*FString::Printf(TEXT("Loop Discretization %d"), Face.Pin()->GetId()));
			DisplayPolyline(LoopSampling);
			for (int32 Index = 0; Index < LoopSampling.Num(); ++Index)
			{
				::Display(LoopSampling[Index], Index);
			}
		}
		{
			F3DDebugSession G(*FString::Printf(TEXT("Seg UMin Loop Discretization %d"), Face.Pin()->GetId()));
			::DisplaySegment(LoopSampling[IndexUMin + 1], LoopSampling[IndexUMin]);
			::Display(LoopSampling[IndexUMin], EVisuProperty::RedPoint, IndexUMin);
		}
		{
			F3DDebugSession G(*FString::Printf(TEXT("Seg UMax Loop Discretization %d"), Face.Pin()->GetId()));
			::DisplaySegment(LoopSampling[IndexUMax + 1], LoopSampling[IndexUMax]);
			::Display(LoopSampling[IndexUMax], EVisuProperty::RedPoint, IndexUMax);
		}
		{
			F3DDebugSession G(*FString::Printf(TEXT("Seg VMin Loop Discretization %d"), Face.Pin()->GetId()));
			::DisplaySegment(LoopSampling[IndexVMin + 1], LoopSampling[IndexVMin]);
			::Display(LoopSampling[IndexVMin], EVisuProperty::RedPoint, IndexVMin);
		}
		{
			F3DDebugSession G(*FString::Printf(TEXT("Seg VMax Loop Discretization %d"), Face.Pin()->GetId()));
			::DisplaySegment(LoopSampling[IndexVMax + 1], LoopSampling[IndexVMax]);
			::Display(LoopSampling[IndexVMax], EVisuProperty::RedPoint, IndexVMax);
		}
		//Wait();
		FMessage::Printf(Log, TEXT("WARNING: Loop Orientation of surface %d could failed\n"), Face.Pin()->GetId());
	}
#endif

	if ((WrongOrientationNum > 2) == bExternalLoop)
	{
		SwapOrientation();
	}
}

void FTopologicalLoop::SwapOrientation()
{
	TArray<FOrientedEdge> TmpEdges;
	TmpEdges.Reserve(Edges.Num());
	for (int32 Index = Edges.Num() - 1; Index >= 0; Index--)
	{
		TmpEdges.Emplace(Edges[Index].Entity, GetReverseOrientation(Edges[Index].Direction));
	}
	Swap(TmpEdges, Edges);
}

void FTopologicalLoop::ReplaceEdge(TSharedPtr<FTopologicalEdge>& OldEdge, TSharedPtr<FTopologicalEdge>& NewEdge)
{
	for (int32 IEdge = 0; IEdge < (int32)Edges.Num(); IEdge++)
	{
		if (Edges[IEdge].Entity == OldEdge)
		{
			Edges[IEdge].Entity = NewEdge;
			OldEdge->RemoveLoop();
			NewEdge->SetLoop(StaticCastSharedRef<FTopologicalLoop>(AsShared()));
			return;
		}
	}
	ensureCADKernel(false);
}

void FTopologicalLoop::SplitEdge(TSharedPtr<FTopologicalEdge> SplitEdge, TSharedPtr<FTopologicalEdge> NewEdge, bool bSplitEdgeIsFirst)
{
	TSharedRef<FTopologicalLoop> Loop = StaticCastSharedRef<FTopologicalLoop>(AsShared());
	NewEdge->SetLoop(Loop);

	for (int32 IEdge = 0; IEdge < Edges.Num(); IEdge++)
	{
		if (Edges[IEdge].Entity == SplitEdge)
		{
			EOrientation OldEdgeDirection = Edges[IEdge].Direction;
			if ((OldEdgeDirection == EOrientation::Front) == bSplitEdgeIsFirst)
			{
				IEdge++;
			}
			Edges.EmplaceAt(IEdge, NewEdge, OldEdgeDirection);
			return;
		}
	}
	ensureCADKernel(false);
}

void FTopologicalLoop::ReplaceEdge(TSharedPtr<FTopologicalEdge>& Edge, TArray<TSharedPtr<FTopologicalEdge>>& NewEdges)
{
	TArray<FOrientedEdge> TmpEdges;
	int32 NewEdgeNum = Edges.Num() + NewEdges.Num();
	TmpEdges.Reserve(NewEdgeNum);

	Edge->RemoveLoop();
	TSharedRef<FTopologicalLoop> Loop = StaticCastSharedRef<FTopologicalLoop>(AsShared());
	for (TSharedPtr<FTopologicalEdge>& NewEdge : NewEdges)
	{
		NewEdge->SetLoop(Loop);
	}

	for (int32 IEdge = 0; IEdge < Edges.Num(); IEdge++)
	{
		if (Edges[IEdge].Entity == Edge)
		{
			EOrientation OldEdgeDirection = Edges[IEdge].Direction;
			if (OldEdgeDirection == EOrientation::Front)
			{
				Edges[IEdge].Entity = NewEdges[0];
				for (int32 INewEdge = 1; INewEdge < NewEdges.Num(); INewEdge++)
				{
					IEdge++;
					Edges.EmplaceAt(IEdge, NewEdges[INewEdge], EOrientation::Front);
				}
			}
			else
			{
				Edges[IEdge].Entity = NewEdges[0];
				for (int32 INewEdge = 1; INewEdge < NewEdges.Num(); INewEdge++)
				{
					Edges.EmplaceAt(IEdge, NewEdges[INewEdge], EOrientation::Back);
				}
			}
			return;
		}
	}
	ensureCADKernel(false);
}

void FTopologicalLoop::ReplaceEdges(TArray<FOrientedEdge>& OldEdges, TSharedPtr<FTopologicalEdge>& NewEdge)
{
	for (FOrientedEdge& Edge : OldEdges)
	{
		Edge.Entity->RemoveLoop();
	}

	TSharedRef<FTopologicalLoop> Loop = StaticCastSharedRef<FTopologicalLoop>(AsShared());
	NewEdge->SetLoop(Loop);

	for (int32 IEdge = 0; IEdge < Edges.Num(); IEdge++)
	{
		if (Edges[IEdge] == OldEdges[0])
		{
			Edges[IEdge].Direction = EOrientation::Front;
			Edges[IEdge].Entity = NewEdge;
			IEdge++;

			int32 EdgeCount = Edges.Num();
			int32 EdgeToRemoveCount = OldEdges.Num() - 1;
			if (IEdge + EdgeToRemoveCount < EdgeCount)
			{
				for (int32 Index = 0; Index < EdgeToRemoveCount; Index++)
				{
					Edges.RemoveAt(IEdge);
				}
			}
			else
			{
				int32 EdgeToRemoveAtEnd = EdgeCount - IEdge;
				int32 EdgeToRemoveAtStart = EdgeToRemoveCount - EdgeToRemoveAtEnd;
				Edges.SetNum(IEdge);
				for (int32 Index = 0; Index < EdgeToRemoveAtStart; Index++)
				{
					Edges.RemoveAt(0);
				}
			}
			return;
		}
	}
	ensureCADKernel(false);
}

// Need to be reviewed
/*void FTopologicalLoop::ReplaceEdgesWithMergedEdge(TArray<TSharedPtr<FTopologicalEdge>>& OldEdges, TSharedPtr<FTopologicalVertex>& MiddleVertex, TSharedPtr<FTopologicalEdge>& NewEdge)
{
	ensureCADKernel(OldEdges.Num() == 2);
	ensureCADKernel(EdgeCount() != 2);

	NewEdge->SetLoop(StaticCastSharedRef<FTopologicalLoop>(AsShared()));
	for (TSharedPtr<FTopologicalEdge> Edge : OldEdges)
	{
		Edge->RemoveLoop();
		Edge->GetStartVertex()->RemoveConnectedEdge(Edge.ToSharedRef());
		Edge->GetEndVertex()->RemoveConnectedEdge(Edge.ToSharedRef());
	}

	int32 IndexFirstOldEdge = GetEdgeIndex(OldEdges[0]);
	int32 IndexLastOldEdge = GetEdgeIndex(OldEdges[1]);

	ensureCADKernel(IndexFirstOldEdge != INDEX_NONE && IndexLastOldEdge != INDEX_NONE);

	Sort(IndexFirstOldEdge, IndexLastOldEdge);
	if (IndexFirstOldEdge == 0 && IndexLastOldEdge != 1)
	{
		IndexLastOldEdge = 0;
		IndexFirstOldEdge = EdgeCount() - 1;
	}
	int32 PreviousEdgeIndex = IndexFirstOldEdge ? IndexFirstOldEdge - 1 : EdgeCount() - 1;
	TSharedPtr<FTopologicalVertex> PreviousVertex = Edges[PreviousEdgeIndex].Direction == EOrientation::Front ? Edges[PreviousEdgeIndex].Entity->GetEndVertex() : Edges[PreviousEdgeIndex].Entity->GetStartVertex();
	EOrientation NewEdgeOrientation = NewEdge->GetStartVertex() == PreviousVertex ? EOrientation::Front : EOrientation::Back;

	if (IndexLastOldEdge)
	{
		Edges.RemoveAt(IndexFirstOldEdge);
		Edges.RemoveAt(IndexFirstOldEdge + 1);
		Edges.EmplaceAt(IndexFirstOldEdge, NewEdge, NewEdgeOrientation);
	}
	else
	{
		Edges.RemoveAt(0);
		Edges.Pop();
		Edges.Emplace(NewEdge, NewEdgeOrientation);
	}
}*/



void FTopologicalLoop::FindSurfaceCorners(TArray<TSharedPtr<FTopologicalVertex>>& OutCorners, TArray<int32>& OutStartSideIndex) const
{
	TArray<double> BreakValues;
	FindBreaks(OutCorners, OutStartSideIndex, BreakValues);
}

void FTopologicalLoop::ComputeBoundaryProperties(const TArray<int32>& StartSideIndex, TArray<FEdge2DProperties>& OutSideProperties) const
{
	if (StartSideIndex.Num() == 0)
	{
		return;
	}

	OutSideProperties.Reserve(StartSideIndex.Num());

	int32 EdgeIndex = StartSideIndex[0];
	for (int32 SideIndex = 0; SideIndex < StartSideIndex.Num(); ++SideIndex)
	{
		int32 LastEdgeIndex = SideIndex + 1;
		LastEdgeIndex = (LastEdgeIndex == StartSideIndex.Num()) ? StartSideIndex[0] : StartSideIndex[LastEdgeIndex];

		FEdge2DProperties& SideProperty = OutSideProperties.Emplace_GetRef();
		do
		{
			Edges[EdgeIndex].Entity->ComputeEdge2DProperties(SideProperty);
			if (++EdgeIndex == Edges.Num())
			{
				EdgeIndex = 0;
			}
		} while (EdgeIndex != LastEdgeIndex);
		SideProperty.Finalize();
	}
}

void FTopologicalLoop::EnsureLogicalClosing(const double Tolerance3D)
{
	TSharedRef<FTopologicalLoop> Loop = StaticCastSharedRef<FTopologicalLoop>(AsShared());
	const TSharedRef<FSurface>& Surface = Edges[0].Entity->GetCurve()->GetCarrierSurface();

	FOrientedEdge PreviousEdge = Edges.Last();

	FSurfacicCurveExtremity PreviousExtremities;
	PreviousEdge.Entity->GetExtremities(PreviousExtremities);
	FSurfacicCurvePointWithTolerance PreviousExtremity;

	if (PreviousEdge.Direction == EOrientation::Front)
	{
		PreviousExtremity = PreviousExtremities[1];
	}
	else
	{
		PreviousExtremity = PreviousExtremities[0];
	}

	for (int32 Index = 0; Index < Edges.Num(); ++Index)
	{
		FOrientedEdge OrientedEdge = Edges[Index];
		FSurfacicCurvePointWithTolerance EdgeExtremities[2];
		OrientedEdge.Entity->GetExtremities(EdgeExtremities);

		double Gap3D;

		int32 ExtremityIndex = OrientedEdge.Direction == EOrientation::Front ? 0 : 1;
		Gap3D = EdgeExtremities[ExtremityIndex].Point.SquareDistance(PreviousExtremity.Point);

		TSharedRef<FTopologicalVertex> PreviousEdgeEndVertex = PreviousEdge.Direction == EOrientation::Front ? PreviousEdge.Entity->GetEndVertex() : PreviousEdge.Entity->GetStartVertex();
		TSharedRef<FTopologicalVertex> EdgeStartVertex = OrientedEdge.Direction == EOrientation::Front ? OrientedEdge.Entity->GetStartVertex() : OrientedEdge.Entity->GetEndVertex();

		FPoint2D Gap2D;
		Gap2D = EdgeExtremities[ExtremityIndex].Point2D - PreviousExtremity.Point2D;
		FSurfacicTolerance Tolerance2D = Max(EdgeExtremities[ExtremityIndex].Tolerance, PreviousExtremity.Tolerance);

		if (Gap3D > Tolerance3D)
		{
			FMessage::Printf(Log, TEXT("Loop %d Gap 3D : %f\n"), Id, sqrt(Gap3D));
			FPoint PreviousTangent = PreviousEdge.Entity->GetTangentAt(PreviousEdgeEndVertex);
			FPoint EdgeTangent = OrientedEdge.Entity->GetTangentAt(EdgeStartVertex);

			FPoint Gap = EdgeExtremities[ExtremityIndex].Point - PreviousExtremity.Point;

			double CosAngle = Gap.ComputeCosinus(EdgeTangent);
			if (CosAngle > 0.9)
			{
				OrientedEdge.Entity->ExtendTo(OrientedEdge.Direction == EOrientation::Front, PreviousExtremity.Point2D, PreviousEdgeEndVertex);
			}
			else
			{
				CosAngle = Gap.ComputeCosinus(PreviousTangent);
				if (CosAngle < -0.9)
				{
					PreviousEdge.Entity->ExtendTo(PreviousEdge.Direction == EOrientation::Front, EdgeExtremities[ExtremityIndex].Point2D, EdgeStartVertex);
				}
				else
				{
					if (PreviousEdgeEndVertex->IsLinkedTo(EdgeStartVertex))
					{
						PreviousEdgeEndVertex->UnlinkTo(EdgeStartVertex);
					}

					TSharedPtr<FTopologicalEdge> Edge = FTopologicalEdge::Make(Surface, PreviousExtremity.Point2D, PreviousEdgeEndVertex, EdgeExtremities[ExtremityIndex].Point2D, EdgeStartVertex);
					if (Edge.IsValid())
					{
						Edges.EmplaceAt(Index, Edge, EOrientation::Front);
						Edge->SetLoop(Loop);
						++Index;
					}
					PreviousEdgeEndVertex = EdgeStartVertex;
				}
			}
		}
		else if (FMath::Abs(Gap2D.U) > Tolerance2D.U || FMath::Abs(Gap2D.V) > Tolerance2D.V)
		{
			FMessage::Printf(Log, TEXT("Loop %d Gap 2D : [%f, %f] vs Tol2D [%f, %f]\n"), Id, FMath::Abs(Gap2D.U), FMath::Abs(Gap2D.V), Tolerance2D.U, Tolerance2D.V);

			FPoint2D PreviousTangent = PreviousEdge.Entity->GetTangent2DAt(PreviousEdgeEndVertex);
			FPoint2D EdgeTangent = OrientedEdge.Entity->GetTangent2DAt(EdgeStartVertex);

			double CosAngle = Gap2D.ComputeCosinus(EdgeTangent);
			if (CosAngle > 0.9)
			{
				OrientedEdge.Entity->ExtendTo(OrientedEdge.Direction == EOrientation::Front, PreviousExtremity.Point2D, PreviousEdgeEndVertex);
			}
			else
			{
				CosAngle = Gap2D.ComputeCosinus(PreviousTangent);
				if (CosAngle < -0.9)
				{
					PreviousEdge.Entity->ExtendTo(PreviousEdge.Direction == EOrientation::Front, EdgeExtremities[ExtremityIndex].Point2D, EdgeStartVertex);
				}
				else
				{
					TSharedPtr<FTopologicalEdge> Edge = FTopologicalEdge::Make(Surface, PreviousExtremity.Point2D, PreviousEdgeEndVertex, EdgeExtremities[ExtremityIndex].Point2D, EdgeStartVertex);
					if (Edge.IsValid())
					{
						Edges.EmplaceAt(Index, Edge, EOrientation::Front);
						Edge->SetLoop(Loop);

						PreviousEdgeEndVertex->Link(EdgeStartVertex);
						++Index;

					}
				}
			}
		}
		else
		{
			PreviousEdgeEndVertex->Link(EdgeStartVertex);
		}

		if (OrientedEdge.Direction == EOrientation::Front)
		{
			PreviousExtremity = EdgeExtremities[1];
		}
		else
		{
			PreviousExtremity = EdgeExtremities[0];
		}
		PreviousEdge = OrientedEdge;
	}
}

#ifdef CADKERNEL_DEV
FInfoEntity& FTopologicalLoop::GetInfo(FInfoEntity& Info) const
{
	return FEntity::GetInfo(Info)
		.Add(TEXT("Edges"), (TArray<TOrientedEntity<FEntity>>&) Edges)
		.Add(TEXT("Hosted by"), Face);
}
#endif

void FTopologicalLoop::FindBreaks(TArray<TSharedPtr<FTopologicalVertex>>& OutBreaks, TArray<int32>& OutStartSideIndex, TArray<double>& OutBreakValues) const
{
	const double MinCosAngleOfBreak = -0.7;  // 135 deg
	if (Edges.Num() == 0)
	{
		return;
	}

	int32 EdgeNum = (int32)Edges.Num();
	OutBreaks.Empty(EdgeNum);
	OutBreakValues.Empty(EdgeNum);
	OutStartSideIndex.Empty(EdgeNum);

	FPoint StartTangentEdge;
	FPoint EndTangentPreviousEdge;
	Edges[EdgeNum - 1].Entity->GetTangentsAtExtremities(StartTangentEdge, EndTangentPreviousEdge, Edges[EdgeNum - 1].Direction == EOrientation::Front);
	bool bPreviousIsSurface = (Edges[EdgeNum - 1].Entity->GetTwinsEntityCount() > 1);

	for (int32 Index = 0; Index < EdgeNum; Index++)
	{
		FPoint EndTangentEdge;
		Edges[Index].Entity->GetTangentsAtExtremities(StartTangentEdge, EndTangentEdge, Edges[Index].Direction == EOrientation::Front);
		bool bIsSurface = (Edges[Index].Entity->GetTwinsEntityCount() > 1);

		// if both edge are border, the rupture is not evaluate. 
		if (bIsSurface || bPreviousIsSurface)
		{
			double CosAngle = StartTangentEdge.ComputeCosinus(EndTangentPreviousEdge);

#ifdef FIND_BREAKS
			{
				FPoint& Start = BoundaryEdges[Index]->GetStartVertex(BoundaryEdgeDirections[Index])->GetCoordinates();
				Open3DDebugSession(TEXT("Cos Angle " + Utils::ToString(CosAngle));
				DisplayPoint(Start, (CosAngle > MinCosAngleOfBreak) ? EVisuProperty::RedPoint : EVisuProperty::BluePoint);
				DisplaySegment(Start, Start + StartTangentEdge);
				DisplaySegment(Start, Start + EndTangentPreviousEdge);
				Close3DDebugSession();
			}
#endif

			if (CosAngle > MinCosAngleOfBreak)
			{
				OutBreaks.Add(Edges[Index].Direction == EOrientation::Front ? Edges[Index].Entity->GetStartVertex() : Edges[Index].Entity->GetEndVertex());
				OutBreakValues.Add(CosAngle);
				OutStartSideIndex.Add(Index);
			}
		}

		EndTangentPreviousEdge = EndTangentEdge;
		bPreviousIsSurface = bIsSurface;
	}
}
