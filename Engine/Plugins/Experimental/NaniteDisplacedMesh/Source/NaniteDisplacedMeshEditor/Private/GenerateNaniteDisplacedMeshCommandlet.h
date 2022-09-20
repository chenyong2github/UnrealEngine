// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Commandlets/Commandlet.h"

#include "GenerateNaniteDisplacedMeshCommandlet.generated.h"

class UNaniteDisplacedMesh;

struct FAssetData;
struct FNaniteDisplacedMeshParams;

/*
 * Commandlet to help keeping up to date generated nanite displacement mesh assets
 * Iterate all the levels and keep track of the linked mesh used.
 */
UCLASS()
class UGenerateNaniteDisplacedMeshCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	//~ Begin UCommandlet Interface
	virtual int32 Main(const FString& Params) override;
	//~ End UCommandlet Interface

private:
	UNaniteDisplacedMesh* OnLinkDisplacedMesh(const FNaniteDisplacedMeshParams& InParameters, const FString& DisplacedMeshFolder);

	void ProcessAssetData(const FAssetData& InAssetData);

	FDelegateHandle OnLinkDisplacedMeshHandle;
};
