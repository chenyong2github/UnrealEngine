// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/GameInstance.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "GameFrameworkComponent.generated.h"

/**
 * GameFrameworkComponent is a base class for a actor components made for the basic game framework classes.
 */
UCLASS(Blueprintable, BlueprintType, HideCategories=(Trigger, PhysicsVolume))
class MODULARGAMEPLAY_API UGameFrameworkComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	
	UGameFrameworkComponent(const FObjectInitializer& ObjectInitializer);

	template <class T>
	T* GetGameInstance() const
	{
		static_assert(TPointerIsConvertibleFromTo<T, UGameInstance>::Value, "'T' template parameter to GetGameInstance must be derived from UGameInstance");
		AActor* Owner = GetOwner();
		check(Owner);
		return Owner->GetGameInstance<T>();
	}

	template <class T>
	T* GetGameInstanceChecked() const
	{
		static_assert(TPointerIsConvertibleFromTo<T, UGameInstance>::Value, "'T' template parameter to GetGameInstance must be derived from UGameInstance");
		AActor* Owner = GetOwner();
		check(Owner);
		T* GameInstance = Owner->GetGameInstance<T>();
		check(GameInstance);
		return GameInstance;
	}

	/** Returns true if the owner's role is ROLE_Authority */
	bool HasAuthority() const;

	/** Returns the world's timer manager */
	class FTimerManager& GetWorldTimerManager() const;
};

/**
 * Iterator for registered components on an actor
 */
template <typename T>
class TComponentIterator
{
public:
	explicit TComponentIterator(AActor* OwnerActor)
		: CompIndex(-1)
	{
		if (OwnerActor && !OwnerActor->IsPendingKill())
		{
			OwnerActor->GetComponents<T>(AllComponents);
		}

		Advance();
	}

	FORCEINLINE void operator++()
	{
		Advance();
	}

	FORCEINLINE explicit operator bool() const
	{
		return AllComponents.IsValidIndex(CompIndex);
	}

	FORCEINLINE bool operator!() const
	{
		return !(bool)*this;
	}

	FORCEINLINE T* operator*() const
	{
		return GetComponent();
	}

	FORCEINLINE T* operator->() const
	{
		return GetComponent();
	}

protected:
	/** Gets the current component */
	FORCEINLINE T* GetComponent() const
	{
		return AllComponents[CompIndex];
	}

	/** Moves the iterator to the next valid component */
	FORCEINLINE bool Advance()
	{
		while (++CompIndex < AllComponents.Num())
		{
			T* Comp = GetComponent();
			check(Comp);
			if (Comp->IsRegistered())
			{
				checkf(!Comp->IsPendingKill(), TEXT("Registered game framework component was pending kill! Comp: %s"), *GetPathNameSafe(Comp));
				return true;
			}
		}

		return false;
	}

private:
	/** Results from GetComponents */
	TInlineComponentArray<T*> AllComponents;

	/** Index of the current element in the componnet array */
	int32 CompIndex;

	FORCEINLINE bool operator==(const TComponentIterator& Other) const { return CompIndex == Other.CompIndex; }
	FORCEINLINE bool operator!=(const TComponentIterator& Other) const { return CompIndex != Other.CompIndex; }
};