// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once
#include "Chaos/Simplex.h"
#include <queue>

namespace Chaos
{

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

	bool Initialize(const TVec3<T>* Vertices, int32 InIdx0, int32 InIdx1, int32 InIdx2, const TVector<int32,3>& InAdjFaces, const TVector<int32,3>& InAdjEdges)
	{
		const TVec3<T>& V0 = Vertices[InIdx0];
		const TVec3<T>& V1 = Vertices[InIdx1];
		const TVec3<T>& V2 = Vertices[InIdx2];

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
};

template <typename T>
TArray<TEPAEntry<T>> InitializeEPA(const TVec3<T>* Verts, const int32 NumVerts)
{
	TArray<TEPAEntry<T>> Entries;
	switch(NumVerts)
	{
		case 4:
		{
			//make sure all triangle normals are facing out
			Entries.AddUninitialized(4);

			ensure(Entries[0].Initialize(Verts, 1, 2, 3, { 3, 1, 2 }, { 1,1, 1 }));
			ensure(Entries[1].Initialize(Verts, 0, 3, 2, { 2,0,3 }, { 2, 1, 0 }));
			ensure(Entries[2].Initialize(Verts, 0, 1, 3, { 3,0, 1 }, { 2,2,0 }));
			ensure(Entries[3].Initialize(Verts, 0, 2, 1, { 1,0,2 }, { 2,0,0 }));

			if (TVec3<T>::DotProduct(Entries[0].PlaneNormal, Verts[0]) > 0)
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

template <typename T, typename SupportLambda>
T EPA(TArray<TVec3<T>> VertsBuffer, const SupportLambda& Support)
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

	TArray<TEPAEntry<T>> Entries = InitializeEPA(VertsBuffer.GetData(), VertsBuffer.Num());
	std::priority_queue<FEPAEntryWrapper, std::vector<FEPAEntryWrapper>, std::greater<FEPAEntryWrapper>> Queue;
	for(int32 Idx = 0; Idx < Entries.Num(); ++Idx)
	{
		Queue.push(FEPAEntryWrapper {&Entries, Idx});
	}

	bool bCloseEnough = false;
	bool bTerminate = bCloseEnough;

	TArray<FEPAFloodEntry> VisibilityBorder;
	int32 Iteration = -1;
	while(Queue.size())
	{
		++Iteration;

		int32 EntryIdx = Queue.top().Idx;
		Queue.pop();
		TEPAEntry<T>& Entry = Entries[EntryIdx];
		if (Entry.bObsolete)
		{
			continue;
		}

		constexpr T Eps = 1e-4;


		const TVec3<T> W = Support(Entry.PlaneNormal);
		const T DistanceToSupportPlane = TVec3<T>::DotProduct(Entry.PlaneNormal, W);

		UpperBound = FMath::Min(UpperBound, DistanceToSupportPlane);
		ensure(LowerBound <= Entry.Distance);	//todo: remove this check, just for early development
		LowerBound = Entry.Distance;

		if (FMath::Abs(UpperBound - LowerBound) < Eps)	//todo: add relative?
		{
			return Entry.Distance;
		}

		const int32 NewVertIdx = VertsBuffer.Add(W);

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
			ensure(NewEntry.Initialize(VertsBuffer.GetData(), BorderEntry.IdxBuffer[BorderEdgeIdx1], BorderEntry.IdxBuffer[BorderEdgeIdx0], NewVertIdx,
				{ BorderEntryIdx, PrevEntryIdx, NextEntryIdx },
				{ BorderEdgeIdx0, 2, 1 }));	//todo: handle false case which is a degenerate triangle
			BorderEntry.AdjFaces[BorderEdgeIdx0] = NewIdx;
			BorderEntry.AdjEdges[BorderEdgeIdx0] = 0;

			ensure(NewEntry.Distance >= LowerBound);	//todo: remove, just used for early dev
			if (/*NewEntry.Distance >= LowerBound && */NewEntry.Distance <= UpperBound)
			{
				Queue.push(FEPAEntryWrapper{ &Entries, NewIdx});
			}

			++NewIdx;
		}
	}

	return 0;
}
}
