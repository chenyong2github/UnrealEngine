// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelSequenceAnimSequenceLink.h"
#include "Animation/AnimSequence.h"


void FLevelSequenceAnimSequenceLinkItem::SetAnimSequence(UAnimSequence* InAnimSequence)
{
	PathToAnimSequence = FSoftObjectPath(InAnimSequence);
}

UAnimSequence* FLevelSequenceAnimSequenceLinkItem::ResolveAnimSequence()
{
	UObject *Object = PathToAnimSequence.TryLoad();
	return Cast<UAnimSequence>(Object);
}


ULevelSequenceAnimSequenceLink::ULevelSequenceAnimSequenceLink(class FObjectInitializer const& ObjectInitializer)
	: Super(ObjectInitializer)
{

}