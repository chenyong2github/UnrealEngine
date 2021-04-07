// Copyright Epic Games, Inc. All Rights Reserved.
#include "Chaos/TriangleMesh.h"

#include "Chaos/Box.h"
#include "Chaos/Defines.h"
#include "Chaos/Plane.h"
#include "HAL/IConsoleManager.h"
#include "Math/NumericLimits.h"
#include "Math/RandomStream.h"
#include "Templates/Sorting.h"
#include "Templates/TypeHash.h"

#include <algorithm>
#include <iostream>

#if INTEL_ISPC
#include "TriangleMesh.ispc.generated.h"
#endif

#if INTEL_ISPC && !UE_BUILD_SHIPPING
bool bChaos_TriangleMesh_ISPC_Enabled = true;
FAutoConsoleVariableRef CVarChaosTriangleMeshISPCEnabled(TEXT("p.Chaos.TriangleMesh.ISPC"), bChaos_TriangleMesh_ISPC_Enabled, TEXT("Whether to use ISPC optimizations in triangle mesh calculations"));
#endif

using namespace Chaos;

FTriangleMesh::FTriangleMesh()
    : MStartIdx(0)
    , MNumIndices(0)
{}

FTriangleMesh::FTriangleMesh(TArray<TVec3<int32>>&& Elements, const int32 StartIdx, const int32 EndIdx, const bool CullDegenerateElements)
{
	Init(Elements, StartIdx, EndIdx, CullDegenerateElements);
}

FTriangleMesh::FTriangleMesh(FTriangleMesh&& Other)
    : MElements(MoveTemp(Other.MElements))
    , MPointToTriangleMap(MoveTemp(Other.MPointToTriangleMap))
    , MStartIdx(Other.MStartIdx)
    , MNumIndices(Other.MNumIndices)
{}

FTriangleMesh::~FTriangleMesh()
{}

void FTriangleMesh::Init(TArray<TVec3<int32>>&& Elements, const int32 StartIdx, const int32 EndIdx, const bool CullDegenerateElements)
{
	MElements = MoveTemp(Elements);
	MStartIdx = 0;
	MNumIndices = 0;
	InitHelper(StartIdx, EndIdx, CullDegenerateElements);
}

void FTriangleMesh::Init(const TArray<TVec3<int32>>& Elements, const int32 StartIdx, const int32 EndIdx, const bool CullDegenerateElements)
{
	MElements = Elements;
	MStartIdx = 0;
	MNumIndices = 0;
	InitHelper(StartIdx, EndIdx, CullDegenerateElements);
}

void FTriangleMesh::InitHelper(const int32 StartIdx, const int32 EndIdx, const bool CullDegenerateElements)
{
	if (MElements.Num())
	{
		MStartIdx = MElements[MElements.Num()-1][0];
		int32 MaxIdx = MStartIdx;
		for (int i = MElements.Num()-1; i >= 0 ; --i)
		{
			for (int Axis = 0; Axis < 3; ++Axis)
			{
				MStartIdx = FMath::Min(MStartIdx, MElements[i][Axis]);
				MaxIdx = FMath::Max(MaxIdx, MElements[i][Axis]);
			}
			if (CullDegenerateElements)
			{
				if (MElements[i][0] == MElements[i][1] ||
					MElements[i][0] == MElements[i][2] ||
					MElements[i][1] == MElements[i][2])
				{
					// It's possible that the order of the triangles might be important.
					// RemoveAtSwap() changes the order of the array.  I figure that if
					// you're up for CullDegenerateElements, then triangle reordering is
					// fair game.
					MElements.RemoveAtSwap(i);
				}

			}
		}
		// This assumes vertices are contiguous in the vertex buffer. Assumption is held throughout FTriangleMesh
		MNumIndices = MaxIdx - MStartIdx + 1;
	}
	check(MStartIdx >= 0);
	check(MNumIndices >= 0);
	ExpandVertexRange(StartIdx, EndIdx);
}

void FTriangleMesh::ResetAuxiliaryStructures()
{
	MPointToTriangleMap.Reset();
	MPointToNeighborsMap.Reset();
	TArray<TVec2<int32>> EmptyEdges;
	MSegmentMesh.Init(EmptyEdges);
	MFaceToEdges.Reset();
	MEdgeToFaces.Reset();
}

TVec2<int32> FTriangleMesh::GetVertexRange() const
{
	return TVec2<int32>(MStartIdx, MStartIdx + MNumIndices - 1);
}

TSet<int32> FTriangleMesh::GetVertices() const
{
	TSet<int32> Vertices;
	GetVertexSet(Vertices);
	return Vertices;
}

void FTriangleMesh::GetVertexSet(TSet<int32>& VertexSet) const
{
	VertexSet.Reset();
	VertexSet.Reserve(MNumIndices);
	for (const TVec3<int32>& Element : MElements)
	{
		VertexSet.Append({Element[0], Element[1], Element[2]});
	}
}

const TMap<int32, TSet<int32>>& FTriangleMesh::GetPointToNeighborsMap() const
{
	if (MPointToNeighborsMap.Num())
	{
		return MPointToNeighborsMap;
	}
	MPointToNeighborsMap.Reserve(MNumIndices);
	for (int i = 0; i < MElements.Num(); ++i)
	{
		TSet<int32>& Elems0 = MPointToNeighborsMap.FindOrAdd(MElements[i][0]);
		TSet<int32>& Elems1 = MPointToNeighborsMap.FindOrAdd(MElements[i][1]);
		TSet<int32>& Elems2 = MPointToNeighborsMap.FindOrAdd(MElements[i][2]);
		Elems0.Reserve(Elems0.Num() + 2);
		Elems1.Reserve(Elems1.Num() + 2);
		Elems2.Reserve(Elems2.Num() + 2);

		const TVec3<int32>& Tri = MElements[i];
		Elems0.Add(Tri[1]);
		Elems0.Add(Tri[2]);
		Elems1.Add(Tri[0]);
		Elems1.Add(Tri[2]);
		Elems2.Add(Tri[0]);
		Elems2.Add(Tri[1]);
	}
	return MPointToNeighborsMap;
}

