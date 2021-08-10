// Copyright Epic Games, Inc. All Rights Reserved.

#include "InterchangeActorFactoryNode.h"

#if WITH_ENGINE
	#include "GameFramework/Actor.h"
#endif

UClass* UInterchangeActorFactoryNode::GetObjectClass() const
{
#if WITH_ENGINE
	return AActor::StaticClass();
#else
	return nullptr;
#endif
}