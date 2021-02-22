// Copyright Epic Games, Inc. All Rights Reserved.

#include "ContextualAnimManager.h"
#include "ContextualAnimTypes.h"
#include "ContextualAnimActorInterface.h"
#include "ContextualAnimSceneInstance.h"
#include "DrawDebugHelpers.h"
#include "ContextualAnimSceneAsset.h"
#include "ContextualAnimCompositeSceneAsset.h"
#include "ContextualAnimation.h"
#include "Engine/World.h"

UContextualAnimManager::UContextualAnimManager(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{

}

UContextualAnimManager* UContextualAnimManager::Get(const UWorld* World)
{
	return (World) ? FContextualAnimationModule::GetManager(World) : nullptr;
}

UWorld* UContextualAnimManager::GetWorld()const
{
	return CastChecked<UWorld>(GetOuter());
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

bool UContextualAnimManager::TryStartScene(const UContextualAnimSceneAssetBase* SceneAsset, const TMap<FName, AActor*>& Bindings)
{
	if (SceneAsset == nullptr)
	{
		UE_LOG(LogContextualAnim, Warning, TEXT("UContextualAnimManager::TryStartScene. Can't start scene. Reason: Invalid Scene Asset"));
		return false;
	}

	TMap<FName, FContextualAnimSceneActorData> SceneActorMap;
	SceneActorMap.Reserve(Bindings.Num());

	// @TODO: Add virtual function in the asset class that resolve the bindings for us
	if (const UContextualAnimSceneAsset* SimpleScene = Cast<const UContextualAnimSceneAsset>(SceneAsset))
	{
		for (const auto& Pair : Bindings)
		{
			FName RoleToBind = Pair.Key;
			AActor* ActorToBind = Pair.Value;

			if(ActorToBind == nullptr)
			{
				UE_LOG(LogTemp, Warning, TEXT("UContextualAnimManager::TryStartScene. Can't start scene. Reason: Attempting to bind an invalid actor. SceneAsset: %s Role: %s"), 
				*GetNameSafe(SceneAsset), *RoleToBind.ToString());

				return false;
			}

			const FContextualAnimTrack* Track = SimpleScene->FindTrack(RoleToBind);
			if(Track == nullptr)
			{
				UE_LOG(LogContextualAnim, Warning, TEXT("UContextualAnimManager::TryStartScene. Can't start scene. Reason: Invalid Track. SceneAsset: %s Role: %s"),
				*GetNameSafe(SceneAsset), *RoleToBind.ToString());

				return false;
			}

			SceneActorMap.Add(RoleToBind, FContextualAnimSceneActorData(ActorToBind, &Track->AnimData));
		}
	}
	else if (const UContextualAnimCompositeSceneAsset* CompositeScene = Cast<const UContextualAnimCompositeSceneAsset>(SceneAsset))
	{
		// Bind the Primary Role first so we can perform the query relative to it
		FContextualAnimSceneActorData PrimaryData;
		for (const auto& Pair : Bindings)
		{
			if (Pair.Key == CompositeScene->PrimaryRole)
			{
				PrimaryData.Actor = Pair.Value;
				PrimaryData.AnimDataPtr = &CompositeScene->InteractableTrack.AnimData;

				if (!PrimaryData.Actor.IsValid())
				{
					UE_LOG(LogContextualAnim, Warning, TEXT("UContextualAnimManager::TryStartScene. Can't start scene. Reason: Attempting to bind an invalid actor to the primary role. SceneAsset: %s Role: %s"),
						*GetNameSafe(SceneAsset), *CompositeScene->PrimaryRole.ToString());

					return false;
				}

				if (PrimaryData.AnimDataPtr == nullptr)
				{
					UE_LOG(LogContextualAnim, Warning, TEXT("UContextualAnimManager::TryStartScene. Can't start scene. Reason: Invalid Track. SceneAsset: %s Role: %s"),
						*GetNameSafe(SceneAsset), *CompositeScene->PrimaryRole.ToString());

					return false;
				}

				SceneActorMap.Add(Pair.Key, PrimaryData);

				break;
			}
		}

		for (const auto& Pair : Bindings)
		{
			FName RoleToBind = Pair.Key;
			AActor* ActorToBind = Pair.Value;

			if (ActorToBind == nullptr)
			{
				UE_LOG(LogTemp, Warning, TEXT("UContextualAnimManager::TryStartScene. Can't start scene. Reason: Attempting to bind an invalid actor. SceneAsset: %s Role: %s"),
					*GetNameSafe(SceneAsset), *RoleToBind.ToString());

				return false;
			}

			// Primary role was already bound
			if (RoleToBind != CompositeScene->PrimaryRole)
			{
				FTransform ToWorldTransform = PrimaryData.GetTransform();

				FContextualAnimQueryResult Result;
				const bool bQueryResult = CompositeScene->QueryData(Result, FContextualAnimQueryParams(ActorToBind, true, true), ToWorldTransform);
				if (bQueryResult == false)
				{
					UE_LOG(LogContextualAnim, Warning, TEXT("UContextualAnimManager::TryStartScene. Can't start scene. Reason: Can't find Track for actor. SceneAsset: %s Role: %s Actor: %s"),
						*GetNameSafe(SceneAsset), *RoleToBind.ToString(), *GetNameSafe(ActorToBind));

					return false;
				}

				SceneActorMap.Add(RoleToBind, FContextualAnimSceneActorData(ActorToBind, &CompositeScene->InteractorTrack.AnimDataContainer[Result.DataIndex], Result.AnimStartTime));
			}
		}
	}

	UContextualAnimSceneInstance* NewInstance = NewObject<UContextualAnimSceneInstance>(this);
	NewInstance->SceneAsset = SceneAsset;
	NewInstance->SceneActorMap = MoveTemp(SceneActorMap);
	NewInstance->Start();
	NewInstance->OnSceneEnded.BindUObject(this, &UContextualAnimManager::OnSceneInstanceEnded);

	Instances.Add(NewInstance);

	return true;
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