TConstArrayView<TArray<int32>> FTriangleMesh::GetPointToTriangleMap() const
{
	if (!MPointToTriangleMap.Num())
	{
		MPointToTriangleMap.AddDefaulted(MNumIndices);
		for (int i = 0; i < MElements.Num(); ++i)
		{
			for (int Axis = 0; Axis < 3; ++Axis)
			{
				MPointToTriangleMap[MElements[i][Axis] - MStartIdx].Add(i);  // Access MPointToTriangleMap with local index
			}
		}
	}
	return TConstArrayView<TArray<int32>>(MPointToTriangleMap.GetData() - MStartIdx, MStartIdx + MNumIndices);  // Return an array view that is using global indexation
}

TArray<TVec2<int32>> FTriangleMesh::GetUniqueAdjacentPoints() const
{
	TArray<TVec2<int32>> BendingConstraints;
	const TArray<TVec4<int32>> BendingElements = GetUniqueAdjacentElements();
	BendingConstraints.Reset(BendingElements.Num());
	for (const TVec4<int32>& Element : BendingElements)
	{
		BendingConstraints.Emplace(Element[2], Element[3]);
	}
	return BendingConstraints;
}

TArray<TVec4<int32>> FTriangleMesh::GetUniqueAdjacentElements() const
{
	// Build a map with a list of opposite points for every edges
	TMap<TVec2<int32> /*Edge*/, TArray<int32> /*OppositePoints*/> EdgeMap;

	auto SortedEdge = [](int32 P0, int32 P1) { return P0 <= P1 ? TVec2<int32>(P0, P1) : TVec2<int32>(P1, P0); };

	for (const TVec3<int32>& Element : MElements)
	{
		EdgeMap.FindOrAdd(SortedEdge(Element[0], Element[1])).AddUnique(Element[2]);
		EdgeMap.FindOrAdd(SortedEdge(Element[1], Element[2])).AddUnique(Element[0]);
		EdgeMap.FindOrAdd(SortedEdge(Element[2], Element[0])).AddUnique(Element[1]);
	}

	// Build constraints
	TArray<TVec4<int32>> BendingConstraints;
	for (const TPair<TVec2<int32>, TArray<int32>>& EdgeOppositePoints : EdgeMap)
	{
		const TVec2<int32>& Edge = EdgeOppositePoints.Key;
		const TArray<int32>& OppositePoints = EdgeOppositePoints.Value;

		for (int32 Index0 = 0; Index0 < OppositePoints.Num(); ++Index0)
		{
			for (int32 Index1 = Index0 + 1; Index1 < OppositePoints.Num(); ++Index1)
			{
				BendingConstraints.Add({ Edge[0], Edge[1], OppositePoints[Index0], OppositePoints[Index1] });
			}
		}
	}

	return BendingConstraints;
}

TArray<FVec3> FTriangleMesh::GetFaceNormals(const TConstArrayView<FVec3>& Points, const bool ReturnEmptyOnError) const
{
	TArray<FVec3> Normals;
	GetFaceNormals(Normals, Points, ReturnEmptyOnError);
	return Normals;
}

// Note:	This function assumes Counter Clockwise triangle windings in a Left Handed coordinate system
//			If this is not the case the returned face normals may need to be inverted
void FTriangleMesh::GetFaceNormals(TArray<FVec3>& Normals, const TConstArrayView<FVec3>& Points, const bool ReturnEmptyOnError) const
{
	Normals.Reset(MElements.Num());
	if (ReturnEmptyOnError)
	{
		for (const TVec3<int32>& Tri : MElements)
		{
			FVec3 p10 = Points[Tri[1]] - Points[Tri[0]];
			FVec3 p20 = Points[Tri[2]] - Points[Tri[0]];
			FVec3 Cross = FVec3::CrossProduct(p20, p10);
			const FReal Size2 = Cross.SizeSquared();
			if (Size2 < SMALL_NUMBER)
			{
				//particles should not be coincident by the time they get here. Return empty to signal problem to caller
				ensure(false);
				Normals.Empty();
				return;
			}
			else
			{
				Normals.Add(Cross.GetUnsafeNormal());
			}
		}
	}
	else
	{
		if (bRealTypeCompatibleWithISPC && bChaos_TriangleMesh_ISPC_Enabled)
		{
			static_assert(std::is_same<FReal, float>::value == true, "ISPC only supports float template type");
			Normals.SetNumUninitialized(MElements.Num());

#if INTEL_ISPC
			ispc::GetFaceNormals(
				(ispc::FVector*)Normals.GetData(),
				(ispc::FVector*)Points.GetData(),
				(ispc::FIntVector*)MElements.GetData(),
				MElements.Num());
#endif
		}
		else
		{
			for (const TVec3<int32>& Tri : MElements)
			{
				FVec3 p10 = Points[Tri[1]] - Points[Tri[0]];
				FVec3 p20 = Points[Tri[2]] - Points[Tri[0]];
				FVec3 Cross = FVec3::CrossProduct(p20, p10);
				Normals.Add(Cross.GetSafeNormal());
			}
		}
	}
}

TArray<FVec3> FTriangleMesh::GetPointNormals(const TConstArrayView<FVec3>& Points, const bool ReturnEmptyOnError)
{
	TArray<FVec3> PointNormals;
	const TArray<FVec3> FaceNormals = GetFaceNormals(Points, ReturnEmptyOnError);
	if (FaceNormals.Num())
	{
		PointNormals.SetNumUninitialized(MNumIndices);
		GetPointNormals(PointNormals, FaceNormals, /*bUseGlobalArray =*/ false);
	}
	return PointNormals;
}

void FTriangleMesh::GetPointNormals(TArrayView<FVec3> PointNormals, const TConstArrayView<FVec3>& FaceNormals, const bool bUseGlobalArray)
{
	GetPointToTriangleMap(); // build MPointToTriangleMap
	const FTriangleMesh* ConstThis = this;
	ConstThis->GetPointNormals(PointNormals, FaceNormals, bUseGlobalArray);
}

