// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "NiagaraComponentPool.h"
#include "HAL/IConsoleManager.h"
#include "Engine/World.h"
#include "NiagaraComponent.h"
#include "NiagaraWorldManager.h"

static float GParticleSystemPoolKillUnusedTime = 180.0f;
static FAutoConsoleVariableRef ParticleSystemPoolKillUnusedTime(
	TEXT("FX.NiagaraComponentPool.KillUnusedTime"),
	GParticleSystemPoolKillUnusedTime,
	TEXT("How long a pooled particle component needs to be unused for before it is destroyed.")
);

static int32 GbEnableParticleSystemPooling = 1;
static FAutoConsoleVariableRef bEnableParticleSystemPooling(
	TEXT("FX.NiagaraComponentPool.Enable"),
	GbEnableParticleSystemPooling,
	TEXT("How many Particle System Components to preallocate when creating new ones for the pool.")
);

static float GParticleSystemPoolingCleanTime = 30.0f;
static FAutoConsoleVariableRef ParticleSystemPoolingCleanTime(
	TEXT("FX.NiagaraComponentPool.CleanTime"),
	GParticleSystemPoolingCleanTime,
	TEXT("How often should the pool be cleaned (in seconds).")
);

void DumpPooledWorldNiagaraParticleSystemInfo(UWorld* World)
{
	check(World);
	FNiagaraWorldManager::Get(World)->GetComponentPool()->Dump();
}

FAutoConsoleCommandWithWorld DumpNCPoolInfoCommand(
	TEXT("FX.DumpNCPoolInfo"),
	TEXT("Dump Particle System Pooling Info"),
	FConsoleCommandWithWorldDelegate::CreateStatic(&DumpPooledWorldNiagaraParticleSystemInfo)
);

FNCPool::FNCPool()
	: MaxUsed(0)
{

}

void FNCPool::Cleanup()
{
	for (FNCPoolElement& Elem : FreeElements)
	{
		if (Elem.Component)
		{
			Elem.Component->PoolingMethod = ENCPoolMethod::None;//Reset so we don't trigger warnings about destroying pooled NCs.
			Elem.Component->DestroyComponent();
		}
		else
		{
			UE_LOG(LogNiagara, Error, TEXT("Free element in the NiagaraComponentPool was null. Someone must be keeping a reference to a NC that has been freed to the pool and then are manually destroying it."));
		}
	}

	for (UNiagaraComponent* NC : InUseComponents_Auto)
	{
		//It's possible for people to manually destroy these so we have to guard against it. Though we warn about it in UNiagaraComponent::BeginDestroy
		if (NC)
		{
			NC->PoolingMethod = ENCPoolMethod::None;//Reset so we don't trigger warnings about destroying pooled NCs.
			NC->DestroyComponent();
		}
	}

	//Warn if there are any manually released NCs still in the world at cleanup time.
	for (UNiagaraComponent* NC : InUseComponents_Manual)
	{
		//It's possible for people to manually destroy these so we have to guard against it. Though we warn about it in UNiagaraComponent::BeginDestroy
		if (NC)
		{
			UE_LOG(LogNiagara, Warning, TEXT("Pooled NC set to manual release is still in use as the pool is being cleaned up. %s"), *NC->GetAsset()->GetFullName());
			NC->PoolingMethod = ENCPoolMethod::None;//Reset so we don't trigger warnings about destroying pooled NCs.
			NC->DestroyComponent();
		}
	}

	FreeElements.Empty();
	InUseComponents_Auto.Empty();
	InUseComponents_Manual.Empty();
}

