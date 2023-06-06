// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CADKernel/Core/Factory.h"
#include "CADKernel/Core/Types.h"
#include "CADKernel/Mesh/MeshEnum.h"
#include "CADKernel/Mesh/Meshers/IsoTriangulator/IsoNode.h"
#include "CADKernel/Mesh/Meshers/IsoTriangulator/IsoSegment.h"
#include "CADKernel/Mesh/Meshers/IsoTriangulator/IntersectionSegmentTool.h"
#include "CADKernel/Mesh/Structure/Grid.h"
#include "CADKernel/Topo/TopologicalFace.h"

namespace UE::CADKernel
{
class FGrid;
class FIsoTriangulator;

struct FLoopCell
{
	int32 Id;
	FPoint2D Barycenter;
	TArray<FLoopNode*> Nodes;
	TArray<FLoopConnexion*> Connexions;
	bool bIsOuterLoop = false;

	FLoopCell(const int32 InIndex, TArray<FLoopNode*>& InNodes, const FGrid& Grid)
		: Id(InIndex)
		, Barycenter(FPoint::ZeroPoint)
		, Nodes(InNodes)
		, bIsOuterLoop(Nodes[0]->GetLoopIndex() == 0)
	{
		for (const FLoopNode* Node : Nodes)
		{
			Barycenter += Node->Get2DPoint(EGridSpace::UniformScaled, Grid);
		}
		Barycenter /= (double)Nodes.Num();
	}
};

struct FLoopConnexion
{
	bool bIsConnexionWithBorder = false;
	FLoopCell& Loop1;
	FLoopCell& Loop2;

	double MinDistance = HUGE_VALUE_SQUARE;

	FLoopNode* NodeA = nullptr;
	FLoopNode* NodeB = nullptr;

	FIsoSegment* Segment = nullptr;

	FLoopConnexion(FLoopCell& InLoop1, FLoopCell& InLoop2, bool bIsConnectingBoder = false)
		: bIsConnexionWithBorder(bIsConnectingBoder)
		, Loop1(InLoop1)
		, Loop2(InLoop2)
	{
	}

	void LinkToLoop()
	{
		Loop1.Connexions.Add(this);
		Loop2.Connexions.Add(this);
	}

	const FLoopCell* GetOtherLoop(const FLoopCell* Loop)
	{
		return (Loop == &Loop1) ? &Loop2 : &Loop1;
	}

	bool IsShortestPath()
	{
		TMap<const FLoopCell*, double> DistanceToLoops;
		DistanceToLoops.Add(&Loop1, 0.);
		double MinPathDistance = HUGE_VALUE;
		while (true)
		{
			double DistanceToCurrent = HUGE_VALUE;
			TPair<const FLoopCell*, double>* CurrentLoop = nullptr;
			for (TPair<const FLoopCell*, double>& DistanceToLoop : DistanceToLoops)
			{
				if (DistanceToLoop.Value >= 0. && DistanceToLoop.Value < DistanceToCurrent)
				{
					DistanceToCurrent = DistanceToLoop.Value;
					CurrentLoop = &DistanceToLoop;
				}
			}

			if (!CurrentLoop || CurrentLoop->Value > MinDistance)
			{
				return true;
			}

			if (CurrentLoop->Key == &Loop2)
			{
				return (CurrentLoop->Value > MinDistance);
			}

			for (FLoopConnexion* Connexion : CurrentLoop->Key->Connexions)
			{
				if (Connexion == this || Connexion->bIsConnexionWithBorder)
				{
					continue;
				}

				const FLoopCell* NextLoop = Connexion->GetOtherLoop(CurrentLoop->Key);
				const double DistanceToNextByCurrent = CurrentLoop->Value + Connexion->MinDistance;

				double* DistanceToNextLoop = DistanceToLoops.Find(NextLoop);
				if (DistanceToNextLoop)
				{
					if (*DistanceToNextLoop > DistanceToNextByCurrent)
					{
						*DistanceToNextLoop = DistanceToNextByCurrent;
					}
				}
				else
				{
					DistanceToLoops.Add(NextLoop, DistanceToNextByCurrent);
				}
			}

			CurrentLoop->Value = -HUGE_VALUE;
		}

		return true;
	}
};

struct FCellPath
{
	double Length = 0;
	FLoopCell* CurrentLoop;
	TArray<FLoopCell> Path;
};

struct FCell
{
	FIsoTriangulator& Triangulator;
	const FGrid& Grid;
	int32 Id;

