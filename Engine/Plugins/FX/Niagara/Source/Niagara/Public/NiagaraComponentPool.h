// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "NiagaraComponentPool.generated.h"

class UNiagaraComponent;
class UNiagaraSystem;

#define ENABLE_NC_POOL_DEBUGGING (!UE_BUILD_SHIPPING)

UENUM()
enum class ENCPoolMethod : uint8
{
	/**
	* The component will be created fresh and not allocated from the pool.
	*/
	None,

	/**
	* The component is allocated from the pool and will be automatically released back to it.
	* User need not handle this any more that other NCs but interaction with the NC after the tick it's spawned in are unsafe.
	* This method is useful for one-shot fx that you don't need to keep a reference to and can fire and forget.
	*/
	AutoRelease,

	/**
	* NC is allocated from the pool but will NOT be automatically released back to it. User has ownership of the NC and must call ReleaseToPool when finished with it otherwise the NC will leak.
	* Interaction with the NC after it has been released are unsafe.
	* This method is useful for persistent FX that you need to modify parameters upon etc over it's lifetime.
	*/
	ManualRelease,
	
	/** 
	Special entry allowing manual release NCs to be manually released but wait until completion to be returned to the pool.
	*/
	ManualRelease_OnComplete UMETA(Hidden),

	/**
	Special entry that marks a NC as having been returned to the pool. All NCs currently in the pool are marked this way.
	*/
	FreeInPool UMETA(Hidden),
};

USTRUCT()
struct FNCPoolElement
{
	GENERATED_BODY()

	UPROPERTY(transient)
	UNiagaraComponent* Component;

	float LastUsedTime;

	FNCPoolElement()
		: Component(nullptr), LastUsedTime(0.0f)
	{

	}
	FNCPoolElement(UNiagaraComponent* InNC, float InLastUsedTime)
		: Component(InNC), LastUsedTime(InLastUsedTime)
	{

	}
};


USTRUCT()
struct FNCPool
{
	GENERATED_BODY()

	//Collection of all currently allocated, free items ready to be grabbed for use.
	//TODO: Change this to a FIFO queue to get better usage. May need to make this whole class behave similar to TCircularQueue.
	UPROPERTY(transient)
	TArray<FNCPoolElement> FreeElements;

	//Array of currently in flight components that will auto release.
	UPROPERTY(transient)
	TArray<UNiagaraComponent*> InUseComponents_Auto;

	//Array of currently in flight components that need manual release.
	UPROPERTY(transient)
	TArray<UNiagaraComponent*> InUseComponents_Manual;
	
	/** Keeping track of max in flight systems to help inform any future pre-population we do. */
	int32 MaxUsed;

public:

	FNCPool();
	void Cleanup();

	/** Gets a component from the pool ready for use. */
	UNiagaraComponent* Acquire(UWorld* World, UNiagaraSystem* Template, ENCPoolMethod PoolingMethod);

	/** Returns a component to the pool. */
	void Reclaim(UNiagaraComponent* NC, const float CurrentTimeSeconds);

	/** Kills any components that have not been used since the passed KillTime. */
	void KillUnusedComponents(float KillTime, UNiagaraSystem* Template);

	int32 NumComponents() { return FreeElements.Num(); }
};

UCLASS(Transient)
class NIAGARA_API UNiagaraComponentPool : public UObject
{
	GENERATED_UCLASS_BODY()

private:
	UPROPERTY()
	TMap<UNiagaraSystem*, FNCPool> WorldParticleSystemPools;

	float LastParticleSytemPoolCleanTime;
public:

	~UNiagaraComponentPool();

	void Cleanup();

	UNiagaraComponent* CreateWorldParticleSystem(UNiagaraSystem* Template, UWorld* World, ENCPoolMethod PoolingMethod);

	/** Called when an in-use particle component is finished and wishes to be returned to the pool. */
	void ReclaimWorldParticleSystem(UNiagaraComponent* Component);

	/** Call if you want to halt & reclaim all active particle systems and return them to their respective pools. */
	void ReclaimActiveParticleSystems();
	
	/** Dumps the current state of the pool to the log. */
	void Dump();
};