void FTriangleMesh::GetPointNormals(TArrayView<FVec3> PointNormals, const TConstArrayView<FVec3>& FaceNormals, const bool bUseGlobalArray) const
{
	check(MPointToTriangleMap.Num() != 0);

	if (bRealTypeCompatibleWithISPC && bChaos_TriangleMesh_ISPC_Enabled)
	{
		static_assert(std::is_same<FReal, float>::value == true, "ISPC only supports float template type");

#if INTEL_ISPC
		ispc::GetPointNormals(
			(ispc::FVector*)PointNormals.GetData(),
			(const ispc::FVector*)FaceNormals.GetData(),
			(const ispc::TArrayInt*)MPointToTriangleMap.GetData(),
			bUseGlobalArray ? LocalToGlobal(0) : 0,
			FaceNormals.Num(),
			MNumIndices);
#endif
	}
	else
	{
		for (int32 Element = 0; Element < MNumIndices; ++Element)  // Iterate points with local indexes
		{
			const int32 NormalIndex = bUseGlobalArray ? LocalToGlobal(Element) : Element;  // Select whether the points normal indices match the points indices or start at 0
			FVec3& Normal = PointNormals[NormalIndex];
			Normal = FVec3(0);
			const TArray<int32>& TriangleMap = MPointToTriangleMap[Element];  // Access MPointToTriangleMap with local index
			for (int32 k = 0; k < TriangleMap.Num(); ++k)
			{
				if (FaceNormals.IsValidIndex(TriangleMap[k]))
				{
					Normal += FaceNormals[TriangleMap[k]];
				}
			}
			Normal = Normal.GetSafeNormal();
		}
	}
}

template<class T>
void AddTrianglesToHull(const TConstArrayView<FVec3>& Points, const int32 I0, const int32 I1, const int32 I2, const TPlane<T, 3>& SplitPlane, const TArray<int32>& InIndices, TArray<TVec3<int32>>& OutIndices)
{
	int32 MaxD = 0; //This doesn't need to be initialized but we need to avoid the compiler warning
	T MaxDistance = 0;
	for (int32 i = 0; i < InIndices.Num(); ++i)
	{
		T Distance = SplitPlane.SignedDistance(Points[InIndices[i]]);
		check(Distance >= 0);
		if (Distance > MaxDistance)
		{
			MaxDistance = Distance;
			MaxD = InIndices[i];
		}
	}
	if (MaxDistance == 0)
	{
		//@todo(mlentine): Do we need to do anything here when InIndices is > 0?
		check(I0 != I1);
		check(I1 != I2);
		OutIndices.AddUnique(TVec3<int32>(I0, I1, I2));
		return;
	}
	if (MaxDistance > 0)
	{
		const FVec3& NewX = Points[MaxD];
		const FVec3& X0 = Points[I0];
		const FVec3& X1 = Points[I1];
		const FVec3& X2 = Points[I2];
		const FVec3 V1 = (NewX - X0).GetSafeNormal();
		const FVec3 V2 = (NewX - X1).GetSafeNormal();
		const FVec3 V3 = (NewX - X2).GetSafeNormal();
		FVec3 Normal1 = FVec3::CrossProduct(V1, V2).GetSafeNormal();
		if (FVec3::DotProduct(Normal1, X2 - X0) > 0)
		{
			Normal1 *= -1;
		}
		FVec3 Normal2 = FVec3::CrossProduct(V1, V3).GetSafeNormal();
		if (FVec3::DotProduct(Normal2, X1 - X0) > 0)
		{
			Normal2 *= -1;
		}
		FVec3 Normal3 = FVec3::CrossProduct(V2, V3).GetSafeNormal();
		if (FVec3::DotProduct(Normal3, X0 - X1) > 0)
		{
			Normal3 *= -1;
		}
		TPlane<FReal, 3> NewPlane1(NewX, Normal1);
		TPlane<FReal, 3> NewPlane2(NewX, Normal2);
		TPlane<FReal, 3> NewPlane3(NewX, Normal3);
		TArray<int32> NewIndices1;
		TArray<int32> NewIndices2;
		TArray<int32> NewIndices3;
		TSet<FIntVector> FacesToFilter;
		for (int32 i = 0; i < InIndices.Num(); ++i)
		{
			if (MaxD == InIndices[i])
			{
				continue;
			}
			T Dist1 = NewPlane1.SignedDistance(Points[InIndices[i]]);
			T Dist2 = NewPlane2.SignedDistance(Points[InIndices[i]]);
			T Dist3 = NewPlane3.SignedDistance(Points[InIndices[i]]);
			check(Dist1 < 0 || Dist2 < 0 || Dist3 < 0);
			if (Dist1 > 0 && Dist2 > 0)
			{
				FacesToFilter.Add(FIntVector(I0, MaxD, InIndices[i]));
			}
			if (Dist1 > 0 && Dist3 > 0)
			{
				FacesToFilter.Add(FIntVector(I1, MaxD, InIndices[i]));
			}
			if (Dist2 > 0 && Dist3 > 0)
			{
				FacesToFilter.Add(FIntVector(I2, MaxD, InIndices[i]));
			}
			if (Dist1 >= 0)
			{
				NewIndices1.Add(InIndices[i]);
			}
			if (Dist2 >= 0)
			{
				NewIndices2.Add(InIndices[i]);
			}
			if (Dist3 >= 0)
			{
				NewIndices3.Add(InIndices[i]);
			}
		}
		AddTrianglesToHull(Points, I0, I1, MaxD, NewPlane1, NewIndices1, OutIndices);
		AddTrianglesToHull(Points, I0, I2, MaxD, NewPlane2, NewIndices2, OutIndices);
		AddTrianglesToHull(Points, I1, I2, MaxD, NewPlane3, NewIndices3, OutIndices);
		for (int32 i = 0; i < OutIndices.Num(); ++i)
		{
			if (FacesToFilter.Contains(FIntVector(OutIndices[i][0], OutIndices[i][1], OutIndices[i][2])))
			{
				OutIndices.RemoveAtSwap(i);
				i--;
			}
		}
	}
}

