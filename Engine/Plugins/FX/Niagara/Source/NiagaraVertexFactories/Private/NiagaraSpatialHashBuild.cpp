// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "NiagaraSpatialHashBuild.h"

IMPLEMENT_GLOBAL_SHADER(FNiagaraPrefixSumCS, "/Plugin/FX/Niagara/Private/NiagaraPrefixSum.usf", "ScanInBlock", SF_Compute);

void FNiagaraPrefixSumCS::ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
{
	FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	OutEnvironment.SetDefine(TEXT("THREAD_COUNT"), NIAGARA_SPATIAL_HASH_THREAD_COUNT);
}

FNiagaraPrefixSumCS::FNiagaraPrefixSumCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
	: FGlobalShader(Initializer)
{
	InScanBuffer.Bind(Initializer.ParameterMap, TEXT("InScan"));
	OutScanBuffer.Bind(Initializer.ParameterMap, TEXT("OutScan"));
	BlockScans.Bind(Initializer.ParameterMap, TEXT("BlockScans"));
}

bool FNiagaraPrefixSumCS::Serialize(FArchive& Ar)
{
	bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
	Ar << InScanBuffer;
	Ar << OutScanBuffer;
	Ar << BlockScans;
	return bShaderHasOutdatedParameters;
}

void FNiagaraPrefixSumCS::SetOutput(FRHICommandList& RHICmdList, FRHIUnorderedAccessView* OutScanUAV, FRHIUnorderedAccessView* OutBlockScansUAV)
{
	FRHIComputeShader* ComputeShaderRHI = GetComputeShader();
	if (OutScanBuffer.IsBound())
	{
		RHICmdList.SetUAVParameter(ComputeShaderRHI, OutScanBuffer.GetBaseIndex(), OutScanUAV);
	}
	if (BlockScans.IsBound())
	{
		RHICmdList.SetUAVParameter(ComputeShaderRHI, BlockScans.GetBaseIndex(), OutBlockScansUAV);
	}
}

void FNiagaraPrefixSumCS::SetParameters(FRHICommandList& RHICmdList, FRHIShaderResourceView* InScanUAV)
{
	FRHIComputeShader* ComputeShaderRHI = GetComputeShader();
	if (InScanBuffer.IsBound())
	{
		RHICmdList.SetShaderResourceViewParameter(ComputeShaderRHI, InScanBuffer.GetBaseIndex(), InScanUAV);
	}
}

void FNiagaraPrefixSumCS::UnbindBuffers(FRHICommandList& RHICmdList)
{
	FRHIComputeShader* ComputeShaderRHI = GetComputeShader();
	if (InScanBuffer.IsBound())
	{
		RHICmdList.SetShaderResourceViewParameter(ComputeShaderRHI, InScanBuffer.GetBaseIndex(), FShaderResourceViewRHIParamRef());
	}
	if (OutScanBuffer.IsBound())
	{
		RHICmdList.SetUAVParameter(ComputeShaderRHI, OutScanBuffer.GetBaseIndex(), FUnorderedAccessViewRHIParamRef());
	}
	if (BlockScans.IsBound())
	{
		RHICmdList.SetUAVParameter(ComputeShaderRHI, BlockScans.GetBaseIndex(), FUnorderedAccessViewRHIParamRef());
	}
}

IMPLEMENT_GLOBAL_SHADER(FNiagaraScanAddBlockResultsCS, "/Plugin/Fx/Niagara/Private/NiagaraPrefixSum.usf", "AddBlockScansToFinalScan", SF_Compute);

void FNiagaraScanAddBlockResultsCS::ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
{
	FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	OutEnvironment.SetDefine(TEXT("THREAD_COUNT"), NIAGARA_SPATIAL_HASH_THREAD_COUNT);
}

FNiagaraScanAddBlockResultsCS::FNiagaraScanAddBlockResultsCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
	: FGlobalShader(Initializer)
{
	IntermediateScan.Bind(Initializer.ParameterMap, TEXT("InScan"));
	BlockScans.Bind(Initializer.ParameterMap, TEXT("InBlockScans"));
	FinalScan.Bind(Initializer.ParameterMap, TEXT("OutScan"));
}

bool FNiagaraScanAddBlockResultsCS::Serialize(FArchive& Ar)
{
	bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
	Ar << IntermediateScan;
	Ar << BlockScans;
	Ar << FinalScan;
	return bShaderHasOutdatedParameters;
}

void FNiagaraScanAddBlockResultsCS::SetOutput(FRHICommandList& RHICmdList, FRHIUnorderedAccessView* OutFinalScan)
{
	FRHIComputeShader* ComputeShaderRHI = GetComputeShader();
	if (FinalScan.IsBound())
	{
		RHICmdList.SetUAVParameter(ComputeShaderRHI, FinalScan.GetBaseIndex(), OutFinalScan);
	}
}

