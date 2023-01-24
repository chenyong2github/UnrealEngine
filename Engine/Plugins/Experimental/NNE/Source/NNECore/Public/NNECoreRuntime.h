// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"

#include "NNECoreRuntime.generated.h"

UINTERFACE()
class NNECORE_API UNNERuntime : public UInterface
{
	GENERATED_BODY()
};

class NNECORE_API INNERuntime
{
	GENERATED_BODY()

public:
	virtual FString GetRuntimeName() const = 0;
	virtual bool IsPlatformSupported(const ITargetPlatform* TargetPlatform) const = 0;

	virtual bool CanCreateModelData(FString FileType, TConstArrayView<uint8> FileData) const = 0;
	virtual TArray<uint8> CreateModelData(FString FileType, TConstArrayView<uint8> FileData) = 0;
};