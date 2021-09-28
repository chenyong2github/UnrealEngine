// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimSequenceLevelSequenceLink.h"
#include "LevelSequence.h"


void UAnimSequenceLevelSequenceLink::SetLevelSequence(ULevelSequence* InLevelSequence)
{
	PathToLevelSequence = FSoftObjectPath(InLevelSequence);
}

ULevelSequence* UAnimSequenceLevelSequenceLink::ResolveLevelSequence()
{
	UObject *Object = PathToLevelSequence.TryLoad();
	return Cast<ULevelSequence>(Object);
}


UAnimSequenceLevelSequenceLink::UAnimSequenceLevelSequenceLink(class FObjectInitializer const& ObjectInitializer)
	: Super(ObjectInitializer)
{

}