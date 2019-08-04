// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/ArrayCollectionArray.h"
#include "Chaos/PBDRigidParticles.h"
#include "Chaos/ImplicitObjectUnion.h"

namespace Chaos
{
// ClusterId 
//    Used within the clustering system to describe the clustering hierarchy. The ID will
//  store the children IDs, and a Parent ID. When Id==NONE_INDEX the cluster is not
//  controlled by another body. 
struct ClusterId
{
	ClusterId() : Id(INDEX_NONE), NumChildren(0) {}
	ClusterId(int32 NewId, int NumChildrenIn)
		: Id(NewId)
		, NumChildren(NumChildrenIn) {}
	int32 Id;
	int32 NumChildren;
};

/** When multiple children are active and can share one collision proxy. Only valid if all original children are still in the cluster*/
template <typename T, int d>
struct CHAOS_API TMultiChildProxyData
{
	TRigidTransform<T, d> RelativeToKeyChild;	//Use one child's transform to determine where to place the geometry. Needed for partial fracture where all children are still present and can therefore use proxy
	uint32 KeyChild;
};

/** Used with TMultiChildProxyData. INDEX_NONE indicates no proxy data available */
struct FMultiChildProxyId
{
	FMultiChildProxyId() : Id(INDEX_NONE) {}
	int32 Id;
};

template <typename T>
struct TConnectivityEdge
{
	TConnectivityEdge() {}
	TConnectivityEdge(uint32 InSibling, T InStrain)
		: Sibling(InSibling)
		, Strain(InStrain) {}

	TConnectivityEdge(const TConnectivityEdge& Other)
		: Sibling(Other.Sibling)
		, Strain(Other.Strain) {}

	uint32 Sibling;
	T Strain;
};

template<class T, int d>
class TPBDRigidClusteredParticles : public TPBDRigidParticles<T, d>
{
  public:

	TPBDRigidClusteredParticles()
	: TPBDRigidParticles<T, d>()
	{
		InitHelper();
	}
	TPBDRigidClusteredParticles(const TPBDRigidParticles<T, d>& Other) = delete;
	TPBDRigidClusteredParticles(TPBDRigidParticles<T, d>&& Other)
	: TPBDRigidParticles<T, d>(MoveTemp(Other))
	, MClusterIds(MoveTemp(Other.MClusterIds))
	, MChildToParent(MoveTemp(Other.MChildToParent))
	, MClusterGroupIndex(MoveTemp(Other.MClusterGroupIndex))
	, MInternalCluster(MoveTemp(Other.MInternalCluster))
	, MMultiChildProxyId(MoveTemp(Other.MMultiChildProxyId))
	, MMultiChildProxyData(MoveTemp(Other.MMultiChildProxyData))
	, MCollisionImpulses(MoveTemp(Other.MCollisionImpulses))
	, MStrains(MoveTemp(Other.MStrains))
	, MConnectivityEdges(MoveTemp(Other.MConnectivityEdges))

	{
		InitHelper();
	}
	~TPBDRigidClusteredParticles() {}

	const auto& ClusterIds(int32 Idx) const { return MClusterIds[Idx]; }
	auto& ClusterIds(int32 Idx) { return MClusterIds[Idx]; }

	const auto& ChildToParent(int32 Idx) const { return MChildToParent[Idx]; }
	auto& ChildToParent(int32 Idx) { return MChildToParent[Idx]; }

	const auto& ClusterGroupIndex(int32 Idx) const { return MClusterGroupIndex[Idx]; }
	auto& ClusterGroupIndex(int32 Idx) { return MClusterGroupIndex[Idx]; }

	const auto& InternalCluster(int32 Idx) const { return MInternalCluster[Idx]; }
	auto& InternalCluster(int32 Idx) { return MInternalCluster[Idx]; }

	const auto& ChildrenSpatial(int32 Idx) const { return MChildrenSpatial[Idx]; }
	auto& ChildrenSpatial(int32 Idx) { return MChildrenSpatial[Idx]; }

