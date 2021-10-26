// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

namespace TestUtil
{
	template<typename T>
	static T* Spawn(UWorld* World, FName Name)
	{
		return Cast<T>(TestUtil::Spawn(World, Name, T::StaticClass()));
	}

	static AActor* Spawn(UWorld* World, FName Name, UClass* Class);
};
