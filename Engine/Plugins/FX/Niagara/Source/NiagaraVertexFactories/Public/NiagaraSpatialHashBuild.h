// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "ShaderPermutation.h"

#define NIAGARA_SPATIAL_HASH_THREAD_COUNT 128

/**
 * Compute shader used to perform a prefix sum on a buffer.
 */
class NIAGARAVERTEXFACTORIES_API FNiagaraPrefixSumCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FNiagaraPrefixSumCS)
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return RHISupportsComputeShaders(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment);

	FNiagaraPrefixSumCS() {}

	explicit FNiagaraPrefixSumCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer);

	virtual bool Serialize(FArchive& Ar) override;

	void SetOutput(FRHICommandList& RHICmdList, FRHIUnorderedAccessView* OutScanUAV, FRHIUnorderedAccessView* OutBlockScansUAV);

	void SetParameters(FRHICommandList& RHICmdList, FRHIShaderResourceView* InScanSRV);

	void UnbindBuffers(FRHICommandList& RHICmdList);

private:
	FShaderResourceParameter InScanBuffer;
	FShaderResourceParameter OutScanBuffer;
	FShaderResourceParameter BlockScans;
};

/**
 * Compute shader used to perform the final prefix sum pass (adding the block scan results to each element).
 */
class NIAGARAVERTEXFACTORIES_API FNiagaraScanAddBlockResultsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FNiagaraScanAddBlockResultsCS)
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return RHISupportsComputeShaders(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment);

	FNiagaraScanAddBlockResultsCS() {}

	explicit FNiagaraScanAddBlockResultsCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer);

	virtual bool Serialize(FArchive& Ar) override;

	void SetOutput(FRHICommandList& RHICmdList, FRHIUnorderedAccessView* OutFinalScan);

	void SetParameters(FRHICommandList& RHICmdList, FRHIShaderResourceView* InIntermediateScanSRV, FRHIShaderResourceView* InBlockScanSRV);

	void UnbindBuffers(FRHICommandList& RHICmdList);

private:
	FShaderResourceParameter IntermediateScan;
	FShaderResourceParameter BlockScans;
	FShaderResourceParameter FinalScan;
};

/**
 * Compute shader used to sort spatial has particle IDs and positions based on a scanned cell count buffer.
 */
class NIAGARAVERTEXFACTORIES_API FNiagaraCountingSortCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FNiagaraCountingSortCS)
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return RHISupportsComputeShaders(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment);

	FNiagaraCountingSortCS() {}

	explicit FNiagaraCountingSortCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer);

	virtual bool Serialize(FArchive& Ar) override;

	void SetOutput(FRHICommandList& RHICmdList, FRHIUnorderedAccessView* OutSortedDataID, FRHIUnorderedAccessView* OutSortedDataPos, FRHIUnorderedAccessView* ScannedCounts);

	void SetParameters(FRHICommandList& RHICmdList, FRHIShaderResourceView* InDataToSortID, FRHIShaderResourceView* InDataToSortPos, int InNumParticles);

	void UnbindBuffers(FRHICommandList& RHICmdList);

private:
	FShaderResourceParameter DataToSortID;
	FShaderResourceParameter DataToSortPos;
	FShaderResourceParameter SortedDataID;
	FShaderResourceParameter SortedDataPos;
	FShaderResourceParameter ScannedCounts;

	FShaderParameter NumParticles;
};

/**
 * Compute shader used to calculate the start and end indices of each cell.
 */
class NIAGARAVERTEXFACTORIES_API FNiagaraSpatialHashIndexCellsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FNiagaraSpatialHashIndexCellsCS)
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return RHISupportsComputeShaders(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment);

	FNiagaraSpatialHashIndexCellsCS() {}

	explicit FNiagaraSpatialHashIndexCellsCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer);

	virtual bool Serialize(FArchive& Ar) override;

	void SetOutput(FRHICommandList& RHICmdList, FRHIUnorderedAccessView* OutCellStartIndices, FRHIUnorderedAccessView* OutCellEndIndices);

	void SetParameters(FRHICommandList& RHICmdList, FRHIShaderResourceView* InSortedIDs, int InNumParticles);

	void UnbindBuffers(FRHICommandList& RHICmdList);

private:
	FShaderResourceParameter CellStartIndices;
	FShaderResourceParameter CellEndIndices;
	FShaderResourceParameter SortedIDs;

	FShaderParameter NumParticles;
};