// @todo(mlentine, ocohen); Merge different hull creation versions
FTriangleMesh FTriangleMesh::GetConvexHullFromParticles(const TConstArrayView<FVec3>& Points)
{
	TArray<TVec3<int32>> Indices;
	if (Points.Num() <= 2)
	{
		return FTriangleMesh(MoveTemp(Indices));
	}
	// Find max and min x points
	int32 MinX = 0;
	int32 MaxX = 0;
	int32 MinY = 0;
	int32 MaxY = 0;
	int32 Index1 = 0;
	int32 Index2 = 0;
	for (int32 Idx = 1; Idx < Points.Num(); ++Idx)
	{
		if (Points[Idx][0] > Points[MaxX][0])
		{
			MaxX = Idx;
		}
		if (Points[Idx][0] < Points[MinX][0])
		{
			MinX = Idx;
		}
		if (Points[Idx][1] > Points[MaxY][1])
		{
			MaxY = Idx;
		}
		if (Points[Idx][1] < Points[MinY][1])
		{
			MinY = Idx;
		}
	}
	if (MaxX == MinX && MinY == MaxY && MinX == MinY)
	{
		// Points are co-linear
		return FTriangleMesh(MoveTemp(Indices));
	}
	// Find max distance
	FReal DistanceY = (Points[MaxY] - Points[MinY]).Size();
	FReal DistanceX = (Points[MaxX] - Points[MinX]).Size();
	if (DistanceX > DistanceY)
	{
		Index1 = MaxX;
		Index2 = MinX;
	}
	else
	{
		Index1 = MaxY;
		Index2 = MinY;
	}
	const FVec3& X1 = Points[Index1];
	const FVec3& X2 = Points[Index2];
	FReal MaxDist = 0;
	int32 MaxD = -1;
	for (int32 Idx = 0; Idx < Points.Num(); ++Idx)
	{
		if (Idx == Index1 || Idx == Index2)
		{
			continue;
		}
		const FVec3& X0 = Points[Idx];
		FReal Distance = FVec3::CrossProduct(X0 - X1, X0 - X2).Size() / (X2 - X1).Size();
		if (Distance > MaxDist)
		{
			MaxDist = Distance;
			MaxD = Idx;
		}
	}
	if (MaxD != -1)
	{
		const FVec3& X0 = Points[MaxD];
		FVec3 Normal = FVec3::CrossProduct((X0 - X1).GetSafeNormal(), (X0 - X2).GetSafeNormal());
		TPlane<FReal, 3> SplitPlane(X0, Normal);
		TPlane<FReal, 3> SplitPlaneNeg(X0, -Normal);
		TArray<int32> Left;
		TArray<int32> Right;
		TArray<int32> Coplanar;
		TSet<int32> CoplanarSet;
		CoplanarSet.Add(MaxD);
		CoplanarSet.Add(Index1);
		CoplanarSet.Add(Index2);
		for (int32 Idx = 0; Idx < Points.Num(); ++Idx)
		{
			if (Idx == Index1 || Idx == Index2 || Idx == MaxD)
			{
				continue;
			}
			if (SplitPlane.SignedDistance(Points[Idx]) > 0)
			{
				Left.Add(Idx);
			}
			else if (SplitPlane.SignedDistance(Points[Idx]) < 0)
			{
				Right.Add(Idx);
			}
			else
			{
				CoplanarSet.Add(Idx);
				Coplanar.Add(Idx);
			}
		}
		if (!Left.Num())
		{
			Right.Append(Coplanar);
			AddTrianglesToHull(Points, MaxD, Index1, Index2, SplitPlane, Left, Indices);
			AddTrianglesToHull(Points, MaxD, Index1, Index2, SplitPlaneNeg, Right, Indices);
		}
		else if (!Right.Num())
		{
			Left.Append(Coplanar);
			AddTrianglesToHull(Points, MaxD, Index1, Index2, SplitPlane, Left, Indices);
			AddTrianglesToHull(Points, MaxD, Index1, Index2, SplitPlaneNeg, Right, Indices);
		}
		else if (Left.Num() && Right.Num())
		{
			Right.Append(Coplanar);
			Left.Append(Coplanar);
			AddTrianglesToHull(Points, MaxD, Index1, Index2, SplitPlane, Left, Indices);
			AddTrianglesToHull(Points, MaxD, Index1, Index2, SplitPlaneNeg, Right, Indices);
			// Remove combinations of MaxD, Index1, Index2, and Coplanar
			for (int32 i = 0; i < Indices.Num(); ++i)
			{
				if (CoplanarSet.Contains(Indices[i].X) && CoplanarSet.Contains(Indices[i].Y) && CoplanarSet.Contains(Indices[i].Z))
				{
					Indices.RemoveAtSwap(i);
					i--;
				}
			}
		}
	}
	return FTriangleMesh(MoveTemp(Indices));
}

FORCEINLINE TVec2<int32> GetOrdered(const TVec2<int32>& elem)
{
	const bool ordered = elem[0] < elem[1];
	return TVec2<int32>(
	    ordered ? elem[0] : elem[1],
	    ordered ? elem[1] : elem[0]);
}

void Order(int32& A, int32& B)
{
	if (B < A)
	{
		int32 Tmp = A;
		A = B;
		B = Tmp;
	}
}

TVec3<int32> GetOrdered(const TVec3<int32>& Elem)
{
	TVec3<int32> OrderedElem = Elem;	   // 3 2 1		1 2 3		1 2 1	2 1 1
	Order(OrderedElem[0], OrderedElem[1]); // 2 3 1		1 2 3		1 2 1	1 2 1
	Order(OrderedElem[1], OrderedElem[2]); // 2 1 3		1 2 3		1 1 2	1 1 2
	Order(OrderedElem[0], OrderedElem[1]); // 1 2 3		1 2 3		1 1 2	1 1 2
	return OrderedElem;
}

/**
 * Comparator for TSet<TVec2<int32>> that compares the components of vectors in ascending
 * order.
 */
struct OrderedEdgeKeyFuncs : BaseKeyFuncs<TVec2<int32>, TVec2<int32>, false>
{
	static FORCEINLINE TVec2<int32> GetSetKey(const TVec2<int32>& elem)
	{
		return GetOrdered(elem);
	}

	static FORCEINLINE bool Matches(const TVec2<int32>& a, const TVec2<int32>& b)
	{
		const auto orderedA = GetSetKey(a);
		const auto orderedB = GetSetKey(b);
		return orderedA[0] == orderedB[0] && orderedA[1] == orderedB[1];
	}

	static FORCEINLINE uint32 GetKeyHash(const TVec2<int32>& elem)
	{
		const uint32 v = HashCombine(GetTypeHash(elem[0]), GetTypeHash(elem[1]));
		return v;
	}
};

