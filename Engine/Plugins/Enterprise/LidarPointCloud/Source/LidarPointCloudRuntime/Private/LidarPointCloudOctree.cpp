// Copyright Epic Games, Inc. All Rights Reserved.
 
#include "LidarPointCloudOctree.h"
#include "LidarPointCloudOctreeMacros.h"
#include "LidarPointCloud.h"
#include "Collision/LidarPointCloudCollision.h"
#include "Misc/ScopeTryLock.h"
#include "Containers/Queue.h"
#include "SceneView.h"
#include "Async/Async.h"
#include "Misc/FileHelper.h"

DECLARE_CYCLE_STAT(TEXT("Node Selection"), STAT_NodeSelection, STATGROUP_LidarPointCloud);

int32 FLidarPointCloudOctree::MaxNodeDepth = (1 << (sizeof(FLidarPointCloudOctreeNode::Depth) * 8)) - 1;
int32 FLidarPointCloudOctree::MaxBucketSize = 200;
int32 FLidarPointCloudOctree::NodeGridResolution = 96;

/** Used for grid allocation calculations */
struct FGridAllocation
{
	/** Index to the point inside of the AllocatedPoints */
	int32 Index;

	/** Index of the child node this point would be placed in */
	int32 ChildNodeLocation;

	/** The calculated distance squared from the center of the given point */
	float DistanceFromCenter;

	FGridAllocation() {}
	FGridAllocation(const int32& Index, const FGridAllocation& GridAllocation)
		: Index(Index)
		, ChildNodeLocation(GridAllocation.ChildNodeLocation)
		, DistanceFromCenter(GridAllocation.DistanceFromCenter)
	{
	}
};

/** Used for node size sorting and node selection. */
struct FNodeSizeData
{
	FLidarPointCloudTraversalOctreeNode* Node;
	float Size;
	FNodeSizeData(FLidarPointCloudTraversalOctreeNode* Node, const float& Size) : Node(Node), Size(Size) {}
};

auto PointVisibilitySortingFunction = [](const FLidarPointCloudPoint& A, const FLidarPointCloudPoint& B) { return A.bVisible > B.bVisible; };

FGridAllocation CalculateGridCellData(const FLidarPointCloudPoint* Point, const FVector& Center, const FLidarPointCloudOctree::FSharedLODData& LODData)
{
	FVector CenterRelativeLocation = Point->Location - Center;
	FVector OffsetLocation = CenterRelativeLocation + LODData.Extent;
	FVector NormalizedGridLocation = OffsetLocation * LODData.NormalizationMultiplier;

	// Calculate the location on this node's Grid
	int32 GridX = FMath::Min(FLidarPointCloudOctree::NodeGridResolution - 1, (int32)NormalizedGridLocation.X);
	int32 GridY = FMath::Min(FLidarPointCloudOctree::NodeGridResolution - 1, (int32)NormalizedGridLocation.Y);
	int32 GridZ = FMath::Min(FLidarPointCloudOctree::NodeGridResolution - 1, (int32)NormalizedGridLocation.Z);

	FGridAllocation Allocation;
	Allocation.Index = GridX * FLidarPointCloudOctree::NodeGridResolution * FLidarPointCloudOctree::NodeGridResolution + GridY * FLidarPointCloudOctree::NodeGridResolution + GridZ;
	Allocation.DistanceFromCenter = (FVector(GridX + 0.5f, GridY + 0.5f, GridZ + 0.5f) * LODData.GridSize - OffsetLocation).SizeSquared();
	Allocation.ChildNodeLocation = (CenterRelativeLocation.X > 0 ? 4 : 0) + (CenterRelativeLocation.Y > 0 ? 2 : 0) + (CenterRelativeLocation.Z > 0);

	return Allocation;
}

FORCEINLINE float BrightnessFromColor(const FColor& Color) { return 0.2126 * Color.R + 0.7152 * Color.G + 0.0722 * Color.B; }

bool IsOnBoundsEdge(const FBox& Bounds, const FVector& Location)
{
	return (Location.X == Bounds.Min.X) || (Location.X == Bounds.Max.X) || (Location.Y == Bounds.Min.Y) || (Location.Y == Bounds.Max.Y) || (Location.Z == Bounds.Min.Z) || (Location.Z == Bounds.Max.Z);
}

//////////////////////////////////////////////////////////// FSharedLODData

FLidarPointCloudOctree::FSharedLODData::FSharedLODData(const FVector& InExtent)
	: Radius(InExtent.Size())
	, RadiusSq(FMath::Square(InExtent.Size()))
	, Extent(InExtent)
{
	GridSize = Extent * 2 / FLidarPointCloudOctree::NodeGridResolution;
	NormalizationMultiplier = FVector(FLidarPointCloudOctree::NodeGridResolution) / (Extent * 2);
}

//////////////////////////////////////////////////////////// FLidarPointCloudOctreeNode

FLidarPointCloudOctreeNode::FLidarPointCloudOctreeNode(FLidarPointCloudOctree* Tree, FLidarPointCloudOctreeNode* Parent, const uint8& Depth, const uint8& LocationInParent, const FVector& Center)
	: Depth(Depth)
	, LocationInParent(LocationInParent)
	, Center(Center)
	, bVisibilityDirty(false)
	, NumVisiblePoints(0)
{
	if (Tree)
	{
		Tree->NodeCount[Depth].Increment();
	}
}

FLidarPointCloudOctreeNode::~FLidarPointCloudOctreeNode()
{
	for (int32 i = 0; i < Children.Num(); i++)
	{
		delete Children[i];
		Children[i] = nullptr;
	}
}

void FLidarPointCloudOctreeNode::UpdateNumVisiblePoints()
{
	if (bVisibilityDirty)
	{
		// Sort points to speed up rendering
		Algo::Sort(AllocatedPoints, MoveTemp(PointVisibilitySortingFunction));
		Algo::Sort(PaddingPoints, MoveTemp(PointVisibilitySortingFunction));

		// Recalculate visibility
		NumVisiblePoints = 0;
		FOR(Point, AllocatedPoints)
		{
			if (!Point->bVisible)
			{
				break;
			}

			NumVisiblePoints++;
		}

		FOR(Point, PaddingPoints)
		{
			if (!Point->bVisible)
			{
				break;
			}

			NumVisiblePoints++;
		}

		bVisibilityDirty = false;
	}
}

FBox FLidarPointCloudOctreeNode::GetBounds(const FLidarPointCloudOctree* Tree) const
{
	return FBox(Center - Tree->SharedData[Depth].Extent, Center + Tree->SharedData[Depth].Extent);
}

FSphere FLidarPointCloudOctreeNode::GetSphereBounds(const FLidarPointCloudOctree* Tree) const
{
	return FSphere(Center, Tree->SharedData[Depth].Radius);
}

FLidarPointCloudOctreeNode* FLidarPointCloudOctreeNode::GetChildNodeAtLocation(const uint8& Location) const
{
	for (FLidarPointCloudOctreeNode* Child : Children)
	{
		if (Child->LocationInParent == Location)
		{
			return Child;
		}
	}

	return nullptr;
}

