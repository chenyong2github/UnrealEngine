// Copyright Epic Games, Inc. All Rights Reserved.

#include "RemoteControlBinding.h"

#include "Engine/EngineTypes.h"
#include "GameFramework/Actor.h"
#include "UObject/SoftObjectPtr.h"

#if WITH_EDITOR
#include "Components/ActorComponent.h"
#include "Editor.h"
#include "Editor/UnrealEd/Public/Editor.h"
#endif

namespace
{
	/**
	 * What world are we looking in to find the counterpart actor/component
	 */
	enum class ECounterpartWorldTarget
	{
		Editor,
		PIE
	};

	/**
	 * Find the counterpart actor/component in PIE/Editor
	 */
	UObject* FindObjectInCounterpartWorld(UObject* Object, ECounterpartWorldTarget WorldTarget)
	{
		UObject* CounterpartObject = nullptr;
#if WITH_EDITOR
		if (Object && GEditor)
		{
			const bool bForPie = WorldTarget == ECounterpartWorldTarget::PIE ? true : false;
			
			if (AActor* Actor = Cast<AActor>(Object))
			{
				CounterpartObject = bForPie ? EditorUtilities::GetSimWorldCounterpartActor(Actor) : EditorUtilities::GetEditorWorldCounterpartActor(Actor);
			}
			else if(AActor* Owner = Object->GetTypedOuter<AActor>())
			{
				if (AActor* CounterpartWorldOwner = bForPie ? EditorUtilities::GetSimWorldCounterpartActor(Owner) : EditorUtilities::GetEditorWorldCounterpartActor(Owner))
				{
					CounterpartObject = FindObject<UObject>(CounterpartWorldOwner, *Object->GetName());
				}
			}
		}
#endif
		return CounterpartObject ? CounterpartObject : Object;
	}
}

void URemoteControlLevelIndependantBinding::SetBoundObject(const TSoftObjectPtr<UObject>& InObject)
{
	if (ensure(InObject))
	{
		BoundObject = FindObjectInCounterpartWorld(InObject.Get(), ECounterpartWorldTarget::Editor);
	}
}

void URemoteControlLevelIndependantBinding::UnbindObject(const TSoftObjectPtr<UObject>& InBoundObject)
{
	if (ensure(InBoundObject) && BoundObject == FindObjectInCounterpartWorld(InBoundObject.Get(), ECounterpartWorldTarget::Editor))
	{
		BoundObject.Reset();
	}
}

UObject* URemoteControlLevelIndependantBinding::Resolve() const
{
	return BoundObject.Get();
}

bool URemoteControlLevelIndependantBinding::IsValid() const
{
	return BoundObject.IsValid();
}

bool URemoteControlLevelIndependantBinding::IsBound(const TSoftObjectPtr<UObject>& Object) const
{
	return BoundObject == Object;
}

void URemoteControlLevelDependantBinding::SetBoundObject(const TSoftObjectPtr<UObject>& InObject)
{
	if (ensure(InObject))
	{
		UObject* EditorObject = FindObjectInCounterpartWorld(InObject.Get(), ECounterpartWorldTarget::Editor);
		BoundObjectMap.FindOrAdd(EditorObject->GetTypedOuter<ULevel>(), EditorObject);
		Name = EditorObject->GetName();
	}
}

void URemoteControlLevelDependantBinding::UnbindObject(const TSoftObjectPtr<UObject>& InBoundObject)
{
	for (auto It = BoundObjectMap.CreateIterator(); It; ++It)
	{
		if (It.Value() == InBoundObject)
		{
			It.RemoveCurrent();
		}
	}
}

UObject* URemoteControlLevelDependantBinding::Resolve() const
{
	// Find the object in PIE if possible
	UObject* Object = FindObjectFromCurrentWorld().Get();
	return FindObjectInCounterpartWorld(Object, ECounterpartWorldTarget::PIE);
}

bool URemoteControlLevelDependantBinding::IsValid() const
{
	return BoundObjectMap.Num() > 0;
}

bool URemoteControlLevelDependantBinding::IsBound(const TSoftObjectPtr<UObject>& Object) const
{
	for (const TPair<TSoftObjectPtr<ULevel>, TSoftObjectPtr<UObject>>& Pair : BoundObjectMap)
	{
		if (Pair.Value == Object)
		{
			return true;
		}
	}

	return false;
}

TSoftObjectPtr<UObject> URemoteControlLevelDependantBinding::FindObjectFromCurrentWorld() const
{
	constexpr bool bForResolving = true;
	if (UWorld* World = GetCurrentWorld())
	{
		for (auto LevelIt = World->GetLevelIterator(); LevelIt; ++LevelIt)
		{
			TSoftObjectPtr<ULevel> WeakLevel = *LevelIt;
			if (const TSoftObjectPtr<UObject>* ObjectPtr = BoundObjectMap.Find(WeakLevel))
			{
				return *ObjectPtr;
			}
		}
	}
	return nullptr;
}

UWorld* URemoteControlLevelDependantBinding::GetCurrentWorld() const
{
	// Since this is used to retrieve the binding in the map, we never use the PIE world in editor.
	UWorld* World = nullptr;

#if WITH_EDITOR
	if (GEditor)
	{
		World = GEditor->GetEditorWorldContext(false).World();
	}
#endif

	return World ? World : GEngine->GetCurrentPlayWorld(); 
}
