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

UContextualAnimSceneActorComponent* UContextualAnimManager::FindClosestSceneActorCompToActor(const AActor* Actor) const
{
	SCOPE_CYCLE_COUNTER(STAT_ContextualAnim_FindClosestSceneActorComp);

	//@TODO: Implement some space partitioning to reduce the cost of this search (a 2D grid might be enough)

	UContextualAnimSceneActorComponent* ClosestSceneActorComp = nullptr;
	if(Actor)
	{
		const FVector TestLocation = Actor->GetActorLocation();

		float BestDistanceSq = MAX_FLT;
		for (UContextualAnimSceneActorComponent* SceneActorComp : SceneActorCompContainer)
		{
			check(SceneActorComp);
			check(SceneActorComp->GetOwner());

			if(SceneActorComp->GetOwner() != Actor)
			{
				const float DistanceSq = FVector::DistSquared(SceneActorComp->GetComponentLocation(), TestLocation);
				if (DistanceSq < BestDistanceSq)
				{
					BestDistanceSq = DistanceSq;
					ClosestSceneActorComp = SceneActorComp;
				}
			}
		}
	}

	return ClosestSceneActorComp;
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

bool UContextualAnimManager::TryStartScene(const UContextualAnimSceneAsset* SceneAsset, const FContextualAnimSceneBindings& Bindings)
{
	if (SceneAsset == nullptr)
	{
		UE_LOG(LogContextualAnim, Warning, TEXT("UContextualAnimManager::TryStartScene. Can't start scene. Reason: Invalid Scene Asset"));
		return false;
	}

	TMap<FName, FContextualAnimSceneActorData> SceneActorMap;
	SceneActorMap.Reserve(Bindings.RoleToActorMap.Num());

	// Find primary actor first so we can perform queries relative to it
	AActor* const* PrimaryActorPtr = Bindings.RoleToActorMap.Find(SceneAsset->PrimaryRole);
	if (PrimaryActorPtr == nullptr)
	{
		UE_LOG(LogContextualAnim, Warning, TEXT("UContextualAnimManager::TryStartScene. Can't start scene. Reason: Attempting to bind an invalid actor to the primary role. SceneAsset: %s Role: %s"),
			*GetNameSafe(SceneAsset), *SceneAsset->PrimaryRole.ToString());

		return false;
	}

	AActor* PrimaryActor = *PrimaryActorPtr;
	const UContextualAnimSceneActorComponent* PrimarySceneActorComp = PrimaryActor->FindComponentByClass<UContextualAnimSceneActorComponent>();
	const FTransform ToWorldTransform = PrimarySceneActorComp ? PrimarySceneActorComp->GetComponentTransform() : PrimaryActor->GetActorTransform();

	for (const auto& Pair : Bindings.RoleToActorMap)
	{
		FName RoleToBind = Pair.Key;
		AActor* ActorToBind = Pair.Value;

		if (ActorToBind == nullptr)
		{
			UE_LOG(LogTemp, Warning, TEXT("UContextualAnimManager::TryStartScene. Can't start scene. Reason: Attempting to bind an invalid actor. SceneAsset: %s Role: %s"),
				*GetNameSafe(SceneAsset), *RoleToBind.ToString());

			return false;
		}

		// Primary role is already bound
		if (RoleToBind != SceneAsset->PrimaryRole)
		{
			int32 AnimDataIndex = INDEX_NONE;
			float AnimStartTime = 0.f;

			// Attempt to use AnimDataIndex and AnimStartTime supplied with the bindings
			if (Bindings.AnimDataIndex != INDEX_NONE)
			{
				if (SceneAsset->GetAnimDataForRoleAtIndex(RoleToBind, Bindings.AnimDataIndex) == nullptr)
				{
					UE_LOG(LogTemp, Warning, TEXT("UContextualAnimManager::TryStartScene. Can't start scene. Reason: Invalid AnimDataIndex. SceneAsset: %s Role: %s AnimDataIndex: %d"),
						*GetNameSafe(SceneAsset), *RoleToBind.ToString(), Bindings.AnimDataIndex);

					return false;
				}

				AnimDataIndex = Bindings.AnimDataIndex;
				AnimStartTime = Bindings.AnimStartTime;
			}
			// Query for best animation to use
			else
			{
				FContextualAnimQueryResult Result;
				const bool bQueryResult = SceneAsset->Query(RoleToBind, Result, FContextualAnimQueryParams(ActorToBind, true, true), ToWorldTransform);
				if (bQueryResult == false)
				{
					UE_LOG(LogContextualAnim, Warning, TEXT("UContextualAnimManager::TryStartScene. Can't start scene. Reason: Can't find Track for actor. SceneAsset: %s Role: %s Actor: %s"),
						*GetNameSafe(SceneAsset), *RoleToBind.ToString(), *GetNameSafe(ActorToBind));

					return false;
				}

				AnimDataIndex = Result.DataIndex;
				AnimStartTime = Result.AnimStartTime;
			}

			//@TODO: Fix this for +2 actors interactions. AnimStartTime must be calculated only once and then everyone should use it.

			SceneActorMap.Add(RoleToBind, FContextualAnimSceneActorData(ActorToBind, SceneAsset->GetAnimDataForRoleAtIndex(RoleToBind, AnimDataIndex), SceneAsset->GetTrackSettings(RoleToBind), AnimStartTime));

			if (!SceneActorMap.Contains(SceneAsset->PrimaryRole))
			{
				SceneActorMap.Add(SceneAsset->PrimaryRole, FContextualAnimSceneActorData(PrimaryActor,
					SceneAsset->GetAnimDataForRoleAtIndex(SceneAsset->PrimaryRole, AnimDataIndex), 
					SceneAsset->GetTrackSettings(SceneAsset->PrimaryRole), AnimStartTime)
				);
			}
		}
	}

	UClass* Class = SceneAsset->SceneInstanceClass;
	UContextualAnimSceneInstance* NewInstance = Class ? NewObject<UContextualAnimSceneInstance>(this, Class) : NewObject<UContextualAnimSceneInstance>(this);
	NewInstance->SceneAsset = SceneAsset;
	NewInstance->SceneActorMap = MoveTemp(SceneActorMap);
	NewInstance->Start();
	NewInstance->OnSceneEnded.BindUObject(this, &UContextualAnimManager::OnSceneInstanceEnded);

	Instances.Add(NewInstance);

	return true;
}

bool UContextualAnimManager::TryStartScene(const UContextualAnimSceneAsset* SceneAsset, AActor* PrimaryActor, const TSet<UContextualAnimSceneActorComponent*>& SceneActorComps)
{
	if (SceneAsset == nullptr)
	{
		UE_LOG(LogContextualAnim, Warning, TEXT("UContextualAnimManager::TryStartScene. Can't start scene. Reason: Invalid Scene Asset"));
		return false;
	}

	if (PrimaryActor == nullptr || IsActorInAnyScene(PrimaryActor))
	{
		UE_LOG(LogContextualAnim, Warning, TEXT("UContextualAnimManager::TryStartScene. Can't start scene. Reason: Invalid PrimaryActor or already in another scene. PrimaryActor: %s"), *GetNameSafe(PrimaryActor));
		return false;
	}

	const TArray<FName>& Roles = SceneAsset->GetRoles();

	FContextualAnimSceneBindings Bindings;
	Bindings.RoleToActorMap.Add(SceneAsset->PrimaryRole, PrimaryActor);

	const UContextualAnimSceneActorComponent* PrimarySceneActorComp = PrimaryActor->FindComponentByClass<UContextualAnimSceneActorComponent>();
	const FTransform ToWorldTransform = PrimarySceneActorComp ? PrimarySceneActorComp->GetComponentTransform() : PrimaryActor->GetActorTransform();

	for (UContextualAnimSceneActorComponent* SceneActorComp : SceneActorComps)
	{
		if (SceneActorComp && SceneActorComp->GetOwner())
		{
			AActor* ActorToBind = SceneActorComp->GetOwner();
			if(PrimaryActor != ActorToBind && !IsActorInAnyScene(ActorToBind))
			{
				for (const FName& RoleToBind : Roles)
				{
					if (!Bindings.RoleToActorMap.Contains(RoleToBind))
					{
						FContextualAnimQueryResult Result;
						if (SceneAsset->Query(RoleToBind, Result, FContextualAnimQueryParams(ActorToBind, true, true), ToWorldTransform))
						{
							Bindings.RoleToActorMap.Add(RoleToBind, ActorToBind);

							if (Bindings.AnimDataIndex != INDEX_NONE)
							{
								Bindings.AnimDataIndex = Result.DataIndex;
								Bindings.AnimStartTime = Result.AnimStartTime;
							}

							break;
						}
					}
				}

				if (Roles.Num() == Bindings.RoleToActorMap.Num())
				{
					return TryStartScene(SceneAsset, Bindings);
				}
			}
		}
	}

	return false;
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