void FLidarPointCloudOctreeNode::InsertPoints(FLidarPointCloudOctree* Tree, const FLidarPointCloudPoint* Points, const int32& Count, ELidarPointCloudDuplicateHandling DuplicateHandling)
{
	const auto& LODData = Tree->SharedData[Depth];

	// Local 
	TArray<FLidarPointCloudPoint> PointBuckets[8];
	TMultiMap<int32, FGridAllocation> NewGridAllocationMap, CurrentGridAllocationMap;

	int32 NumPointsAdded = 0;

	const float MaxDistanceForDuplicate = GetDefault<ULidarPointCloudSettings>()->MaxDistanceForDuplicate;

	// Filter the local set of incoming data
	for (int32 Index = 0; Index < Count; ++Index)
	{
		auto InGridData = CalculateGridCellData(&Points[Index], Center, LODData);
		FGridAllocation* GridCell = NewGridAllocationMap.Find(InGridData.Index);

		// Attempt to allocate the point to this node
		if (GridCell)
		{
			bool bStoreInBucket = true;

			if (DuplicateHandling != ELidarPointCloudDuplicateHandling::Ignore && Points[GridCell->Index].Location.Equals(Points[Index].Location, MaxDistanceForDuplicate))
			{
				if (DuplicateHandling == ELidarPointCloudDuplicateHandling::SelectFirst || BrightnessFromColor(Points[Index].Color) <= BrightnessFromColor(Points[GridCell->Index].Color))
				{
					continue;
				}
				else 
				{
					bStoreInBucket = false;
				}				
			}

			if (InGridData.DistanceFromCenter < GridCell->DistanceFromCenter)
			{
				if (bStoreInBucket)
				{
					PointBuckets[GridCell->ChildNodeLocation].Add(Points[GridCell->Index]);
				}
				
				GridCell->Index = Index;
				GridCell->DistanceFromCenter = InGridData.DistanceFromCenter;
			}
			else if(bStoreInBucket)
			{
				PointBuckets[InGridData.ChildNodeLocation].Add(Points[Index]);
			}
		}
		else
		{
			NewGridAllocationMap.Add(InGridData.Index, FGridAllocation(Index, InGridData));
		}
	}

	// Process incoming points
	{
		FScopeLock Lock(&MapLock);

		// Rebuild Current Grid Mapping
		for (int32 i = 0; i < AllocatedPoints.Num(); ++i)
		{
			auto InGridData = CalculateGridCellData(&AllocatedPoints[i], Center, LODData);
			CurrentGridAllocationMap.Add(InGridData.Index, FGridAllocation(i, InGridData));
		}

		// Compare the incoming data to the currently held set, and replace if necessary
		for (auto& Element : NewGridAllocationMap)
		{
			const int32& GridIndex = Element.Key;
			const FLidarPointCloudPoint& Point = Points[Element.Value.Index];
			FGridAllocation* GridCell = CurrentGridAllocationMap.Find(GridIndex);

			// Attempt to allocate the point to this node
			if (GridCell)
			{
				bool bStoreInBucket = true;

				if (DuplicateHandling != ELidarPointCloudDuplicateHandling::Ignore && AllocatedPoints[GridCell->Index].Location.Equals(Point.Location, MaxDistanceForDuplicate))
				{
					if (DuplicateHandling == ELidarPointCloudDuplicateHandling::SelectFirst || BrightnessFromColor(Point.Color) <= BrightnessFromColor(AllocatedPoints[GridCell->Index].Color))
					{
						continue;
					}
					else
					{
						bStoreInBucket = false;
					}
				}

				// If the new point's distance from center of node is shorter than the existing point's, replace the point
				if (Element.Value.DistanceFromCenter < GridCell->DistanceFromCenter)
				{
					if (bStoreInBucket)
					{
						PointBuckets[GridCell->ChildNodeLocation].Add(AllocatedPoints[GridCell->Index]);
					}

					AllocatedPoints[GridCell->Index] = Point;
					GridCell->DistanceFromCenter = Element.Value.DistanceFromCenter;
				}
				// ... otherwise add it straight to the bucket
				else if (bStoreInBucket)
				{
					PointBuckets[Element.Value.ChildNodeLocation].Add(Point);
				}
			}
			else
			{
				CurrentGridAllocationMap.Add(GridIndex, FGridAllocation(AllocatedPoints.Add(Point), Element.Value));
				NumPointsAdded++;
			}
		}

		// Distribute PaddingPoints into the relevant arrays of PointBuckets
		for (auto& Point : PaddingPoints)
		{
			PointBuckets[CalculateGridCellData(&Point, Center, LODData).ChildNodeLocation].Add(Point);
		}
		NumPointsAdded -= PaddingPoints.Num();
		PaddingPoints.Reset();

		for (uint8 i = 0; i < 8; ++i)
		{
			if (!GetChildNodeAtLocation(i))
			{
				// While the threads are locked, check if any child nodes need creating
				if (Depth < FLidarPointCloudOctree::MaxNodeDepth && PointBuckets[i].Num() > FLidarPointCloudOctree::MaxBucketSize)
				{
					const FVector ChildNodeCenter = Center + LODData.Extent * (FVector(-0.5f) + FVector((i & 4) == 4, (i & 2) == 2, (i & 1) == 1));
					Children.Add(new FLidarPointCloudOctreeNode(Tree, this, Depth + 1, i, ChildNodeCenter));

					// The recursive InserPoints call will happen later, after the Lock is released
				}
				// ... otherwise, points can be re-added back as padding
				else
				{
					PaddingPoints.Append(PointBuckets[i]);
					PointBuckets[i].Reset();
				}
			}
		}

		NumPointsAdded += PaddingPoints.Num();

		// Shrink the data usage
		AllocatedPoints.Shrink();
		PaddingPoints.Shrink();
	}
	
	AddPointCount(Tree, NumPointsAdded);

	// Pass surplus points
	for (uint8 i = 0; i < 8; ++i)
	{
		if (PointBuckets[i].Num() > 0)
		{
			GetChildNodeAtLocation(i)->InsertPoints(Tree, PointBuckets[i].GetData(), PointBuckets[i].Num(), DuplicateHandling);
		}
	}
}

void FLidarPointCloudOctreeNode::InsertPoints(FLidarPointCloudOctree* Tree, FLidarPointCloudPoint** Points, const int32& Count, ELidarPointCloudDuplicateHandling DuplicateHandling)
{
	const auto& LODData = Tree->SharedData[Depth];

	// Local 
	TArray<FLidarPointCloudPoint> PointBuckets[8];
	TMultiMap<int32, FGridAllocation> NewGridAllocationMap, CurrentGridAllocationMap;

	int32 NumPointsAdded = 0;

	const float MaxDistanceForDuplicate = GetDefault<ULidarPointCloudSettings>()->MaxDistanceForDuplicate;

	// Filter the local set of incoming data
	for (int32 Index = 0; Index < Count; Index++)
	{
		auto InGridData = CalculateGridCellData(Points[Index], Center, LODData);
		FGridAllocation* GridCell = NewGridAllocationMap.Find(InGridData.Index);

		// Attempt to allocate the point to this node
		if (GridCell)
		{
			bool bStoreInBucket = true;

			if (DuplicateHandling != ELidarPointCloudDuplicateHandling::Ignore && Points[GridCell->Index]->Location.Equals(Points[Index]->Location, MaxDistanceForDuplicate))
			{
				if (DuplicateHandling == ELidarPointCloudDuplicateHandling::SelectFirst || BrightnessFromColor(Points[Index]->Color) <= BrightnessFromColor(Points[GridCell->Index]->Color))
				{
					continue;
				}
				else
				{
					bStoreInBucket = false;
				}
			}

			if (InGridData.DistanceFromCenter < GridCell->DistanceFromCenter)
			{
				if (bStoreInBucket)
				{
					PointBuckets[GridCell->ChildNodeLocation].Emplace(*Points[GridCell->Index]);
				}

				GridCell->Index = Index;
				GridCell->DistanceFromCenter = InGridData.DistanceFromCenter;
			}
			else if (bStoreInBucket)
			{
				PointBuckets[InGridData.ChildNodeLocation].Emplace(*Points[Index]);
			}
		}
		else
		{
			NewGridAllocationMap.Add(InGridData.Index, FGridAllocation(Index, InGridData));
		}
	}

	// Process incoming points
	{
		FScopeLock Lock(&MapLock);

		// Rebuild Current Grid Mapping
		for (int32 i = 0; i < AllocatedPoints.Num(); i++)
		{
			auto InGridData = CalculateGridCellData(&AllocatedPoints[i], Center, LODData);
			CurrentGridAllocationMap.Add(InGridData.Index, FGridAllocation(i, InGridData));
		}

		// Compare the incoming data to the currently held set, and replace if necessary
		FLidarPointCloudPoint Point;
		for (auto& Element : NewGridAllocationMap)
		{
			const int32& GridIndex = Element.Key;
			Point.CopyFrom(*Points[Element.Value.Index]);
			FGridAllocation* GridCell = CurrentGridAllocationMap.Find(GridIndex);

			// Attempt to allocate the point to this node
			if (GridCell)
			{
				bool bStoreInBucket = true;

				if (DuplicateHandling != ELidarPointCloudDuplicateHandling::Ignore && AllocatedPoints[GridCell->Index].Location.Equals(Point.Location, MaxDistanceForDuplicate))
				{
					if (DuplicateHandling == ELidarPointCloudDuplicateHandling::SelectFirst || BrightnessFromColor(Point.Color) <= BrightnessFromColor(AllocatedPoints[GridCell->Index].Color))
					{
						continue;
					}
					else
					{
						bStoreInBucket = false;
					}
				}

				// If the new point's distance from center of node is shorter than the existing point's, replace the point
				if (Element.Value.DistanceFromCenter < GridCell->DistanceFromCenter)
				{
					if (bStoreInBucket)
					{
						PointBuckets[GridCell->ChildNodeLocation].Add(AllocatedPoints[GridCell->Index]);
					}

					AllocatedPoints[GridCell->Index] = Point;
					GridCell->DistanceFromCenter = Element.Value.DistanceFromCenter;
				}
				// ... otherwise add it straight to the bucket
				else if (bStoreInBucket)
				{
					PointBuckets[Element.Value.ChildNodeLocation].Add(Point);
				}
			}
			else
			{
				CurrentGridAllocationMap.Add(GridIndex, FGridAllocation(AllocatedPoints.Add(Point), Element.Value));
				NumPointsAdded++;
			}
		}

		// Distribute PaddingPoints into the relevant arrays of PointBuckets
		for (auto& PaddingPoint : PaddingPoints)
		{
			PointBuckets[CalculateGridCellData(&PaddingPoint, Center, LODData).ChildNodeLocation].Add(PaddingPoint);
		}
		NumPointsAdded -= PaddingPoints.Num();
		PaddingPoints.Reset();

		for (uint8 i = 0; i < 8; i++)
		{
			if (!GetChildNodeAtLocation(i))
			{
				// While the threads are locked, check if any child nodes need creating
				if (Depth < FLidarPointCloudOctree::MaxNodeDepth && PointBuckets[i].Num() > FLidarPointCloudOctree::MaxBucketSize)
				{
					const FVector ChildNodeCenter = Center + LODData.Extent * (FVector(-0.5f) + FVector((i & 4) == 4, (i & 2) == 2, (i & 1) == 1));
					Children.Add(new FLidarPointCloudOctreeNode(Tree, this, Depth + 1, i, ChildNodeCenter));

					// The recursive InserPoints call will happen later, after the Lock is released
				}
				// ... otherwise, points can be re-added back as padding
				else
				{
					PaddingPoints.Append(PointBuckets[i]);
					PointBuckets[i].Reset();
				}
			}
		}

		NumPointsAdded += PaddingPoints.Num();

		// Shrink the data usage
		AllocatedPoints.Shrink();
		PaddingPoints.Shrink();
	}

	AddPointCount(Tree, NumPointsAdded);

	// Pass surplus points
	for (uint8 i = 0; i < 8; i++)
	{
		if (PointBuckets[i].Num() > 0)
		{
			GetChildNodeAtLocation(i)->InsertPoints(Tree, PointBuckets[i].GetData(), PointBuckets[i].Num(), DuplicateHandling);
		}
	}
}

