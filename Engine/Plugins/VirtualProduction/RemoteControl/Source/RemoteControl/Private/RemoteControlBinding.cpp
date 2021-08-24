// Copyright Epic Games, Inc. All Rights Reserved.

#include "RemoteControlBinding.h"

#include "Engine/Engine.h"
#include "Engine/EngineTypes.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/SoftObjectPtr.h"

#if WITH_EDITOR
#include "Components/ActorComponent.h"
#include "Editor.h"
#include "Editor/UnrealEd/Public/Editor.h"
#include "Misc/App.h"
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
	BoundObject = InObject;
}

void URemoteControlLevelIndependantBinding::UnbindObject(const TSoftObjectPtr<UObject>& InBoundObject)
{
	if (BoundObject == InBoundObject)
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
		BoundObjectMap.FindOrAdd(EditorObject->GetTypedOuter<ULevel>()) = EditorObject;
		
		Name = EditorObject->GetName();
	}
}

void URemoteControlLevelDependantBinding::InitializeForNewLevel()
{
	static const int32 PersistentLevelStrLength = FCString::Strlen(TEXT("PersistentLevel"));

	if (!LevelWithLastSuccessfulResolve.IsNull())
	{
		if (UWorld* CurrentWorld = GetCurrentWorld())
		{
			ULevel* CurrentLevel = CurrentWorld->PersistentLevel;
			if (BoundObjectMap.Contains(CurrentLevel))
			{
				// If there is already a binding for this level, don't overwrite it.
				return;
			}

			if (TSoftObjectPtr<UObject>* BoundObjectPtr = BoundObjectMap.Find(LevelWithLastSuccessfulResolve))
			{
				// Try to find the bound object in the current world by reparenting its path to the current level.
				FSoftObjectPath NewPath = CurrentLevel->GetPathName() + BoundObjectPtr->ToSoftObjectPath().GetSubPathString().RightChop(PersistentLevelStrLength);
				if (NewPath.ResolveObject())
				{
					BoundObjectMap.Add(CurrentLevel, TSoftObjectPtr<UObject>{NewPath});
				}
			}
		}
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
				if (ObjectPtr->IsValid())
				{
					LevelWithLastSuccessfulResolve = WeakLevel;
				}
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
	if (GEditor && FApp::CanEverRender())
	{
		World = GEditor->GetEditorWorldContext(false).World();
	}
#endif

	if (World)
	{
		return World;
	}

	if (GEngine)
	{
		for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
		{
			if (WorldContext.WorldType == EWorldType::Game)
			{
				World = WorldContext.World();
				break;
			}
		}
	}
		
	return World;
}

void URemoteControlLevelDependantBinding::SetBoundObject(const TSoftObjectPtr<ULevel>& Level, const TSoftObjectPtr<UObject>& BoundObject)
{
	BoundObjectMap.FindOrAdd(Level) = BoundObject;
	const FSoftObjectPath& Path = BoundObject.ToSoftObjectPath();
	const FString& SubPath = Path.GetSubPathString();
	FString LeftPart;
	FString ObjectName;
	SubPath.Split(TEXT("."), &LeftPart, &ObjectName, ESearchCase::Type::IgnoreCase, ESearchDir::FromEnd);
	ensure(ObjectName.Len());
	Name = MoveTemp(ObjectName);
}
