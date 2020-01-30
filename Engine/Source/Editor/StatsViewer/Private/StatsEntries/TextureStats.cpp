// Copyright Epic Games, Inc. All Rights Reserved.

#include "TextureStats.h"

UTextureStats::UTextureStats(const FObjectInitializer& ObjectInitializer) :
	Super(ObjectInitializer),
	LastTimeRendered( FLT_MAX )
{
}