void FLidarPointCloudOctreeNode::Empty(bool bRecursive)
{
	AllocatedPoints.Empty();
	PaddingPoints.Empty();

	if (bRecursive)
	{
		for (int32 i = 0; i < Children.Num(); i++)
		{
			Children[i]->Empty(true);
		}
	}
}

uint32 FLidarPointCloudOctreeNode::GetMaxDepth() const
{
	uint32 MaxDepth = Depth;

	for (FLidarPointCloudOctreeNode* Child : Children)
	{
		MaxDepth = FMath::Max(MaxDepth, Child->GetMaxDepth());
	}

	return MaxDepth;
}

int64 FLidarPointCloudOctreeNode::GetAllocatedSize(bool bRecursive) const
{
	int64 Size = sizeof(FLidarPointCloudOctreeNode);

	Size += Children.GetAllocatedSize();
	Size += AllocatedPoints.GetAllocatedSize();
	Size += PaddingPoints.GetAllocatedSize();

	if (bRecursive)
	{
		for (FLidarPointCloudOctreeNode* Child : Children)
		{
			Size += Child->GetAllocatedSize(true);
		}
	}

	return Size;
}

void FLidarPointCloudOctreeNode::Serialize(FArchive& Ar, FLidarPointCloudOctree* Tree)
{
	// This is needed as LocationInParent doesn't use a full byte and the FArchive complains
	uint8 FullLocationInParent = LocationInParent;
	Ar << FullLocationInParent << Center << AllocatedPoints << PaddingPoints;

	int32 NumChildren = Children.Num();
	Ar << NumChildren;

	if (Ar.IsLoading())
	{
		LocationInParent = FullLocationInParent;

		AddPointCount(Tree);

		bVisibilityDirty = true;

		Children.AddUninitialized(NumChildren);
		for (int32 i = 0; i < NumChildren; i++)
		{
			Children[i] = new FLidarPointCloudOctreeNode(Tree, this, Depth + 1);
		}
	}

	for (int32 i = 0; i < NumChildren; i++)
	{
		Children[i]->Serialize(Ar, Tree);
	}
}

void FLidarPointCloudOctreeNode::AddPointCount(FLidarPointCloudOctree* Tree, int32 PointCount /*= INT32_MIN*/)
{
	const int64 Count = PointCount == INT32_MIN ? GetNumPoints() : PointCount;

	Tree->PointCount[Depth].Add(Count);
	NumVisiblePoints += Count;
}

//////////////////////////////////////////////////////////// FLidarPointCloudOctree

FLidarPointCloudOctree::FLidarPointCloudOctree()
{
	PointCount.AddDefaulted(MaxNodeDepth + 1);
	NodeCount.AddDefaulted(MaxNodeDepth + 1);
	SharedData.AddDefaulted(MaxNodeDepth + 1);
}

FLidarPointCloudOctree::~FLidarPointCloudOctree()
{
	MarkTraversalOctreesForInvalidation();
}

int32 FLidarPointCloudOctree::GetNumLODs() const
{
	int32 LODCount = 0;

	for (; LODCount < NodeCount.Num(); LODCount++)
	{
		if (NodeCount[LODCount].GetValue() == 0)
		{
			break;
		}
	}

	return LODCount;
}

void FLidarPointCloudOctree::RefreshPointsBounds()
{
	PointsBounds.Init();

	TQueue<const FLidarPointCloudOctreeNode*> Nodes;
	const FLidarPointCloudOctreeNode* CurrentNode = nullptr;
	Nodes.Enqueue(&Root);
	while (Nodes.Dequeue(CurrentNode))
	{
		FOR(Point, CurrentNode->AllocatedPoints)
		{
			PointsBounds += Point->Location;
		}

		FOR(Point, CurrentNode->PaddingPoints)
		{
			PointsBounds += Point->Location;
		}

		for (auto Child : CurrentNode->Children)
		{
			Nodes.Enqueue(Child);
		}
	}
}

int64 FLidarPointCloudOctree::GetNumPoints() const
{
	int64 TotalPointCount = 0;

	for (int32 i = 0; i < PointCount.Num(); i++)
	{
		int64 NumPoints = PointCount[i].GetValue();

		if (NumPoints > 0)
		{
			TotalPointCount += NumPoints;
		}
		else
		{
			break;
		}
	}

	return TotalPointCount;
}

int32 FLidarPointCloudOctree::GetNumNodes() const
{
	int32 TotalNodeCount = 0;

	for (int32 i = 0; i < NodeCount.Num(); i++)
	{
		int32 NumNodes = NodeCount[i].GetValue();
		if (NumNodes > 0)
		{
			TotalNodeCount += NumNodes;
		}
		else
		{
			break;
		}
	}

	return TotalNodeCount;
}

int64 FLidarPointCloudOctree::GetAllocatedSize() const
{
	// Use cached version, if possible
	if (PreviousPointCount == GetNumPoints() && PreviousNodeCount == GetNumNodes())
	{
		return PreviousAllocatedSize;
	}

	FScopeTryLock Lock(&DataLock);
	if (!Lock.IsLocked())
	{
		return 0;
	}

	PreviousPointCount = GetNumPoints();
	PreviousNodeCount = GetNumNodes();

	PreviousAllocatedSize = sizeof(FLidarPointCloudOctree);

	PreviousAllocatedSize += SharedData.GetAllocatedSize();
	PreviousAllocatedSize += PointCount.GetAllocatedSize();

	PreviousAllocatedSize += Root.GetAllocatedSize(true);

	return PreviousAllocatedSize;
}

float FLidarPointCloudOctree::GetEstimatedPointSpacing() const
{
	float Spacing = 0;
	const int64 TotalPointCount = GetNumPoints();

	for (int32 i = 0; i < PointCount.Num(); ++i)
	{
		Spacing += SharedData[i].GridSize.GetMax() * PointCount[i].GetValue() / TotalPointCount;
	}

	return Spacing;
}

void FLidarPointCloudOctree::BuildCollision(const float& Accuracy, const bool& bVisibleOnly)
{
	LidarPointCloudCollision::BuildCollisionMesh(this, Accuracy, bVisibleOnly, &CollisionMesh);
}

void FLidarPointCloudOctree::RemoveCollision()
{
	FScopeLock Lock(&DataLock);

	CollisionMesh.~FTriMeshCollisionData();
	new (&CollisionMesh) FTriMeshCollisionData();
}

