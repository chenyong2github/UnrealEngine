// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CADKernel/Core/Types.h"
#include "CADKernel/Mesh/MeshEnum.h"
#include "CADKernel/Mesh/Meshers/IsoTriangulator/IsoNode.h"
#include "CADKernel/Mesh/Meshers/IsoTriangulator/IntersectionSegmentTool.h"
#include "CADKernel/Mesh/Structure/Grid.h"
#include "CADKernel/Topo/TopologicalFace.h"

namespace CADKernel
{
	class FGrid;

	struct FCell
	{
		const FGrid& Grid;
		int32 Id;
		int32 LoopNodeCount;
		bool bHasOuterLoop = false;
		int32 InnerLoopCount = 0;

		TArray<int32> LoopIndexToIndex;
		TArray<TArray<FLoopNode*>> Loops;

		// the outer loop is subdivide into connected (in the cell) nodes
		TArray<TArray<FLoopNode*>> OuterLoopSubdivision;

		TArray<FIsoSegment*> CandidateSegments;
		TArray<FIsoSegment*> FinalSegments;
		FIntersectionSegmentTool IntersectionTool;

		/** Loop that barycenter is on the border of the barycenter mesh */
		TSet<int32> BorderLoopIndices;

		FCell(const int32 InLoopIndex, const TArray<FLoopNode*>& InNodes, const FGrid& InGrid)
			: Grid(InGrid)
			, Id(InLoopIndex)
			, LoopNodeCount(InNodes.Num())
			, IntersectionTool(Grid)
		{
			ensureCADKernel(InNodes.Num() > 0);

			int32 FaceLoopCount = Grid.GetFace()->LoopCount();
			int32 LoopCount = 0;
			{
				TArray<int32> LoopVertexCount;
				LoopVertexCount.Init(0, FaceLoopCount);
				for (const FLoopNode* Node : InNodes)
				{
					LoopVertexCount[Node->GetLoopIndex()]++;
				}

				for (const int32& VertexCount : LoopVertexCount)
				{
					if (VertexCount > 0)
					{
						LoopCount++;
					}
				}

				Loops.SetNum(LoopCount);
				int32 Index = 0;
				for (int32 LoopIndex = 0; LoopIndex < FaceLoopCount; ++LoopIndex)
				{
					if (LoopVertexCount[LoopIndex] > 0)
					{
						Loops[Index].Reserve(LoopVertexCount[LoopIndex]);
						LoopVertexCount[LoopIndex] = Index;
						Index++;
					}
				}
				Swap(LoopIndexToIndex, LoopVertexCount);
			}

			for (FLoopNode* Node : InNodes)
			{
				int32 Index = LoopIndexToIndex[Node->GetLoopIndex()];
				Loops[Index].Add(Node);
			}

			bHasOuterLoop = (Loops[0][0]->GetLoopIndex() == 0);
			InnerLoopCount = bHasOuterLoop ? (Loops.Num() - 1): Loops.Num();

			if (bHasOuterLoop)
			{
				// Subdivide Loop0 in SubLoop
				TArray<FLoopNode*>& Loop0 = Loops[0];
				Algo::Sort(Loop0, [&](const FLoopNode* LoopNode1, const FLoopNode* LoopNode2)
					{
						return LoopNode1->GetIndex() < LoopNode2->GetIndex();
					});

				OuterLoopSubdivision.Reserve(Loop0.Num());
				FLoopNode* PreviousNode = nullptr;
				TArray<FLoopNode*>* SubLoop = nullptr;
				for (FLoopNode* Node : Loop0)
				{
					if (&Node->GetPreviousNode() != PreviousNode)
					{
						SubLoop = &OuterLoopSubdivision.Emplace_GetRef();
					}
					SubLoop->Add(Node);
					PreviousNode = Node;
				}
			}
		}

		void SelectSegmentInCandidateSegments(TFactory<FIsoSegment>& SegmentFactory)
		{
			Algo::Sort(CandidateSegments, [&](const FIsoSegment* Segment1, const FIsoSegment* Segment2)
				{
					return Segment1->Get2DLengthSquare(EGridSpace::UniformScaled, Grid) < Segment2->Get2DLengthSquare(EGridSpace::UniformScaled, Grid);
				});

			// Validate all candidate segments
			for (FIsoSegment* Segment : CandidateSegments)
			{
				if (IntersectionTool.DoesIntersect(*Segment))
				{
					SegmentFactory.DeleteEntity(Segment);
					continue;
				}

				FinalSegments.Add(Segment);
				IntersectionTool.AddSegment(*Segment);
				Segment->SetSelected();
				Segment->ConnectToNode();
			}
			CandidateSegments.Empty();
		}

	};

} // namespace CADKernel

