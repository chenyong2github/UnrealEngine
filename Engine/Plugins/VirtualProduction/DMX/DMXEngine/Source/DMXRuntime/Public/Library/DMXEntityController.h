// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Library/DMXEntity.h"

#include "DMXEntityController.generated.h"

struct FDMXBuffer;

#if WITH_EDITOR
struct FPropertyChangedEvent;
struct FPropertyChangedChainEvent;
#endif // WITH_EDITOR

UCLASS(meta = (DisplayName = "DMX Controller"))
class DMXRUNTIME_API UDMXEntityController
	: public UDMXEntity
{
	GENERATED_BODY()

};
