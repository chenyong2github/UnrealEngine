// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once
#include "Chaos/ISpatialAcceleration.h"
#include "Chaos/Box.h"
#include "Chaos/Collision/StatsData.h"
#include "GeometryParticlesfwd.h"

#include <tuple>

namespace Chaos
{
	class FAsyncCollisionReceiver;
	class FNarrowPhase;
	class FSpatialAccelerationBroadPhase;

template <typename TPayloadType, typename T, int d>
class CHAOS_API ISpatialAccelerationCollection : public ISpatialAcceleration<TPayloadType, T, d>
{
public:
	ISpatialAccelerationCollection()
	: ISpatialAcceleration<TPayloadType, T, d>(StaticType)
	, ActiveBucketsMask(0)
	, AllAsyncTrasksComplete(true)
	{}
	static constexpr ESpatialAcceleration StaticType = ESpatialAcceleration::Collection;
	virtual FSpatialAccelerationIdx AddSubstructure(TUniquePtr<ISpatialAcceleration<TPayloadType, T, d>>&& Substructure, uint16 Bucket) = 0;
	virtual TUniquePtr<ISpatialAcceleration<TPayloadType, T, d>> RemoveSubstructure(FSpatialAccelerationIdx Idx) = 0;
	virtual ISpatialAcceleration<TPayloadType, T, d>* GetSubstructure(FSpatialAccelerationIdx Idx) = 0;

	/** This is kind of a hack to avoid virtuals. We simply route calls into templated functions */
	virtual void PBDComputeConstraintsLowLevel(T Dt, FSpatialAccelerationBroadPhase& BroadPhase, FNarrowPhase& NarrowPhase, FAsyncCollisionReceiver& Receiver, CollisionStats::FStatData& StatData) const = 0;
	virtual TArray<FSpatialAccelerationIdx> GetAllSpatialIndices() const = 0;

	bool IsBucketActive(uint8 BucketIdx) const
	{
		return (1 << BucketIdx) & ActiveBucketsMask;
	}

	bool IsAllAsyncTrasksComplete() const { return AllAsyncTrasksComplete; }
	void SetAllAsyncTrasksComplete(bool State) { AllAsyncTrasksComplete = State; }

protected:
	uint8 ActiveBucketsMask;
	bool AllAsyncTrasksComplete;
};

}