FSegmentMesh& FTriangleMesh::GetSegmentMesh()
{
	if (MSegmentMesh.GetNumElements() != 0)
	{
		return MSegmentMesh;
	}

	// XXX - Unfortunately, TSet is not a tree, it's a hash set.  This exposes
	// us to the possibility we'll see hash collisions, and that's not something
	// we should allow.  So we use a TArray instead.
	TArray<TVec2<int32>> UniqueEdges;
	UniqueEdges.Reserve(MElements.Num() * 3);

	MEdgeToFaces.Reset();
	MEdgeToFaces.Reserve(MElements.Num() * 3); // over estimate
	MFaceToEdges.Reset();
	MFaceToEdges.SetNum(MElements.Num());
	for (int32 FaceIdx = 0; FaceIdx < MElements.Num(); FaceIdx++)
	{
		const TVec3<int32>& Tri = MElements[FaceIdx];
		TVec3<int32>& EdgeIds = MFaceToEdges[FaceIdx];
		for (int32 j = 0; j < 3; j++)
		{
			TVec2<int32> Edge(Tri[j], Tri[(j + 1) % 3]);

			const int32 EdgeIdx = UniqueEdges.AddUnique(GetOrdered(Edge));
			EdgeIds[j] = EdgeIdx;

			// Track which faces are shared by edges.
			const int currNum = MEdgeToFaces.Num();
			if (currNum <= EdgeIdx)
			{
				// Add and initialize new entries
				MEdgeToFaces.SetNum(EdgeIdx + 1, false);
				for (int32 k = currNum; k < EdgeIdx + 1; k++)
				{
					MEdgeToFaces[k] = TVec2<int32>(-1, -1);
				}
			}

			TVec2<int32>& FacesSharingThisEdge = MEdgeToFaces[EdgeIdx];
			if (FacesSharingThisEdge[0] < 0)
			{
				// 0th initialized, but not set
				FacesSharingThisEdge[0] = FaceIdx;
			}
			else if (FacesSharingThisEdge[1] < 0)
			{
				// 0th already set, only 1 is left
				FacesSharingThisEdge[1] = FaceIdx;
			}
			else
			{
				// This is a non-manifold mesh, where this edge is shared by
				// more than 2 faces.
				CHAOS_ENSURE_MSG(false, TEXT("Skipping non-manifold edge to face mapping."));
			}
		}
	}
	MSegmentMesh.Init(MoveTemp(UniqueEdges));
	return MSegmentMesh;
}

const TArray<TVec3<int32>>& FTriangleMesh::GetFaceToEdges()
{
	GetSegmentMesh();
	return MFaceToEdges;
}

const TArray<TVec2<int32>>& FTriangleMesh::GetEdgeToFaces()
{
	GetSegmentMesh();
	return MEdgeToFaces;
}


TSet<int32> FTriangleMesh::GetBoundaryPoints()
{
	FSegmentMesh& SegmentMesh = GetSegmentMesh();
	const TArray<TVec2<int32>>& Edges = SegmentMesh.GetElements();
	const TArray<TVec2<int32>>& EdgeToFaces = GetEdgeToFaces();
	TSet<int32> OpenBoundaryPoints;
	for (int32 EdgeIdx = 0; EdgeIdx < EdgeToFaces.Num(); ++EdgeIdx)
	{
		const TVec2<int32>& CoincidentFaces = EdgeToFaces[EdgeIdx];
		if (CoincidentFaces[0] == INDEX_NONE || CoincidentFaces[1] == INDEX_NONE)
		{
			const TVec2<int32>& Edge = Edges[EdgeIdx];
			OpenBoundaryPoints.Add(Edge[0]);
			OpenBoundaryPoints.Add(Edge[1]);
		}
	}
	return OpenBoundaryPoints;
}

TMap<int32, int32> FTriangleMesh::FindCoincidentVertexRemappings(
	const TArray<int32>& TestIndices,
	const TConstArrayView<FVec3>& Points)
{
	// From index -> To index
	TMap<int32, int32> Remappings;

	const int32 NumPoints = TestIndices.Num();
	if (NumPoints <= 1)
	{
		return Remappings;
	}

	// Move the points to the origin to avoid floating point aliasing far away
	// from the origin.
	FAABB3 Bbox(Points[0], Points[0]);
	for (int i = 1; i < NumPoints; i++)
	{
		Bbox.GrowToInclude(Points[TestIndices[i]]);
	}
	const FVec3 Center = Bbox.Center();

	TArray<FVec3> LocalPoints;
	LocalPoints.AddUninitialized(NumPoints);
	LocalPoints[0] = Points[TestIndices[0]] - Center;
	FAABB3 LocalBBox(LocalPoints[0], LocalPoints[0]);
	for (int i = 1; i < NumPoints; i++)
	{
		LocalPoints[i] = Points[TestIndices[i]] - Center;
		LocalBBox.GrowToInclude(LocalPoints[i]);
	}

	// Return early if all points are coincident
	if (LocalBBox.Extents().Max() < KINDA_SMALL_NUMBER)
	{
		int32 First = INDEX_NONE;
		for (const int32 Pt : TestIndices)
		{
			if (First == INDEX_NONE)
			{
				First = Pt;
			}
			else
			{
				// Remap Pt to First
				Remappings.Add(Pt, First);
			}
		}
		return Remappings;
	}

	LocalBBox.Thicken(1.0e-3);
	const FVec3 LocalCenter = LocalBBox.Center();
	const FVec3& LocalMin = LocalBBox.Min();

	const FReal MaxBBoxDim = LocalBBox.Extents().Max();

	// Find coincident vertices.
	// We hash to a grid of fine enough resolution such that if 2 particles 
	// hash to the same cell, then we're going to consider them coincident.
	TMap<int64, TSet<int32>> OccupiedCells;
	OccupiedCells.Reserve(NumPoints);

	const int64 Resolution = static_cast<int64>(floor(MaxBBoxDim / 0.01));
	const FReal CellSize = MaxBBoxDim / Resolution;
	for (int i = 0; i < 2; i++)
	{
		OccupiedCells.Reset();

		// Shift the grid by 1/2 a grid cell the second iteration so that
		// we don't miss slightly adjacent coincident points across cell
		// boundaries.
		const FVec3 GridCenter = LocalCenter - FVec3(i * CellSize / 2);
		for (int32 LocalIdx = 0; LocalIdx < NumPoints; LocalIdx++)
		{
			const int32 Idx = TestIndices[LocalIdx];
			if (i != 0 && Remappings.Contains(Idx))
			{
				// Already remapped
				continue;
			}

			const FVec3& Pos = LocalPoints[LocalIdx];
			const TVec3<int64> Coord(
				static_cast<int64>(floor((Pos[0] - GridCenter[0]) / CellSize + Resolution / 2)),
				static_cast<int64>(floor((Pos[1] - GridCenter[1]) / CellSize + Resolution / 2)),
				static_cast<int64>(floor((Pos[2] - GridCenter[2]) / CellSize + Resolution / 2)));
			const int64 FlatIdx =
				((Coord[0] * Resolution + Coord[1]) * Resolution) + Coord[2];

			TSet<int32>& Bucket = OccupiedCells.FindOrAdd(FlatIdx);
			Bucket.Add(Idx);
		}

		// Iterate over all occupied cells and remap redundant vertices to the first index.
		for (auto& KV : OccupiedCells)
		{
			const TSet<int32>& CoincidentVertices = KV.Value;
			if (CoincidentVertices.Num() <= 1)
				continue;
			int32 First = INDEX_NONE;
			for (const int32 Idx : CoincidentVertices)
			{
				if (First == INDEX_NONE)
				{
					First = Idx;
				}
				else
				{
					Remappings.Add(Idx, First);
				}
			}
		}
	}

	return Remappings;
}

