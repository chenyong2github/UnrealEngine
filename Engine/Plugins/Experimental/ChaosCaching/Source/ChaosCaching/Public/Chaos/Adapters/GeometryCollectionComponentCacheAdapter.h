// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CacheAdapter.h"
#include "Chaos/CacheEvents.h"
#include "EventsData.h"
#include "GeometryCollectionComponentCacheAdapter.generated.h"

USTRUCT()
struct FEnableStateEvent : public FCacheEventBase
{
	GENERATED_BODY()

	static FName EventName;

	FEnableStateEvent()
		: Index(INDEX_NONE)
		, bEnable(false)
	{
	}

	FEnableStateEvent(int32 InIndex, bool bInEnable)
		: Index(InIndex)
		, bEnable(bInEnable)
	{
	}

	UPROPERTY()
	int32 Index;

	UPROPERTY()
	bool bEnable;
};

USTRUCT()
struct FBreakingEvent : public FCacheEventBase
{
	GENERATED_BODY()

	static FName EventName;

	FBreakingEvent()
		: Index(INDEX_NONE)
		, Location(0.0f, 0.0f, 0.0f)
		, Velocity(0.0f, 0.0f, 0.0f)
		, AngularVelocity(0.0f, 0.0f, 0.0f)
		, Mass(1.0f)
		, BoundingBoxMin(0.0f, 0.0f, 0.0f)
		, BoundingBoxMax(0.0f, 0.0f, 0.0f)
	{
	}

	FBreakingEvent(int32 InIndex, const Chaos::TBreakingData<float, 3>& InData)
		: Index(InIndex)
		, Location(InData.Location)
		, Velocity(InData.Velocity)
		, AngularVelocity(InData.AngularVelocity)
		, Mass(InData.Mass)
		, BoundingBoxMin(InData.BoundingBox.Min())
		, BoundingBoxMax(InData.BoundingBox.Max())
	{
	}

	UPROPERTY()
	int32 Index;

	UPROPERTY()
	FVector Location;
	
	UPROPERTY()
	FVector Velocity;

	UPROPERTY()
	FVector AngularVelocity;
	
	UPROPERTY()
	float Mass;

	UPROPERTY()
	FVector BoundingBoxMin;

	UPROPERTY()
	FVector BoundingBoxMax;
};


class UChaosCache;
class UPrimitiveComponent;

namespace Chaos
{
	class FGeometryCollectionCacheAdapter : public FComponentCacheAdapter
	{
	public:
		FGeometryCollectionCacheAdapter()
			: ProxyKey(nullptr)
			, BreakingDataArray(nullptr)
			, ProxyBreakingDataIndices(nullptr)
		{}

		virtual ~FGeometryCollectionCacheAdapter() = default;

		// FComponentCacheAdapter interface
		SupportType            SupportsComponentClass(UClass* InComponentClass) const override;
		UClass*                GetDesiredClass() const override;
		uint8                  GetPriority() const override;
		FGuid                  GetGuid() const override;
		Chaos::FPhysicsSolver* GetComponentSolver(UPrimitiveComponent* InComponent) const override;
		bool                   ValidForPlayback(UPrimitiveComponent* InComponent, UChaosCache* InCache) const override;
		bool                   InitializeForRecord(UPrimitiveComponent* InComponent, UChaosCache* InCache) override;
		bool                   InitializeForPlayback(UPrimitiveComponent* InComponent, UChaosCache* InCache) const override;
		void                   Record_PostSolve(UPrimitiveComponent* InComp, const FTransform& InRootTransform, FPendingFrameWrite& OutFrame, Chaos::FReal InTime) const override;
		void                   Playback_PreSolve(UPrimitiveComponent*                               InComponent,
												 UChaosCache*                                       InCache,
												 Chaos::FReal                                       InTime,
												 FPlaybackTickRecord&                               TickRecord,
												 TArray<TPBDRigidParticleHandle<Chaos::FReal, 3>*>& OutUpdatedRigids) const override;

		// End FComponentCacheAdapter interface

	protected:
		void HandleBreakingEvents(const Chaos::FBreakingEventData& Event);
	
	private:
		const IPhysicsProxyBase* ProxyKey;
		const Chaos::FBreakingDataArray* BreakingDataArray;
		const TArray<int32>* ProxyBreakingDataIndices;

	};
}    // namespace Chaos
