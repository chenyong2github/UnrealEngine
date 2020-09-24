// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "VectorTypes.h"
#include "MatrixTypes.h"
#include "BoxTypes.h"
#include "FrameTypes.h"
#include "Util/IndexPriorityQueue.h"
#include "Util/DynamicVector.h"


/**
 * TMeshDijkstra computes graph distances on a mesh from seed point(s) using Dijkstra's algorithm.
 *
 * Templated on the point set type, which must provide positions, normals, and neighbours.
 * Currently will only work for FDynamicMesh3 and FDynamicPointSet3 because of call to PointSetType->VtxVerticesItr()
 */
template<class PointSetType>
class TMeshDijkstra
{
public:
	/** PointSet we are calculating on */
	const PointSetType* PointSet;

	/**
	 * Return the 3D Position of a given Point in the PointSet. 
	 * This function is set to PointSet->GetVertex() in the constructor below, but can be 
	 * replaced with an external function if necessary (eg to provide deformed mesh positions, etc)
	 */
	TUniqueFunction<FVector3d(int32)> GetPositionFunc;


	/**
	 * Construct TMeshDijkstra for the given PointSet. We will hold a reference to this
	 * PointSet for the lifetime of the class.
	 */
	TMeshDijkstra(const PointSetType* PointSetIn)
	{
		PointSet = PointSetIn;

		int32 MaxID = PointSet->MaxVertexID();
		Queue.Initialize(MaxID);

		MaxGraphDistance = 0.0;
		MaxGraphDistancePointID = -1;

		GetPositionFunc = [this](int32 PointID) { return  PointSet->GetVertex(PointID); };
	}

	/**
	 * Reset internal data structures but keep allocated memory
	 */
	void Reset()
	{
		IDToNodeIndexMap.Reset();
		AllocatedNodes.Clear();
		Queue.Clear(false);
		MaxGraphDistance = 0.0;
		MaxGraphDistancePointID = -1;
	}



	/**
	 * Computes outwards from seed points to all points that are less/equal to ComputeToMaxDistance from the seed.
	 * @param SeedPointsIn seed points defined as 2D vector tuples, will be interpreted as (seed_point_vertex_id, seed_distance)
	 * @param ComputeToMaxDistance target radius for parameterization, will not set UVs on points with graph-distance larger than this
	 */
	void ComputeToMaxDistance(const TArray<FVector2d>& SeedPointsIn, double ComputeToMaxDistanceIn)
	{
		SeedPoints = SeedPointsIn;
		MaxGraphDistance = 0.0f;
		MaxGraphDistancePointID = -1;

		for (const FVector2d& SeedPoint : SeedPoints )
		{
			int32 PointID = (int32)SeedPoint.X;
			if (Queue.Contains(PointID) == false)
			{
				FGraphNode* Node = GetNodeForPointSetID(PointID, true);
				Node->GraphDistance = SeedPoint.Y;
				Node->bFrozen = true;
				Queue.Insert(PointID, Node->GraphDistance);
			}
		}

		while (Queue.GetCount() > 0) 
		{
			int32 NextID = Queue.Dequeue();
			FGraphNode* Node = GetNodeForPointSetID(NextID, false);
			check(Node != nullptr);

			MaxGraphDistance = TMathUtil<double>::Max(Node->GraphDistance, MaxGraphDistance);
			if (MaxGraphDistance > ComputeToMaxDistanceIn)
			{
				return;
			}

			Node->bFrozen = true;
			MaxGraphDistancePointID = Node->PointID;
			UpdateNeighboursSparse(Node);
		}
	}