void FNiagaraScanAddBlockResultsCS::SetParameters(FRHICommandList& RHICmdList, FRHIShaderResourceView* InIntermediateScanSRV, FRHIShaderResourceView* InBlockScanSRV)
{
	FRHIComputeShader* ComputeShaderRHI = GetComputeShader();
	if (IntermediateScan.IsBound())
	{
		RHICmdList.SetShaderResourceViewParameter(ComputeShaderRHI, IntermediateScan.GetBaseIndex(), InIntermediateScanSRV);
	}
	if (BlockScans.IsBound())
	{
		RHICmdList.SetShaderResourceViewParameter(ComputeShaderRHI, BlockScans.GetBaseIndex(), InBlockScanSRV);
	}
}

void FNiagaraScanAddBlockResultsCS::UnbindBuffers(FRHICommandList& RHICmdList)
{
	FRHIComputeShader* ComputeShaderRHI = GetComputeShader();
	if (IntermediateScan.IsBound())
	{
		RHICmdList.SetShaderResourceViewParameter(ComputeShaderRHI, IntermediateScan.GetBaseIndex(), FShaderResourceViewRHIParamRef());
	}
	if (BlockScans.IsBound())
	{
		RHICmdList.SetShaderResourceViewParameter(ComputeShaderRHI, BlockScans.GetBaseIndex(), FShaderResourceViewRHIParamRef());
	}
	if (FinalScan.IsBound())
	{
		RHICmdList.SetUAVParameter(ComputeShaderRHI, FinalScan.GetBaseIndex(), FUnorderedAccessViewRHIParamRef());
	}
}

IMPLEMENT_GLOBAL_SHADER(FNiagaraCountingSortCS, "/Plugin/FX/Niagara/Private/NiagaraDISpatialHashCountingSort.usf", "SpatialHashCountingSortCS", SF_Compute);

void FNiagaraCountingSortCS::ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
{
	FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	OutEnvironment.SetDefine(TEXT("THREAD_COUNT"), NIAGARA_SPATIAL_HASH_THREAD_COUNT);
}

FNiagaraCountingSortCS::FNiagaraCountingSortCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
	: FGlobalShader(Initializer)
{
	DataToSortID.Bind(Initializer.ParameterMap, TEXT("InDataToSortID"));
	DataToSortPos.Bind(Initializer.ParameterMap, TEXT("InDataToSortPos"));
	SortedDataID.Bind(Initializer.ParameterMap, TEXT("OutSortedDataID"));
	SortedDataPos.Bind(Initializer.ParameterMap, TEXT("OutSortedDataPos"));
	ScannedCounts.Bind(Initializer.ParameterMap, TEXT("ScannedCounts"));
	NumParticles.Bind(Initializer.ParameterMap, TEXT("N"));
}

bool FNiagaraCountingSortCS::Serialize(FArchive& Ar)
{
	bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
	Ar << DataToSortID;
	Ar << DataToSortPos;
	Ar << SortedDataID;
	Ar << SortedDataPos;
	Ar << ScannedCounts;
	Ar << NumParticles;
	return bShaderHasOutdatedParameters;
}

void FNiagaraCountingSortCS::SetOutput(FRHICommandList& RHICmdList, FRHIUnorderedAccessView* OutSortedDataID, FRHIUnorderedAccessView* OutSortedDataPos, FRHIUnorderedAccessView* OutScannedCounts)
{
	FRHIComputeShader* ComputeShaderRHI = GetComputeShader();
	if (SortedDataID.IsBound())
	{
		RHICmdList.SetUAVParameter(ComputeShaderRHI, SortedDataID.GetBaseIndex(), OutSortedDataID);
	}
	if (SortedDataPos.IsBound())
	{
		RHICmdList.SetUAVParameter(ComputeShaderRHI, SortedDataPos.GetBaseIndex(), OutSortedDataPos);
	}
	if (ScannedCounts.IsBound())
	{
		RHICmdList.SetUAVParameter(ComputeShaderRHI, ScannedCounts.GetBaseIndex(), OutScannedCounts);
	}
}

void FNiagaraCountingSortCS::SetParameters(FRHICommandList& RHICmdList, FRHIShaderResourceView* InDataToSortID, FRHIShaderResourceView* InDataToSortPos, int InNumParticles)
{
	FRHIComputeShader* ComputeShaderRHI = GetComputeShader();
	if (DataToSortID.IsBound())
	{
		RHICmdList.SetShaderResourceViewParameter(ComputeShaderRHI, DataToSortID.GetBaseIndex(), InDataToSortID);
	}
	if (DataToSortPos.IsBound())
	{
		RHICmdList.SetShaderResourceViewParameter(ComputeShaderRHI, DataToSortPos.GetBaseIndex(), InDataToSortPos);
	}
	if (NumParticles.IsBound())
	{
		RHICmdList.SetShaderParameter(ComputeShaderRHI, NumParticles.GetBufferIndex(), NumParticles.GetBaseIndex(), sizeof(int32), &InNumParticles);
	}
}