void FLidarPointCloudOctree::GetPoints(TArray<FLidarPointCloudPoint*>& Points, int64 StartIndex, int64 Count) const
{
	check(StartIndex >= 0 && StartIndex < GetNumPoints());

	if (Count < 0)
	{
		Count = GetNumPoints();
	}

	Count = FMath::Min(Count, GetNumPoints() - StartIndex);

	// TArray only supports int32 ax max number of elements!
	check(Count <= INT32_MAX);

	Points.Empty(Count);

	FScopeLock Lock(&DataLock);

	ITERATE_NODES_CONST(
	{
		// If no data is required, quit
		if (Count == 0)
		{
			return;
		}

		// Should this node's points be injected?
		if (StartIndex < CurrentNode->GetNumPoints())
		{
			// If the index is at 0 and the requested count is at least the size of this node, append whole arrays
			if (StartIndex == 0 && Count >= CurrentNode->GetNumPoints())
			{
				// Expand the array
				int32 Offset = Points.Num();
				Points.AddUninitialized(CurrentNode->GetNumPoints());
				FLidarPointCloudPoint** Dest = Points.GetData() + Offset;

				// Assign pointers to the data
				for (auto& Point : CurrentNode->AllocatedPoints)
				{
					*Dest++ = &Point;
				}
				for (auto& Point : CurrentNode->PaddingPoints)
				{
					*Dest++ = &Point;
				}

				Count -= CurrentNode->GetNumPoints();
			}
			// ... otherwise iterate over needed data
			else
			{
				// AllocatedPoints
				{
					int32 NumPointsToCopy = FMath::Max(0LL, FMath::Min((int64)CurrentNode->AllocatedPoints.Num(), Count + StartIndex) - StartIndex);
					if (NumPointsToCopy > 0)
					{
						// Expand the array
						int32 Offset = Points.Num();
						Points.AddUninitialized(NumPointsToCopy);
												
						// Setup pointers
						FLidarPointCloudPoint* Src = CurrentNode->AllocatedPoints.GetData() + StartIndex;
						FLidarPointCloudPoint** Dest = Points.GetData() + Offset;

						// Assign source pointers to the destination
						for (int32 i = 0; i < NumPointsToCopy; i++)
						{
							*Dest++ = Src++;
						}

						StartIndex = FMath::Max(0LL, StartIndex - NumPointsToCopy);
						Count -= NumPointsToCopy;
					}
				}

				// PaddingPoints
				{
					int32 NumPointsToCopy = FMath::Max(0LL, FMath::Min((int64)CurrentNode->PaddingPoints.Num(), Count + StartIndex) - StartIndex);
					if (NumPointsToCopy > 0)
					{
						// Expand the array
						int32 Offset = Points.Num();
						Points.AddUninitialized(NumPointsToCopy);

						// Setup pointers
						FLidarPointCloudPoint* Src = CurrentNode->PaddingPoints.GetData() + StartIndex;
						FLidarPointCloudPoint** Dest = Points.GetData() + Offset;

						// Assign source pointers to the destination
						for (int32 i = 0; i < NumPointsToCopy; i++)
						{
							*Dest++ = Src++;
						}

						StartIndex = FMath::Max(0LL, StartIndex - NumPointsToCopy);;
						Count -= NumPointsToCopy;
					}
				}
			}
		}
		// ... or skipped completely?
		else
		{
			StartIndex -= CurrentNode->GetNumPoints();
		}
	}, true);
}

void FLidarPointCloudOctree::GetPointsInSphere(TArray<FLidarPointCloudPoint*>& SelectedPoints, const FSphere& Sphere, const bool& bVisibleOnly) const
{
	SelectedPoints.Reset();
	PROCESS_IN_SPHERE_CONST({ SelectedPoints.Add(Point); });
}

void FLidarPointCloudOctree::GetPointsInBox(TArray<FLidarPointCloudPoint*>& SelectedPoints, const FBox& Box, const bool& bVisibleOnly) const
{
	SelectedPoints.Reset(); 
	PROCESS_IN_BOX_CONST({ SelectedPoints.Add(Point); });
}

void FLidarPointCloudOctree::GetPointsInFrustum(TArray<FLidarPointCloudPoint*>& SelectedPoints, const FConvexVolume& Frustum, const bool& bVisibleOnly) const
{
	SelectedPoints.Reset();
	PROCESS_IN_FRUSTUM_CONST({ SelectedPoints.Add(Point); });
}

void FLidarPointCloudOctree::GetPointsAsCopies(TArray<FLidarPointCloudPoint>& Points, int64 StartIndex, int64 Count) const
{
	// If empty, abort
	if (GetNumPoints() == 0)
	{
		return;
	}

	// Make sure to operate on correct range
	check(StartIndex >= 0 && StartIndex < GetNumPoints());

	if (Count < 0)
	{
		Count = GetNumPoints();
	}

	Count = FMath::Min(Count, GetNumPoints() - StartIndex);

	// TArray only supports int32 ax max number of elements!
	check(Count <= INT32_MAX);

	Points.Empty(Count);

	FScopeLock Lock(&DataLock);

	TQueue<const FLidarPointCloudOctreeNode*> Nodes;
	const FLidarPointCloudOctreeNode* CurrentNode = nullptr;
	Nodes.Enqueue(&Root);
	while (Nodes.Dequeue(CurrentNode))
	{
		// If no data is required, quit
		if (Count == 0)
		{
			return;
		}

		// Should this node's points be injected?
		if (StartIndex < CurrentNode->GetNumPoints())
		{
			// If the index is at 0 and the requested count is at least the size of this node, append whole arrays
			if (StartIndex == 0 && Count >= CurrentNode->GetNumPoints())
			{
				Points.Append(CurrentNode->AllocatedPoints);
				Points.Append(CurrentNode->PaddingPoints);

				Count -= CurrentNode->GetNumPoints();
			}
			// ... otherwise iterate over needed data
			else
			{
				// AllocatedPoints
				{
					int32 NumPointsToCopy = FMath::Max(0LL, FMath::Min((int64)CurrentNode->AllocatedPoints.Num(), Count + StartIndex) - StartIndex);
					if (NumPointsToCopy > 0)
					{
						int32 SrcOffset = StartIndex;
						int32 DestOffset = Points.Num();

						// Expand the array
						Points.AddUninitialized(NumPointsToCopy);

						// Copy contents
						FMemory::Memcpy(Points.GetData() + DestOffset, CurrentNode->AllocatedPoints.GetData() + SrcOffset, NumPointsToCopy * sizeof(FLidarPointCloudPoint));

						StartIndex = FMath::Max(0LL, StartIndex - NumPointsToCopy);
						Count -= NumPointsToCopy;
					}
				}

				// PaddingPoints
				{
					int32 NumPointsToCopy = FMath::Max(0LL, FMath::Min((int64)CurrentNode->PaddingPoints.Num(), Count + StartIndex) - StartIndex);
					if (NumPointsToCopy > 0)
					{
						int32 SrcOffset = StartIndex;
						int32 DestOffset = Points.Num();

						// Expand the array
						Points.AddUninitialized(NumPointsToCopy);

						// Copy contents
						FMemory::Memcpy(Points.GetData() + DestOffset, CurrentNode->PaddingPoints.GetData() + SrcOffset, NumPointsToCopy * sizeof(FLidarPointCloudPoint));

						StartIndex = FMath::Max(0LL, StartIndex - NumPointsToCopy);;
						Count -= NumPointsToCopy;
					}
				}
			}
		}
		// ... or skipped completely?
		else
		{
			StartIndex -= CurrentNode->GetNumPoints();
		}

		for (auto Child : CurrentNode->Children)
		{
			Nodes.Enqueue(Child);
		}
	}
}

void FLidarPointCloudOctree::GetPointsInSphereAsCopies(TArray<FLidarPointCloudPoint>& SelectedPoints, const FSphere& Sphere, const bool& bVisibleOnly) const
{
	SelectedPoints.Reset();
	PROCESS_IN_SPHERE_CONST({ SelectedPoints.Add(*Point); });
}

void FLidarPointCloudOctree::GetPointsInBoxAsCopies(TArray<FLidarPointCloudPoint>& SelectedPoints, const FBox& Box, const bool& bVisibleOnly) const
{
	SelectedPoints.Reset();
	PROCESS_IN_BOX_CONST({ SelectedPoints.Add(*Point); });
}

FLidarPointCloudPoint* FLidarPointCloudOctree::RaycastSingle(const FLidarPointCloudRay& Ray, const float& Radius, const bool& bVisibleOnly)
{
	PROCESS_BY_RAY_CONST({ return Point; });
	return nullptr;
}

bool FLidarPointCloudOctree::RaycastMulti(const FLidarPointCloudRay& Ray, const float& Radius, const bool& bVisibleOnly, TArray<FLidarPointCloudPoint*>& OutHits)
{
	OutHits.Reset();
	PROCESS_BY_RAY_CONST({ OutHits.Add(Point); });
	return OutHits.Num() > 0;
}

