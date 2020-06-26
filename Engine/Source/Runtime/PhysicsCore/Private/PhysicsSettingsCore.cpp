// Copyright Epic Games, Inc. All Rights Reserved.

#include "PhysicsSettingsCore.h"

UPhysicsSettingsCore::UPhysicsSettingsCore(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, DefaultGravityZ(-980.f)
	, DefaultTerminalVelocity(4000.f)
	, DefaultFluidFriction(0.3f)
	, SimulateScratchMemorySize(262144)
	, RagdollAggregateThreshold(4)
	, TriangleMeshTriangleMinAreaThreshold(5.0f)
	, bEnableShapeSharing(false)
	, bEnablePCM(true)
	, bEnableStabilization(false)
	, bWarnMissingLocks(true)
	, bEnable2DPhysics(false)
	, BounceThresholdVelocity(200.f)
	, MaxAngularVelocity(3600)	//10 revolutions per second
	, ContactOffsetMultiplier(0.02f)
	, MinContactOffset(2.f)
	, MaxContactOffset(8.f)
	, bSimulateSkeletalMeshOnDedicatedServer(true)
{
	SectionName = TEXT("Physics");
}