UNiagaraComponent* FNCPool::Acquire(UWorld* World, UNiagaraSystem* Template, ENCPoolMethod PoolingMethod)
{
	check(GbEnableParticleSystemPooling);
	check(PoolingMethod != ENCPoolMethod::None);

	FNCPoolElement RetElem;
	if (FreeElements.Num())
	{
		RetElem = FreeElements.Pop(false);
		check(RetElem.Component->GetAsset() == Template);
		check(!RetElem.Component->IsPendingKill());
		RetElem.Component->SetUserParametersToDefaultValues();

		if (RetElem.Component->GetWorld() != World)
		{
			// Rename the NC to move it into the current PersistentLevel - it may have been spawned in one
			// level but is now needed in another level.
			// Use the REN_ForceNoResetLoaders flag to prevent the rename from potentially calling FlushAsyncLoading.
			RetElem.Component->Rename(nullptr, World, REN_ForceNoResetLoaders);
		}
	}
	else
	{
		//None in the pool so create a new one.
		RetElem.Component = NewObject<UNiagaraComponent>(World);
		RetElem.Component->SetAutoDestroy(false);// we don't auto destroy, just periodically clear up the pool.
		RetElem.Component->bAutoActivate = false;
		RetElem.Component->SetAsset(Template);
	}

	RetElem.Component->PoolingMethod = PoolingMethod;

#if ENABLE_NC_POOL_DEBUGGING
	if (PoolingMethod == ENCPoolMethod::AutoRelease)
	{
		InUseComponents_Auto.Add(RetElem.Component);
	}
	else if (PoolingMethod == ENCPoolMethod::ManualRelease)
	{
		InUseComponents_Manual.Add(RetElem.Component);
	}
#endif 

	MaxUsed = FMath::Max(MaxUsed, InUseComponents_Manual.Num() + InUseComponents_Auto.Num());
	return RetElem.Component;
}

void FNCPool::Reclaim(UNiagaraComponent* Component, const float CurrentTimeSeconds)
{
	check(Component);

#if ENABLE_NC_POOL_DEBUGGING
	int32 InUseIdx = INDEX_NONE;
	if (Component->PoolingMethod == ENCPoolMethod::AutoRelease)
	{
		InUseIdx = InUseComponents_Auto.IndexOfByKey(Component);
		if (InUseIdx != INDEX_NONE)
		{
			InUseComponents_Auto.RemoveAtSwap(InUseIdx);
		}
	}
	else if (Component->PoolingMethod == ENCPoolMethod::ManualRelease)
	{
		InUseIdx = InUseComponents_Manual.IndexOfByKey(Component);
		if (InUseIdx != INDEX_NONE)
		{
			InUseComponents_Manual.RemoveAtSwap(InUseIdx);
		}
	}
	
	if(InUseIdx == INDEX_NONE)
	{
		UE_LOG(LogNiagara, Error, TEXT("World Particle System Pool is reclaiming a component that is not in it's InUse list!"));
	}
#endif

	//Don't add back to the pool if we're no longer pooling or we've hit our max resident pool size.
	if (GbEnableParticleSystemPooling != 0 && FreeElements.Num() < (int32)Component->GetAsset()->MaxPoolSize)
	{
		Component->DeactivateImmediate();

		Component->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform); // When detaching, maintain world position for optimization purposes
		Component->SetRelativeScale3D(FVector(1.f)); // Reset scale to avoid future uses of this NC having incorrect scale
		Component->SetAbsolute(); // Clear out Absolute settings to defaults
		Component->UnregisterComponent();
		Component->SetCastShadow(false);

		//TODO: reset the delegates here once they are working
		
		//Ensure a small cull distance doesn't linger to future users.
		Component->SetCullDistance(FLT_MAX);

		Component->PoolingMethod = ENCPoolMethod::FreeInPool;
		FreeElements.Push(FNCPoolElement(Component, CurrentTimeSeconds));
	}
	else
	{
		//We've stopped pooling while some effects were in flight so ensure they're destroyed now.
		Component->PoolingMethod = ENCPoolMethod::None;//Reset so we don't trigger warnings about destroying pooled NCs.
		Component->DestroyComponent();
	}
}

