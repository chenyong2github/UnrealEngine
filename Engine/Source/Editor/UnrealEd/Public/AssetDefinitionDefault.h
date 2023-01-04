// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetDefinition.h"

#include "AssetDefinitionDefault.generated.h"

UCLASS(Abstract)
class UNREALED_API UAssetDefinitionDefault : public UAssetDefinition
{
	GENERATED_BODY()

public:
	// UAssetDefinition Begin
	virtual EAssetCommandResult OpenAssets(const FAssetOpenArgs& OpenArgs) const override;
	virtual EAssetCommandResult GetSourceFiles(const FAssetSourceFileArgs& SourceFileArgs, TArray<FAssetSourceFile>& OutSourceAssets) const override;
	virtual EAssetCommandResult PerformAssetDiff(const FAssetDiffArgs& DiffArgs) const override;
	// UAssetDefinition End
};
