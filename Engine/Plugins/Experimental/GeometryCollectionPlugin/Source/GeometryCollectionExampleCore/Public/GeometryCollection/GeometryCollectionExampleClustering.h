// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "GeometryCollection/GeometryCollectionExample.h"
#include "GeometryCollection/GeometryCollectionExampleResponse.h"
#include "Chaos/Declares.h"

namespace GeometryCollectionExample
{

#if TODO_REIMPLEMENT_RIGID_CLUSTERING
	template<class T>
	bool RigidBodies_ClusterTest_SingleLevelNonBreaking(ExampleResponse&& R);

	template<class T>
	bool RigidBodies_ClusterTest_DeactivateClusterParticle(ExampleResponse&& R);

	template<class T>
	bool RigidBodies_ClusterTest_SingleLevelBreaking(ExampleResponse&& R);

	template<class T>
	bool RigidBodies_ClusterTest_NestedCluster(ExampleResponse&& R);

	template<class T>
	bool RigidBodies_ClusterTest_NestedCluster_MultiStrain(ExampleResponse&& R);

	template<class T>
	bool RigidBodies_ClusterTest_NestedCluster_Halt(ExampleResponse&& R);

	template<class T>
	bool RigidBodies_ClusterTest_KinematicAnchor(ExampleResponse&& R);

	template<class T>
	bool RigidBodies_ClusterTest_StaticAnchor(ExampleResponse&& R);

	template<class T>
	bool RigidBodies_ClusterTest_UnionClusters(ExampleResponse&& R);

	template<class T>
	bool RigidBodies_ClusterTest_ReleaseClusterParticle_ClusteredNode(ExampleResponse&& R);

	template<class T>
	bool RigidBodies_ClusterTest_ReleaseClusterParticle_ClusteredKinematicNode(ExampleResponse&& R);

	template<class T>
	bool RigidBodies_ClusterTest_ReleaseClusterParticles_AllLeafNodes(ExampleResponse&& R);

	template<class T>
	bool RigidBodies_ClusterTest_ReleaseClusterParticles_ClusterNodeAndSubClusterNode(ExampleResponse&& R);

	template<class T>
	bool RigidBodies_ClusterTest_RemoveOnFracture(ExampleResponse&& R);

	template<class T>
	bool RigidBodiess_ClusterTest_ParticleImplicitCollisionGeometry(ExampleResponse&& R);

#endif

}
