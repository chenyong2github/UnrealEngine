// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/PullPhysicsData.h"

namespace Chaos
{

void FPullPhysicsData::Reset()
{
	DirtyRigids.Reset();
	DirtyGeometryCollections.Reset();
	DirtyJointConstraints.Reset();
}

}