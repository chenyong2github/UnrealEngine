// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CustomPhysXPayload.h"

struct UE_DEPRECATED(4.26, "APEX is deprecated. Destruction in future will be supported using Chaos Destruction.") FUpdateChunksInfo
{
	int32 ChunkIndex;
	FTransform WorldTM;

	FUpdateChunksInfo(int32 InChunkIndex, const FTransform & InWorldTM) : ChunkIndex(InChunkIndex), WorldTM(InWorldTM)
	{}
};

#if WITH_APEX

class UDestructibleComponent;

struct UE_DEPRECATED(4.26, "APEX is deprecated. Destruction in future will be supported using Chaos Destruction.") FApexDestructionSyncActors : public FCustomPhysXSyncActors
{
	virtual void BuildSyncData_AssumesLocked(const TArray<physx::PxRigidActor*> & RigidActors) override;

	virtual void FinalizeSync() override;

private:
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	/** Sync data for updating physx sim result */
	TMap<TWeakObjectPtr<UDestructibleComponent>, TArray<FUpdateChunksInfo> > ComponentUpdateMapping;
PRAGMA_ENABLE_DEPRECATION_WARNINGS
};

struct UE_DEPRECATED(4.26, "APEX is deprecated. Destruction in future will be supported using Chaos Destruction.") FApexDestructionCustomPayload : public FCustomPhysXPayload
{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	FApexDestructionCustomPayload()
		: FCustomPhysXPayload(SingletonCustomSync)
	{
	}
PRAGMA_ENABLE_DEPRECATION_WARNINGS

	virtual TWeakObjectPtr<UPrimitiveComponent> GetOwningComponent() const override;

	virtual int32 GetItemIndex() const override;

	virtual FName GetBoneName() const override;

	virtual FBodyInstance* GetBodyInstance() const override;

	/** Index of the chunk this data belongs to*/
	int32 ChunkIndex;
	/** Component owning this chunk info*/
	TWeakObjectPtr<UDestructibleComponent> OwningComponent;

private:
	friend class FApexDestructionModule;

PRAGMA_DISABLE_DEPRECATION_WARNINGS
	static FApexDestructionSyncActors* SingletonCustomSync;
PRAGMA_ENABLE_DEPRECATION_WARNINGS
};

#endif // WITH_APEX