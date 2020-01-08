// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class DMXPROTOCOL_API IDMXProtocolBase
{
public:
	virtual bool Init() = 0;

	virtual bool Shutdown() = 0;

	virtual bool Tick(float DeltaTime) = 0;
};

