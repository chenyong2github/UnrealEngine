// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/GameFrameworkComponentManager.h"
#include "Components/GameFrameworkComponent.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "ModularGameplayLogs.h"

#if !UE_BUILD_SHIPPING
#include "HAL/IConsoleManager.h"
#include "UObject/UObjectIterator.h"
static FAutoConsoleCommand CVarDumpGameFrameworkComponentManagers(
	TEXT("ModularGameplay.DumpGameFrameworkComponentManagers"),
	TEXT("Lists all active component requests, all receiver actors, and all instanced components on all game framework component managers."),
	FConsoleCommandDelegate::CreateStatic(UGameFrameworkComponentManager::DumpGameFrameworkComponentManagers));
#endif // !UE_BUILD_SHIPPING

FComponentRequestHandle::~FComponentRequestHandle()
{
	UGameFrameworkComponentManager* LocalManager = OwningManager.Get();
	if (LocalManager)
	{
		LocalManager->RemoveComponentRequest(ReceiverClass, ComponentClass);
	}
}

bool FComponentRequestHandle::IsValid() const
{
	return OwningManager.IsValid();
}

void UGameFrameworkComponentManager::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	if (UGameFrameworkComponentManager* GFCM = Cast<UGameFrameworkComponentManager>(InThis))
	{
		for (auto MapIt = GFCM->ReceiverClassToComponentClassMap.CreateIterator(); MapIt; ++MapIt)
		{
			for (UClass* ValueElement : MapIt.Value())
			{
				Collector.AddReferencedObject(ValueElement);
			}
		}
	}
}

#if !UE_BUILD_SHIPPING
void UGameFrameworkComponentManager::DumpGameFrameworkComponentManagers()
{
	UE_LOG(LogModularGameplay, Display, TEXT("Dumping GameFrameworkComponentManagers..."));
	for (TObjectIterator<UGameFrameworkComponentManager> ManagerIt; ManagerIt; ++ManagerIt)
	{
		if (UGameFrameworkComponentManager* Manager = *ManagerIt)
		{
			UE_LOG(LogModularGameplay, Display, TEXT("  Manager: %s"), *GetPathNameSafe(Manager));

#if WITH_EDITOR
			UE_LOG(LogModularGameplay, Display, TEXT("    Receivers... (Num:%d)"), Manager->AllReceivers.Num());
			for (auto SetIt = Manager->AllReceivers.CreateConstIterator(); SetIt; ++SetIt)
			{
				UE_LOG(LogModularGameplay, Display, TEXT("      ReceiverInstance: %s"), *GetPathNameSafe(*SetIt));
			}
#endif // WITH_EDITOR

			UE_LOG(LogModularGameplay, Display, TEXT("    Components... (Num:%d)"), Manager->ComponentClassToComponentInstanceMap.Num());
			for (auto MapIt = Manager->ComponentClassToComponentInstanceMap.CreateConstIterator(); MapIt; ++MapIt)
			{
				UE_LOG(LogModularGameplay, Display, TEXT("      ComponentClass: %s (Num:%d)"), *GetPathNameSafe(MapIt.Key()), MapIt.Value().Num());
				for (const FObjectKey& ComponentInstance : MapIt.Value())
				{
					UE_LOG(LogModularGameplay, Display, TEXT("        ComponentInstance: %s"), *GetPathNameSafe(ComponentInstance.ResolveObjectPtr()));
				}
			}
			UE_LOG(LogModularGameplay, Display, TEXT("    Requests... (Num:%d)"), Manager->ReceiverClassToComponentClassMap.Num());
			for (auto MapIt = Manager->ReceiverClassToComponentClassMap.CreateConstIterator(); MapIt; ++MapIt)
			{
				UE_LOG(LogModularGameplay, Display, TEXT("      RequestReceiverClass: %s (Num:%d)"), *MapIt.Key().ToDebugString(), MapIt.Value().Num());
				for (UClass* ComponentClass : MapIt.Value())
				{
					UE_LOG(LogModularGameplay, Display, TEXT("        RequestComponentClass: %s"), *GetPathNameSafe(ComponentClass));
				}
			}
		}
	}
}
#endif // !UE_BUILD_SHIPPING

void UGameFrameworkComponentManager::AddReceiver(AActor* Receiver, bool bAddOnlyInGameWorlds)
{
	if (Receiver != nullptr)
	{
		if (bAddOnlyInGameWorlds)
		{
			UWorld* ReceiverWorld = Receiver->GetWorld();
			if ((ReceiverWorld == nullptr) || !ReceiverWorld->IsGameWorld() || ReceiverWorld->IsPreviewWorld())
			{
				return;
			}
		}

		AddReceiverInternal(Receiver);
	}
}