	int32 InnerLoopCount = 0;
	int32 OuterLoopCount = 0;

	TArray<FIsoSegment*> CandidateSegments;
	TArray<FIsoSegment*> FinalSegments;

	FIntersectionSegmentTool IntersectionTool;

	TArray<FLoopCell> LoopCells;
	TArray<FLoopConnexion> LoopConnexions;
	TArray<int32> LoopCellBorderIndices;

	FCell(const int32 InLoopIndex, TArray<FLoopNode*>& InNodes, FIsoTriangulator& InTriangulator)
		: Triangulator(InTriangulator)
		, Grid(Triangulator.GetGrid())
		, Id(InLoopIndex)
		, IntersectionTool(Triangulator.GetGrid())
	{
		const int32 NodeCount = InNodes.Num();
		ensureCADKernel(NodeCount > 0);

		// Subdivide InNodes in SubLoop
		Algo::Sort(InNodes, [&](const FLoopNode* LoopNode1, const FLoopNode* LoopNode2)
			{
				return LoopNode1->GetGlobalIndex() < LoopNode2->GetGlobalIndex();
			});

		int32 LoopCount = 0;
		FLoopNode* PreviousNode = nullptr;
		for (FLoopNode* Node : InNodes)
		{
			if (&Node->GetPreviousNode() != PreviousNode)
			{
				LoopCount++;
			}
			PreviousNode = Node;
		}

		LoopCells.Reserve(LoopCount);

		LoopCount = 0;
		TArray<FLoopNode*> LoopNodes;
		LoopNodes.Reserve(NodeCount);

		int32 LoopIndex = -1;
		FLoopCell* FirstLoopCell = nullptr;

		TFunction<void(TArray<FLoopNode*>&)> MakeLoopCell = [&FirstLoopCell, &LoopCells = LoopCells, &LoopIndex, &Grid = Grid](TArray<FLoopNode*>& LoopNodes)
		{
			if (LoopNodes.Num())
			{
				if ((LoopIndex == LoopNodes[0]->GetLoopIndex()) && (&LoopNodes.Last()->GetNextNode() == FirstLoopCell->Nodes[0]))
				{
					LoopNodes.Append(FirstLoopCell->Nodes);
					FirstLoopCell->Nodes = LoopNodes;
				}
				else
				{
					LoopCells.Emplace(LoopCells.Num(), LoopNodes, Grid);
				}

				if (LoopIndex != LoopNodes[0]->GetLoopIndex())
				{
					LoopIndex = LoopNodes[0]->GetLoopIndex();
					FirstLoopCell = &LoopCells.Last();
				}
			}
		};

		PreviousNode = nullptr;
		for (FLoopNode* Node : InNodes)
		{
			if (Node->IsDelete())
			{
				continue;
			}

			if (&Node->GetPreviousNode() != PreviousNode)
			{
				MakeLoopCell(LoopNodes);
				LoopNodes.Reset(NodeCount);
			}
			LoopNodes.Add(Node);
			PreviousNode = Node;
		}
		MakeLoopCell(LoopNodes);

		for(const FLoopCell& LoopCell : LoopCells)
		{
			if (LoopCell.bIsOuterLoop)
			{
				OuterLoopCount++;
			}
			else
			{
				InnerLoopCount++;
			}
		}
	}

	void InitLoopConnexions();

	TArray<TPair<int32, FPoint2D>> GetLoopBarycenters()
	{
		TArray<TPair<int32, FPoint2D>> LoopBarycenters;
		LoopBarycenters.Reserve(LoopCells.Num());
		for (const FLoopCell& LoopCell : LoopCells)
		{
			if(!LoopCell.bIsOuterLoop)
			{
				LoopBarycenters.Emplace(LoopCell.Id, LoopCell.Barycenter);
			}
		}
		return MoveTemp(LoopBarycenters);
	}