void FNiagaraCountingSortCS::UnbindBuffers(FRHICommandList& RHICmdList)
{
	FRHIComputeShader* ComputeShaderRHI = GetComputeShader();
	if (SortedDataID.IsBound())
	{
		RHICmdList.SetUAVParameter(ComputeShaderRHI, SortedDataID.GetBaseIndex(), FUnorderedAccessViewRHIParamRef());
	}
	if (SortedDataPos.IsBound())
	{
		RHICmdList.SetUAVParameter(ComputeShaderRHI, SortedDataPos.GetBaseIndex(), FUnorderedAccessViewRHIParamRef());
	}
	if (ScannedCounts.IsBound())
	{
		RHICmdList.SetUAVParameter(ComputeShaderRHI, ScannedCounts.GetBaseIndex(), FUnorderedAccessViewRHIParamRef());
	}
	if (DataToSortID.IsBound())
	{
		RHICmdList.SetShaderResourceViewParameter(ComputeShaderRHI, DataToSortID.GetBaseIndex(), FShaderResourceViewRHIParamRef());
	}
	if (DataToSortPos.IsBound())
	{
		RHICmdList.SetShaderResourceViewParameter(ComputeShaderRHI, DataToSortPos.GetBaseIndex(), FShaderResourceViewRHIParamRef());
	}
}

IMPLEMENT_GLOBAL_SHADER(FNiagaraSpatialHashIndexCellsCS, "/Plugin/FX/Niagara/Private/NiagaraDISpatialHashIndexCells.usf", "IndexCellsCS", SF_Compute);

void FNiagaraSpatialHashIndexCellsCS::ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
{
	FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	OutEnvironment.SetDefine(TEXT("THREAD_COUNT"), NIAGARA_SPATIAL_HASH_THREAD_COUNT);
}

FNiagaraSpatialHashIndexCellsCS::FNiagaraSpatialHashIndexCellsCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
	: FGlobalShader(Initializer)
{
	CellStartIndices.Bind(Initializer.ParameterMap, TEXT("CellStartIndices"));
	CellEndIndices.Bind(Initializer.ParameterMap, TEXT("CellEndIndices"));
	SortedIDs.Bind(Initializer.ParameterMap, TEXT("InParticleIDs"));
	NumParticles.Bind(Initializer.ParameterMap, TEXT("N"));
}

bool FNiagaraSpatialHashIndexCellsCS::Serialize(FArchive& Ar)
{
	bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
	Ar << CellStartIndices;
	Ar << CellEndIndices;
	Ar << SortedIDs;
	Ar << NumParticles;
	return bShaderHasOutdatedParameters;
}

void FNiagaraSpatialHashIndexCellsCS::SetOutput(FRHICommandList& RHICmdList, FRHIUnorderedAccessView* OutCellStartIndices, FRHIUnorderedAccessView* OutCellEndIndices)
{
	FRHIComputeShader* ComputeShaderRHI = GetComputeShader();
	if (CellStartIndices.IsBound())
	{
		RHICmdList.SetUAVParameter(ComputeShaderRHI, CellStartIndices.GetBaseIndex(), OutCellStartIndices);
	}
	if (CellEndIndices.IsBound())
	{
		RHICmdList.SetUAVParameter(ComputeShaderRHI, CellEndIndices.GetBaseIndex(), OutCellEndIndices);
	}
}

void FNiagaraSpatialHashIndexCellsCS::SetParameters(FRHICommandList& RHICmdList, FRHIShaderResourceView* InSortedIDs, int InNumParticles)
{
	FRHIComputeShader* ComputeShaderRHI = GetComputeShader();
	if (SortedIDs.IsBound())
	{
		RHICmdList.SetShaderResourceViewParameter(ComputeShaderRHI, SortedIDs.GetBaseIndex(), InSortedIDs);
	}
	if (NumParticles.IsBound())
	{
		RHICmdList.SetShaderParameter(ComputeShaderRHI, NumParticles.GetBufferIndex(), NumParticles.GetBaseIndex(), sizeof(int32), &InNumParticles);
	}
}

void FNiagaraSpatialHashIndexCellsCS::UnbindBuffers(FRHICommandList& RHICmdList)
{
	FRHIComputeShader* ComputeShaderRHI = GetComputeShader();
	if (CellStartIndices.IsBound())
	{
		RHICmdList.SetUAVParameter(ComputeShaderRHI, CellStartIndices.GetBaseIndex(), FUnorderedAccessViewRHIParamRef());
	}
	if (CellEndIndices.IsBound())
	{
		RHICmdList.SetUAVParameter(ComputeShaderRHI, CellEndIndices.GetBaseIndex(), FUnorderedAccessViewRHIParamRef());
	}
	if (SortedIDs.IsBound())
	{
		RHICmdList.SetShaderResourceViewParameter(ComputeShaderRHI, SortedIDs.GetBaseIndex(), FShaderResourceViewRHIParamRef());
	}
}
