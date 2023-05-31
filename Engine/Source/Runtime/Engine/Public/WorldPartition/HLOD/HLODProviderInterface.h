// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "HLODProviderInterface.generated.h"


class AWorldPartitionHLOD;


UINTERFACE()
class ENGINE_API UWorldPartitionHLODProvider : public UInterface
{
	GENERATED_UINTERFACE_BODY()
};


class ENGINE_API IWorldPartitionHLODProvider
{
	GENERATED_IINTERFACE_BODY()

public:
	virtual AWorldPartitionHLOD* CreateHLODActor() = 0;
};