void UGameFrameworkComponentManager::AddReceiverInternal(AActor* Receiver)
{
	checkSlow(Receiver);
	
#if WITH_EDITOR
	AllReceivers.Add(Receiver);
#endif
	
	for (UClass* Class = Receiver->GetClass(); Class && Class != AActor::StaticClass(); Class = Class->GetSuperClass())
	{
		FComponentRequestReceiverClassPath ReceiverClassPath(Class);
		if (TSet<UClass*>* ComponentClasses = ReceiverClassToComponentClassMap.Find(ReceiverClassPath))
		{
			for (UClass* ComponentClass : *ComponentClasses)
			{
				if (ComponentClass)
				{
					CreateComponentOnInstance(Receiver, ComponentClass);
				}
			}
		}
	}
}

void UGameFrameworkComponentManager::RemoveReceiver(AActor* Receiver)
{
	if (Receiver != nullptr)
	{
		RemoveReceiverInternal(Receiver);
	}
}

void UGameFrameworkComponentManager::RemoveReceiverInternal(AActor* Receiver)
{
	checkSlow(Receiver);
	
#if WITH_EDITOR
	ensureMsgf(AllReceivers.Remove(Receiver) > 0, TEXT("Called RemoveReceiver without first calling AddReceiver. Actor:%s"), *GetPathNameSafe(Receiver));
#endif
	
	TInlineComponentArray<UActorComponent*> ComponentsToDestroy;
	for (UActorComponent* Component : Receiver->GetComponents())
	{
		if (UActorComponent* GFC = Cast<UActorComponent>(Component))
		{
			UClass* ComponentClass = GFC->GetClass();
			TSet<FObjectKey>* ComponentInstances = ComponentClassToComponentInstanceMap.Find(ComponentClass);
			if (ComponentInstances)
			{
				if (ComponentInstances->Contains(GFC))
				{
					ComponentsToDestroy.Add(GFC);
				}
			}
		}
	}

	for (UActorComponent* Component : ComponentsToDestroy)
	{
		DestroyInstancedComponent(Component);
	}
}

TSharedPtr<FComponentRequestHandle> UGameFrameworkComponentManager::AddComponentRequest(const TSoftClassPtr<AActor>& ReceiverClass, TSubclassOf<UActorComponent> ComponentClass)
{
	// You must have a receiver and component class. The receiver cannot be AActor, that is too broad and would be bad for performance.
	if (!ensure(!ReceiverClass.IsNull()) || !ensure(ComponentClass) || !ensure(ReceiverClass.ToString() != TEXT("/Script/Engine.Actor")))
	{
		return nullptr;
	}

	FComponentRequestReceiverClassPath ReceiverClassPath(ReceiverClass);
	UClass* ComponentClassPtr = ComponentClass.Get();

	FComponentRequest NewRequest;
	NewRequest.ReceiverClassPath = ReceiverClassPath;
	NewRequest.ComponentClass = ComponentClassPtr;
	int32& RequestCount = RequestTrackingMap.FindOrAdd(NewRequest);
	RequestCount++;

	if (RequestCount == 1)
	{
		TSet<UClass*>& ComponentClasses = ReceiverClassToComponentClassMap.FindOrAdd(ReceiverClassPath);
		ComponentClasses.Add(ComponentClassPtr);

		if (UClass* ReceiverClassPtr = ReceiverClass.Get())
		{
			UGameInstance* LocalGameInstance = GetGameInstance();
			if (ensure(LocalGameInstance))
			{
				UWorld* LocalWorld = LocalGameInstance->GetWorld();
				if (ensure(LocalWorld))
				{
					for (TActorIterator<AActor> ActorIt(LocalWorld, ReceiverClassPtr); ActorIt; ++ActorIt)
					{
						if (ActorIt->HasActorBegunPlay())
						{
#if WITH_EDITOR
							ensureMsgf(AllReceivers.Contains(*ActorIt), TEXT("You may not add a component request for an actor class that does not call AddReceiver/RemoveReceiver in code! Class:%s"), *GetPathNameSafe(ReceiverClassPtr));
#endif
							CreateComponentOnInstance(*ActorIt, ComponentClass);
						}
					}
				}
			}
		}
		else
		{
			// Actor class is not in memory, there will be no actor instances
		}

		return MakeShared<FComponentRequestHandle>(this, ReceiverClass, ComponentClass);
	}

	return nullptr;
}

