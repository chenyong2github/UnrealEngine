// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/GameState.h"
#include "GameFramework/GameMode.h"
#include "GameFrameworkComponent.h"
#include "GameStateComponent.generated.h"

/**
 * GameStateComponent is an actor component made for AGameState and receives GameState events.
 */
UCLASS()
class MODULARGAMEPLAY_API UGameStateComponent : public UGameFrameworkComponent
{
	GENERATED_BODY()

public:
	
	UGameStateComponent(const FObjectInitializer& ObjectInitializer);

	template <class T>
	T* GetGameState() const
	{
		static_assert(TPointerIsConvertibleFromTo<T, AGameState>::Value, "'T' template parameter to GetGameState must be derived from AGameState");
		return Cast<T>(GetOwner());
	}

	template <class T>
	T* GetGameStateChecked() const
	{
		static_assert(TPointerIsConvertibleFromTo<T, AGameState>::Value, "'T' template parameter to GetGameStateChecked must be derived from AGameState");
		return CastChecked<T>(GetOwner());
	}

	//////////////////////////////////////////////////////////////////////////////
	// GameState accessors
	//////////////////////////////////////////////////////////////////////////////

	template <class T>
	T* GetGameMode() const
	{
		static_assert(TPointerIsConvertibleFromTo<T, AGameMode>::Value, "'T' template parameter to GetGameMode must be derived from AGameMode");
		return Cast<T>(GetGameStateChecked<AGameState>()->AuthorityGameMode);
	}

public:

	//////////////////////////////////////////////////////////////////////////////
	// GameState events
	//////////////////////////////////////////////////////////////////////////////

	virtual void HandleMatchHasStarted() {}
};