// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once
#include "Chaos/Simplex.h"
#include <queue>

namespace Chaos
{

template <typename T>
struct TEPAEntry
{
	TVec3<T> Vertices[3];	//vertices of triangle
	TVec3<T> V;	//closest point on affine hull of triangle
	T Lambdas[3];	//barycentric coordinates of V (\sum Lambda[i] * Vertices[i])
	T VSize2;	//distance from V to origin squared
	TVector<int32,3> AdjIdxs;	//Adjacent triangles ordered CCW
	TVector<int32,3> AdjIdxInOther;	//Used to find this entry in the corresponding adjacent triangle
	bool bObsolete;	//indicates that an entry can be skipped (became part of bigger polytope)
	bool bVInterior;	//indicates whether V is an interior point or not

	bool operator>(const TEPAEntry<T>& Other) const
	{
		return VSize2 > Other.VSize2;
	}

	void Initialize(const TVec3<T>& V0, const TVec3<T>& V1, const TVec3<T>& V2,
		const TVector<int32,3>& InAdjIdx, const TVector<int32,3>& InAdjIdxInOther)
	{
		Vertices[0] = V0;
		Vertices[1] = V1;
		Vertices[2] = V2;

		AdjIdxs = InAdjIdx;
		AdjIdxInOther = InAdjIdxInOther;

		FSimplex Simplex = {0,1,2};
		V = TriangleSimplexFindOrigin(Vertices, Simplex, Lambdas);
		VSize2 = V.SizeSquared();

		bVInterior = Simplex.NumVerts == 3;
		bObsolete = false;
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
			Entries.AddUninitialized(4);
			Entries[0].Initialize(Verts[0], Verts[1], Verts[2], {1,2,3}, {2, 2, 1});
			Entries[1].Initialize(Verts[0], Verts[3], Verts[1], {3,2,0}, {0,0,0});
			Entries[2].Initialize(Verts[1], Verts[3], Verts[2], {1,3,0}, {1,2,1});
			Entries[3].Initialize(Verts[3], Verts[0], Verts[2], {1,0,2}, {0,2,1});
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
			ToVisitStack.Add({ Entry.AdjIdxs[i], Entry.AdjIdxInOther[i] });
		}
	}

	while (ToVisitStack.Num())
	{
		const FEPAFloodEntry FloodEntry = ToVisitStack.Pop(false);
		TEPAEntry<T>& Entry = Entries[FloodEntry.EntryIdx];
		if (!Entry.bObsolete)
		{
			const T VDotW = TVec3<T>::DotProduct(Entry.V, W);
			if (VDotW < Entry.VSize2)	//plane check
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
				ToVisitStack.Add({ Entry.AdjIdxs[Idx1], Entry.AdjIdxInOther[Idx1] });
				ToVisitStack.Add({ Entry.AdjIdxs[Idx2], Entry.AdjIdxInOther[Idx2] });
			}
		}
	}
}

template <typename T, typename SupportLambda>
T EPA(const TVec3<T>* Verts, const int32 NumVerts, const SupportLambda& Support)
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

	TArray<TEPAEntry<T>> Entries = InitializeEPA(Verts, NumVerts);
	std::priority_queue<FEPAEntryWrapper, std::vector<FEPAEntryWrapper>, std::greater<FEPAEntryWrapper>> Queue;
	for(int32 Idx = 0; Idx < Entries.Num(); ++Idx)
	{
		if(Entries[Idx].bVInterior)
		{
			Queue.push(FEPAEntryWrapper {&Entries, Idx});
		}
	}

	bool bCloseEnough = false;
	bool bTerminate = bCloseEnough;

	TArray<FEPAFloodEntry> VisibilityBorder;

	while(Queue.size() && !bTerminate)
	{
		int32 EntryIdx = Queue.top().Idx;
		Queue.pop();
		TEPAEntry<T>& Entry = Entries[EntryIdx];
		if (Entry.bObsolete)
		{
			continue;
		}

		const TVec3<T>& V = Entry.V;
		const TVec3<T> W = Support(V);
		constexpr T Eps = 1e-4;
		const T VDotW = TVec3<T>::DotProduct(V, W);
		bCloseEnough = VDotW < Entry.VSize2 + Eps;	//todo: add relative eps
		if (!bCloseEnough)
		{
			Entry.bObsolete = true;
			VisibilityBorder.Reset();
			//question: in the paper we call this per edge which means we traverse new faces for second and third edge, does it matter?
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
				NewEntry.Initialize(BorderEntry.Vertices[BorderEdgeIdx1], BorderEntry.Vertices[BorderEdgeIdx0], W,
					{ BorderEntryIdx, NextEntryIdx, PrevEntryIdx },
					{ BorderEdgeIdx0, 2, 1 });
				BorderEntry.AdjIdxs[BorderEdgeIdx0] = NewIdx;
				BorderEntry.AdjIdxInOther[BorderEdgeIdx0] = 0;

				if (NewEntry.bVInterior)
				{
					Queue.push(FEPAEntryWrapper{ &Entries, NewIdx});
				}

				++NewIdx;
			}
		}
		else
		{
			return FMath::Sqrt(Entry.VSize2);
		}

		bTerminate = bCloseEnough;
	}

	return 0;
}
}
