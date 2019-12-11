// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once
#include "Chaos/Simplex.h"
#include <queue>

namespace Chaos
{

template <typename T>
FORCEINLINE const TVec3<T> MinkowskiVert(const TVec3<T>* VertsABuffer, const TVec3<T>* VertsBBuffer, const int32 Idx)
{
	return VertsABuffer[Idx] - VertsBBuffer[Idx];
}

template <typename T>
struct TEPAEntry
{
	int32 IdxBuffer[3];

	TVec3<T> PlaneNormal;	//Triangle normal
	T Distance;	//Triangle distance from origin
	TVector<int32,3> AdjFaces;	//Adjacent triangles
	TVector<int32,3> AdjEdges;	//Adjacent edges (idx in adjacent face)
	bool bObsolete;	//indicates that an entry can be skipped (became part of bigger polytope)

	bool operator>(const TEPAEntry<T>& Other) const
	{
		return Distance > Other.Distance;
	}

	bool Initialize(const TVec3<T>* VerticesA, const TVec3<T>* VerticesB, int32 InIdx0, int32 InIdx1, int32 InIdx2, const TVector<int32,3>& InAdjFaces, const TVector<int32,3>& InAdjEdges)
	{
		const TVec3<T>& V0 = MinkowskiVert(VerticesA, VerticesB, InIdx0);
		const TVec3<T>& V1 = MinkowskiVert(VerticesA, VerticesB, InIdx1);
		const TVec3<T>& V2 = MinkowskiVert(VerticesA, VerticesB, InIdx2);

		const TVec3<T> V0V1 = V1 - V0;
		const TVec3<T> V0V2 = V2 - V0;
		const TVec3<T> Norm = TVec3<T>::CrossProduct(V0V1, V0V2);
		PlaneNormal = Norm.GetSafeNormal();
		constexpr T Eps = 1e-4;
		if (PlaneNormal.SizeSquared() < Eps)
		{
			return false;
		}
		
		IdxBuffer[0] = InIdx0;
		IdxBuffer[1] = InIdx1;
		IdxBuffer[2] = InIdx2;

		AdjFaces = InAdjFaces;
		AdjEdges = InAdjEdges;

		Distance = TVec3<T>::DotProduct(PlaneNormal, V0);
		bObsolete = false;

		return true;
	}

	void SwapWinding(TEPAEntry* Entries)
	{
		//change vertex order
		std::swap(IdxBuffer[0], IdxBuffer[1]);

		//edges went from 0,1,2 to 1,0,2
		//0th edge/face is the same (0,1 becomes 1,0)
		//1th edge/face is now (0,2 instead of 1,2)
		//2nd edge/face is now (2,1 instead of 2,0)

		//update the adjacent face's adjacent edge first
		auto UpdateAdjEdge = [Entries, this](int32 Old, int32 New)
		{
			TEPAEntry& AdjFace = Entries[AdjFaces[Old]];
			int32& StaleAdjIdx = AdjFace.AdjEdges[AdjEdges[Old]];
			check(StaleAdjIdx == Old);
			StaleAdjIdx = New;
		};
		
		UpdateAdjEdge(1, 2);
		UpdateAdjEdge(2, 1);
		
		//now swap the actual edges and faces
		std::swap(AdjFaces[1], AdjFaces[2]);
		std::swap(AdjEdges[1], AdjEdges[2]);

		PlaneNormal = -PlaneNormal;
		Distance = -Distance;
	}

	T DistanceToPlane(const TVec3<T>& X) const
	{
		return TVec3<T>::DotProduct(PlaneNormal, X) - Distance;
	}

