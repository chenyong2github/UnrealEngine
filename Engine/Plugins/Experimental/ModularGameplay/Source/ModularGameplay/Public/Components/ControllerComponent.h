// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"
#include "GameFrameworkComponent.h"
#include "ControllerComponent.generated.h"

/**
 * ControllerComponent is an actor component made for AController and receives controller events.
 */
UCLASS()
class MODULARGAMEPLAY_API UControllerComponent : public UGameFrameworkComponent
{
	GENERATED_BODY()

public:
	
	UControllerComponent(const FObjectInitializer& ObjectInitializer);

	template <class T>
	T* GetController() const
	{
		static_assert(TPointerIsConvertibleFromTo<T, AController>::Value, "'T' template parameter to GetController must be derived from AController");
		return Cast<T>(GetOwner());
	}

	template <class T>
	T* GetControllerChecked() const
	{
		static_assert(TPointerIsConvertibleFromTo<T, AController>::Value, "'T' template parameter to GetControllerChecked must be derived from AController");
		return CastChecked<T>(GetOwner());
	}

	//////////////////////////////////////////////////////////////////////////////
	// Controller accessors
	// Usable for any type of AController owner
	//////////////////////////////////////////////////////////////////////////////

	template <class T>
	T* GetPawn() const
	{
		static_assert(TPointerIsConvertibleFromTo<T, APawn>::Value, "'T' template parameter to GetPawn must be derived from APawn");
		return Cast<T>(GetControllerChecked<AController>()->GetPawn());
	}

	template <class T>
	T* GetViewTarget() const
	{
		return Cast<T>(GetControllerChecked<AController>()->GetViewTarget());
	}

	template <class T>
	T* GetPawnOrViewTarget() const
	{
		if (T* Pawn = GetPawn<T>())
		{
			return Pawn;
		}
		else
		{
			return GetViewTarget<T>();
		}
	}

	template <class T>
	T* GetPlayerState() const
	{
		static_assert(TPointerIsConvertibleFromTo<T, APlayerState>::Value, "'T' template parameter to GetPlayerState must be derived from APlayerState");
		return GetControllerChecked<AController>()->GetPlayerState<T>();
	}

	template <class T>
	T* GetGameInstance() const
	{
		static_assert(TPointerIsConvertibleFromTo<T, UGameInstance>::Value, "'T' template parameter to GetGameInstance must be derived from UGameInstance");
		return GetControllerChecked<AController>()->GetGameInstance<T>();
	}

	bool IsLocalController() const;
	void GetPlayerViewPoint(FVector& Location, FRotator& Rotation) const;

	//////////////////////////////////////////////////////////////////////////////
	// PlayerController accessors
	// Only returns correct values for APlayerController owners
	//////////////////////////////////////////////////////////////////////////////

	template <class T>
	T* GetPlayer() const
	{
		static_assert(TPointerIsConvertibleFromTo<T, UPlayer>::Value, "'T' template parameter to GetPlayer must be derived from UPlayer");
		APlayerController* PC = Cast<APlayerController>(GetOwner());
		return PC ? Cast<T>(PC->Player) : nullptr;
	}

public:

	//////////////////////////////////////////////////////////////////////////////
	// PlayerController events
	// These only happen if the controller is a PlayerController
	//////////////////////////////////////////////////////////////////////////////

	/** Called after the PlayerController's viewport/net connection is associated with this player controller. */
	virtual void ReceivedPlayer() {}

	/** PlayerTick is only called if the PlayerController has a PlayerInput object. Therefore, it will only be called for locally controlled PlayerControllers. */
	virtual void PlayerTick(float DeltaTime) {}
};