// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "UObject/ObjectKey.h"
#include "UObject/ClassTree.h"
#include "Templates/SubclassOf.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "GameFrameworkComponentManager.generated.h"

class AActor;
class UActorComponent;
class UGameFrameworkComponent;

/** 
 * A handle for a request to put components or call a delegate for an extensible actor class.
 * When this handle is destroyed, it will remove the associated request from the system.
 */
struct FComponentRequestHandle
{
	FComponentRequestHandle(const TWeakObjectPtr<UGameFrameworkComponentManager>& InOwningManager, const TSoftClassPtr<AActor>& InReceiverClass, const TSubclassOf<UActorComponent>& InComponentClass)
		: OwningManager(InOwningManager)
		, ReceiverClass(InReceiverClass)
		, ComponentClass(InComponentClass)
	{}

	FComponentRequestHandle(const TWeakObjectPtr<UGameFrameworkComponentManager>& InOwningManager, const TSoftClassPtr<AActor>& InReceiverClass, FDelegateHandle InExtensionHandle)
		: OwningManager(InOwningManager)
		, ReceiverClass(InReceiverClass)
		, ExtensionHandle(InExtensionHandle)
	{}

	MODULARGAMEPLAY_API ~FComponentRequestHandle();

	/** Returns true if the manager that this request is for still exists */
	MODULARGAMEPLAY_API bool IsValid() const;

private:
	/** The manager that this request was for */
	TWeakObjectPtr<UGameFrameworkComponentManager> OwningManager;

	/** The class of actor to put components */
	TSoftClassPtr<AActor> ReceiverClass;

	/** The class of component to put on actors */
	TSubclassOf<UActorComponent> ComponentClass;

	/** A handle to an extension delegate to run */
	FDelegateHandle ExtensionHandle;
};

/** 
 * GameFrameworkComponentManager
 *
 * A manager to handle putting components on actors as they come and go.
 * Put in a request to instantiate components of a given class on actors of a given class and they will automatically be made for them as the actors are spawned.
 * Submit delegate handlers to listen for actors of a given class. Those handlers will automatically run when actors of a given class or registered as receivers or game events are sent.
 * Actors must opt-in to this behavior by calling AddReceiver/RemoveReceiver for themselves when they are ready to receive the components and when they want to remove them.
 * Any actors that are in memory when a request is made will automatically get the components, and any in memory when a request is removed will lose the components immediately.
 * Requests are reference counted, so if multiple requests are made for the same actor class and component class, only one component will be added and that component wont be removed until all requests are removed.
 */
UCLASS(MinimalAPI)
class UGameFrameworkComponentManager : public UGameInstanceSubsystem
{
	GENERATED_BODY()

	/** Using a fake multicast delegate so order can be kept consistent */
	DECLARE_DELEGATE_TwoParams(FExtensionHandlerDelegateInternal, AActor*, FName);
	using FExtensionHandlerEvent = TMap<FDelegateHandle, FExtensionHandlerDelegateInternal>;
public:
	/** Delegate types for extension handlers */
	using FExtensionHandlerDelegate = FExtensionHandlerDelegateInternal;

	void InitGameFrameworkComponentManager() {}

	/** Adds an actor as a receiver for components. If it passes the actorclass filter on requests it will get the components. */
	UFUNCTION(BlueprintCallable, Category="Gameplay", meta=(DefaultToSelf="Receiver", AdvancedDisplay=1))
	MODULARGAMEPLAY_API void AddReceiver(AActor* Receiver, bool bAddOnlyInGameWorlds = true);

	/** Removes an actor as a receiver for components. */
	UFUNCTION(BlueprintCallable, Category="Gameplay", meta=(DefaultToSelf="Receiver"))
	MODULARGAMEPLAY_API void RemoveReceiver(AActor* Receiver);

	/** Adds an actor as a receiver for components (automatically finding the manager for the actor's  game instance). If it passes the actorclass filter on requests it will get the components. */
	static MODULARGAMEPLAY_API void AddGameFrameworkComponentReceiver(AActor* Receiver, bool bAddOnlyInGameWorlds = true);

	/** Removes an actor as a receiver for components (automatically finding the manager for the actor's game instance). */
	static MODULARGAMEPLAY_API void RemoveGameFrameworkComponentReceiver(AActor* Receiver);

	/** Adds a request to instantiate components on actors of the given classes. Returns a handle that will keep the request "alive" until it is destructed, at which point the request is removed. */
	MODULARGAMEPLAY_API TSharedPtr<FComponentRequestHandle> AddComponentRequest(const TSoftClassPtr<AActor>& ReceiverClass, TSubclassOf<UActorComponent> ComponentClass);


	//////////////////////////////////////////////////////////////////////////////////////////////
	// The extension system allows registering for arbitrary event callbacks on receiver actors.
	// These are the default events but games can define, send, and listen for their own.

	/** AddReceiver was called for a registered class and components were added, called early in initialization */
	static MODULARGAMEPLAY_API FName NAME_ReceiverAdded;

	/** RemoveReceiver was called for a registered class and components were removed, normally called from EndPlay */
	static MODULARGAMEPLAY_API FName NAME_ReceiverRemoved;

	/** A new extension handler was added */
	static MODULARGAMEPLAY_API FName NAME_ExtensionAdded;

	/** An extension handler was removed by a freed request handle */
	static MODULARGAMEPLAY_API FName NAME_ExtensionRemoved;

	/** 
	 * Game-specific event indicating an actor is mostly initialized and ready for extension. 
	 * All extensible games are expected to send this event at the appropriate actor-specific point, as plugins may be listening for it.
	 */
	static MODULARGAMEPLAY_API FName NAME_GameActorReady;


