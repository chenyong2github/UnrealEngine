// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ActorDataLayer.generated.h"

USTRUCT(BlueprintType)
struct ENGINE_API FActorDataLayer
{
	GENERATED_USTRUCT_BODY()

	FActorDataLayer()
	: Name(NAME_None)
	{}

	FActorDataLayer(const FName& InName)
	: Name(InName)
	{}

	FORCEINLINE bool operator==(const FActorDataLayer& Other) const { return Name == Other.Name; }
	FORCEINLINE bool operator<(const FActorDataLayer& Other) const { return Name.FastLess(Other.Name); }

	/** The name of this layer */
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = DataLayer)
	FName Name;
};