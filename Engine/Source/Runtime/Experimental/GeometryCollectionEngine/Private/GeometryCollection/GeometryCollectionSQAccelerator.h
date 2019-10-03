// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once


#if !WITH_CHAOS_NEEDS_TO_BE_FIXED
#include "SQAccelerator.h"

class UGeometryCollectionComponent;

class FGeometryCollectionSQAccelerator
{
public:
	void Raycast(const FVector& Start, const FVector& Dir, const float DeltaMagnitude, ChaosInterface::FSQHitBuffer<ChaosInterface::FRaycastHit>& HitBuffer, EHitFlags OutputFlags, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase& QueryCallback) const;
	void Sweep(const Chaos::TImplicitObject<float, 3>& QueryGeom, const FTransform& StartTM, const FVector& Dir, const float DeltaMagnitude, ChaosInterface::FSQHitBuffer<ChaosInterface::FSweepHit>& HitBuffer, EHitFlags OutputFlags, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase& QueryCallback) const;
	void Overlap(const Chaos::TImplicitObject<float, 3>& QueryGeom, const FTransform& GeomPose, ChaosInterface::FSQHitBuffer<ChaosInterface::FOverlapHit>& HitBuffer, const FQueryFilterData& QueryFilterData, ICollisionQueryFilterCallbackBase& QueryCallback) const;
	virtual ~FGeometryCollectionSQAccelerator() {}

	void AddComponent(UGeometryCollectionComponent* Component);
	void RemoveComponent(UGeometryCollectionComponent* Component);

private:
	TSet<UGeometryCollectionComponent*> Components;
};
#endif