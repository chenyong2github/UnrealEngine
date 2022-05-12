// Copyright Epic Games, Inc. All Rights Reserved.

#include "ContextualAnimManager.h"
#include "ContextualAnimActorInterface.h"
#include "ContextualAnimSceneInstance.h"
#include "DrawDebugHelpers.h"
#include "ContextualAnimSceneAsset.h"
#include "ContextualAnimation.h"
#include "ContextualAnimSceneActorComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

DECLARE_CYCLE_STAT(TEXT("ContextualAnim FindClosestSceneActorComp"), STAT_ContextualAnim_FindClosestSceneActorComp, STATGROUP_Anim);

UContextualAnimManager::UContextualAnimManager(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

UContextualAnimManager* UContextualAnimManager::Get(const UWorld* World)
{
	return (World) ? FContextualAnimationModule::GetManager(World) : nullptr;
}

UContextualAnimManager* UContextualAnimManager::GetContextualAnimManager(UObject* WorldContextObject)
{
	return Get(GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull));
}

UWorld* UContextualAnimManager::GetWorld()const
{
	return CastChecked<UWorld>(GetOuter());
}

ETickableTickType UContextualAnimManager::GetTickableTickType() const
{
	//@TODO: Switch to Conditional and use IsTickable to determine whether to tick. It should only tick when scene instances are active
	return (HasAnyFlags(RF_ClassDefaultObject)) ? ETickableTickType::Never : ETickableTickType::Always;
}
TStatId UContextualAnimManager::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UContextualAnimManager, STATGROUP_Tickables);
}

void UContextualAnimManager::RegisterSceneActorComponent(UContextualAnimSceneActorComponent* SceneActorComp)
{
	if(SceneActorComp)
	{
		SceneActorCompContainer.Add(SceneActorComp);
	}
}

void UContextualAnimManager::UnregisterSceneActorComponent(UContextualAnimSceneActorComponent* SceneActorComp)
{
	if (SceneActorComp)
	{
		SceneActorCompContainer.Remove(SceneActorComp);
	}
}

void UContextualAnimManager::Tick(float DeltaTime)
{
	for (UContextualAnimSceneInstance* SceneInstance : Instances)
	{
		SceneInstance->Tick(DeltaTime);
	}
}

bool UContextualAnimManager::IsActorInAnyScene(AActor* Actor) const
{
	if (Actor)
	{
		for (UContextualAnimSceneInstance* SceneInstance : Instances)
		{
			if (SceneInstance->IsActorInThisScene(Actor))
			{
				return true;
			}
		}
	}

	return false;
}

UContextualAnimSceneInstance* UContextualAnimManager::GetSceneWithActor(AActor* Actor)
{
	if (Actor)
	{
		for (UContextualAnimSceneInstance* SceneInstance : Instances)
		{
			if (SceneInstance->IsActorInThisScene(Actor))
			{
				return SceneInstance;
			}
		}
	}

	return nullptr;
}

UContextualAnimSceneInstance* UContextualAnimManager::ForceStartScene(const UContextualAnimSceneAsset& SceneAsset, const FContextualAnimStartSceneParams& Params)
{
	FContextualAnimSceneBindings Bindings;
	for (const auto& Pair : Params.RoleToActorMap)
	{
		FName RoleToBind = Pair.Key;

		AActor* ActorToBind = Pair.Value.GetActor();
		if (ActorToBind == nullptr)
		{
			UE_LOG(LogContextualAnim, Warning, TEXT("UContextualAnimManager::ForceStartScene. Can't start scene. Reason: Trying to bind Invalid Actor. SceneAsset: %s Role: %s"),
				*GetNameSafe(&SceneAsset), *RoleToBind.ToString());

			return nullptr;
		}

		const FContextualAnimTrack* AnimTrack = SceneAsset.GetAnimTrack(RoleToBind, Params.VariantIdx);
		if (AnimTrack == nullptr)
		{
			UE_LOG(LogContextualAnim, Warning, TEXT("UContextualAnimManager::ForceStartScene. Can't start scene. Reason: Can't find anim track for '%s'. SceneAsset: %s"), 
				*RoleToBind.ToString(), *GetNameSafe(&SceneAsset));

			return nullptr;
		}

		Bindings.Add(FContextualAnimSceneBinding(Pair.Value, SceneAsset, *AnimTrack));
	}

	UClass* Class = SceneAsset.GetSceneInstanceClass();
	UContextualAnimSceneInstance* NewInstance = Class ? NewObject<UContextualAnimSceneInstance>(this, Class) : NewObject<UContextualAnimSceneInstance>(this);
	NewInstance->SceneAsset = &SceneAsset;
	NewInstance->Bindings = MoveTemp(Bindings);
	NewInstance->Start();
	NewInstance->OnSceneEnded.AddDynamic(this, &UContextualAnimManager::OnSceneInstanceEnded);

	Instances.Add(NewInstance);

	return NewInstance;
}

UContextualAnimSceneInstance* UContextualAnimManager::BP_TryStartScene(const UContextualAnimSceneAsset* SceneAsset, const FContextualAnimStartSceneParams& Params)
{
	if(SceneAsset == nullptr)
	{
		UE_LOG(LogContextualAnim, Warning, TEXT("UContextualAnimManager::TryStartScene. Can't start scene. Reason: Invalid Scene Asset"));
		return nullptr;
	}

	return TryStartScene(*SceneAsset, Params);
}

UContextualAnimSceneInstance* UContextualAnimManager::TryStartScene(const UContextualAnimSceneAsset& SceneAsset, const FContextualAnimStartSceneParams& Params)
{
	FContextualAnimSceneBindings Bindings;

	bool bSuccess = false;
	if (Params.VariantIdx != INDEX_NONE)
	{
		bSuccess = FContextualAnimSceneBindings::TryCreateBindings(SceneAsset, Params.VariantIdx, Params.RoleToActorMap, Bindings);
	}
	else
	{
		const int32 VariantsNum = SceneAsset.GetTotalVariants();
		for (int32 VariantIdx = 0; VariantIdx < VariantsNum; VariantIdx++)
		{
			if (FContextualAnimSceneBindings::TryCreateBindings(SceneAsset, VariantIdx, Params.RoleToActorMap, Bindings))
			{
				bSuccess = true;
				break;
			}
		}
	}

	if (bSuccess)
	{
		UClass* Class = SceneAsset.GetSceneInstanceClass();
		UContextualAnimSceneInstance* NewInstance = Class ? NewObject<UContextualAnimSceneInstance>(this, Class) : NewObject<UContextualAnimSceneInstance>(this);
		NewInstance->SceneAsset = &SceneAsset;
		NewInstance->Bindings = MoveTemp(Bindings);
		NewInstance->Start();
		NewInstance->OnSceneEnded.AddDynamic(this, &UContextualAnimManager::OnSceneInstanceEnded);

		Instances.Add(NewInstance);

		return NewInstance;
	}

	return nullptr;
}

bool UContextualAnimManager::TryStopSceneWithActor(AActor* Actor)
{
	if(UContextualAnimSceneInstance* SceneInstance = GetSceneWithActor(Actor))
	{
		SceneInstance->Stop();
		return true;
	}

	return false;
}

void UContextualAnimManager::OnSceneInstanceEnded(UContextualAnimSceneInstance* SceneInstance)
{
	if(SceneInstance)
	{
		Instances.Remove(SceneInstance);
	}
}