	bool IsOriginProjectedInside(const TVec3<T>* VertsABuffer, const TVec3<T>* VertsBBuffer) const
	{
		//Compare the projected point (PlaneNormal) to the triangle in the plane
		const TVec3<T> PA = MinkowskiVert(VertsABuffer, VertsBBuffer, IdxBuffer[0]) - PlaneNormal;
		const TVec3<T> PB = MinkowskiVert(VertsABuffer, VertsBBuffer, IdxBuffer[1]) - PlaneNormal;
		const TVec3<T> PC = MinkowskiVert(VertsABuffer, VertsBBuffer, IdxBuffer[2]) - PlaneNormal;

		const TVec3<T> PACNormal = TVec3<T>::CrossProduct(PA, PC);
		const T PACSign = TVec3<T>::DotProduct(PACNormal, PlaneNormal);
		const TVec3<T> PCBNormal = TVec3<T>::CrossProduct(PC, PB);
		const T PCBSign = TVec3<T>::DotProduct(PCBNormal, PlaneNormal);

		if((PACSign < 0 && PCBSign > 0) || (PACSign > 0 && PCBSign < 0))
		{
			return false;
		}

		const TVec3<T> PBANormal = TVec3<T>::CrossProduct(PB, PA);
		const T PBASign = TVec3<T>::DotProduct(PBANormal, PlaneNormal);

		if((PACSign < 0 && PBASign > 0) || (PACSign > 0 && PBASign < 0))
		{
			return false;
		}

		return true;
	}
};

template <typename T, typename SupportALambda, typename SupportBLambda >
TArray<TEPAEntry<T>> InitializeEPA(TArray<TVec3<T>>& VertsA, TArray<TVec3<T>>& VertsB, const SupportALambda& SupportA, const SupportBLambda& SupportB)
{
	TArray<TEPAEntry<T>> Entries;
	const int32 NumVerts = VertsA.Num();
	check(VertsB.Num() == NumVerts);

	auto AddFartherPoint = [&](const TVec3<T>& Dir)
	{
		const TVec3<T> NegDir = -Dir;
		const TVec3<T> A0 = SupportA(Dir);	//should we have a function that does both directions at once?
		const TVec3<T> A1 = SupportA(NegDir);
		const TVec3<T> B0 = SupportB(NegDir);
		const TVec3<T> B1 = SupportB(Dir);

		const TVec3<T> W0 = A0 - B0;
		const TVec3<T> W1 = A1 - B1;

		const T Dist0 = TVec3<T>::DotProduct(W0, Dir);
		const T Dist1 = TVec3<T>::DotProduct(W1, NegDir);

		if (Dist1 >= Dist0)
		{
			VertsA.Add(A1);
			VertsB.Add(B1);
		}
		else
		{
			VertsA.Add(A0);
			VertsB.Add(B0);
		}
	};

	Entries.AddUninitialized(4);

	bool bValid = false;

	switch(NumVerts)
	{
		case 1:
		{
			//assuming it's a touching hit at origin
			break;
		}
		case 2:
		{
			//line, add farthest point along most orthogonal axes
			TVec3<T> Dir = MinkowskiVert(VertsA.GetData(), VertsB.GetData(), 1) - MinkowskiVert(VertsA.GetData(), VertsB.GetData(), 0);

			bValid = Dir.SizeSquared() > 1e-4;
			if (ensure(bValid))	//two verts given should be distinct
			{
				//find most opposing axis
				int32 BestAxis = 0;
				T MinVal = TNumericLimits<T>::Max();
				for (int32 Axis = 0; Axis < 3; ++Axis)
				{
					const T AbsVal = FMath::Abs(Dir[Axis]);
					if (MinVal > AbsVal)
					{
						BestAxis = Axis;
						MinVal = AbsVal;
					}
				}
				const TVec3<T> OtherAxis = TVec3<T>::AxisVector(BestAxis);
				const TVec3<T> Orthog = TVec3<T>::CrossProduct(Dir, OtherAxis);
				const TVec3<T> Orthog2 = TVec3<T>::CrossProduct(Orthog, Dir);

				AddFartherPoint(Orthog);
				AddFartherPoint(Orthog2);

				bValid = Entries[0].Initialize(VertsA.GetData(), VertsB.GetData(), 1, 2, 3, { 3, 1, 2 }, { 1,1, 1 });
				bValid &= Entries[1].Initialize(VertsA.GetData(), VertsB.GetData(), 0, 3, 2, { 2,0,3 }, { 2, 1, 0 });
				bValid &= Entries[2].Initialize(VertsA.GetData(), VertsB.GetData(), 0, 1, 3, { 3,0, 1 }, { 2,2,0 });
				bValid &= Entries[3].Initialize(VertsA.GetData(), VertsB.GetData(), 0, 2, 1, { 1,0,2 }, { 2,0,0 });
			}
			break;
		}
		case 3:
		{
			//triangle, add farthest point along normal
			bValid = Entries[3].Initialize(VertsA.GetData(), VertsB.GetData(), 0, 2, 1, { 1,0,2 }, { 2,0,0 });	
			if (ensure(bValid)) //input verts must form a valid triangle
			{
				const TEPAEntry<T>& Base = Entries[3];

				AddFartherPoint(Base.PlaneNormal);

				bValid = Entries[0].Initialize(VertsA.GetData(), VertsB.GetData(), 1, 2, 3, { 3, 1, 2 }, { 1,1, 1 });
				bValid &= Entries[1].Initialize(VertsA.GetData(), VertsB.GetData(), 0, 3, 2, { 2,0,3 }, { 2, 1, 0 });
				bValid &= Entries[2].Initialize(VertsA.GetData(), VertsB.GetData(), 0, 1, 3, { 3,0, 1 }, { 2,2,0 });
			}
			break;
		}
		case 4:
		{
			bValid = Entries[0].Initialize(VertsA.GetData(), VertsB.GetData(), 1, 2, 3, { 3, 1, 2 }, { 1,1, 1 });
			bValid &= Entries[1].Initialize(VertsA.GetData(), VertsB.GetData(), 0, 3, 2, { 2,0,3 }, { 2, 1, 0 });
			bValid &= Entries[2].Initialize(VertsA.GetData(), VertsB.GetData(), 0, 1, 3, { 3,0, 1 }, { 2,2,0 });
			bValid &= Entries[3].Initialize(VertsA.GetData(), VertsB.GetData(), 0, 2, 1, { 1,0,2 }, { 2,0,0 });
			ensure(bValid);	//expect user to give us valid tetrahedron
			break;
		}

		default: ensure(false);
	}

	if (bValid)
	{
		//make sure normals are pointing out of tetrahedron
		if (TVec3<T>::DotProduct(Entries[0].PlaneNormal, MinkowskiVert(VertsA.GetData(), VertsB.GetData(), 0)) > 0)
		{
			for (TEPAEntry<T>& Entry : Entries)
			{
				Entry.SwapWinding(Entries.GetData());
			}
		}
	}
	else
	{
		Entries.SetNum(0);
	}

	return Entries;
}

struct FEPAFloodEntry
{
	int32 EntryIdx;
	int32 EdgeIdx;
};

template <typename T>
void EPAComputeVisibilityBorder(TArray<TEPAEntry<T>>& Entries, int32 EntryIdx, const TVec3<T>& W, TArray<FEPAFloodEntry>& OutBorderEdges)
{
	TArray<FEPAFloodEntry> ToVisitStack;
	{
		TEPAEntry<T>& Entry = Entries[EntryIdx];
		for (int i = 0; i < 3; ++i)
		{
			ToVisitStack.Add({ Entry.AdjFaces[i], Entry.AdjEdges[i] });
		}
	}

	while (ToVisitStack.Num())
	{
		const FEPAFloodEntry FloodEntry = ToVisitStack.Pop(false);
		TEPAEntry<T>& Entry = Entries[FloodEntry.EntryIdx];
		if (!Entry.bObsolete)
		{
			if (Entry.DistanceToPlane(W) < 0)
			{
				//W can't see this triangle so mark the edge as a border
				OutBorderEdges.Add(FloodEntry);
			}
			else
			{
				//W can see this triangle so continue flood fill
				Entry.bObsolete = true;	//no longer needed
				const int32 Idx0 = FloodEntry.EdgeIdx;
				const int32 Idx1 = (Idx0 + 1) % 3;
				const int32 Idx2 = (Idx0 + 2) % 3;
				ToVisitStack.Add({ Entry.AdjFaces[Idx1], Entry.AdjEdges[Idx1] });
				ToVisitStack.Add({ Entry.AdjFaces[Idx2], Entry.AdjEdges[Idx2] });
			}
		}
	}
}

template <typename T>
void ComputeEPAResults(const TVec3<T>* VertsA, const TVec3<T>* VertsB, const TEPAEntry<T>& Entry, T& OutPenetration, TVec3<T>& OutDir, TVec3<T>& OutA, TVec3<T>& OutB)
{
	//NOTE: We use this function as fallback when robustness breaks. So - do not assume adjacency is valid as these may be new uninitialized traingles that failed
	FSimplex SimplexIDs({ 0,1,2 });
	TVec3<T> As[4] = { VertsA[Entry.IdxBuffer[0]], VertsA[Entry.IdxBuffer[1]], VertsA[Entry.IdxBuffer[2]] };
	TVec3<T> Bs[4] = { VertsB[Entry.IdxBuffer[0]], VertsB[Entry.IdxBuffer[1]], VertsB[Entry.IdxBuffer[2]] };
	TVec3<T> Simplex[4] = { As[0] - Bs[0], As[1] - Bs[1], As[2] - Bs[2] };
	T Barycentric[4];

	OutDir = SimplexFindClosestToOrigin(Simplex, SimplexIDs, Barycentric, As, Bs);
	OutPenetration = OutDir.Size();

	if (OutPenetration < 1e-4)	//if closest point is on the origin (edge case when surface is right on the origin)
	{
		OutDir = Entry.PlaneNormal;	//just fall back on plane normal
	}
	else
	{
		OutDir /= OutPenetration;
	}

	OutA = TVec3<T>(0);
	OutB = TVec3<T>(0);

	for (int i = 0; i < SimplexIDs.NumVerts; ++i)
	{
		OutA += As[i] * Barycentric[i];
		OutB += Bs[i] * Barycentric[i];
	}
}

enum class EPAResult
{
	Ok,
	MaxIterations,
	BadInitialSimplex
};

#ifndef DEBUG_EPA
#define DEBUG_EPA 0
#endif

template <typename T, typename SupportALambda, typename SupportBLambda>
EPAResult EPA(TArray<TVec3<T>>& VertsABuffer, TArray<TVec3<T>>& VertsBBuffer, const SupportALambda& SupportA, const SupportBLambda& SupportB, T& OutPenetration, TVec3<T>& OutDir, TVec3<T>& WitnessA, TVec3<T>& WitnessB)
{
	struct FEPAEntryWrapper
	{
		const TArray<TEPAEntry<T>>* Entries;		
		int32 Idx;

		bool operator>(const FEPAEntryWrapper& Other) const
		{
			return (*Entries)[Idx] > (*Entries)[Other.Idx];
		}
	};

	T UpperBound = TNumericLimits<T>::Max();
	T LowerBound = TNumericLimits<T>::Lowest();

	constexpr T Eps = 1e-2;

	TArray<TEPAEntry<T>> Entries = InitializeEPA(VertsABuffer, VertsBBuffer, SupportA, SupportB);

	if (Entries.Num() < 4)
	{
		//either degenerate or a touching hit. Either way return penetration 0
		OutPenetration = 0;
		OutDir = TVec3<T>(0, 0, 1);
		WitnessA = TVec3<T>(0);
		WitnessB = TVec3<T>(0);
		return EPAResult::BadInitialSimplex;
	}


#if DEBUG_EPA
	TArray<TVec3<T>> VertsWBuffer;
	for (int32 Idx = 0; Idx < 4; ++Idx)
	{
		VertsWBuffer.Add(MinkowskiVert(VertsABuffer.GetData(), VertsBBuffer.GetData(), Idx));
	}
#endif
	
	std::priority_queue<FEPAEntryWrapper, std::vector<FEPAEntryWrapper>, std::greater<FEPAEntryWrapper>> Queue;
	for(int32 Idx = 0; Idx < Entries.Num(); ++Idx)
	{
		//ensure(Entries[Idx].Distance > -Eps);
		if(Entries[Idx].IsOriginProjectedInside(VertsABuffer.GetData(), VertsBBuffer.GetData()))
		{
			Queue.push(FEPAEntryWrapper {&Entries, Idx});
		}
	}

	
	//TEPAEntry<T> BestEntry;
	//BestEntry.Distance = 0;

	TEPAEntry<T> LastEntry = Entries[0];

	TArray<FEPAFloodEntry> VisibilityBorder;
	int32 Iteration = 0;
	int32 constexpr MaxIterations = 128;
	while(Queue.size() && Iteration++ < MaxIterations)
	{
		int32 EntryIdx = Queue.top().Idx;
		Queue.pop();
		TEPAEntry<T>& Entry = Entries[EntryIdx];
		//bool bBadFace = Entry.IsOriginProjectedInside(VertsABuffer.GetData(), VertsBBuffer.GetData());
		{
			//UE_LOG(LogChaos, Warning, TEXT("%d BestW:%f, Distance:%f, bObsolete:%d, InTriangle:%d"),
			//	Iteration, BestW.Size(), Entry.Distance, Entry.bObsolete, bBadFace);
		}
		if (Entry.bObsolete)
		{
			continue;
		}

		LastEntry = Entry;


		const TVec3<T> ASupport = SupportA(Entry.PlaneNormal);
		const TVec3<T> BSupport = SupportB(-Entry.PlaneNormal);
		const TVec3<T> W = ASupport - BSupport;
		const T DistanceToSupportPlane = TVec3<T>::DotProduct(Entry.PlaneNormal, W);
		//ensure(DistanceToSupportPlane > -Eps);
		if(DistanceToSupportPlane < UpperBound)
		{
			UpperBound = DistanceToSupportPlane;
			//Remember the entry that gave us the lowest upper bound and use it in case we have to terminate early
			//This can result in very deep planes. Ideally we'd just use the plane formed at W, but it's not clear how you get back points in A, B for that
			//BestEntry = Entry;
		}

		LowerBound = Entry.Distance;

		//It's possible the origin is not contained by the CSO. In this case the upper bound will be negative, at which point we should just exit. Maybe return a different enum value?
		if ((UpperBound - LowerBound) <= FMath::Abs(Eps * LowerBound))
		{
			//UE_LOG(LogChaos, Warning, TEXT("Iteration:%d"), Iteration);
			ComputeEPAResults(VertsABuffer.GetData(), VertsBBuffer.GetData(), Entry, OutPenetration, OutDir, WitnessA, WitnessB);
			return EPAResult::Ok;
		}

		const int32 NewVertIdx = VertsABuffer.Add(ASupport);
		VertsBBuffer.Add(BSupport);

#if DEBUG_EPA
		VertsWBuffer.Add(MinkowskiVert(VertsABuffer.GetData(), VertsBBuffer.GetData(), NewVertIdx));
#endif

		Entry.bObsolete = true;
		VisibilityBorder.Reset();
		EPAComputeVisibilityBorder(Entries, EntryIdx, W, VisibilityBorder);
		const int32 NumBorderEdges = VisibilityBorder.Num();
		const int32 FirstIdxInBatch = Entries.Num();
		int32 NewIdx = FirstIdxInBatch;
		Entries.AddUninitialized(NumBorderEdges);
		if (NumBorderEdges >= 3)
		{
			bool bTerminate = false;
			for (int32 VisibilityIdx = 0; VisibilityIdx < NumBorderEdges; ++VisibilityIdx)
			{
				//create new entries and update adjacencies
				const FEPAFloodEntry& BorderInfo = VisibilityBorder[VisibilityIdx];
				TEPAEntry<T>& NewEntry = Entries[NewIdx];
				const int32 BorderEntryIdx = BorderInfo.EntryIdx;
				TEPAEntry<T>& BorderEntry = Entries[BorderEntryIdx];
				const int32 BorderEdgeIdx0 = BorderInfo.EdgeIdx;
				const int32 BorderEdgeIdx1 = (BorderEdgeIdx0 + 1) % 3;
				const int32 NextEntryIdx = (VisibilityIdx + 1) < VisibilityBorder.Num() ? NewIdx + 1 : FirstIdxInBatch;
				const int32 PrevEntryIdx = NewIdx > FirstIdxInBatch ? NewIdx - 1 : FirstIdxInBatch + NumBorderEdges - 1;
				const bool bValidTri = NewEntry.Initialize(VertsABuffer.GetData(), VertsBBuffer.GetData(), BorderEntry.IdxBuffer[BorderEdgeIdx1], BorderEntry.IdxBuffer[BorderEdgeIdx0], NewVertIdx,
					{ BorderEntryIdx, PrevEntryIdx, NextEntryIdx },
					{ BorderEdgeIdx0, 2, 1 });
				BorderEntry.AdjFaces[BorderEdgeIdx0] = NewIdx;
				BorderEntry.AdjEdges[BorderEdgeIdx0] = 0;
				
				if (!bValidTri)
				{
					//couldn't properly expand polytope, so just stop
					bTerminate = true;
					break;
				}

				//We should never need to check the lower bound, but in the case of bad precision this can happen
				//We simply ignore this direction as it likely has even more bad precision
				if (bValidTri && NewEntry.Distance >= LowerBound && NewEntry.Distance <= UpperBound)
				{
					if (NewEntry.IsOriginProjectedInside(VertsABuffer.GetData(), VertsBBuffer.GetData()))
					{
						Queue.push(FEPAEntryWrapper{ &Entries, NewIdx });
					}
				}

				++NewIdx;
			}

			if (bTerminate)
			{
				break;
			}
		}
		else
		{
			//couldn't properly expand polytope, just stop now
			break;
		}
	}

	ComputeEPAResults(VertsABuffer.GetData(), VertsBBuffer.GetData(), LastEntry, OutPenetration, OutDir, WitnessA, WitnessB);
	
	return EPAResult::MaxIterations;
}
}
