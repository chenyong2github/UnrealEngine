// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CacheAdapter.h"

namespace Chaos
{
	class FStaticMeshCacheAdapter : public FComponentCacheAdapter
	{
	public:
		virtual ~FStaticMeshCacheAdapter() = default;

		SupportType            SupportsComponentClass(UClass* InComponentClass) const override;
		UClass*                GetDesiredClass() const override;
		uint8                  GetPriority() const override;
		FGuid                  GetGuid() const override;
		bool                   ValidForPlayback(UPrimitiveComponent* InComponent, UChaosCache* InCache) const override;
		Chaos::FPhysicsSolver* GetComponentSolver(UPrimitiveComponent* InComponent) const override;
		bool                   InitializeForRecord(UPrimitiveComponent* InComponent, UChaosCache* InCache) const override;
		bool                   InitializeForPlayback(UPrimitiveComponent* InComponent, UChaosCache* InCache) const override;
		void                   Record_PostSolve(UPrimitiveComponent* InComp, const FTransform& InRootTransform, FPendingFrameWrite& OutFrame, Chaos::FReal InTime) const override;
		void                   Playback_PreSolve(UPrimitiveComponent*                               InComponent,
												 UChaosCache*                                       InCache,
												 Chaos::FReal                                       InTime,
												 FPlaybackTickRecord&                               TickRecord,
												 TArray<TPBDRigidParticleHandle<Chaos::FReal, 3>*>& OutUpdatedRigids) const override;

	private:
	};
}    // namespace Chaos