void FNCPool::KillUnusedComponents(float KillTime, UNiagaraSystem* Template)
{
	int32 i = 0;
	while (i < FreeElements.Num())
	{
		if (FreeElements[i].LastUsedTime < KillTime)
		{
			UNiagaraComponent* Component = FreeElements[i].Component;
			if (Component)
			{
				Component->PoolingMethod = ENCPoolMethod::None; // Reset so we don't trigger warnings about destroying pooled NCs.
				Component->DestroyComponent();
			}

			FreeElements.RemoveAtSwap(i, 1, false);
		}
		else
		{
			++i;
		}
	}
	FreeElements.Shrink();

#if ENABLE_NC_POOL_DEBUGGING
	//Clean up any in use components that have been cleared out from under the pool. This could happen in someone manually destroys a component for example.
	i = 0;
	while (i < InUseComponents_Manual.Num())
	{
		if (!InUseComponents_Manual[i])
		{
			UE_LOG(LogNiagara, Log, TEXT("Manual Pooled NC has been destroyed! Possibly via a DestroyComponent() call. You should not destroy these but rather call ReleaseToPool on the component so it can be re-used. |\t System: %s"), *Template->GetFullName());
			InUseComponents_Manual.RemoveAtSwap(i, 1, false);
		}
		else
		{
			++i;
		}
	}
	InUseComponents_Manual.Shrink();

	i = 0;
	while (i < InUseComponents_Auto.Num())
	{
		if (!InUseComponents_Auto[i])
		{
			UE_LOG(LogNiagara, Log, TEXT("Auto Pooled NC has been destroyed! Possibly via a DestroyComponent() call. You should not destroy these manually. Just deactivate them and allow then to be reclaimed by the pool automatically. |\t System: %s"), *Template->GetFullName());
			InUseComponents_Auto.RemoveAtSwap(i, 1, false);
		}
		else
		{
			++i;
		}
	}
	InUseComponents_Auto.Shrink();
#endif
}

//////////////////////////////////////////////////////////////////////////


UNiagaraComponentPool::UNiagaraComponentPool(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	LastParticleSytemPoolCleanTime = 0.0f;
}

UNiagaraComponentPool::~UNiagaraComponentPool()
{
	Cleanup();
}

void UNiagaraComponentPool::Cleanup()
{
	for (TPair<UNiagaraSystem*, FNCPool>& Pool : WorldParticleSystemPools)
	{
		Pool.Value.Cleanup();
	}

	WorldParticleSystemPools.Empty();
}

UNiagaraComponent* UNiagaraComponentPool::CreateWorldParticleSystem(UNiagaraSystem* Template, UWorld* World, ENCPoolMethod PoolingMethod)
{
	check(IsInGameThread());
	check(World);
	if (!Template)
	{
		UE_LOG(LogNiagara, Warning, TEXT("Attempted CreateWorldParticleSystem() with a NULL Template!"));
		return nullptr;
	}

	if (World->bIsTearingDown)
	{
		UE_LOG(LogNiagara, Warning, TEXT("Failed to create pooled particle system as we are tearing the world down."));
		return nullptr;
	}

	UNiagaraComponent* Component = nullptr;
	if (GbEnableParticleSystemPooling != 0)
	{
		if (Template->MaxPoolSize > 0)
		{
			FNCPool& Pool = WorldParticleSystemPools.FindOrAdd(Template);
			Component = Pool.Acquire(World, Template, PoolingMethod);
		}
	}
	else
	{
		WorldParticleSystemPools.Empty();//Ensure the pools are cleared out if we've just switched to not pooling.
	}

	if(Component == nullptr)
	{
		//Create a new auto destroy system if we're not pooling.
		Component = NewObject<UNiagaraComponent>(World);
		Component->SetAutoDestroy(true);
		Component->bAutoActivate = false;
		Component->SetAsset(Template);
	}

	check(Component);
	return Component;
}