TArray<FReal> FTriangleMesh::GetCurvatureOnEdges(const TArray<FVec3>& FaceNormals)
{
	const int32 NumNormals = FaceNormals.Num();
	check(NumNormals == MElements.Num());
	const FSegmentMesh& SegmentMesh = GetSegmentMesh(); // builds MEdgeToFaces
	TArray<FReal> EdgeAngles;
	EdgeAngles.SetNumZeroed(MEdgeToFaces.Num());
	for (int32 EdgeId = 0; EdgeId < MEdgeToFaces.Num(); EdgeId++)
	{
		const TVec2<int32>& FaceIds = MEdgeToFaces[EdgeId];
		if (FaceIds[0] >= 0 &&
		    FaceIds[1] >= 0 && // -1 is sentinel, which denotes a boundary edge.
		    FaceIds[0] < NumNormals &&
		    FaceIds[1] < NumNormals) // Stay in bounds
		{
			const FVec3& Norm1 = FaceNormals[FaceIds[0]];
			const FVec3& Norm2 = FaceNormals[FaceIds[1]];
			EdgeAngles[EdgeId] = FVec3::AngleBetween(Norm1, Norm2);
		}
	}
	return EdgeAngles;
}

TArray<FReal> FTriangleMesh::GetCurvatureOnEdges(const TConstArrayView<FVec3>& Points)
{
	const TArray<FVec3> FaceNormals = GetFaceNormals(Points, false);
	return GetCurvatureOnEdges(FaceNormals);
}

TArray<FReal> FTriangleMesh::GetCurvatureOnPoints(const TArray<FReal>& EdgeCurvatures)
{
	const FSegmentMesh& SegmentMesh = GetSegmentMesh();
	const TArray<TVec2<int32>>& Segments = SegmentMesh.GetElements();
	check(EdgeCurvatures.Num() == Segments.Num());

	if (MNumIndices < 1)
	{
		return TArray<FReal>();
	}

	TArray<FReal> PointCurvatures;
	// 0.0 means the faces are coplanar.
	// M_PI are as creased as they can be.
	// Initialize to -FLT_MAX so that free particles are penalized.
	PointCurvatures.Init(-TNumericLimits<FReal>::Max(), MNumIndices);
	for (int32 i = 0; i < Segments.Num(); i++)
	{
		const FReal EdgeCurvature = EdgeCurvatures[i];
		const TVec2<int32>& Edge = Segments[i];
		PointCurvatures[GlobalToLocal(Edge[0])] = FMath::Max(PointCurvatures[GlobalToLocal(Edge[0])], EdgeCurvature);
		PointCurvatures[GlobalToLocal(Edge[1])] = FMath::Max(PointCurvatures[GlobalToLocal(Edge[1])], EdgeCurvature);
	}
	return PointCurvatures;
}

TArray<FReal> FTriangleMesh::GetCurvatureOnPoints(const TConstArrayView<FVec3>& Points)
{
	const TArray<FReal> EdgeCurvatures = GetCurvatureOnEdges(Points);
	return GetCurvatureOnPoints(EdgeCurvatures);
}

/**
* Binary predicate for sorting indices according to a secondary array of values to sort
* by.  Puts values into ascending order.
*/
template<class T>
class AscendingPredicate
{
public:
	AscendingPredicate(const TArray<T>& InCompValues, const int32 InOffset)
		: CompValues(InCompValues)
		, Offset(InOffset)
	{}

	bool
		operator()(const int i, const int j) const
	{
		// If an index is out of range, put it at the end.
		const int iOffset = i - Offset;
		const int jOffset = j - Offset;
		const T vi = iOffset >= 0 && iOffset < CompValues.Num() ? CompValues[iOffset] : TNumericLimits<T>::Max();
		const T vj = jOffset >= 0 && jOffset < CompValues.Num() ? CompValues[jOffset] : TNumericLimits<T>::Max();
		return vi < vj;
	}

private:
	const TArray<T>& CompValues;
	const int32 Offset;
};

/**
* Binary predicate for sorting indices according to a secondary array of values to sort
* by.  Puts values into descending order.
*/
template<class T>
class DescendingPredicate
{
public:
	DescendingPredicate(const TArray<T>& CompValues, const int32 Offset = 0)
		: CompValues(CompValues)
		, Offset(Offset)
	{}

	bool
		operator()(const int i, const int j) const
	{
		// If an index is out of range, put it at the end.
		const int iOffset = i - Offset;
		const int jOffset = j - Offset;
		const T vi = iOffset >= 0 && iOffset < CompValues.Num() ? CompValues[iOffset] : -TNumericLimits<T>::Max();
		const T vj = jOffset >= 0 && jOffset < CompValues.Num() ? CompValues[jOffset] : -TNumericLimits<T>::Max();
		return vi > vj;
	}

private:
	const TArray<T>& CompValues;
	const int32 Offset;
};

