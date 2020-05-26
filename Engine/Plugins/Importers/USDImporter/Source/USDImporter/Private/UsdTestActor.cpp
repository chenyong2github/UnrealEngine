// Copyright Epic Games, Inc. All Rights Reserved.

#include "UsdTestActor.h"



ADEPRECATED_AUsdTestActor::ADEPRECATED_AUsdTestActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	RootComponent = TestComponent_DEPRECATED = CreateDefaultSubobject<UDEPRECATED_UUsdTestComponent>(TEXT("Root"));
}