bool FLidarPointCloudOctree::RaycastMulti(const FLidarPointCloudRay& Ray, const float& Radius, const bool& bVisibleOnly, TArray<FLidarPointCloudPoint>& OutHits)
{
	OutHits.Reset();
	PROCESS_BY_RAY_CONST({ OutHits.Add(*Point); });
	return OutHits.Num() > 0;
}

bool FLidarPointCloudOctree::HasPointsInSphere(const FSphere& Sphere, const bool& bVisibleOnly) const
{
	PROCESS_IN_SPHERE_CONST({ return true; });
	return false;
}

bool FLidarPointCloudOctree::HasPointsInBox(const FBox& Box, const bool& bVisibleOnly) const
{
	PROCESS_IN_BOX_CONST({ return true; });
	return false;
}

void FLidarPointCloudOctree::SetVisibilityOfPointsInSphere(const bool& bNewVisibility, const FSphere& Sphere)
{
	// Build a box to quickly filter out the points - (IsInsideOrOn vs comparing DistSquared)
	FBox Box(Sphere.Center - FVector(Sphere.W), Sphere.Center + FVector(Sphere.W));
	float RadiusSq = Sphere.W * Sphere.W;

	ITERATE_NODES({
		// Skip node if it already has all points set to the required visibility state
		bool bSkipNode = (CurrentNode->NumVisiblePoints == CurrentNode->GetNumPoints() && bNewVisibility) || (CurrentNode->NumVisiblePoints == 0 && !bNewVisibility);
		if (!bSkipNode)
		{
			CurrentNode->NumVisiblePoints = 0;

			// If node fully inside the radius - do not check individual points
			if (CurrentNode->GetSphereBounds(this).IsInside(Sphere))
			{
				ITERATE_POINTS({ Point->bVisible = bNewVisibility; });

				if (bNewVisibility)
				{
					CurrentNode->NumVisiblePoints = CurrentNode->GetNumPoints();
				}
			}
			else
			{
				ITERATE_POINTS(
				{
					if (Point->bVisible != bNewVisibility && POINT_IN_SPHERE)
					{
						Point->bVisible = bNewVisibility;
					}

					if (Point->bVisible)
					{
						++CurrentNode->NumVisiblePoints;
					}
				});
			}

			CurrentNode->bVisibilityDirty = false;

			// Sort points to speed up rendering
			Algo::Sort(CurrentNode->AllocatedPoints, MoveTemp(PointVisibilitySortingFunction));
			Algo::Sort(CurrentNode->PaddingPoints, MoveTemp(PointVisibilitySortingFunction));
		}
	}, NODE_IN_BOX);
}

void FLidarPointCloudOctree::SetVisibilityOfPointsInSphereAsync(const bool& bNewVisibility, const FSphere& Sphere, TFunction<void(void)> CompletionCallback)
{
	Async(EAsyncExecution::Thread, [this, bNewVisibility, Sphere]
	{
		SetVisibilityOfPointsInSphere(bNewVisibility, Sphere);
	}, MoveTemp(CompletionCallback));
}

void FLidarPointCloudOctree::SetVisibilityOfPointsInBox(const bool& bNewVisibility, const FBox& Box)
{
	ITERATE_NODES({
		// Skip node if it already has all points set to the required visibility state
		bool bSkipNode = (CurrentNode->NumVisiblePoints == CurrentNode->GetNumPoints() && bNewVisibility) || (CurrentNode->NumVisiblePoints == 0 && !bNewVisibility);
		if (!bSkipNode)
		{
			CurrentNode->NumVisiblePoints = 0;

			// If node fully inside the radius - do not check individual points
			if (Box.IsInsideOrOn(CurrentNode->Center - SharedData[CurrentNode->Depth].Extent) && Box.IsInsideOrOn(CurrentNode->Center + SharedData[CurrentNode->Depth].Extent))
			{
				ITERATE_POINTS({ Point->bVisible = bNewVisibility; });

				if (bNewVisibility)
				{
					CurrentNode->NumVisiblePoints = CurrentNode->GetNumPoints();
				}
			}
			else
			{
				ITERATE_POINTS(
				{
					if (Point->bVisible != bNewVisibility && POINT_IN_BOX)
					{
						Point->bVisible = bNewVisibility;
					}

					if (Point->bVisible)
					{
						++CurrentNode->NumVisiblePoints;
					}
				});
			}

			CurrentNode->bVisibilityDirty = false;

			// Sort points to speed up rendering
			Algo::Sort(CurrentNode->AllocatedPoints, MoveTemp(PointVisibilitySortingFunction));
			Algo::Sort(CurrentNode->PaddingPoints, MoveTemp(PointVisibilitySortingFunction));
		}
	}, NODE_IN_BOX);
}

void FLidarPointCloudOctree::SetVisibilityOfPointsInBoxAsync(const bool& bNewVisibility, const FBox& Box, TFunction<void(void)> CompletionCallback)
{
	Async(EAsyncExecution::Thread, [this, bNewVisibility, Box]
	{
		SetVisibilityOfPointsInBox(bNewVisibility, Box);
	}, MoveTemp(CompletionCallback));
}

void FLidarPointCloudOctree::SetVisibilityOfPointsByRay(const bool& bNewVisibility, const FLidarPointCloudRay& Ray, const float& Radius)
{
	const float RadiusSq = Radius * Radius;
	ITERATE_NODES({
		// Skip node if it already has all points set to the required visibility state
		bool bSkipNode = (CurrentNode->NumVisiblePoints == CurrentNode->GetNumPoints() && bNewVisibility) || (CurrentNode->NumVisiblePoints == 0 && !bNewVisibility);
		if (!bSkipNode && Ray.Intersects(CurrentNode->GetBounds(this)))
		{
			CurrentNode->NumVisiblePoints = 0;

			ITERATE_POINTS(
			{
				if (Point->bVisible != bNewVisibility && POINT_BY_RAY)
				{
					Point->bVisible = bNewVisibility;
				}

				if (Point->bVisible)
				{
					++CurrentNode->NumVisiblePoints;
				}
			});

			CurrentNode->bVisibilityDirty = false;

			// Sort points to speed up rendering
			Algo::Sort(CurrentNode->AllocatedPoints, MoveTemp(PointVisibilitySortingFunction));
			Algo::Sort(CurrentNode->PaddingPoints, MoveTemp(PointVisibilitySortingFunction));

			for (auto Child : CurrentNode->Children)
			{
				Nodes.Enqueue(Child);
			}
		}
	}, false);
}

void FLidarPointCloudOctree::SetVisibilityOfPointsByRayAsync(const bool& bNewVisibility, const FLidarPointCloudRay& Ray, const float& Radius, TFunction<void(void)> CompletionCallback /*= nullptr*/)
{
	Async(EAsyncExecution::Thread, [this, bNewVisibility, Ray, Radius]
	{
		SetVisibilityOfPointsByRay(bNewVisibility, Ray, Radius);
	}, MoveTemp(CompletionCallback));
}

void FLidarPointCloudOctree::HideAll()
{
	ITERATE_NODES({
		if (CurrentNode->NumVisiblePoints > 0)
		{
			ITERATE_POINTS({ Point->bVisible = false; });

			CurrentNode->NumVisiblePoints = 0;
			CurrentNode->bVisibilityDirty = false;
		}
		}, true);
}

void FLidarPointCloudOctree::UnhideAll()
{
	ITERATE_NODES({
		if (CurrentNode->NumVisiblePoints != CurrentNode->GetNumPoints())
		{
			ITERATE_POINTS({ Point->bVisible = true; });

			CurrentNode->NumVisiblePoints = CurrentNode->GetNumPoints();
			CurrentNode->bVisibilityDirty = false;
		}
	}, true);
}

void FLidarPointCloudOctree::ResetVisibilityAsync(TFunction<void(void)> CompletionCallback)
{
	Async(EAsyncExecution::Thread, [this]
	{
		ResetVisibilityAsync();
	}, MoveTemp(CompletionCallback));
}

void FLidarPointCloudOctree::ExecuteActionOnAllPoints(TFunction<void(FLidarPointCloudPoint*)> Action, const bool& bVisibleOnly)
{
	PROCESS_ALL({ Action(Point); });
}

void FLidarPointCloudOctree::ExecuteActionOnAllPointsAsync(TFunction<void(FLidarPointCloudPoint*)> Action, const bool& bVisibleOnly, TFunction<void(void)> CompletionCallback /*= nullptr*/)
{
	Async(EAsyncExecution::Thread, [this, Action, bVisibleOnly]
	{
		ExecuteActionOnAllPointsAsync(Action, bVisibleOnly);
	}, MoveTemp(CompletionCallback));
}