	/**
	 * Computes outwards from seed points to TargetPointID, or stop when all points are further than ComputeToMaxDistance from the seed
	 * @param SeedPointsIn seed points defined as 2D vector tuples, will be interpreted as (seed_point_vertex_id, seed_distance)
	 * @param TargetPointID
	 * @param ComputeToMaxDistance target radius for parameterization, will not set UVs on points with graph-distance larger than this
	 * @return true if TargetPointID was reached
	 */
	bool ComputeToTargetPoint(const TArray<FVector2d>& SeedPointsIn, int32 TargetPointID,  double ComputeToMaxDistanceIn = TNumericLimits<double>::Max())
	{
		SeedPoints = SeedPointsIn;
		MaxGraphDistance = 0.0f;
		MaxGraphDistancePointID = -1;

		for (const FVector2d& SeedPoint : SeedPoints)
		{
			int32 PointID = (int32)SeedPoint.X;
			if (Queue.Contains(PointID) == false)
			{
				FGraphNode* Node = GetNodeForPointSetID(PointID, true);
				Node->GraphDistance = SeedPoint.Y;
				Node->bFrozen = true;
				Queue.Insert(PointID, Node->GraphDistance);
			}
		}

		while (Queue.GetCount() > 0)
		{
			int32 NextID = Queue.Dequeue();
			FGraphNode* Node = GetNodeForPointSetID(NextID, false);
			check(Node != nullptr);

			MaxGraphDistance = TMathUtil<double>::Max(Node->GraphDistance, MaxGraphDistance);
			if (MaxGraphDistance > ComputeToMaxDistanceIn)
			{
				return false;
			}

			Node->bFrozen = true;
			MaxGraphDistancePointID = Node->PointID;

			if (Node->PointID == TargetPointID)
			{
				return true;
			}

			UpdateNeighboursSparse(Node);
		}

		return false;
	}




	/**
	 * @return the maximum graph distance encountered during the computation
	 */
	double GetMaxGraphDistance() const 
	{
		return MaxGraphDistance;
	}


	/**
	 * @return vertex id associated with the maximum graph distance returned by GetMaxGraphDistance()
	 */
	int32 GetMaxGraphDistancePointID() const
	{
		return MaxGraphDistancePointID;
	}


	/**
	 * @return true if the distance for index PointID was calculated
	 */
	bool HasDistance(int32 PointID) const
	{
		const FGraphNode* Node = GetNodeForPointSetID(PointID);
		return (Node != nullptr && Node->bFrozen);
	}


	/**
	 * @return the distance calculated for index PointID
	 */
	double GetDistance(int32 PointID) const
	{
		const FGraphNode* Node = GetNodeForPointSetID(PointID);
		return (Node != nullptr && Node->bFrozen) ? Node->GraphDistance : InvalidValue();
	}

	
	//bool GetInterpTriDistance(const FIndex3i Triangle, const FVector3d& BaryCoords, double& DistanceOut) const
	//{
	//	const FGraphNode* NodeA = GetNodeForPointSetID(Triangle.A);
	//	const FGraphNode* NodeB = GetNodeForPointSetID(Triangle.B);
	//	const FGraphNode* NodeC = GetNodeForPointSetID(Triangle.C);
	//	if (NodeA == nullptr || NodeA.bFrozen == false || NodeB == nullptr || NodeB.bFrozen == false || NodeC == nullptr || NodeC.bFrozen == false)
	//	{
	//		DistanceOut = InvalidValue();
	//		return false;
	//	}

	//	DistanceOut = BaryCoords.X*NodeA->GraphDistance + BaryCoords.Y*NodeB->GraphDistance + BaryCoords.Z*NodeC->GraphDistance;
	//	return true;
	//}

