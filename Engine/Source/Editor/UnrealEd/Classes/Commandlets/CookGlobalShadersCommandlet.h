// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Commandlets/Commandlet.h"
#include "CookGlobalShadersCommandlet.generated.h"

UCLASS(config=Editor)
class UCookGlobalShadersCommandlet : public UCommandlet
{
	GENERATED_BODY()

	//~ Begin UCommandlet Interface
	virtual int32 Main(const FString& Params) override;
	//~ End UCommandlet Interface
};

UCLASS(abstract, transient, MinimalAPI)
class UCookGlobalShadersDeviceHelperBase : public UObject
{
	GENERATED_BODY()
public:
	virtual bool CopyFilesToDevice(class ITargetDevice* Device, const TArray<TPair<FString, FString>>& FilesToCopy) const { check(false); return false; }
};

UCLASS()
class UCookGlobalShadersDeviceHelperStaged : public UCookGlobalShadersDeviceHelperBase
{
	GENERATED_BODY()
public:
	virtual bool CopyFilesToDevice(class ITargetDevice* Device, const TArray<TPair<FString, FString>>& FilesToCopy) const override;

	FString StagedBuildPath;
};