/** Called when an in-use particle component is finished and wishes to be returned to the pool. */
void UNiagaraComponentPool::ReclaimWorldParticleSystem(UNiagaraComponent* Component)
{
	check(IsInGameThread());
	
	//If this component has been already destroyed we don't add it back to the pool. Just warn so users can fix it.
	if (Component->IsPendingKill())
	{
		UE_LOG(LogNiagara, Log, TEXT("Pooled NC has been destroyed! Possibly via a DestroyComponent() call. You should not destroy components set to auto destroy manually. \nJust deactivate them and allow them to destroy themselves or be reclaimed by the pool if pooling is enabled. | NC: %p |\t System: %s"), Component, *Component->GetAsset()->GetFullName());
		return;
	}

	if (GbEnableParticleSystemPooling)
	{
		float CurrentTime = Component->GetWorld()->GetTimeSeconds();

		//Periodically clear up the pools.
		if (CurrentTime - LastParticleSytemPoolCleanTime > GParticleSystemPoolingCleanTime)
		{
			LastParticleSytemPoolCleanTime = CurrentTime;
			for (TPair<UNiagaraSystem*, FNCPool>& Pair : WorldParticleSystemPools)
			{
				Pair.Value.KillUnusedComponents(CurrentTime - GParticleSystemPoolKillUnusedTime, Component->GetAsset());
			}
		}
		
		FNCPool* NCPool = WorldParticleSystemPools.Find(Component->GetAsset());
		if (!NCPool)
		{
			UE_LOG(LogNiagara, Warning, TEXT("WorldNC Pool trying to reclaim a system for which it doesn't have a pool! Likely because SetAsset() has been called on this NC. | World: %p | NC: %p | Sys: %s"), Component->GetWorld(), Component, *Component->GetAsset()->GetFullName());
			//Just add the new pool and reclaim to that one.
			NCPool = &WorldParticleSystemPools.Add(Component->GetAsset());
		}

		NCPool->Reclaim(Component, CurrentTime);
	}
	else
	{
		Component->DestroyComponent();
	}
}

void UNiagaraComponentPool::ReclaimActiveParticleSystems()
{
	check(IsInGameThread());

	for (TPair<UNiagaraSystem*, FNCPool>& Pair : WorldParticleSystemPools)
	{
		FNCPool& Pool = Pair.Value;

		for(int32 i = Pool.InUseComponents_Auto.Num() - 1; i >= 0; --i)
		{
			UNiagaraComponent* Component = Pool.InUseComponents_Auto[i];
			if (ensureAlways(Component))
			{
				Component->DeactivateImmediate();
			}
		}

		for(int32 i = Pool.InUseComponents_Manual.Num() - 1; i >= 0; --i)
		{
			UNiagaraComponent* Component = Pool.InUseComponents_Manual[i];
			if (ensureAlways(Component))
			{
				Component->DeactivateImmediate();
			}
		}
	}
}

void UNiagaraComponentPool::Dump()
{
#if ENABLE_NC_POOL_DEBUGGING
	FString DumpStr;

	uint32 TotalMemUsage = 0;
	for (TPair<UNiagaraSystem*, FNCPool>& Pair : WorldParticleSystemPools)
	{
		UNiagaraSystem* System = Pair.Key;
		FNCPool& Pool = Pair.Value;		
		uint32 FreeMemUsage = 0;
		for (FNCPoolElement& Elem : Pool.FreeElements)
		{
			if (ensureAlways(Elem.Component))
			{
				FreeMemUsage += Elem.Component->GetApproxMemoryUsage();
			}
		}
		uint32 InUseMemUsage = 0;
		for (UNiagaraComponent* Component : Pool.InUseComponents_Auto)
		{
			if (ensureAlways(Component))
			{
				InUseMemUsage += Component->GetApproxMemoryUsage();				
			}
		}
		for (UNiagaraComponent* Component : Pool.InUseComponents_Manual)
		{
			if (ensureAlways(Component))
			{
				InUseMemUsage += Component->GetApproxMemoryUsage();
			}
		}

		TotalMemUsage += FreeMemUsage;
		TotalMemUsage += InUseMemUsage;

		DumpStr += FString::Printf(TEXT("Free: %d (%uB) \t|\t Used(Auto - Manual): %d - %d (%uB) \t|\t MaxUsed: %d \t|\t System: %s\n"), Pool.FreeElements.Num(), FreeMemUsage, Pool.InUseComponents_Auto.Num(), Pool.InUseComponents_Manual.Num(), InUseMemUsage, Pool.MaxUsed, *System->GetFullName());
	}

	UE_LOG(LogNiagara, Log, TEXT("***************************************"));
	UE_LOG(LogNiagara, Log, TEXT("*Particle System Pool Info - Total Mem = %.2fMB*"), TotalMemUsage / 1024.0f / 1024.0f);
	UE_LOG(LogNiagara, Log, TEXT("***************************************"));
	UE_LOG(LogNiagara, Log, TEXT("%s"), *DumpStr);
	UE_LOG(LogNiagara, Log, TEXT("***************************************"));

#endif
}
