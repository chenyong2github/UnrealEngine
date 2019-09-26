// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved. 

#include "GroomActor.h"
#include "GroomComponent.h"


AGroomActor::AGroomActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	GroomComponent = CreateDefaultSubobject<UGroomComponent>(TEXT("GroomComponent0"));
	RootComponent = GroomComponent;
}