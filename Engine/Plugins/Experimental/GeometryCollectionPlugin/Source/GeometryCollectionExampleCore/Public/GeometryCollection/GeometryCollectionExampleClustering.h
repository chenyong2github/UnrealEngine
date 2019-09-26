// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "GeometryCollection/GeometryCollectionExample.h"
#include "Chaos/Declares.h"

namespace GeometryCollectionExample
{
	template<class T>
	void RigidBodies_ClusterTest_SingleLevelNonBreaking();

	template<class T>
	void RigidBodies_ClusterTest_DeactivateClusterParticle();

	template<class T>
	void RigidBodies_ClusterTest_SingleLevelBreaking();

	template<class T>
	void RigidBodies_ClusterTest_NestedCluster();

	template<class T>
	void RigidBodies_ClusterTest_NestedCluster_MultiStrain();

	template<class T>
	void RigidBodies_ClusterTest_NestedCluster_Halt();

	template<class T>
	void RigidBodies_ClusterTest_KinematicAnchor();

	template<class T>
	void RigidBodies_ClusterTest_StaticAnchor();

	template<class T>
	void RigidBodies_ClusterTest_UnionClusters();

	template<class T>
	void RigidBodies_ClusterTest_ReleaseClusterParticle_ClusteredNode();

	template<class T>
	void RigidBodies_ClusterTest_ReleaseClusterParticle_ClusteredKinematicNode();

	template<class T>
	void RigidBodies_ClusterTest_ReleaseClusterParticles_AllLeafNodes();

	template<class T>
	void RigidBodies_ClusterTest_ReleaseClusterParticles_ClusterNodeAndSubClusterNode();

	template<class T>
	void RigidBodies_ClusterTest_RemoveOnFracture();

	template<class T>
	void RigidBodiess_ClusterTest_ParticleImplicitCollisionGeometry();
}
