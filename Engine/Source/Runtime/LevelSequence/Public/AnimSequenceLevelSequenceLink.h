// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "UObject/Object.h"
#include "Engine/AssetUserData.h"
#include "AnimSequenceLevelSequenceLink.generated.h"

class ULevelSequence;

/** Link To Level Sequence That may be driving the anim sequence*/
UCLASS()
class LEVELSEQUENCE_API UAnimSequenceLevelSequenceLink : public UAssetUserData
{
	GENERATED_UCLASS_BODY()

public:
	UPROPERTY()
	FGuid SkelTrackGuid;

	UPROPERTY()
	FSoftObjectPath PathToLevelSequence;

	void SetLevelSequence(ULevelSequence* InLevelSequence);
	ULevelSequence* ResolveLevelSequence();
};