void FLidarPointCloudOctree::ExecuteActionOnPointsInSphere(TFunction<void(FLidarPointCloudPoint*)> Action, const FSphere& Sphere, const bool& bVisibleOnly)
{
	PROCESS_IN_SPHERE({ Action(Point); });
}

void FLidarPointCloudOctree::ExecuteActionOnPointsInSphereAsync(TFunction<void(FLidarPointCloudPoint*)> Action, const FSphere& Sphere, const bool& bVisibleOnly, TFunction<void(void)> CompletionCallback)
{
	Async(EAsyncExecution::Thread, [this, Action, Sphere, bVisibleOnly]
	{
		ExecuteActionOnPointsInSphere(Action, Sphere, bVisibleOnly);
	}, MoveTemp(CompletionCallback));
}

void FLidarPointCloudOctree::ExecuteActionOnPointsInBox(TFunction<void(FLidarPointCloudPoint*)> Action, const FBox& Box, const bool& bVisibleOnly)
{
	PROCESS_IN_BOX({ Action(Point); });
}

void FLidarPointCloudOctree::ExecuteActionOnPointsInBoxAsync(TFunction<void(FLidarPointCloudPoint*)> Action, const FBox& Box, const bool& bVisibleOnly, TFunction<void(void)> CompletionCallback)
{
	Async(EAsyncExecution::Thread, [this, Action, Box, bVisibleOnly]
	{
		ExecuteActionOnPointsInBox(Action, Box, bVisibleOnly);
	}, MoveTemp(CompletionCallback));
}

void FLidarPointCloudOctree::ExecuteActionOnPointsByRay(TFunction<void(FLidarPointCloudPoint*)> Action, const FLidarPointCloudRay& Ray, const float& Radius, bool bVisibleOnly)
{
	PROCESS_BY_RAY({ Action(Point); });
}

void FLidarPointCloudOctree::ExecuteActionOnPointsByRayAsync(TFunction<void(FLidarPointCloudPoint*)> Action, const FLidarPointCloudRay& Ray, const float& Radius, bool bVisibleOnly, TFunction<void(void)> CompletionCallback)
{
	Async(EAsyncExecution::Thread, [this, Action, Ray, Radius, bVisibleOnly]
	{
		ExecuteActionOnPointsByRay(Action, Ray, Radius, bVisibleOnly);
	}, MoveTemp(CompletionCallback));
}

void FLidarPointCloudOctree::ApplyColorToAllPoints(const FColor& NewColor, const bool& bVisibleOnly)
{
	PROCESS_ALL({ Point->Color = NewColor; });
}

void FLidarPointCloudOctree::ApplyColorToPointsInSphere(const FColor& NewColor, const FSphere& Sphere, const bool& bVisibleOnly)
{
	PROCESS_IN_SPHERE({ Point->Color = NewColor; });
}

void FLidarPointCloudOctree::ApplyColorToPointsInBox(const FColor& NewColor, const FBox& Box, const bool& bVisibleOnly)
{
	PROCESS_IN_BOX({ Point->Color = NewColor; });
}

void FLidarPointCloudOctree::ApplyColorToPointsByRay(const FColor& NewColor, const FLidarPointCloudRay& Ray, const float& Radius, bool bVisibleOnly)
{
	PROCESS_BY_RAY({ Point->Color = NewColor; });
}

void FLidarPointCloudOctree::MarkPointVisibilityDirty()
{
	TQueue<FLidarPointCloudOctreeNode*> Nodes;
	FLidarPointCloudOctreeNode* CurrentNode = nullptr;
	Nodes.Enqueue(&Root);
	while (Nodes.Dequeue(CurrentNode))
	{
		CurrentNode->bVisibilityDirty = true;

		for (auto Child : CurrentNode->Children)
		{
			Nodes.Enqueue(Child);
		}
	}
}

void FLidarPointCloudOctree::Initialize(const FBox& InBounds)
{
	if (!SetNewBounds(InBounds))
	{
		return;
	}

	MaxBucketSize = GetDefault<ULidarPointCloudSettings>()->MaxBucketSize;
	NodeGridResolution = GetDefault<ULidarPointCloudSettings>()->NodeGridResolution;

	// Pre-calculate the shared per LOD data
	for (int32 i = 0; i < SharedData.Num(); i++)
	{
		SharedData[i] = FSharedLODData(UniformBounds.GetExtent() / FMath::Pow(2, i));
		NodeCount[i].Reset();
		PointCount[i].Reset();
	}

	Empty(true);
}

void FLidarPointCloudOctree::ShiftPointsBy(FDoubleVector Offset, bool bRefreshPointsBounds)
{
	TQueue<FLidarPointCloudOctreeNode*> Nodes;
	FLidarPointCloudOctreeNode* CurrentNode = nullptr;
	Nodes.Enqueue(&Root);
	while (Nodes.Dequeue(CurrentNode))
	{
		CurrentNode->Center = (Offset + CurrentNode->Center).ToVector();

		FOR(Point, CurrentNode->AllocatedPoints)
		{
			Point->Location = (Offset + Point->Location).ToVector();
		}

		FOR(Point, CurrentNode->PaddingPoints)
		{
			Point->Location = (Offset + Point->Location).ToVector();
		}

		for (auto Child : CurrentNode->Children)
		{
			Nodes.Enqueue(Child);
		}
	}

	Bounds.Min = (Offset + Bounds.Min).ToVector();
	Bounds.Max = (Offset + Bounds.Max).ToVector();
	UniformBounds.Min = (Offset + UniformBounds.Min).ToVector();
	UniformBounds.Max = (Offset + UniformBounds.Max).ToVector();

	MarkTraversalOctreesForInvalidation();

	if (bRefreshPointsBounds)
	{
		RefreshPointsBounds();
	}
}

void FLidarPointCloudOctree::RemovePoint(const FLidarPointCloudPoint* Point, bool bRefreshPointsBounds)
{
	if (!Point)
	{
		return;
	}
	
	int32 Index = -1;
	bool bAllocatedPoint = false;
	FLidarPointCloudOctreeNode* CurrentNode = &Root;
	while (CurrentNode)
	{
		// Scan Allocated Points
		{
			const FLidarPointCloudPoint* Start = CurrentNode->AllocatedPoints.GetData();
			const int32 NumElements = CurrentNode->AllocatedPoints.Num();
			for (const FLidarPointCloudPoint* Data = Start, *DataEnd = Start + NumElements; Data != DataEnd; ++Data)
			{
				if (Data == Point)
				{
					Index = Data - Start;
					bAllocatedPoint = true;
					break;
				}
			}
		}

		// Scan Padding Points
		{
			const FLidarPointCloudPoint* Start = CurrentNode->PaddingPoints.GetData();
			const int32 NumElements = CurrentNode->PaddingPoints.Num();
			for (const FLidarPointCloudPoint* Data = Start, *DataEnd = Start + NumElements; Data != DataEnd; ++Data)
			{
				if (Data == Point)
				{
					Index = Data - Start;
					break;
				}
			}
		}

		if (Index > 0)
		{
			RemovePoint_Internal(CurrentNode, Index, bAllocatedPoint);
			break;
		}
		else
		{
			FVector CenterRelativeLocation = Point->Location - CurrentNode->Center;
			CurrentNode = CurrentNode->GetChildNodeAtLocation((CenterRelativeLocation.X > 0 ? 4 : 0) + (CenterRelativeLocation.Y > 0 ? 2 : 0) + (CenterRelativeLocation.Z > 0));
		}
	}

	if (bRefreshPointsBounds)
	{
		RefreshPointsBounds();
	}
}

void FLidarPointCloudOctree::RemovePoint(FLidarPointCloudPoint Point, bool bRefreshPointsBounds)
{
	bool bAllocatedPoint = false;
	FLidarPointCloudOctreeNode* CurrentNode = &Root;
	while (CurrentNode)
	{
		int32 Index = CurrentNode->AllocatedPoints.IndexOfByKey(Point);
		if (Index != INDEX_NONE)
		{
			bAllocatedPoint = true;
		}
		else
		{
			Index = CurrentNode->PaddingPoints.IndexOfByKey(Point);
		}

		if (Index != INDEX_NONE)
		{
			RemovePoint_Internal(CurrentNode, Index, bAllocatedPoint);
			break;
		}
		else
		{
			FVector CenterRelativeLocation = Point.Location - CurrentNode->Center;
			CurrentNode = CurrentNode->GetChildNodeAtLocation((CenterRelativeLocation.X > 0 ? 4 : 0) + (CenterRelativeLocation.Y > 0 ? 2 : 0) + (CenterRelativeLocation.Z > 0));
		}
	}

	if (bRefreshPointsBounds)
	{
		RefreshPointsBounds();
	}
}

