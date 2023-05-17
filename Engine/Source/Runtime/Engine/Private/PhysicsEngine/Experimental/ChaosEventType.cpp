// Copyright Epic Games, Inc. All Rights Reserved.

#include "Physics/Experimental/ChaosEventType.h"
#include "Chaos/ExternalCollisionData.h"


#include UE_INLINE_GENERATED_CPP_BY_NAME(ChaosEventType)

FBreakChaosEvent::FBreakChaosEvent()
	: Component(nullptr)
	, Location(FVector::ZeroVector)
	, Velocity(FVector::ZeroVector)
	, AngularVelocity(FVector::ZeroVector)
	, Extents(FVector::ZeroVector)
	, Mass(0.0f)
	, Index(INDEX_NONE)
	, bFromCrumble(false)
{
}

FBreakChaosEvent::FBreakChaosEvent(const Chaos::FBreakingData& BreakingData)
	: Component(nullptr)
	, Location(BreakingData.Location)
	, Velocity(BreakingData.Velocity)
	, AngularVelocity(BreakingData.AngularVelocity)
	, Extents(BreakingData.BoundingBox.Extents())
	, Mass(BreakingData.Mass)
	, Index(BreakingData.TransformGroupIndex)
	, bFromCrumble(BreakingData.bFromCrumble)
{
}