void UGameFrameworkComponentManager::RemoveComponentRequest(const TSoftClassPtr<AActor>& ReceiverClass, TSubclassOf<UActorComponent> ComponentClass)
{
	FComponentRequestReceiverClassPath ReceiverClassPath(ReceiverClass);
	UClass* ComponentClassPtr = ComponentClass.Get();

	FComponentRequest NewRequest;
	NewRequest.ReceiverClassPath = ReceiverClassPath;
	NewRequest.ComponentClass = ComponentClassPtr;
	int32& RequestCount = RequestTrackingMap.FindChecked(NewRequest);
	check(RequestCount > 0);
	RequestCount--;

	if (RequestCount == 0)
	{
		if (TSet<UClass*>* ComponentClasses = ReceiverClassToComponentClassMap.Find(ReceiverClassPath))
		{
			ComponentClasses->Remove(ComponentClassPtr);
			if (ComponentClasses->Num() == 0)
			{
				ReceiverClassToComponentClassMap.Remove(ReceiverClassPath);
			}
		}

		if (UClass* ReceiverClassPtr = ReceiverClass.Get())
		{
			if (TSet<FObjectKey>* ComponentInstances = ComponentClassToComponentInstanceMap.Find(ComponentClassPtr))
			{
				TArray<UActorComponent*> ComponentsToDestroy;
				for (const FObjectKey& InstanceKey : *ComponentInstances)
				{
					UActorComponent* Comp = Cast<UActorComponent>(InstanceKey.ResolveObjectPtr());
					if (Comp)
					{
						AActor* OwnerActor = Comp->GetOwner();
						if (OwnerActor && OwnerActor->IsA(ReceiverClassPtr))
						{
							ComponentsToDestroy.Add(Comp);
						}
					}
				}

				for (UActorComponent* Component : ComponentsToDestroy)
				{
					DestroyInstancedComponent(Component);
				}
			}
		}
		else
		{
			// Actor class is not in memory, there will be no actor or component instances.
			ensure(!ComponentClassToComponentInstanceMap.Contains(ComponentClassPtr));
		}
	}
}

void UGameFrameworkComponentManager::CreateComponentOnInstance(AActor* ActorInstance, TSubclassOf<UActorComponent> ComponentClass)
{
	check(ActorInstance);
	check(ComponentClass);

	if (!ComponentClass->GetDefaultObject<UActorComponent>()->GetIsReplicated() || ActorInstance->GetLocalRole() == ROLE_Authority)
	{
		UActorComponent* NewComp = NewObject<UActorComponent>(ActorInstance, ComponentClass, ComponentClass->GetFName());
		TSet<FObjectKey>& ComponentInstances = ComponentClassToComponentInstanceMap.FindOrAdd(*ComponentClass);
		ComponentInstances.Add(NewComp);

		NewComp->RegisterComponent();
	}
}

void UGameFrameworkComponentManager::DestroyInstancedComponent(UActorComponent* Component)
{
	check(Component);

	UClass* ComponentClass = Component->GetClass();
	if (TSet<FObjectKey>* ComponentInstances = ComponentClassToComponentInstanceMap.Find(ComponentClass))
	{
		ComponentInstances->Remove(Component);
		if (ComponentInstances->Num() == 0)
		{
			ComponentClassToComponentInstanceMap.Remove(ComponentClass);
		}
	}
	Component->DestroyComponent();
	Component->SetFlags(RF_Transient);
}

void UGameFrameworkComponentManager::AddGameFrameworkComponentReceiver(AActor* Receiver, bool bAddOnlyInGameWorlds)
{
	if (Receiver != nullptr)
	{
		if (bAddOnlyInGameWorlds)
		{
			UWorld* ReceiverWorld = Receiver->GetWorld();
			if ((ReceiverWorld != nullptr) && ReceiverWorld->IsGameWorld() && !ReceiverWorld->IsPreviewWorld())
			{
				if (UGameFrameworkComponentManager* GFCM = UGameInstance::GetSubsystem<UGameFrameworkComponentManager>(ReceiverWorld->GetGameInstance()))
				{
					GFCM->AddReceiverInternal(Receiver);
				}
			}
		}
		else
		{
			if (UGameFrameworkComponentManager* GFCM = UGameInstance::GetSubsystem<UGameFrameworkComponentManager>(Receiver->GetGameInstance()))
			{
				GFCM->AddReceiverInternal(Receiver);
			}
		}
	}
}

void UGameFrameworkComponentManager::RemoveGameFrameworkComponentReceiver(AActor* Receiver)
{
	if (Receiver != nullptr)
	{
		if (UGameFrameworkComponentManager* GFCM = UGameInstance::GetSubsystem<UGameFrameworkComponentManager>(Receiver->GetGameInstance()))
		{
			GFCM->RemoveReceiverInternal(Receiver);
		}
	}
}