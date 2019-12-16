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

		if(PACSign < 0 && PCBSign > 0 || PACSign > 0 && PCBSign < 0)
		{
			return false;
		}

		const TVec3<T> PBANormal = TVec3<T>::CrossProduct(PB, PA);
		const T PBASign = TVec3<T>::DotProduct(PBANormal, PlaneNormal);

		if(PACSign < 0 && PBASign > 0 || PACSign > 0 && PBASign < 0)
		{
			return false;
		}

		return true;
	}
};

template <typename T>
TArray<TEPAEntry<T>> InitializeEPA(const TVec3<T>* VertsA, const TVec3<T>* VertsB, const int32 NumVerts)
{
	TArray<TEPAEntry<T>> Entries;
	switch(NumVerts)
	{
		case 4:
		{
			//make sure all triangle normals are facing out
			Entries.AddUninitialized(4);

			ensure(Entries[0].Initialize(VertsA, VertsB, 1, 2, 3, { 3, 1, 2 }, { 1,1, 1 }));
			ensure(Entries[1].Initialize(VertsA, VertsB, 0, 3, 2, { 2,0,3 }, { 2, 1, 0 }));
			ensure(Entries[2].Initialize(VertsA, VertsB, 0, 1, 3, { 3,0, 1 }, { 2,2,0 }));
			ensure(Entries[3].Initialize(VertsA, VertsB, 0, 2, 1, { 1,0,2 }, { 2,0,0 }));

			if (TVec3<T>::DotProduct(Entries[0].PlaneNormal, MinkowskiVert(VertsA, VertsB, 0)) > 0)
			{
				//tet faces are pointing inwards
				for (TEPAEntry<T>& Entry : Entries)
				{
					Entry.SwapWinding(Entries.GetData());
				}
			}

			return Entries;
		}

		default: ensure(false);	return Entries;	//todo: handle other cases
	}
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

enum EPAResult
{
	Ok,
	MaxIterations,
};

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

	TArray<TEPAEntry<T>> Entries = InitializeEPA(VertsABuffer.GetData(), VertsBBuffer.GetData(), VertsABuffer.Num());
	std::priority_queue<FEPAEntryWrapper, std::vector<FEPAEntryWrapper>, std::greater<FEPAEntryWrapper>> Queue;
	for(int32 Idx = 0; Idx < Entries.Num(); ++Idx)
	{
		if(Entries[Idx].IsOriginProjectedInside(VertsABuffer.GetData(), VertsBBuffer.GetData()))
		{
			Queue.push(FEPAEntryWrapper {&Entries, Idx});
		}
	}

	bool bCloseEnough = false;
	bool bTerminate = bCloseEnough;
	
	//TEPAEntry<T> BestEntry;
	//BestEntry.Distance = 0;

	TEPAEntry<T> LastEntry;
	LastEntry.Distance = 0;
	LastEntry.PlaneNormal = TVec3<T>(0,0,1);

	TArray<FEPAFloodEntry> VisibilityBorder;
	int32 Iteration = 0;
	int32 constexpr MaxIterations = 128;
	while(Queue.size() && Iteration++ < MaxIterations)
	{
		int32 EntryIdx = Queue.top().Idx;
		Queue.pop();
		TEPAEntry<T>& Entry = Entries[EntryIdx];
		bool bBadFace = Entry.IsOriginProjectedInside(VertsABuffer.GetData(), VertsBBuffer.GetData());
		{
			//UE_LOG(LogChaos, Warning, TEXT("%d BestW:%f, Distance:%f, bObsolete:%d, InTriangle:%d"),
			//	Iteration, BestW.Size(), Entry.Distance, Entry.bObsolete, bBadFace);
		}
		if (Entry.bObsolete)
		{
			continue;
		}

		LastEntry = Entry;

		constexpr T Eps = 1e-2;

		const TVec3<T> ASupport = SupportA(Entry.PlaneNormal);
		const TVec3<T> BSupport = SupportB(-Entry.PlaneNormal);
		const TVec3<T> W = ASupport - BSupport;
		const T DistanceToSupportPlane = TVec3<T>::DotProduct(Entry.PlaneNormal, W);

		if(DistanceToSupportPlane < UpperBound)
		{
			UpperBound = DistanceToSupportPlane;
			//Remember the entry that gave us the lowest upper bound and use it in case we have to terminate early
			//This can result in very deep planes. Ideally we'd just use the plane formed at W, but it's not clear how you get back points in A, B for that
			//BestEntry = Entry;
		}

		LowerBound = Entry.Distance;

		if (FMath::Abs(UpperBound - LowerBound) < Eps)	//todo: add relative error?
		{
			//UE_LOG(LogChaos, Warning, TEXT("Iteration:%d"), Iteration);
			OutPenetration = Entry.Distance;
			OutDir = Entry.PlaneNormal;
			return EPAResult::Ok;
		}

		const int32 NewVertIdx = VertsABuffer.Add(ASupport);
		VertsBBuffer.Add(BSupport);

		Entry.bObsolete = true;
		VisibilityBorder.Reset();
		EPAComputeVisibilityBorder(Entries, EntryIdx, W, VisibilityBorder);
		const int32 NumBorderEdges = VisibilityBorder.Num();
		const int32 FirstIdxInBatch = Entries.Num();
		int32 NewIdx = FirstIdxInBatch;
		Entries.AddUninitialized(NumBorderEdges);
		ensure(NumBorderEdges >= 3);
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

			//We should never need to check the lower bound, but in the case of bad precision this can happen
			//We simply ignore this direction as it likely has even more bad precision
			if (bValidTri && NewEntry.Distance >= LowerBound && NewEntry.Distance <= UpperBound)
			{
				if(NewEntry.IsOriginProjectedInside(VertsABuffer.GetData(), VertsBBuffer.GetData()))
				{
					Queue.push(FEPAEntryWrapper{ &Entries, NewIdx});
				}
			}

			++NewIdx;
		}
	}

	OutPenetration = LastEntry.Distance;
	OutDir = LastEntry.PlaneNormal;
	return EPAResult::MaxIterations;
}
}