	void ConnectLoopsByNeighborhood();

	/**
	 *  SubLoopA                  SubLoopB
	 *      --X---X             X-----X--
	 *             \           /
	 *              \         /
	 *               X=======X
	 *              /         \
	 *             /           \
	 *      --X---X             X-----X--
	 *
	 *     ======= ShortestSegment
	 */
	void TryToConnectTwoSubLoopsWithShortestSegment(FLoopConnexion& LoopConnexion);

	void TryToCreateSegment(FLoopConnexion& LoopConnexion);

	void SelectSegmentInCandidateSegments(TFactory<FIsoSegment>& SegmentFactory, bool bSelectWithCellSizeCriteria)
	{
#ifdef DEBUG_SELECT_SEGMENT
		F3DDebugSession _(Grid.bDisplay, TEXT("SelectSegmentInCandidateSegments "));
		if(Grid.bDisplay)
		{
			IntersectionTool.Display(Grid.bDisplay, TEXT("Cell.IntersectionTool at SelectSegmentInCandidateSegments start"));
			Wait();
		}
#endif

		double CellSquareLength = HUGE_VALUE;
		if(bSelectWithCellSizeCriteria)
		{
			int32 IndexU;
			int32 IndexV;
			Grid.UVIndexFromGlobalIndex(Id, IndexU, IndexV);

			CellSquareLength = Grid.GetInner2DPoint(EGridSpace::UniformScaled, IndexU, IndexV).SquareDistance(Grid.GetInner2DPoint(EGridSpace::UniformScaled, IndexU, IndexV + 1));
			const double CellLength2 = Grid.GetInner2DPoint(EGridSpace::UniformScaled, IndexU, IndexV).SquareDistance(Grid.GetInner2DPoint(EGridSpace::UniformScaled, IndexU + 1, IndexV));
			CellSquareLength = FMath::Min(CellSquareLength, CellLength2);
		}

		TArray<TPair<double, FIsoSegment*>> LengthOfCandidateSegments;
		LengthOfCandidateSegments.Reserve(CandidateSegments.Num());
		for (FIsoSegment* Segment : CandidateSegments)
		{
			LengthOfCandidateSegments.Emplace(Segment->Get2DLengthSquare(EGridSpace::UniformScaled, Grid), Segment);
		}

		Algo::Sort(LengthOfCandidateSegments, [&](const TPair<double, FIsoSegment*>& P1, const TPair < double, FIsoSegment*>& P2) { return P1.Key < P2.Key; });

		// Validate the first candidate segments
		for (const TPair<double, FIsoSegment*>& Candidate : LengthOfCandidateSegments)
		{
			if (Candidate.Key > CellSquareLength)
			{
				break;
			}

			FIsoSegment* Segment = Candidate.Value;
#ifdef DEBUG_SELECT_SEGMENT
			F3DDebugSession _(Grid.bDisplay, TEXT("Segment"));
#endif
			if (IntersectionTool.DoesIntersect(*Segment))
			{
#ifdef DEBUG_SELECT_SEGMENT
				F3DDebugSession _(Grid.bDisplay, TEXT("SelectSegmentInCandidateSegments "));
				Grid.DisplayIsoSegment(EGridSpace::UniformScaled, *Segment, YellowCurve);
#endif
				SegmentFactory.DeleteEntity(Segment);
				continue;
			}

			if (FIsoSegment::IsItAlreadyDefined(&Segment->GetFirstNode(), &Segment->GetSecondNode()))
			{
#ifdef DEBUG_SELECT_SEGMENT
				Grid.DisplayIsoSegment(EGridSpace::UniformScaled, *Segment, GreenCurve);
#endif
				SegmentFactory.DeleteEntity(Segment);
				continue;
			}

#ifdef DEBUG_SELECT_SEGMENT
			Grid.DisplayIsoSegment(EGridSpace::UniformScaled, *Segment, BlueCurve);
#endif

			FinalSegments.Add(Segment);
			IntersectionTool.AddSegment(*Segment);
			Segment->SetSelected();
			Segment->ConnectToNode();
		}
		CandidateSegments.Empty();
	}

};

} // namespace UE::CADKernel

