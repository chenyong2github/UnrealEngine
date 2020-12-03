// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "UObject/WeakObjectPtr.h"

class ALevelSequenceActor;
class FMatineeConverter;

class FMatineeToLevelSequenceConverter
{
public:
	FMatineeToLevelSequenceConverter(const FMatineeConverter* InMatineeConverter);

	/** Callback for converting a matinee to a level sequence asset. */
	void ConvertMatineeToLevelSequence(TArray<TWeakObjectPtr<AActor>> ActorsToConvert);

private:
	/** Convert a single matinee to a level sequence asset */
	TWeakObjectPtr<ALevelSequenceActor> ConvertSingleMatineeToLevelSequence(TWeakObjectPtr<AActor> ActorToConvert, int32& NumWarnings);

private:
	const FMatineeConverter* MatineeConverter;
};
