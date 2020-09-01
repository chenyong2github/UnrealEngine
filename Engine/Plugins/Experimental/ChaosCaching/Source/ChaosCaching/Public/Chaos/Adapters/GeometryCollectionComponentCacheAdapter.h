// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CacheAdapter.h"
#include "Chaos/CacheEvents.h"
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

class UChaosCache;
class UPrimitiveComponent;

namespace Chaos
{
	class FGeometryCollectionCacheAdapter : public FComponentCacheAdapter
	{
	public:
		virtual ~FGeometryCollectionCacheAdapter() = default;

		// FComponentCacheAdapter interface
		SupportType            SupportsComponentClass(UClass* InComponentClass) const override;
		UClass*                GetDesiredClass() const override;
		uint8                  GetPriority() const override;
		FGuid                  GetGuid() const override;
		Chaos::FPhysicsSolver* GetComponentSolver(UPrimitiveComponent* InComponent) const override;
		bool                   ValidForPlayback(UPrimitiveComponent* InComponent, UChaosCache* InCache) const override;
		bool                   InitializeForRecord(UPrimitiveComponent* InComponent, UChaosCache* InCache) const override;
		bool                   InitializeForPlayback(UPrimitiveComponent* InComponent, UChaosCache* InCache) const override;
		void                   Record_PostSolve(UPrimitiveComponent* InComp, const FTransform& InRootTransform, FPendingFrameWrite& OutFrame, Chaos::FReal InTime) const override;
		void                   Playback_PreSolve(UPrimitiveComponent*                               InComponent,
												 UChaosCache*                                       InCache,
												 Chaos::FReal                                       InTime,
												 FPlaybackTickRecord&                               TickRecord,
												 TArray<TPBDRigidParticleHandle<Chaos::FReal, 3>*>& OutUpdatedRigids) const override;

		// End FComponentCacheAdapter interface

	private:
	};
}    // namespace Chaos