	/**
	 * Find path from a point to the nearest seed point
	 * @param PointID starting point, assumption is that we have computed dijkstra to this point
	 * @param PathToSeedOut path is returned here, includes PointID and seed point as last element
	 * @param MaxLength if PathToSeedOut grows beyond this length, we abort the search
	 * @return true if valid path was found
	 */
	bool FindPathToNearestSeed(int32 PointID, TArray<int32>& PathToSeedOut, int32 MaxLength = 100000)
	{
		const FGraphNode* CurNode = GetNodeForPointSetID(PointID);
		if (CurNode == nullptr || CurNode->bFrozen == false)
		{
			return false;
		}

		PathToSeedOut.Reset();
		PathToSeedOut.Add(PointID);

		int32 IterCount = 0;
		while (IterCount++ < MaxLength)
		{
			if (CurNode->ParentPointID == -1)
			{
				return true;
			}

			PathToSeedOut.Add(CurNode->ParentPointID);

			CurNode = GetNodeForPointSetID(CurNode->ParentPointID);
			if (CurNode == nullptr || CurNode->bFrozen == false)
			{
				PathToSeedOut.Reset();
				return false;
			}
		}
		return true;
	}

private:

	// information about each active/computed point
	struct FGraphNode
	{
		int32 PointID;
		int32 ParentPointID;
		double GraphDistance;
		bool bFrozen;
	};

	// To avoid constructing FGraphNode for all input points (because we are computing a "local" param),
	// we only allocate on demand, and then store a sparse mapping in IDToNodeIndexMap
	TMap<int32, int32> IDToNodeIndexMap;
	TDynamicVector<FGraphNode> AllocatedNodes;

	// queue of nodes to process (for dijkstra front propagation)
	FIndexPriorityQueue Queue;

	static double InvalidValue() { return TNumericLimits<double>::Max(); };

	TArray<FVector2d> SeedPoints;

	// max distances encountered during last compute
	double MaxGraphDistance;

	int32 MaxGraphDistancePointID;

	FGraphNode* GetNodeForPointSetID(int32 PointSetID, bool bCreateIfMissing)
	{
		const int32* AllocatedIndex = IDToNodeIndexMap.Find(PointSetID);
		if (AllocatedIndex == nullptr)
		{
			if (bCreateIfMissing)
			{
				FGraphNode NewNode{ PointSetID, -1, 0, false };
				int32 NewIndex = AllocatedNodes.Num();
				AllocatedNodes.Add(NewNode);
				IDToNodeIndexMap.Add(PointSetID, NewIndex);
				return &AllocatedNodes[NewIndex];
			}
			else
			{
				return nullptr;
			}
		}
		else
		{
			return &AllocatedNodes[*AllocatedIndex];
		}
	}


	const FGraphNode* GetNodeForPointSetID(int32 PointSetID) const
	{
		const int32* AllocatedIndex = IDToNodeIndexMap.Find(PointSetID);
		return (AllocatedIndex != nullptr) ? &AllocatedNodes[*AllocatedIndex] : nullptr;
	}


	// given new Distance/UV at Parent, check if any of its neighbours are in the queue,
	// and if they are, and the new graph distance is shorter, update their queue position
	// (this is basically the update step of Disjktras algorithm)
	void UpdateNeighboursSparse(FGraphNode* Parent)
	{
		FVector3d ParentPos(GetPositionFunc(Parent->PointID));
		double ParentDist = Parent->GraphDistance;

		for (int32 NbrPointID : PointSet->VtxVerticesItr(Parent->PointID))
		{
			FGraphNode* NbrNode = GetNodeForPointSetID(NbrPointID, true);
			if (NbrNode->bFrozen)
			{
				continue;
			}

			double NbrDist = ParentDist + ParentPos.Distance(GetPositionFunc(NbrPointID));
			if (Queue.Contains(NbrPointID))
			{
				if (NbrDist < NbrNode->GraphDistance)
				{
					NbrNode->ParentPointID = Parent->PointID;
					NbrNode->GraphDistance = NbrDist;
					Queue.Update(NbrPointID, NbrNode->GraphDistance);
				}
			}
			else 
			{
				NbrNode->ParentPointID = Parent->PointID;
				NbrNode->GraphDistance = NbrDist;
				Queue.Insert(NbrPointID, NbrNode->GraphDistance);
			}
		}
	}

};