TArray<int32> FTriangleMesh::GetVertexImportanceOrdering(
    const TConstArrayView<FVec3>& Points,
    const TArray<FReal>& PointCurvatures,
    TArray<int32>* CoincidentVertices,
    const bool RestrictToLocalIndexRange)
{
	const int32 NumPoints = RestrictToLocalIndexRange ? MNumIndices : Points.Num();
	const int32 Offset = RestrictToLocalIndexRange ? MStartIdx : 0;

	TArray<int32> PointOrder;
	if (!NumPoints)
	{
		return PointOrder;
	}

	// Initialize pointOrder to be 0, 1, 2, 3, ..., n-1.
	PointOrder.SetNumUninitialized(NumPoints);
	for (int32 i = 0; i < NumPoints; i++)
	{
		PointOrder[i] = i + Offset;
	}

	if (NumPoints == 1)
	{
		return PointOrder;
	}

	// A linear ordering biases towards the order in which the vertices were
	// authored, which is likely to be topologically adjacent.  Randomize the
	// initial ordering.
	FRandomStream Rand(NumPoints);
	for (int32 i = 0; i < NumPoints; i++)
	{
		const int32 j = Rand.RandRange(0, NumPoints - 1);
		Swap(PointOrder[i], PointOrder[j]);
	}

	// Find particles with no connectivity and send them to the back of the
	// list.  We penalize free points, but we don't exclude them.  It's
	// possible they were added for extra resolution.
	TArray<uint8> Rank;
	Rank.AddUninitialized(NumPoints);
	AscendingPredicate<uint8> AscendingRankPred(Rank, Offset); // low to high
	bool FoundFreeParticle = false;
	for (int i = 0; i < NumPoints; i++)
	{
		const int32 Idx = PointOrder[i];
		const TSet<int32>* Neighbors = MPointToNeighborsMap.Find(Idx);
		const bool IsFree = Neighbors == nullptr || Neighbors->Num() == 0;
		Rank[Idx - Offset] = IsFree ? 1 : 0;
		FoundFreeParticle |= IsFree;
	}
	if (FoundFreeParticle)
	{
		StableSort(&PointOrder[0], NumPoints, AscendingRankPred);
	}

	// Sort the pointOrder array by pointCurvatures so that points attached
	// to edges with the highest curvatures come first.
	if (PointCurvatures.Num() > 0)
	{
		// Curvature is measured by the angle between face normals.  0.0 means
		// coplanar, angles approaching M_PI are more creased.  So, sort from
		// high to low.
		check(PointCurvatures.Num() == MNumIndices);

		// PointCurvatures is sized to the index range of the mesh.  That may
		// not include all free particles.  If the DescendingPredicate gets an
		// index that is out of bounds of the curvature array, it'll use
		// -FLT_MAX, which will put free particles at the end.  In order to get
		// PointCurvatures to line up with PointOrder indices, offset them by
		// -MStartIdx when not RestrictToLocalIndexRange.

		// PointCurvatures[0] always corresponds to Points[MStartIdx]
		DescendingPredicate<FReal> curvaturePred(PointCurvatures, MStartIdx); // high to low

		// The indexing scheme used for sorting is a little complicated.  The pointOrder
		// array contains point indices.  The sorting binary predicate is handed 2 of
		// those indices, which we use to look up values we want to sort by; curvature
		// in this case.  So, the sort data array needs to be in the original ordering.
		StableSort(&PointOrder[0], PointOrder.Num(), curvaturePred);
	}

	// Move the points to the origin to avoid floating point aliasing far away
	// from the origin.
	FAABB3 Bbox(Points[0], Points[0]);
	for (int i = 1; i < NumPoints; i++)
	{
		Bbox.GrowToInclude(Points[i + Offset]);
	}
	const FVec3 Center = Bbox.Center();

	TArray<FVec3> LocalPoints;
	LocalPoints.AddUninitialized(NumPoints);
	LocalPoints[0] = Points[Offset] - Center;
	FAABB3 LocalBBox(LocalPoints[0], LocalPoints[0]);
	for (int i = 1; i < NumPoints; i++)
	{
		LocalPoints[i] = Points[Offset + i] - Center;
		LocalBBox.GrowToInclude(LocalPoints[i]);
	}
	LocalBBox.Thicken(1.0e-3);
	const FVec3 LocalCenter = LocalBBox.Center();
	const FVec3& LocalMin = LocalBBox.Min();

	// Bias towards points further away from the center of the bounding box.
	// Send points that are the furthest away to the front of the list.
	TArray<FReal> Dist;
	Dist.AddUninitialized(NumPoints);
	for (int i = 0; i < NumPoints; i++)
	{
		Dist[i] = (LocalPoints[i] - LocalCenter).SizeSquared();
	}
	DescendingPredicate<FReal> DescendingDistPred(Dist); // high to low
	StableSort(&PointOrder[0], NumPoints, DescendingDistPred);

	// If all points are coincident, return early.
	const FReal MaxBBoxDim = LocalBBox.Extents().Max();
	if (MaxBBoxDim <= 1.0e-6)
	{
		if (CoincidentVertices && NumPoints > 0)
		{
			CoincidentVertices->Append(&PointOrder[1], NumPoints - 1);
		}
		return PointOrder;
	}

	// We've got our base ordering.  Find coincident vertices and send them to
	// the back of the list.  We hash to a grid of fine enough resolution such
	// that if 2 particles hash to the same cell, then we're going to consider
	// them coincident.
	TSet<int64> OccupiedCells;
	OccupiedCells.Reserve(NumPoints);
	if (CoincidentVertices)
	{
		CoincidentVertices->Reserve(64); // a guess
	}
	int32 NumCoincident = 0;
	{
		const int64 Resolution = static_cast<int64>(floor(MaxBBoxDim / 0.01));
		const FReal CellSize = MaxBBoxDim / Resolution;
		for (int i = 0; i < 2; i++)
		{
			OccupiedCells.Reset();
			Rank.Reset();
			Rank.AddZeroed(NumPoints);
			// Shift the grid by 1/2 a grid cell the second iteration so that
			// we don't miss slightly adjacent coincident points across cell
			// boundaries.
			const FVec3 GridCenter = LocalCenter - FVec3(i * CellSize / 2);
			const int NumCoincidentPrev = NumCoincident;
			for (int j = 0; j < NumPoints - NumCoincidentPrev; j++)
			{
				const int32 Idx = PointOrder[j];
				const FVec3& Pos = LocalPoints[Idx - Offset];
				const TVec3<int64> Coord(
				    static_cast<int64>(floor((Pos[0] - GridCenter[0]) / CellSize + Resolution / 2)),
				    static_cast<int64>(floor((Pos[1] - GridCenter[1]) / CellSize + Resolution / 2)),
				    static_cast<int64>(floor((Pos[2] - GridCenter[2]) / CellSize + Resolution / 2)));
				const int64 FlatIdx =
				    ((Coord[0] * Resolution + Coord[1]) * Resolution) + Coord[2];

				bool AlreadyInSet = false;
				OccupiedCells.Add(FlatIdx, &AlreadyInSet);
				if (AlreadyInSet)
				{
					Rank[Idx - Offset] = 1;
					if (CoincidentVertices)
					{
						CoincidentVertices->Add(Idx);
					}
					NumCoincident++;
				}
			}
			if (NumCoincident > NumCoincidentPrev)
			{
				StableSort(&PointOrder[0], NumPoints - NumCoincidentPrev, AscendingRankPred);
			}
		}
	}
	check(NumCoincident < NumPoints);

	// Use spatial hashing to a grid of variable resolution to distribute points
	// evenly across the volume.
	for (int i = 2; i <= 1024; i += 2) // coarse to fine
	{
		OccupiedCells.Reset();
		Rank.Reset();
		Rank.AddZeroed(NumPoints);

		const int32 Resolution = i;
		check(Resolution > 0);
		check(Resolution % 2 == 0);
		const FReal CellSize = MaxBBoxDim / Resolution;

		// The order in which we process these points matters.  Must do
		// the current highest rank first.
		for (int j = 0; j < NumPoints - NumCoincident; j++)
		{
			const int32 Idx = PointOrder[j];
			const FVec3& Pos = LocalPoints[Idx - Offset];
			// grid center co-located at bbox center:
			const TVec3<int64> Coord(
			    static_cast<int64>(floor((Pos[0] - LocalCenter[0]) / CellSize)) + Resolution / 2,
			    static_cast<int64>(floor((Pos[1] - LocalCenter[1]) / CellSize)) + Resolution / 2,
			    static_cast<int64>(floor((Pos[2] - LocalCenter[2]) / CellSize)) + Resolution / 2);
			const int64 FlatIdx =
			    ((Coord[0] * Resolution + Coord[1]) * Resolution) + Coord[2];

			bool AlreadyInSet = false;
			OccupiedCells.Add(FlatIdx, &AlreadyInSet);
			Rank[Idx - Offset] = AlreadyInSet ? 1 : 0;
		}

		// If every particle mapped to its own cell, we're done.
		if (OccupiedCells.Num() == NumPoints)
		{
			break;
		}
		// If every particle mapped to 1 cell, don't bother sorting.
		if (OccupiedCells.Num() == 1)
		{
			continue;
		}

		// Stable sort by rank.  When the resolution is high, stable sort will
		// do nothing as we'll have nothing but rank 0's.  But then as the grid
		// gets coarser, stable sort will get more and more selective about
		// which particles get promoted.
		//
		// Since the initial ordering was biased by curvature and distance from
		// the center, each rank is similarly ordered. That is, the first vertex
		// to land in a cell will be the most distant, and the highest curvature.
		StableSort(&PointOrder[0], NumPoints - NumCoincident, AscendingRankPred);
	} // end for

	return PointOrder;
}

