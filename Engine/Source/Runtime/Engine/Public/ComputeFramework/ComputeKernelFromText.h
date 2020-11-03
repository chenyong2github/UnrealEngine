// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ComputeKernelSource.h"
#include "Engine/EngineTypes.h"

#include "ComputeKernelFromText.generated.h"

/* 
 * Responsible for loading HLSL text and parsing the options available.
 */
UCLASS(BlueprintType)
class ENGINE_API UComputeKernelFromText : public UComputeKernelSource
{
	GENERATED_BODY()

public:
	UComputeKernelFromText();

	/* Filepath to the source file containing the kernel entry points. */
	UPROPERTY(EditDefaultsOnly, AssetRegistrySearchable, meta = (ContentDir, RelativeToGameContentDir, FilePathFilter = "Unreal Shader File (*.usf)|*.usf"), Category = "Kernel")
	FFilePath SourceFile;

	UPROPERTY(VisibleAnywhere, AssetRegistrySearchable, Category = "Kernel")
	FString EntryPointName;

	UPROPERTY()
	FGuid UniqueId;

	UPROPERTY()
	uint64 SourceHash = 0;

#if WITH_EDITOR
	FString KernelSourceText;

	FString GetEntryPoint() const override
	{
		return EntryPointName;
	}

	FString GetSource() const override
	{
		return KernelSourceText;
	}

	uint64 GetSourceHashCode() const override
	{
		return SourceHash;
	}
#endif

	void PostLoad() override;

#if WITH_EDITOR
	FFilePath PrevSourceFile;

	virtual void PreEditChange(FEditPropertyChain& PropertyAboutToChange) override;
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent) override;
#endif

private:
#if WITH_EDITOR
	void ReparseKernelSourceText();
#endif
};