	const auto& MultiChildProxyId(int32 Idx) const { return MMultiChildProxyId[Idx]; }
	auto& MultiChildProxyId(int32 Idx) { return MMultiChildProxyId[Idx]; }

	const auto& MultiChildProxyData(int32 Idx) const { return MMultiChildProxyData[Idx]; }
	auto& MultiChildProxyData(int32 Idx) { return MMultiChildProxyData[Idx]; }

	const auto& CollisionImpulses(int32 Idx) const { return MCollisionImpulses[Idx]; }
	auto& CollisionImpulses(int32 Idx) { return MCollisionImpulses[Idx]; }

	const auto& Strains(int32 Idx) const { return MStrains[Idx]; }
	auto& Strains(int32 Idx) { return MStrains[Idx]; }

	const auto& ConnectivityEdges(int32 Idx) const { return MConnectivityEdges[Idx]; }
	auto& ConnectivityEdges(int32 Idx) { return MConnectivityEdges[Idx]; }

	//TODO_CHAOSPARTICLE_HANDLE: is this really needed?
	const auto& ConnectivityEdgesArray() const { return MConnectivityEdges; }
	const auto& MultiChildProxyDataArray() const { return MMultiChildProxyData; }
	const auto& MultiChildProxyIdArray() const { return MMultiChildProxyId; }

	const auto& ClusterIdsArray() const { return MClusterIds; }
	auto& ClusterIdsArray() { return MClusterIds; }

	const auto& ChildToParentArray() const { return MChildToParent; }
	auto& ChildToParentArray() { return MChildToParent; }

	const auto& StrainsArray() const { return MStrains; }
	auto& StrainsArray() { return MStrains; }

	const auto& ClusterGroupIndexArray() const { return MClusterGroupIndex; }
	auto& ClusterGroupIndexArray() { return MClusterGroupIndex; }

	const auto& InternalClusterArray() const { return MInternalCluster; }
	auto& InternalClusterArray() { return MInternalCluster; }

	typedef TPBDRigidClusteredParticleHandle<T, d> THandleType;
	const THandleType* Handle(int32 Index) const { return static_cast<const THandleType*>(TGeometryParticles<T,d>::Handle(Index)); }

	//cannot be reference because double pointer would allow for badness, but still useful to have non const access to handle
	THandleType* Handle(int32 Index) { return static_cast<THandleType*>(TGeometryParticles<T, d>::Handle(Index)); }

	
  private:

	  void InitHelper()
	  {
		  this->MParticleType = EParticleType::Clustered;
		  TArrayCollection::AddArray(&MClusterIds);
		  TArrayCollection::AddArray(&MChildToParent);
		  TArrayCollection::AddArray(&MClusterGroupIndex);
		  TArrayCollection::AddArray(&MInternalCluster);
		  TArrayCollection::AddArray(&MChildrenSpatial);
		  TArrayCollection::AddArray(&MMultiChildProxyId);
		  TArrayCollection::AddArray(&MMultiChildProxyData);
		  TArrayCollection::AddArray(&MCollisionImpulses);
		  TArrayCollection::AddArray(&MStrains);
		  TArrayCollection::AddArray(&MConnectivityEdges);
	  }

	  TArrayCollectionArray<ClusterId> MClusterIds;
	  TArrayCollectionArray<TRigidTransform<T, d>> MChildToParent;
	  TArrayCollectionArray<int32> MClusterGroupIndex;
	  TArrayCollectionArray<bool> MInternalCluster;
	  TArrayCollectionArray<TUniquePtr<TImplicitObjectUnion<T, d>>> MChildrenSpatial;
	  TArrayCollectionArray<FMultiChildProxyId> MMultiChildProxyId;
	  TArrayCollectionArray<TUniquePtr<TMultiChildProxyData<T, d>>> MMultiChildProxyData;

	  // Collision Impulses
	  TArrayCollectionArray<T> MCollisionImpulses;

	  // User set parameters
	  TArrayCollectionArray<T> MStrains;

	  TArrayCollectionArray<TArray<TConnectivityEdge<T>>> MConnectivityEdges;
};

}