void FLidarPointCloudOctree::RemovePoints(TArray<FLidarPointCloudPoint*>& Points, bool bRefreshPointsBounds)
{
	if (Points.Num() == 0)
	{
		return;
	}

	FOR(Point, Points)
	{
		(*Point)->bMarkedForDeletion = true;
	}

	ITERATE_NODES(
	{
		int64 NumRemoved = 0;

		// Scan Allocated Points
		{
			FLidarPointCloudPoint* Start = CurrentNode->AllocatedPoints.GetData();
			const int32 NumElements = CurrentNode->AllocatedPoints.Num();
			for (FLidarPointCloudPoint* Data = Start, *DataEnd = Start + NumElements; Data != DataEnd; ++Data)
			{
				if (Data->bMarkedForDeletion)
				{
					CurrentNode->AllocatedPoints.RemoveAtSwap(Data - Start, 1, false);
					++NumRemoved;
					--DataEnd; 
					--Data;
				}
			}
		}

		// Scan Padding Points
		{
			FLidarPointCloudPoint* Start = CurrentNode->PaddingPoints.GetData();
			const int32 NumElements = CurrentNode->PaddingPoints.Num();
			for (FLidarPointCloudPoint* Data = Start, *DataEnd = Start + NumElements; Data != DataEnd; ++Data)
			{
				if (Data->bMarkedForDeletion)
				{
					CurrentNode->PaddingPoints.RemoveAtSwap(Data - Start, 1, false);
					++NumRemoved;
					--DataEnd;
					--Data;
				}
			}
		}

		if (NumRemoved > 0)
		{
			// #todo: Fetch points from child nodes / padding points to fill the gap
			
			CurrentNode->AddPointCount(this, -NumRemoved);

			// Reduce space usage
			CurrentNode->AllocatedPoints.Shrink();
			CurrentNode->PaddingPoints.Shrink();

			// Sort points to speed up rendering
			Algo::Sort(CurrentNode->AllocatedPoints, MoveTemp(PointVisibilitySortingFunction));
			Algo::Sort(CurrentNode->PaddingPoints, MoveTemp(PointVisibilitySortingFunction));
		}
	}, true);

	if (bRefreshPointsBounds)
	{
		RefreshPointsBounds();
	}
}

void FLidarPointCloudOctree::RemovePointsInSphere(const FSphere& Sphere, const bool& bVisibleOnly, bool bRefreshPointsBounds)
{
	// #todo: This can be optimized by removing points inline
	TArray<FLidarPointCloudPoint*> SelectedPoints;
	GetPointsInSphere(SelectedPoints, Sphere, bVisibleOnly);
	RemovePoints(SelectedPoints, bRefreshPointsBounds);
}

void FLidarPointCloudOctree::RemovePointsInBox(const FBox& Box, const bool& bVisibleOnly, bool bRefreshPointsBounds)
{
	// #todo: This can be optimized by removing points inline
	TArray<FLidarPointCloudPoint*> SelectedPoints;
	GetPointsInBox(SelectedPoints, Box, bVisibleOnly);
	RemovePoints(SelectedPoints, bRefreshPointsBounds);
}

void FLidarPointCloudOctree::RemovePointsByRay(const FLidarPointCloudRay& Ray, const float& Radius, const bool& bVisibleOnly, bool bRefreshPointsBounds)
{
	// #todo: This can be optimized by removing points inline
	TArray<FLidarPointCloudPoint*> SelectedPoints;
	RaycastMulti(Ray, Radius, bVisibleOnly, SelectedPoints);
	RemovePoints(SelectedPoints, bRefreshPointsBounds);
}

void FLidarPointCloudOctree::RemoveHiddenPoints(bool bRefreshPointsBounds)
{
	ITERATE_NODES(
	{
		int64 NumRemoved = 0;

		// Scan Allocated Points
		{
			FLidarPointCloudPoint* Start = CurrentNode->AllocatedPoints.GetData();
			const int32 NumElements = CurrentNode->AllocatedPoints.Num();
			for (FLidarPointCloudPoint* Data = Start, *DataEnd = Start + NumElements; Data != DataEnd; ++Data)
			{
				if (!Data->bVisible)
				{
					CurrentNode->AllocatedPoints.RemoveAtSwap(Data - Start, 1, false);
					++NumRemoved;
					--DataEnd; 
					--Data;
				}
			}
		}

		// Scan Padding Points
		{
			FLidarPointCloudPoint* Start = CurrentNode->PaddingPoints.GetData();
			const int32 NumElements = CurrentNode->PaddingPoints.Num();
			for (FLidarPointCloudPoint* Data = Start, *DataEnd = Start + NumElements; Data != DataEnd; ++Data)
			{
				if (!Data->bVisible)
				{
					CurrentNode->PaddingPoints.RemoveAtSwap(Data - Start, 1, false);
					++NumRemoved;
					--DataEnd;
					--Data;
				}
			}
		}

		if (NumRemoved > 0)
		{
			// #todo: Fetch points from child nodes / padding points to fill the gap
			
			CurrentNode->AddPointCount(this, -NumRemoved);

			// Reduce space usage
			CurrentNode->AllocatedPoints.Shrink();
			CurrentNode->PaddingPoints.Shrink();

			// Set visibility data
			CurrentNode->NumVisiblePoints = CurrentNode->GetNumPoints();
			CurrentNode->bVisibilityDirty = false;
		}
	}, true);

	if (bRefreshPointsBounds)
	{
		RefreshPointsBounds();
	}
}

void FLidarPointCloudOctree::Empty(bool bDestroyNodes)
{
	if (bDestroyNodes)
	{
		Root.~FLidarPointCloudOctreeNode();

		// Reset node counters
		for (auto& Count : NodeCount)
		{
			Count.Reset();
		}

		new (&Root) FLidarPointCloudOctreeNode(this, nullptr, 0, 0, Bounds.GetCenter());

		MarkTraversalOctreesForInvalidation();
	}
	else
	{
		Root.Empty(true);
	}

	PointsBounds.Init();

	// Reset point counters
	for (auto& Count : PointCount)
	{
		Count.Reset();
	}
}

bool FLidarPointCloudOctree::SetNewBounds(const FBox& InBounds)
{
	if (!InBounds.IsValid)
	{
		return false;
	}

	Bounds = InBounds;

	FVector BoundsExtent = FVector(Bounds.GetExtent().GetMax());
	UniformBounds = FBox(Bounds.GetCenter() - BoundsExtent, Bounds.GetCenter() + BoundsExtent);

	return true;
}

void FLidarPointCloudOctree::RemovePoint_Internal(FLidarPointCloudOctreeNode* Node, int32 Index, bool bAllocatedPoint)
{
	Node->AddPointCount(this, -1);

	// Remove the point from relevant array
	(bAllocatedPoint ? Node->AllocatedPoints : Node->PaddingPoints).RemoveAt(Index);

	// #todo: Fetch points from child nodes / padding points to fill the gap
}

void FLidarPointCloudOctree::MarkTraversalOctreesForInvalidation()
{
	for (int32 i = 0; i < LinkedTraversalOctrees.Num(); ++i)
	{
		auto TraversalOctree = LinkedTraversalOctrees[i];

		// Remove null
		if (!TraversalOctree)
		{
			LinkedTraversalOctrees.RemoveAtSwap(i--);
		}
		else if(TraversalOctree->bValid)
		{
			TraversalOctree->bValid = false;
		}
	}
}

FArchive& operator<<(FArchive& Ar, FLidarPointCloudOctree& O)
{
	Ar << O.Bounds;

	// Collision Mesh data
	if (Ar.CustomVer(ULidarPointCloud::PointCloudFileGUID) > 13)
	{
		FTriMeshCollisionData Dummy;
		FTriMeshCollisionData* CollisionMesh = Ar.IsCooking() ? &Dummy : &O.CollisionMesh;

		Ar << CollisionMesh->Vertices;

		int32 NumIndices = CollisionMesh->Indices.Num();
		Ar << NumIndices;

		if (Ar.IsLoading())
		{
			CollisionMesh->Indices.AddUninitialized(NumIndices);
		}

		Ar.Serialize(CollisionMesh->Indices.GetData(), NumIndices * sizeof(FTriIndices));
	}

	if (Ar.IsLoading())
	{
		O.Initialize(O.Bounds);
	}

	O.Root.Serialize(Ar, &O);

	// Points Bounds
	if (Ar.CustomVer(ULidarPointCloud::PointCloudFileGUID) > 14)
	{
		Ar << O.PointsBounds;
	}
	else
	{
		O.RefreshPointsBounds();
	}

	return Ar;
}

//////////////////////////////////////////////////////////// FLidarPointCloudTraversalOctreeNode