TArray<int32>
FTriangleMesh::GetVertexImportanceOrdering(const TConstArrayView<FVec3>& Points, TArray<int32>* CoincidentVertices, const bool RestrictToLocalIndexRange)
{
	const TArray<FReal> pointCurvatures = GetCurvatureOnPoints(Points);
	return GetVertexImportanceOrdering(Points, pointCurvatures, CoincidentVertices, RestrictToLocalIndexRange);
}

void FTriangleMesh::RemapVertices(const TArray<int32>& Order)
{
	// Remap element indices
	int32 MinIdx = TNumericLimits<int32>::Max();
	int32 MaxIdx = -TNumericLimits<int32>::Max();
	for (int32 i = 0; i < MElements.Num(); i++)
	{
		TVec3<int32>& elem = MElements[i];
		for (int32 j = 0; j < 3; ++j)
		{
			if (elem[j] != Order[elem[j]])
			{
				elem[j] = Order[elem[j]];
				MinIdx = elem[j] < MinIdx ? elem[j] : MinIdx;
				MaxIdx = elem[j] > MaxIdx ? elem[j] : MaxIdx;
			}
		}
	}
	if (MinIdx != TNumericLimits<int32>::Max())
	{
		ExpandVertexRange(MinIdx, MaxIdx);
		RemoveDuplicateElements();
		RemoveDegenerateElements();
		ResetAuxiliaryStructures();
	}
}

void FTriangleMesh::RemapVertices(const TMap<int32, int32>& Remapping)
{
	if (!Remapping.Num())
	{
		return;
	}
	int32 MinIdx = TNumericLimits<int32>::Max();
	int32 MaxIdx = -TNumericLimits<int32>::Max();
	for (TVec3<int32>& Tri : MElements)
	{
		for (int32 Idx = 0; Idx < 3; ++Idx)
		{
			if (const int32* ToIdx = Remapping.Find(Tri[Idx]))
			{
				Tri[Idx] = *ToIdx;
				MinIdx = *ToIdx < MinIdx ? *ToIdx : MinIdx;
				MaxIdx = *ToIdx > MaxIdx ? *ToIdx : MaxIdx;
			}
		}
	}
	if (MinIdx != TNumericLimits<int32>::Max())
	{
		ExpandVertexRange(MinIdx, MaxIdx);
		RemoveDuplicateElements();
		RemoveDegenerateElements();
		ResetAuxiliaryStructures();
	}
}

void FTriangleMesh::RemoveDuplicateElements()
{
	TArray<int32> ToRemove;
	TSet<TVec3<int32>> Existing;
	for (int32 Idx = 0; Idx < MElements.Num(); ++Idx)
	{
		const TVec3<int32>& Tri = MElements[Idx];
		const TVec3<int32> OrderedTri = GetOrdered(Tri);
		if (!Existing.Contains(OrderedTri))
		{
			Existing.Add(OrderedTri);
			continue;
		}
		ToRemove.Add(Idx);
	}
	for (int32 Idx = ToRemove.Num() - 1; Idx >= 0; --Idx)
	{
		MElements.RemoveAtSwap(ToRemove[Idx]);
	}
}

void FTriangleMesh::RemoveDegenerateElements()
{
	for (int i = MElements.Num() - 1; i >= 0; --i)
	{
		if (MElements[i][0] == MElements[i][1] ||
			MElements[i][0] == MElements[i][2] ||
			MElements[i][1] == MElements[i][2])
		{
			// It's possible that the order of the triangles might be important.
			// RemoveAtSwap() changes the order of the array.  I figure that if
			// you're up for CullDegenerateElements, then triangle reordering is
			// fair game.
			MElements.RemoveAtSwap(i);
		}
	}
}