	/** Adds an extension handler to run on actors of the given class. Returns a handle that will keep the handler "alive" until it is destructed, at which point the delegate is removed */
	MODULARGAMEPLAY_API TSharedPtr<FComponentRequestHandle> AddExtensionHandler(const TSoftClassPtr<AActor>& ReceiverClass, FExtensionHandlerDelegate ExtensionHandler);

	/** Sends an arbitrary extension event that can be listened for by other systems */
	UFUNCTION(BlueprintCallable, Category = "Gameplay", meta = (DefaultToSelf = "Receiver", AdvancedDisplay = 1))
	MODULARGAMEPLAY_API void SendExtensionEvent(AActor* Receiver, FName EventName, bool bOnlyInGameWorlds = true);

	/** Sends an arbitrary extension event that can be listened for by other systems */
	static MODULARGAMEPLAY_API void SendGameFrameworkComponentExtensionEvent(AActor* Receiver, const FName& EventName, bool bOnlyInGameWorlds = true);

	static void AddReferencedObjects(UObject* InThis, class FReferenceCollector& Collector);

#if !UE_BUILD_SHIPPING
	static void DumpGameFrameworkComponentManagers();
#endif // !UE_BUILD_SHIPPING

private:
	void AddReceiverInternal(AActor* Receiver);
	void RemoveReceiverInternal(AActor* Receiver);
	void SendExtensionEventInternal(AActor* Receiver, const FName& EventName);

	/** Called by FComponentRequestHandle's destructor to remove a request for components to be created. */
	void RemoveComponentRequest(const TSoftClassPtr<AActor>& ReceiverClass, TSubclassOf<UActorComponent> ComponentClass);

	/** Called by FComponentRequestHandle's destructor to remove a handler from the system. */
	void RemoveExtensionHandler(const TSoftClassPtr<AActor>& ReceiverClass, FDelegateHandle DelegateHandle);

	/** Creates an instance of a component on an actor */
	void CreateComponentOnInstance(AActor* ActorInstance, TSubclassOf<UActorComponent> ComponentClass);

	/** Removes an instance of a component on an actor */
	void DestroyInstancedComponent(UActorComponent* Component);

	/** A list of FNames to represent an object path. Used for fast hashing and comparison of paths */
	struct FComponentRequestReceiverClassPath
	{
		TArray<FName> Path;

		FComponentRequestReceiverClassPath() {}

		FComponentRequestReceiverClassPath(UClass* InClass)
		{
			check(InClass);
			for (UObject* Obj = InClass; Obj; Obj = Obj->GetOuter())
			{
				Path.Insert(Obj->GetFName(), 0);
			}
		}

		FComponentRequestReceiverClassPath(const TSoftClassPtr<AActor>& InSoftClassPtr)
		{
			TArray<FString> StringPath;
			InSoftClassPtr.ToString().ParseIntoArray(StringPath, TEXT("."));
			Path.Reserve(StringPath.Num());
			for (const FString& StringPathElement : StringPath)
			{
				Path.Add(FName(*StringPathElement));
			}
		}

#if !UE_BUILD_SHIPPING
		FString ToDebugString() const
		{
			FString ReturnString;
			if (Path.Num() > 0)
			{
				ReturnString = Path[0].ToString();
				for (int32 PathIdx = 1; PathIdx < Path.Num(); ++PathIdx)
				{
					ReturnString += TEXT(".") + Path[PathIdx].ToString();
				}
			}

			return ReturnString;
		}
#endif // !UE_BUILD_SHIPPING

		bool operator==(const FComponentRequestReceiverClassPath& Other) const
		{
			return Path == Other.Path;
		}

		friend FORCEINLINE uint32 GetTypeHash(const FComponentRequestReceiverClassPath& Request)
		{
			uint32 ReturnHash = 0;
			for (const FName& PathElement : Request.Path)
			{
				ReturnHash ^= GetTypeHash(PathElement);
			}
			return ReturnHash;
		}
	};

	/** A pair of classes that describe a request. Together these form a key that is used to batch together requests that are identical and reference count them */
	struct FComponentRequest
	{
		FComponentRequestReceiverClassPath ReceiverClassPath;
		UClass* ComponentClass;

		bool operator==(const FComponentRequest& Other) const
		{
			return ReceiverClassPath == Other.ReceiverClassPath && ComponentClass == Other.ComponentClass;
		}

		friend FORCEINLINE uint32 GetTypeHash(const FComponentRequest& Request)
		{
			return GetTypeHash(Request.ReceiverClassPath) ^ GetTypeHash(Request.ComponentClass);
		}
	};

	/** All active component requests. Used to avoid adding the same component twice if requested from multiple sources */
	TMap<FComponentRequest, int32> RequestTrackingMap;

	/** A map of component classes to instances of that component class made by this component manager */
	TMap<UClass*, TSet<FObjectKey>> ComponentClassToComponentInstanceMap;

	/** A map of actor classes to component classes that should be made for that class. */
	TMap<FComponentRequestReceiverClassPath, TSet<UClass*>> ReceiverClassToComponentClassMap;

	/** A map of actor classes to delegate handlers that should be executed for actors of that class. */
	TMap<FComponentRequestReceiverClassPath, FExtensionHandlerEvent> ReceiverClassToEventMap;

#if WITH_EDITORONLY_DATA
	/** Editor-only set to validate that component requests are only being added for actors that call AddReceiver and RemoveReceiver */
	UPROPERTY(transient)
	TSet<AActor*> AllReceivers;
#endif // WITH_EDITORONLY_DATA

	friend struct FComponentRequestHandle;
};
