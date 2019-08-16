// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "LiveLinkFrameTranslator.h"
#include "UObject/UObjectIterator.h"

bool ILiveLinkFrameTranslatorWorker::CanTranslate(TSubclassOf<ULiveLinkRole> InToRole) const
{
	TSubclassOf<ULiveLinkRole> ToRole = GetToRole();
	return InToRole.Get() && ToRole.Get() && ToRole->IsChildOf(InToRole.Get());
}


bool ULiveLinkFrameTranslator::CanTranslate(TSubclassOf<ULiveLinkRole> InToRole) const
{
	TSubclassOf<ULiveLinkRole> ToRole = GetToRole();
	return InToRole.Get() && ToRole.Get() && ToRole->IsChildOf(InToRole.Get());
}
