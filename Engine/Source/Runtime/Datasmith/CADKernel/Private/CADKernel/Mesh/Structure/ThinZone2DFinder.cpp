// Copyright Epic Games, Inc. All Rights Reserved.

#include "CADKernel/Mesh/Structure/ThinZone2DFinder.h"

#include "CADKernel/Math/Point.h"
#include "CADKernel/Mesh/Structure/Grid.h"
#include "CADKernel/Mesh/Structure/EdgeSegment.h"
#include "CADKernel/Topo/TopologicalEdge.h"
#include "CADKernel/Topo/TopologicalFace.h"
#include "CADKernel/Topo/TopologicalLoop.h"
#include "CADKernel/UI/Display.h"

namespace UE::CADKernel
{

FIdent FEdgeSegment::LastId = 0;

void FThinZone2DFinder::FindCloseSegments()
{
	TFunction<double()> ComputeMaxSegmentLength = [&]()-> double
	{
		double MaxLength = 0;
		for (const FEdgeSegment* Segment : LoopSegments)
		{
			double Length = Segment->GetLength();
			if (Length > MaxLength)
			{
				MaxLength = Length;
			}
		}
		MaxLength *= 1.01;
		return MaxLength;
	};

	const double MaxSegmentLength = ComputeMaxSegmentLength();
	const double MaxSpace = FMath::Max(MaxSegmentLength, FinderTolerance * 1.01);
	const double MaxSquareSpace = 4 * FMath::Square(MaxSpace);
	const double MaxSpacePlusLength = 1.1 * (MaxSpace + MaxSegmentLength);

	// Copy of loop segments to generate a sorted array of segments
	TArray<FEdgeSegment*> SortedThinZoneSegments = LoopSegments;
	Algo::Sort(SortedThinZoneSegments, [](FEdgeSegment* SegmentA, FEdgeSegment* SegmentB)
		{
			return (SegmentA->GetAxeMin() < SegmentB->GetAxeMin());
		});

#ifdef DEBUG_FIND_CLOSE_SEGMENTS
	F3DDebugSession _(bDisplay, ("Close Segments"));
	int32 SegmentID = 1291;
	int32 SegmentIndex = 0;
#endif

	// For each segment, the nearest segment is search
	// If segment is from an inner loop, the nearest could not be from the same loop
	int32 SegmentToBeginIndex = 0;
	for (FEdgeSegment* Segment : SortedThinZoneSegments)
	{
		FTopologicalLoop* SegmentLoop = nullptr;
		if (Segment->IsInner())
		{
			SegmentLoop = Segment->GetEdge()->GetLoop();
		}
		else
		{
			SegmentLoop = nullptr;
		}

		const FPoint2D SegmentMiddle = Segment->GetCenter();

		FPoint2D ClosePoint, ClosePoint2;
		FEdgeSegment* CloseSegment = nullptr;

		//     DistanceMin between Candidate and Segment with Candidate is before 
		//     DistanceMin = (Segment.SegmentMin - Candidate.SegmentMax) = Segment.SegmentMin - (Candidate.SegmentMin + MaxSegmentLength) 
		//
		//     Candidate                              Segment
		//     *---*                                  x-----x
		//     | Candidate.SegmentMin                 | Segment.SegmentMin
		//         | Candidate.SegmentMax = Candidate.SegmentMin + MaxSegmentLength
		//
		//     We want DistanceMin < MaxSpace
		//     Segment.SegmentMin - (Candidate.SegmentMin + MaxSegmentLength) < MaxSpace
		//     Candidate.SegmentMin > Segment.SegmentMin - MaxSegmentLength - MaxSpace

		//     DistanceMin between Candidate and Segment, if Candidate is after
		//     DistanceMin = (Candidate.SegmentMin - Segment.SegmentMax) = Candidate.SegmentMin - (Segment.SegmentMin + MaxSegmentLength) 
		//
		//     Segment                                Candidate
		//     *--x--*                                  x-----x
		//        | Segment.Middle                      | Candidate.SegmentMin
		//
		//     We want DistanceMin < MaxSpace + MaxSegmentLength
		//     Candidate.SegmentMin - Segment.Middle < MaxSpace + MaxSegmentLength
		//     Candidate.SegmentMin < Segment.Middle + MaxSpace + MaxSegmentLength
		//
		//     MaxSpacePlusLength = MaxSpace + MaxSegmentLength;

		const double SegmentMinMin = Segment->GetAxeMin() - MaxSpacePlusLength;
		const double SegmentMiddleMax = SegmentMiddle.DiagonalAxisCoordinate() + MaxSpacePlusLength;
		double MinSquareThickness = HUGE_VALUE;

#ifdef DEBUG_FIND_CLOSE_SEGMENTS
		if (SegmentIndex == SegmentID)
		{
			F3DDebugSession _(bDisplay, FString::Printf(TEXT("Segment %d"), SegmentIndex));
			ThinZone::DisplayEdgeSegment(Segment, EVisuProperty::RedCurve);
			Wait();
		}
#endif
		for (int32 CandidateIndex = SegmentToBeginIndex; CandidateIndex < SortedThinZoneSegments.Num(); ++CandidateIndex)
		{

			FEdgeSegment* Candidate = SortedThinZoneSegments[CandidateIndex];
			if (Candidate == Segment)
			{
				continue;
			}

			// Inner boundary are not check for thin inner boundary
			if (SegmentLoop == Candidate->GetEdge()->GetLoop())
			{
				continue;
			}


			const double CandidateSegmentMin = Candidate->GetAxeMin();
			// If min point of candidate segment - maxSpace (= max length of the segments + Tolerance) is smaller than Min point of current segment then the distance between both segments cannot be smaller the Tolerance
			if (CandidateSegmentMin < SegmentMinMin)
			{
				SegmentToBeginIndex = CandidateIndex;
				continue;
			}

			// If Min point of current segment + maxSpace is smaller than Min point of candidate segment then the distance between both segments cannot be smaller the Tolerance.
			// As segments are sorted, next segments are not close to the current segment
			if (CandidateSegmentMin > SegmentMiddleMax)
			{
				break;
			}

			const FPoint2D& FirstPointCandidate = Candidate->GetExtemity(ELimit::Start);

			// if the distance of FirstPointCandidate with the middle of current segment is biggest than MaxSpace, then the projection of the point cannot be smaller than the tolerance, 
			{
				const double SquareDistance = SegmentMiddle.SquareDistance(FirstPointCandidate);
				if (SquareDistance > MaxSquareSpace)
				{
					continue;
				}
			}

			// check the angle between segments. As they are opposite, the cosAngle as to be close to -1
			{
				const double Slope = Segment->ComputeUnorientedSlopeOf(Candidate);
				if (Slope < 3.) // Angle < 3Pi/4 (135 deg)
				{
					continue;
				}
			}

#ifdef DEBUG_FIND_CLOSE_SEGMENTS
			if (SegmentIndex == SegmentID)
			{
				F3DDebugSession _(bDisplay, FString::Printf(TEXT("Candidate %d %d"), SegmentIndex, CandidateIndex));
				ThinZone::DisplayEdgeSegmentAndProjection(Segment, Candidate, EVisuProperty::BlueCurve, EVisuProperty::BlueCurve, EVisuProperty::RedCurve);
				Wait();
  			}
			Wait(SegmentIndex == SegmentID + 1);
#endif
			const FPoint2D& SecondPointCandidate = Candidate->GetExtemity(ELimit::End);
			double Coordinate;
			const FPoint2D Projection = ProjectPointOnSegment(SegmentMiddle, FirstPointCandidate, SecondPointCandidate, Coordinate, true);

			const double SquareDistance = SegmentMiddle.SquareDistance(Projection);
			if (SquareDistance > SquareFinderTolerance)
			{
				continue;
			}

			if (MinSquareThickness > SquareDistance)
			{
				// check the angle between segment and Middle-Projection. As they are opposite, the cosAngle as to be close to 0
				const double Slope = Segment->ComputeUnorientedSlopeOf(SegmentMiddle, Projection);
				if (Slope < 1. || Slope > 3.)
				{
					continue;
				}

				MinSquareThickness = SquareDistance;
				ClosePoint = Projection;
				CloseSegment = Candidate;
			}
		}

		if (CloseSegment)
		{
			Segment->SetCloseSegment(CloseSegment, MinSquareThickness);

#ifdef DEBUG_FIND_CLOSE_SEGMENTS
			if (bDisplay)
			{
				if (SegmentIndex == SegmentID)
				{
					Wait();
				}

				F3DDebugSession _(bDisplay, FString::Printf(TEXT("Close Segment %d"), SegmentIndex));
				ThinZone::DisplayEdgeSegmentAndProjection(Segment, CloseSegment, EVisuProperty::BlueCurve, EVisuProperty::BlueCurve, EVisuProperty::RedCurve);
				SegmentIndex++;
				//Wait(SegmentIndex > 22);
			}
#endif
		}
	}
}

//#define DEBUG_FIND_CLOSE_SEGMENTS
void FThinZone2DFinder::FindCloseSegments(const TArray<FEdgeSegment*> Segments, const TArray< const TArray<FEdgeSegment*>*> OppositeSides)
{
#ifdef DEBUG_FIND_CLOSE_SEGMENTS
	F3DDebugSession _(TEXT("FindCloseSegments"));
	{
		F3DDebugSession _(TEXT("OppositeSide"));
		for (const TArray<FEdgeSegment*>* OppositeSidePtr : OppositeSides)
		{
			ThinZone::DisplayThinZoneSide(*OppositeSidePtr, 0, EVisuProperty::YellowCurve);
		}
	}
#endif

	int32 SegmentToBeginIndex = 0;
	for (FEdgeSegment* Segment : Segments)
	{

#ifdef DEBUG_FIND_CLOSE_SEGMENTS
		{
			F3DDebugSession _(TEXT("Segment"));
			ThinZone::DisplayEdgeSegment(Segment, Segment->HasMarker1() ? EVisuProperty::RedCurve : EVisuProperty::BlueCurve);
		}
#endif

		if (!Segment->HasMarker1())
		{
			continue;
		}
		Segment->ResetMarker1();

		FPoint2D SegmentMiddle = Segment->GetCenter();

		FPoint2D ClosePoint, ClosePoint2;
		FEdgeSegment* CloseSegment = nullptr;

		double MinSquareThickness = HUGE_VALUE;

#ifdef DEBUG_FIND_CLOSE_SEGMENTS_2
		//if (SegmentIndex == SegmentID)
		F3DDebugSession _(bDisplay, TEXT("Segment"));
		{
			F3DDebugSession _(bDisplay, TEXT("Segment"));
			DisplaySegmentWithScale(Segment->GetExtemity(ELimit::Start), Segment->GetExtemity(ELimit::End), 0, EVisuProperty::YellowCurve);
		}
#endif

		for (const TArray<FEdgeSegment*>* OppositeSidePtr : OppositeSides)
		{
			const TArray<FEdgeSegment*>& OppositeSide = *OppositeSidePtr;
			for (FEdgeSegment* Candidate : OppositeSide)
			{
				// check the angle between segments. As they are opposite, the Angle as to be close to Pi i.e. Slop ~= 4
				double Slope = Segment->ComputeUnorientedSlopeOf(Candidate);
				if (Slope < 3.) // Angle < 3Pi/4 (135 deg)
				{
					continue;
				}

				const FPoint2D& FirstPointCandidate = Candidate->GetExtemity(ELimit::Start);
				const FPoint2D& SecondPointCandidate = Candidate->GetExtemity(ELimit::End);

				double Coordinate;
				const FPoint2D Projection = ProjectPointOnSegment(SegmentMiddle, FirstPointCandidate, SecondPointCandidate, Coordinate, true);

				const double SquareDistance = SegmentMiddle.SquareDistance(Projection);
				if (MinSquareThickness > SquareDistance)
				{
					// check the angle between segment and Middle-Projection. As they are opposite, the cosAngle as to be close to 0
					Slope = Segment->ComputeUnorientedSlopeOf(SegmentMiddle, Projection);
					if (Slope < 1. || Slope > 3.)
					{
						continue;
					}

					MinSquareThickness = SquareDistance;
					ClosePoint = Projection;
					CloseSegment = Candidate;
				}
			}
		}

		if (CloseSegment)
		{
			Segment->SetCloseSegment(CloseSegment, MinSquareThickness);
#ifdef DEBUG_FIND_CLOSE_SEGMENTS
			if (bDisplay)
			{
				F3DDebugSession _(bDisplay, TEXT("Close Segment"));
				ThinZone::DisplayEdgeSegmentAndProjection(Segment, CloseSegment, EVisuProperty::BlueCurve, EVisuProperty::BlueCurve, EVisuProperty::RedCurve);
			}
#endif
		}
	}
}

//#define DEBUG_LINK_CLOSE_SEGMENTS
void FThinZone2DFinder::LinkCloseSegments()
{
	TArray<FEdgeSegment*> ThinZoneSegments;
	ThinZoneSegments.Reserve(LoopSegments.Num());

	FIdent ChainIndex = Ident::Undefined;
	FTopologicalLoop* Loop = nullptr;
	bool bThinZone = false;

	// Add in ThinZoneSegments all segments of thin zone 
	for (FEdgeSegment* EdgeSegment : LoopSegments)
	{
		if (Loop != EdgeSegment->GetEdge()->GetLoop())
		{
			Loop = EdgeSegment->GetEdge()->GetLoop();
			if(bThinZone)
			{
				ChainIndex++;
				bThinZone = false;
			}
		}

		if (const FEdgeSegment* CloseSegment = EdgeSegment->GetCloseSegment())
		{
			// Pick case i.e. all the segments are connected, 
			//    _____ . ____ .
			//   /
			//  . --- . ---- . --- .
			// As soon as a CloseSegment chain index is set and is equal to index, this mean that the segment is on the other side of the pick.
			if (CloseSegment->GetChainIndex() == ChainIndex)
			{
				ChainIndex++; // new chain
			}

			EdgeSegment->SetChainIndex(ChainIndex);
			ThinZoneSegments.Add(EdgeSegment);
			bThinZone = true;
		}
		else if (bThinZone)
		{
			ChainIndex++;  // new chain
			bThinZone = false;
		}
	}

#ifdef DEBUG_LINK_CLOSE_SEGMENTS
	{
		DisplaySegmentsOfThinZone();
	}
#endif

	if (ThinZoneSegments.IsEmpty())
	{
		return;
	}

	// Fill ThinZoneSides
	{
		// Reserve
		ThinZoneSides.SetNum(ChainIndex);
		ChainIndex = Ident::Undefined;
		int32 SegmentCount = 0;
		for (FEdgeSegment* EdgeSegment : ThinZoneSegments)
		{
			if (ChainIndex != EdgeSegment->GetChainIndex())
			{
				if (SegmentCount)
				{
					TArray<FEdgeSegment*>& ThinZoneSide = ThinZoneSides[ChainIndex];
					ThinZoneSide.Reserve(ThinZoneSide.Max() + SegmentCount);
					SegmentCount = 0;
				}
				ChainIndex = EdgeSegment->GetChainIndex();
			}
			SegmentCount++;
		}
		ThinZoneSides.Emplace_GetRef().Reserve(SegmentCount);

		// Fill
		ChainIndex = Ident::Undefined;
		TArray<FEdgeSegment*>* ThinZoneSide = nullptr;
		for (FEdgeSegment* EdgeSegment : ThinZoneSegments)
		{
			if (ChainIndex != EdgeSegment->GetChainIndex())
			{
				ChainIndex = EdgeSegment->GetChainIndex();
				ThinZoneSide = &ThinZoneSides[ChainIndex];
			}
			ThinZoneSide->Add(EdgeSegment);
		}
	}
}

void FThinZone2DFinder::ImproveThinSide()
{
	TFunction<TArray<FEdgeSegment*>(const FEdgeSegment*)> GetComplementary = [](const FEdgeSegment* EdgeSegment) ->TArray<FEdgeSegment*>
	{
		// We allow to extend a thin zone up to 4 times the local thickness if it lets to link another ThinZone
		constexpr double ComplementaryFactor = 4.;

		TArray<FEdgeSegment*> ComplementaryEdges;
		ComplementaryEdges.Reserve(100);

		double Length = 0;
		double MaxLength = FMath::Sqrt(EdgeSegment->GetCloseSquareDistance()) * ComplementaryFactor;

		for (FEdgeSegment* PreviousSegment = EdgeSegment->GetPrevious();; PreviousSegment = PreviousSegment->GetPrevious())
		{
			if (PreviousSegment->GetCloseSegment())
			{
				break;
			}
			Length += PreviousSegment->GetLength();
			ComplementaryEdges.Add(PreviousSegment);

			if (Length > MaxLength)
			{
				ComplementaryEdges.Empty();
				break;
			}
		}

		Algo::Reverse(ComplementaryEdges);
		return MoveTemp(ComplementaryEdges);
	};

	TFunction<void(TArray<FEdgeSegment*>&, TArray<FEdgeSegment*>&)> MergeChains = [&](TArray<FEdgeSegment*>& ThinZoneSide, TArray<FEdgeSegment*>& ComplementaryEdges)
	{
		FEdgeSegment* PreviousSegment = ComplementaryEdges[0]->GetPrevious();
		const FIdent ChainId = ThinZoneSide[0]->GetChainIndex();

		TArray<FEdgeSegment*> NewChain = MoveTemp(ThinZoneSides[PreviousSegment->GetChainIndex()]);
		NewChain.Reserve(NewChain.Num() + ThinZoneSide.Num() + ComplementaryEdges.Num());
		NewChain.Append(ComplementaryEdges);
		NewChain.Append(ThinZoneSide);
		ThinZoneSide = MoveTemp(NewChain);

		for (FEdgeSegment* Segment : ThinZoneSide)
		{
			Segment->SetChainIndex(ChainId);
		}
	};

	TFunction<void(TArray<FEdgeSegment*>&)> MergeWithPreviousChain = [&](TArray<FEdgeSegment*>& ThinZoneSide)
	{
		const FEdgeSegment* PreviousSegment = ThinZoneSide[0]->GetPrevious();
		const FIdent ChainId = ThinZoneSide[0]->GetChainIndex();

		TArray<FEdgeSegment*> NewChain = MoveTemp(ThinZoneSides[PreviousSegment->GetChainIndex()]);
		NewChain.Reserve(NewChain.Num() + ThinZoneSide.Num());
		NewChain.Append(ThinZoneSide);
		ThinZoneSide = MoveTemp(NewChain);

		for (FEdgeSegment* Segment : ThinZoneSide)
		{
			Segment->SetChainIndex(ChainId);
		}
	};

	TFunction<void(TArray<FEdgeSegment*>&)> SetMarker1ToComplementarySegments = [&](TArray<FEdgeSegment*>& Complementary)
	{
		for (FEdgeSegment* Segment : Complementary)
		{
			Segment->SetMarker1();
		}
	};

	// two adjacent chains can be:
	// - connected. In this case, the both chains are merged
	// - separated by a small chain slightly too far to be considered as close. If this kind of chain is find, the three chains are merged.
	for (TArray<FEdgeSegment*>& Side : ThinZoneSides)
	{
		if (Side.IsEmpty())
		{
			continue;
		}

		const FEdgeSegment* Segment0Side = Side[0];
		if (Segment0Side->GetPrevious()->GetCloseSegment() != nullptr)
		{
			// Connected case
			// 
			//                  Segment0Side->GetPrevious()            
			//                  | Segment0Side            
			//       Side(n-1)  | |  Side(n)       Side(n-1) = ThinZoneSides[n-1]
			//    #--------------#-----------#
			//
			//    #--------------------------#
			//                  | |
			//                  | Segment0Side->GetCloseSegment()
			//                  Segment0Side->GetPrevious()->GetCloseSegment()

			FIdent CloseOfPreviousSideIndex = Segment0Side->GetPrevious()->GetCloseSegment()->GetChainIndex();
			if (CloseOfPreviousSideIndex != Side[0]->GetChainIndex() && CloseOfPreviousSideIndex == Segment0Side->GetCloseSegment()->GetChainIndex())
			{
				MergeWithPreviousChain(Side);
			}
		}
		else
		{
			TArray<FEdgeSegment*> Complementary = GetComplementary(Segment0Side);
			if (Complementary.Num())
			{
				// Case2 : separated by a small chain slightly too far to be considered as close

				//                  PreviousComplementary[0]
				//                  | Complementary[0]     Seg0Side            Side(n)   = ThinZoneSides[n]
				//       Side(n-1)  | |   Complementary    |  Side(n)          Side(n-1) = ThinZoneSides[n-1]
				//    #--------------#--------------------#-----------#
				//
				//    #-----------------------------------------------#
				//                  |                      |
				//                  |                      Seg0Side->GetCloseSegment()
				//                  ClosePreviousComplementary[0]
				const FEdgeSegment* PreviousComplementary = Complementary[0]->GetPrevious();
				const FEdgeSegment* ClosePreviousComplementary = PreviousComplementary->GetCloseSegment();


				// Case Side(n-1) and Side(n) are closed together
				// 
				//                               PreviousComplementary[0]
				//       Side(n-1)               | 
				//    #---------------------------#
				//                                |
				//                                |  Complementary
				//    #---------------------------#
				//                Side(n)        |       
				//                               ClosePreviousComplementary 
				if (ClosePreviousComplementary->GetChainIndex() == Segment0Side->GetChainIndex())
				{
					// Do nothing
					continue;
				}

				FIdent CloseSideIndex = Segment0Side->GetCloseSegment()->GetChainIndex();
				if (CloseSideIndex == ClosePreviousComplementary->GetChainIndex())
				{
					MergeChains(Side, Complementary);
					SetMarker1ToComplementarySegments(Complementary);
				}
				else
				{
					//                  PreviousComplementary[0]
					//                  | Complementary[0]     Seg0Side            Side(n)   = ThinZoneSides[n]
					//       Side(n-1)  | |   Complementary    |  Side(n)          Side(n-1) = ThinZoneSides[n-1]
					//    #--------------#--------------------#-----------#  ->
					//
					//    #--------------#--------------------#-----------#  <-
					//                  |  ComplementaryClose  |
					//                  |                      Seg0Side->GetCloseSegment() == PreviousComplementaryClose[0]
					//                  ClosePreviousComplementary[0]
					TArray<FEdgeSegment*> ComplementaryClose = GetComplementary(ClosePreviousComplementary);
					if (ComplementaryClose.Num())
					{
						const FEdgeSegment* PreviousComplementaryClose = ComplementaryClose[0]->GetPrevious(); // == Seg0Side->GetCloseSegment()
						const FEdgeSegment* CloseOfPreviousComplementaryClose = PreviousComplementaryClose->GetCloseSegment(); // == Seg0Side

						if (CloseOfPreviousComplementaryClose->GetChainIndex() == Segment0Side->GetChainIndex())
						{
							MergeChains(Side, Complementary);
							SetMarker1ToComplementarySegments(Complementary);

							TArray<FEdgeSegment*>& SideOfClosePreviousComplementary = ThinZoneSides[ClosePreviousComplementary->GetChainIndex()];
							MergeChains(SideOfClosePreviousComplementary, ComplementaryClose);
							SetMarker1ToComplementarySegments(ComplementaryClose);
						}
					}
				}
			}
		}
	}

	TFunction<const TArray<const TArray<FEdgeSegment*>*>(const TArray<FEdgeSegment*>&)> FindOppositeSides = [&](const TArray<FEdgeSegment*>& Side) -> const TArray<const TArray<FEdgeSegment*>*>
	{
		TArray<int32> OppositeSideIndexes;
		OppositeSideIndexes.Reserve(5);
		int32 LastIndex = -1;
		for (const FEdgeSegment* Seg : Side)
		{
			const FEdgeSegment* Opposite = Seg->GetCloseSegment();
			if (Opposite != nullptr)
			{
				const int32 Index = Opposite->GetChainIndex();
				if (Index != LastIndex)
				{
					OppositeSideIndexes.AddUnique(Index);
					LastIndex = Index;
				}
			}
		}

		TArray<const TArray<FEdgeSegment*>*> OppositeSides;
		OppositeSides.Reserve(OppositeSideIndexes.Num());
		for (int32 Index : OppositeSideIndexes)
		{
			OppositeSides.Add(&ThinZoneSides[Index]);
		}

		return MoveTemp(OppositeSides);
	};

	// Finalize:
	// Find close segments to the added segments
	for (TArray<FEdgeSegment*>& Side : ThinZoneSides)
	{
		if (!Algo::AllOf(Side, [](const FEdgeSegment* Seg) { return !Seg->HasMarker1(); }))
		{
			const TArray<const TArray<FEdgeSegment*>*> OppositeSides = FindOppositeSides(Side);
			FindCloseSegments(Side, OppositeSides);
		}
	}

#ifdef DEBUG_THIN_ZONES_IMPROVE
	DisplayThinZoneSides(ThinZoneSides);
	Wait();
#endif
}

void FThinZone2DFinder::SplitThinSide()
{
	TArray<TArray<FEdgeSegment*>> NewThinZoneSides;
	FIdent NewSideIndex = ThinZoneSides.Num();

	for (TArray<FEdgeSegment*>& ThinZoneSide : ThinZoneSides)
	{
		if (ThinZoneSide.IsEmpty())
		{
			continue;
		}

		FIdent CloseSideIndex = ThinZoneSide[0]->GetCloseSegment()->GetChainIndex();
		if (!Algo::AllOf(ThinZoneSide, [&](const FEdgeSegment* EdgeSegment) { return EdgeSegment->GetCloseSegment()->GetChainIndex() == CloseSideIndex; }))
		{
			int32 Index = 0;
			for (; Index < ThinZoneSide.Num(); ++Index)
			{
				if (ThinZoneSide[Index]->GetCloseSegment()->GetChainIndex() != CloseSideIndex)
				{
					break;
				}
			}

			const int32 LastIndexFirstSide = Index;
			while (Index < ThinZoneSide.Num())
			{
				CloseSideIndex = ThinZoneSide[Index]->GetCloseSegment()->GetChainIndex();
				TArray<FEdgeSegment*>& NewThinZoneSide = NewThinZoneSides.Emplace_GetRef();
				for (; Index < ThinZoneSide.Num(); ++Index)
				{
					FEdgeSegment* Segment = ThinZoneSide[Index];
					if (Segment->GetCloseSegment()->GetChainIndex() != CloseSideIndex)
					{
						break;
					}
					NewThinZoneSide.Add(Segment);
					Segment->SetChainIndex(NewSideIndex);
				}
				++NewSideIndex;
			}
			ThinZoneSide.SetNum(LastIndexFirstSide);
		}
	}

	for (TArray<FEdgeSegment*>& ThinZoneSide : NewThinZoneSides)
	{
		TArray<FEdgeSegment*>& NewThinZoneSide = ThinZoneSides.Emplace_GetRef();
		NewThinZoneSide = MoveTemp(ThinZoneSide);
	}

}

void FThinZone2DFinder::BuildThinZone()
{
#ifdef DEBUG_BUILD_THIN_ZONES
	ThinZone::DisplayThinZoneSides(ThinZoneSides);
	Wait();
#endif

	// The number of ThinZone should be less than the number of ThinZoneSides, 
	ThinZones.Reserve(ThinZoneSides.Num());

	for (TArray<FEdgeSegment*>& FirstSide : ThinZoneSides)
	{
		if (FirstSide.IsEmpty())
		{
			continue;
		}

		const FEdgeSegment* FirstSideSegment = FirstSide[0];
		TArray<FEdgeSegment*>& SecondSide = ThinZoneSides[FirstSideSegment->GetCloseSegment()->GetChainIndex()];
		if (SecondSide.IsEmpty())
		{
			continue;
		}

		BuildThinZone(FirstSide, SecondSide);

		FirstSide.Empty(); // to avoid to rebuild a second thin zone with SecondSide & FirstSide
		SecondSide.Empty();
	}
}

void FThinZone2DFinder::GetThinZoneSideConnectionsLength(const TArray<FEdgeSegment*>& FirstSide, const TArray<FEdgeSegment*>& SecondSide, double InMaxLength, double* OutLengthBetweenExtremities, TArray<const FTopologicalEdge*>* OutPeakEdges)
{
	TFunction<double(const FEdgeSegment*, const FEdgeSegment*, TFunction<const FEdgeSegment* (const FEdgeSegment*)>, TArray<const FTopologicalEdge*>&)> ComputeLengthBetweenExtremities = 
		[&](const FEdgeSegment* Start, const FEdgeSegment* End, TFunction<const FEdgeSegment* (const FEdgeSegment*)> GetNext, TArray<const FTopologicalEdge*>& PeakEdges) -> double
	{
		PeakEdges.Reserve(10);

		double LengthBetweenExtremities = 0;
		const FEdgeSegment* Segment = GetNext(Start);
		const FTopologicalEdge* Edge = nullptr;

		while (Segment != End)
		{
			if (Edge != Segment->GetEdge())
			{
				Edge = Segment->GetEdge();
				PeakEdges.Add(Edge);
			}

			LengthBetweenExtremities += Segment->GetLength();
			if (LengthBetweenExtremities > InMaxLength)
			{
				LengthBetweenExtremities = HUGE_VALUE;
				break;
			}
			Segment = GetNext(Segment);
		}
		return LengthBetweenExtremities;
	};

	OutLengthBetweenExtremities[0] = ComputeLengthBetweenExtremities(FirstSide[0], SecondSide.Last(), [](const FEdgeSegment* Segment) { return Segment->GetPrevious(); }, OutPeakEdges[0]);
	OutLengthBetweenExtremities[1] = ComputeLengthBetweenExtremities(FirstSide.Last(), SecondSide[0], [](const FEdgeSegment* Segment) { return Segment->GetNext(); }, OutPeakEdges[1]);
}

void FThinZone2DFinder::BuildThinZone(const TArray<FEdgeSegment*>& FirstSide, const TArray<FEdgeSegment*>& SecondSide)
{
	FThinZone2D& Zone = ThinZones.Emplace_GetRef(FirstSide, SecondSide);

	if (!FirstSide[0]->IsInner() && !SecondSide[0]->IsInner())
	{
		const double MaxThickness = Zone.GetMaxThickness();
		double LengthBetweenExtremity[2] = { HUGE_VALUE, HUGE_VALUE };
		TArray<const FTopologicalEdge*> PeakEdges[2];
		GetThinZoneSideConnectionsLength(FirstSide, SecondSide, 3. * MaxThickness, LengthBetweenExtremity, PeakEdges);

		//                     Side 0 
		//       #-------------------------------------# 
		//      /
		//     /  <- LengthBetweendSidesToBePeak  
		//    /
		//   #-----------------------------------------# 
		//                     Side 1 
		//
		// Two EdgeSegments are close if, among other things, the shortest distance between the both EdgeSegments make an angle smallest than 45 deg with the EdgeSegments.
		// So the MaxLengthBetweendSidesToBePeak is theoretically MaxThickness x Sqrt(2)...
		// It's simplified to MaxThickness x 2.
		const double MaxLengthBetweendSidesToBeAPeak = MaxThickness * 2.;
		const double ThinZoneLength = Zone.Length();
		const double MinThinZoneLengthToBeGlobal = ExternalLoopLength - 2. * MaxLengthBetweendSidesToBeAPeak;

		if (ThinZoneLength > MinThinZoneLengthToBeGlobal || (LengthBetweenExtremity[0] < MaxLengthBetweendSidesToBeAPeak && LengthBetweenExtremity[1] < MaxLengthBetweendSidesToBeAPeak))
		{
			Zone.SetCategory(EThinZone2DType::Global);
		}
		else if (LengthBetweenExtremity[0] < MaxLengthBetweendSidesToBeAPeak)
		{
			if (ThinZoneLength < MaxThickness * 5.)
			{
				// The thin zone is too small, it's deleted
				ThinZones.Pop();
				return;

			}
			else
			{
				Zone.SetCategory(EThinZone2DType::PeakStart);
				Zone.SetPeakEdgesMarker(PeakEdges[0]);
			}
		}
		else if (LengthBetweenExtremity[1] < MaxLengthBetweendSidesToBeAPeak)
		{
			if (ThinZoneLength < MaxThickness * 5.)
			{
				// The thin zone is too small, it's deleted
				ThinZones.Pop();
				return;

			}
			else
			{
				Zone.SetCategory(EThinZone2DType::PeakEnd);
				Zone.SetPeakEdgesMarker(PeakEdges[1]);
			}
		}
		else
		{
			Zone.SetCategory(EThinZone2DType::Butterfly);
		}

	}
	else
	{
		Zone.SetCategory(EThinZone2DType::BetweenLoops);
	}
}

bool FThinZone2DFinder::SearchThinZones(double InTolerance)
{
#ifdef DEBUG_THIN_ZONES
	F3DDebugSession A(bDisplay, FString::Printf(TEXT("ThinZone Finder %d"), Grid.GetFace().GetId()));
	if (bDisplay)
	{
		F3DDebugSession _(TEXT("Thin surface grid"));
		Grid.DisplayGridPoints(EGridSpace::UniformScaled);
	}
#endif

	SetTolerance(InTolerance);

	//FMessage::Printf(DBG, TEXT("Searching thin zones on Surface %d\n", Surface->GetId());
	FTimePoint StartTime = FChrono::Now();
	if (!BuildLoopSegments())
	{
		Face.SetAsDegenerated();
		return false;
	}
	Chronos.BuildLoopSegmentsTime = FChrono::Elapse(StartTime);

#ifdef DEBUG_THIN_ZONES
	DisplayLoopSegments();
#endif

	StartTime = FChrono::Now();
	FindCloseSegments();
	Chronos.FindCloseSegmentTime = FChrono::Elapse(StartTime);

#ifdef DEBUG_SEARCH_THIN_ZONES
	if (bDisplay)
	{
		DisplayCloseSegments();
		Wait();
	}
#endif

	StartTime = FChrono::Now();
	LinkCloseSegments();

#ifdef DEBUG_SEARCH_THIN_ZONES
	if(bDisplay)
	{
		ThinZone::DisplayThinZoneSides(ThinZoneSides);
	}
#endif

	ImproveThinSide();

#ifdef DEBUG_SEARCH_THIN_ZONES
	if (bDisplay)
	{
		ThinZone::DisplayThinZoneSides(ThinZoneSides);
		Wait();
	}
#endif

	SplitThinSide();

#ifdef DEBUG_SEARCH_THIN_ZONES
	if (bDisplay)
	{
		ThinZone::DisplayThinZoneSides(ThinZoneSides);
		Wait();
	}
#endif
	Chronos.LinkCloseSegmentTime = FChrono::Elapse(StartTime);

	StartTime = FChrono::Now();
	BuildThinZone();
	Chronos.BuildThinZoneTime = FChrono::Elapse(StartTime);

#ifdef DEBUG_THIN_ZONES
	if(bDisplay)
	{
		ThinZone::DisplayThinZones(ThinZones);
		Wait(false);
	}
#endif

	return (ThinZones.Num() > 0);
}

bool FThinZone2DFinder::BuildLoopSegments()
{
	const double GeometricTolerance = Grid.GetTolerance();
	const double WishedSegmentLength = FinderTolerance / 5.;
	const TArray<TSharedPtr<FTopologicalLoop>>& Loops = Face.GetLoops();

	{
		double Length = 0;
		ExternalLoopLength = -1.;
		for (const TSharedPtr<FTopologicalLoop>& Loop : Loops)
		{
			const double LoopLength = Loop->Length();
			Length += LoopLength;

			if (Loop->IsExternal())
			{
				ExternalLoopLength = LoopLength;
			}
		}

		const int32 SegmentNum = (int32)(1.2 * Length / WishedSegmentLength);
		LoopSegments.Empty(SegmentNum);
	}

	const TSharedPtr<FTopologicalLoop> OuterLoop = Loops[0];
	for (const TSharedPtr<FTopologicalLoop>& Loop : Loops)
	{
		const bool bIsInnerLoop = (Loop != OuterLoop);
		const TArray<FOrientedEdge>& Edges = Loop->GetEdges();

		FEdgeSegment* FirstSegment = nullptr;
		FEdgeSegment* PrecedingSegment = nullptr;

		for (const FOrientedEdge& Edge : Edges)
		{
			TArray<double> Coordinates;
			Edge.Entity->Sample(WishedSegmentLength, Coordinates);

			TArray<FPoint2D> ScaledPoints;
			{
				TArray<FPoint2D> Points;
				Edge.Entity->Approximate2DPoints(Coordinates, Points);
				Grid.TransformPoints(EGridSpace::UniformScaled, Points, ScaledPoints);
			}

			// Remove duplicated points (vs GeometricTolerance) of the end
			{
				int32 EndPointIndex = ScaledPoints.Num() - 1;
				while (EndPointIndex > 0 && ScaledPoints[EndPointIndex].Distance(ScaledPoints[EndPointIndex - 1]) < GeometricTolerance)
				{
					EndPointIndex--;
					ScaledPoints.RemoveAt(EndPointIndex);
					Coordinates.RemoveAt(EndPointIndex);
				}

				if (ScaledPoints.IsEmpty())
				{
					continue;
				}
			}

			// Remove duplicated points (vs GeometricTolerance)
			{
				for (int32 PointIndex = 1; PointIndex < ScaledPoints.Num(); PointIndex++)
				{
					while (ScaledPoints[PointIndex - 1].Distance(ScaledPoints[PointIndex]) < GeometricTolerance)
					{
						ScaledPoints.RemoveAt(PointIndex);
						Coordinates.RemoveAt(PointIndex);
					}
				}
			}

			const int32 LastPointIndex = ScaledPoints.Num() - 1;

			const int32 Increment = (Edge.Direction == EOrientation::Front) ? 1 : -1;
			int32 ISegment2 = (Edge.Direction == EOrientation::Front) ? 1 : LastPointIndex - 1;

			for (; ISegment2 >= 0 && ISegment2 <= LastPointIndex; ISegment2 += Increment)
			{
				const int32 ISegment1 = ISegment2 - Increment;
				FEdgeSegment& CurrentSeg = SegmentFatory.New();
				CurrentSeg.SetBoundarySegment(bIsInnerLoop, Edge.Entity.Get(), Coordinates[ISegment1], Coordinates[ISegment2], ScaledPoints[ISegment1], ScaledPoints[ISegment2]);

				LoopSegments.Add(&CurrentSeg);
				if (!FirstSegment)
				{
					FirstSegment = PrecedingSegment = &CurrentSeg;
				}
				else
				{
					PrecedingSegment->SetNext(&CurrentSeg);
				}
				PrecedingSegment = &CurrentSeg;
			}
		}

		if(PrecedingSegment)
		{
			PrecedingSegment->SetNext(FirstSegment);
		}

		if (!bIsInnerLoop && LoopSegments.Num() < 2)
		{
			return false;
		}
	}

	return true;
}

FThinZoneSide::FThinZoneSide(FThinZoneSide* InFrontSide, const TArray<FEdgeSegment*>& InSegments)
	: FrontSide(*InFrontSide)
	, SideLength(-1)
	, MediumThickness(-1)
	, MaxThickness(-1)
{
	const int32 SegmentCount = InSegments.Num();
	Segments.Reserve(SegmentCount);
	for (const FEdgeSegment* Segment : InSegments)
	{
		Segments.Emplace(*Segment);
	}
}

void FThinZone2D::Finalize()
{
	TFunction<void(TArray<FEdgeSegment>&, TMap<int32, FEdgeSegment*>&)> BuildMap = [](TArray<FEdgeSegment>& Segments, TMap<int32, FEdgeSegment*>& Map)
	{
		for (FEdgeSegment& Segment : Segments)
		{
			Map.Emplace(Segment.GetId(), &Segment);
		}
	};

	TFunction<void(TArray<FEdgeSegment>&, TMap<int32, FEdgeSegment*>&)> UpdateReference = [](TArray<FEdgeSegment>& Segments, TMap<int32, FEdgeSegment*>& Map)
	{
		for (FEdgeSegment& Segment : Segments)
		{
			Segment.UpdateReferences(Map);
		}
	};

	TMap<int32, FEdgeSegment*> NewSegmentMap;

	TArray<FEdgeSegment>& FirstSideSegments = FirstSide.GetSegments();
	TArray<FEdgeSegment>& SecondSideSegments = SecondSide.GetSegments();
	BuildMap(FirstSideSegments, NewSegmentMap);
	BuildMap(SecondSideSegments, NewSegmentMap);

	UpdateReference(FirstSideSegments, NewSegmentMap);
	UpdateReference(SecondSideSegments, NewSegmentMap);

	FirstSide.ComputeThicknessAndLength();
	SecondSide.ComputeThicknessAndLength();

	Thickness = (FirstSide.GetThickness() + SecondSide.GetThickness()) * 0.5;
	MaxThickness = FMath::Max(FirstSide.GetMaxThickness(), SecondSide.GetMaxThickness());
}

void FThinZoneSide::ComputeThicknessAndLength()
{
	SideLength = 0;
	double SquareMediumThickness = 0;
	double SquareMaxThickness = 0;

	for (const FEdgeSegment& Segment : Segments)
	{
		const double SquareThickness = Segment.GetCloseSquareDistance();
		const double SegmentLength = Segment.GetLength();
		SideLength += SegmentLength;
		SquareMediumThickness += SquareThickness * SegmentLength;
		if (SquareMaxThickness < SquareThickness)
		{
			SquareMaxThickness = SquareThickness;
		}
	}

	SquareMediumThickness /= SideLength;
	MediumThickness = FMath::Sqrt(SquareMediumThickness);
	MaxThickness = FMath::Sqrt(SquareMaxThickness);
}

void FThinZoneSide::SetEdgesAsThinZone()
{
	FTopologicalEdge* Edge = nullptr;
	for (FEdgeSegment& Segment : Segments)
	{
		if (Edge != Segment.GetEdge())
		{
			Edge = Segment.GetEdge();
			Edge->SetThinZoneMarker();
		}
	}
}

void FThinZone2D::SetPeakEdgesMarker(const TArray<const FTopologicalEdge*>& PeakEdges)
{
	for (const FTopologicalEdge* Edge : PeakEdges)
	{
		Edge->SetThinPeakMarker();
	}
}

EMeshingState FThinZoneSide::GetMeshingState() const
{
	EMeshingState MeshingState = NotMeshed;
	int32 EdgeCount = 0;
	int32 MeshedEdgeCount = 0;

	const FTopologicalEdge* Edge = nullptr;
	for (const FEdgeSegment& EdgeSegment : Segments)
	{
		if (Edge != EdgeSegment.GetEdge())
		{
			Edge = EdgeSegment.GetEdge();
			++EdgeCount;
			if (Edge->GetLinkActiveEdge()->IsMeshed())
			{
				++MeshedEdgeCount;
			}
		}
	}

	if (MeshedEdgeCount == 0)
	{
		return NotMeshed;
	}
	else if (MeshedEdgeCount != EdgeCount)
	{
		return PartiallyMeshed;
	}
	return FullyMeshed;
}

void FThinZone2D::SetEdgesAsThinZone()
{
	FirstSide.SetEdgesAsThinZone();
	SecondSide.SetEdgesAsThinZone();
}

}

