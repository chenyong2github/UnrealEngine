// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FLidarPointCloudOctree;
struct FTriMeshCollisionData;

namespace LidarPointCloudCollision
{
	void BuildCollisionMesh(FLidarPointCloudOctree* Octree, const float& CellSize, const bool& bVisibleOnly, FTriMeshCollisionData* CollisionMesh);
}