FLidarPointCloudTraversalOctreeNode::FLidarPointCloudTraversalOctreeNode()
	: DataNode(nullptr)
	, Parent(nullptr)
{
}

void FLidarPointCloudTraversalOctreeNode::Build(FLidarPointCloudOctreeNode* Node, const FTransform& LocalToWorld)
{
	DataNode = Node;
	Center = LocalToWorld.TransformPosition(Node->Center);
	Depth = Node->Depth;

	Children.AddZeroed(Node->Children.Num());
	for (int32 i = 0; i < Children.Num(); i++)
	{
		if (Node->Children[i])
		{
			Children[i].Build(Node->Children[i], LocalToWorld);
			Children[i].Parent = this;
		}
	}
}

void FLidarPointCloudTraversalOctreeNode::CalculateVirtualDepth(const TArray<float>& LevelWeights, const float& VDMultiplier, const float& PointSizeBias)
{
	if (!bSelected)
	{
		return;
	}

	TQueue<const FLidarPointCloudTraversalOctreeNode*> Nodes;
	const FLidarPointCloudTraversalOctreeNode* CurrentNode = nullptr;

	// Calculate virtual depth factor
	float VDFactor = 0;
	Nodes.Enqueue(this);
	while (Nodes.Dequeue(CurrentNode))
	{
		for (auto& Child : CurrentNode->Children)
		{
			if (Child.bSelected)
			{
				Nodes.Enqueue(&Child);
			}
		}

		float LocalVDFactor = CurrentNode->Depth * CurrentNode->DataNode->GetNumPoints() * LevelWeights[CurrentNode->Depth];

		if (CurrentNode != this && PointSizeBias > 0)
		{
			LocalVDFactor /= (CurrentNode->Parent->Children.Num() - 1) * PointSizeBias + 1;
		}

		VDFactor += LocalVDFactor;
	}

	// Calculate weighted number of visible points
	float NumPoints = 0;
	Nodes.Enqueue(this);
	while (Nodes.Dequeue(CurrentNode))
	{
		for (auto& Child : CurrentNode->Children)
		{
			if (Child.bSelected)
			{
				Nodes.Enqueue(&Child);
			}
		}

		NumPoints += CurrentNode->DataNode->GetNumPoints() * LevelWeights[CurrentNode->Depth];
	}

	// Calculate the Virtual Depth
	VirtualDepth = VDFactor / NumPoints * VDMultiplier;
}

//////////////////////////////////////////////////////////// FLidarPointCloudTraversalOctree

FLidarPointCloudTraversalOctree::FLidarPointCloudTraversalOctree(FLidarPointCloudOctree* Octree, const FTransform& LocalToWorld)
	: Octree(Octree)
	, bValid(false)
{
	Octree->RegisterTraversalOctree(this);
	
	// Reset the tree
	Extents.Empty();
	RadiiSq.Empty();
	PointCount.Empty();
	NumPoints = 0;
	Root.~FLidarPointCloudTraversalOctreeNode();
	new (&Root) FLidarPointCloudTraversalOctreeNode();

	// Calculate properties
	NumLODs = Octree->GetNumLODs();

	FBox WorldBounds = Octree->UniformBounds.TransformBy(LocalToWorld);
	for (int32 i = 0; i < NumLODs; i++)
	{
		Extents.Emplace(i == 0 ? WorldBounds.GetExtent() : Extents.Last() * 0.5f);
		RadiiSq.Emplace(FMath::Square(Extents.Last().Size()));
	}

	for (auto& Count : Octree->PointCount)
	{
		PointCount.Add(Count.GetValue());
		NumPoints += Count.GetValue();
	}

	// Star cloning the node data
	Root.Build(&Octree->Root, LocalToWorld);

	bValid = true;
}

FLidarPointCloudTraversalOctree::~FLidarPointCloudTraversalOctree()
{
	Octree->UnregisterTraversalOctree(this);
}

TArray<FLidarPointCloudTraversalOctreeNode*> FLidarPointCloudTraversalOctree::GetVisibleNodes(const FSceneView* View, const float& MinScreenSizeSq, const int32& PointBudget, const float& ScreenCenterImportance, const int32& MinDepth, const int32& MaxDepth)
{
	SCOPE_CYCLE_COUNTER(STAT_NodeSelection);

	// For Top / Front / Side views disable ScreenSize checks
	const bool bSkipMinScreenSize = !View->bIsGameView && !View->IsPerspectiveProjection();

	// Collect Nodes
	TArray<FNodeSizeData> NodeSizeData;
	{
		const FMatrix& ProjMatrix = View->ViewMatrices.GetProjectionMatrix();
		const float ScreenSizeFactor = FMath::Square(FMath::Max(0.5f * ProjMatrix.M[0][0], 0.5f * ProjMatrix.M[1][1]));
		const FVector& ViewOrigin = View->ViewMatrices.GetViewOrigin();

		TQueue<FLidarPointCloudTraversalOctreeNode*> Nodes;
		FLidarPointCloudTraversalOctreeNode* CurrentNode = nullptr;
		Nodes.Enqueue(&Root);
		while (Nodes.Dequeue(CurrentNode))
		{
			// Reset selection flag
			CurrentNode->bSelected = false;

			// Update number of visible points, if needed
			CurrentNode->DataNode->UpdateNumVisiblePoints();

			// In Frustum?
			// #todo: Skip frustum checks for nodes fully in frustum
			if (!View->ViewFrustum.IntersectBox(CurrentNode->Center, Extents[CurrentNode->Depth]))
			{
				continue;
			}

			// Only process this node if it has any visible points - do not use continue; as the children may still contain visible points!
			if (CurrentNode->DataNode->GetNumVisiblePoints() > 0 && CurrentNode->Depth >= MinDepth)
			{
				float ScreenSizeSq = 0;

				FVector VectorToNode = CurrentNode->Center - ViewOrigin;
				const float DistSqr = VectorToNode.SizeSquared();

				// If the camera is within this node's bounds, it should always be qualified for rendering
				if (DistSqr <= RadiiSq[CurrentNode->Depth])
				{
					// Subtract Depth to maintain hierarchy 
					ScreenSizeSq = 10000 - CurrentNode->Depth;
				}
				else
				{
					ScreenSizeSq = ScreenSizeFactor * RadiiSq[CurrentNode->Depth] / FMath::Max(1.0f, DistSqr);

					// Check for minimum screen size
					if (!bSkipMinScreenSize && ScreenSizeSq < MinScreenSizeSq)
					{
						continue;
					}

					// Add optional preferential selection for nodes closer to the screen center
					if (ScreenCenterImportance > 0)
					{
						VectorToNode.Normalize();
						float Dot = FVector::DotProduct(View->GetViewDirection(), VectorToNode);

						ScreenSizeSq = FMath::Lerp(ScreenSizeSq, ScreenSizeSq * Dot, ScreenCenterImportance);
					}
				}

				NodeSizeData.Emplace(CurrentNode, ScreenSizeSq);
			}

			if (MaxDepth < 0 || CurrentNode->Depth < MaxDepth)
			{
				for (auto& Child : CurrentNode->Children)
				{
					Nodes.Enqueue(&Child);
				}
			}
		}
	}

	// Sort Nodes
	Algo::Sort(NodeSizeData, [](const FNodeSizeData& A, const FNodeSizeData& B) { return A.Size > B.Size; });

	// Select Nodes
	int32 TotalPointsSelected = 0;
	TArray<FLidarPointCloudTraversalOctreeNode*> SelectedNodes;

	for (FNodeSizeData& Element : NodeSizeData)
	{
		int32 NewNumPointsSelected = TotalPointsSelected + Element.Node->DataNode->GetNumVisiblePoints();

		if (NewNumPointsSelected <= PointBudget)
		{
			SelectedNodes.Add(Element.Node);
			TotalPointsSelected = NewNumPointsSelected;
			Element.Node->bSelected = true;
		}
	}

	return SelectedNodes;
}

void FLidarPointCloudTraversalOctree::CalculateSpriteSize(TArray<FLidarPointCloudTraversalOctreeNode*>& SelectedNodes, const float& PointSizeBias)
{
	for (auto Node : SelectedNodes)
	{
		Node->CalculateVirtualDepth(CalculateLevelWeights(), 255.0f / NumLODs, PointSizeBias);
	}
}

TArray<float> FLidarPointCloudTraversalOctree::CalculateLevelWeights() const
{
	TArray<float> LevelWeights;
	LevelWeights.AddZeroed(NumLODs);

	for (int32 i = 0; i < LevelWeights.Num(); i++)
	{
		LevelWeights[i] = (float)PointCount[i] / NumPoints;
	}

	return LevelWeights;
}
