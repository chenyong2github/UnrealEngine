// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "NiagaraShaderParticleID.h"
#include "GlobalShader.h"
#include "ShaderParameterUtils.h"

class FNiagaraInitFreeIDBufferCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FNiagaraInitFreeIDBufferCS);

public:
	FNiagaraInitFreeIDBufferCS() : FGlobalShader() {}

	FNiagaraInitFreeIDBufferCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) : FGlobalShader(Initializer)
	{
		NewBufferParam.Bind(Initializer.ParameterMap, TEXT("NewBuffer"));
		ExistingBufferParam.Bind(Initializer.ParameterMap, TEXT("ExistingBuffer"));
		NumNewElementsParam.Bind(Initializer.ParameterMap, TEXT("NumNewElements"));
		NumExistingElementsParam.Bind(Initializer.ParameterMap, TEXT("NumExistingElements"));
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return RHISupportsComputeShaders(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREAD_COUNT"), THREAD_COUNT);
	}

	bool Serialize(FArchive& Ar)
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << NewBufferParam;
		Ar << ExistingBufferParam;
		Ar << NumNewElementsParam;
		Ar << NumExistingElementsParam;
		return bShaderHasOutdatedParameters;
	}

	void Execute(FRHICommandList& RHICmdList, uint32 NumElementsToAlloc, FRWBuffer& NewBuffer, uint32 NumExistingElements, FRHIShaderResourceView* ExistingBuffer)
	{
		// To simplify the shader code, the size of the ID table must be a multiple of the thread count.
		check(NumElementsToAlloc % THREAD_COUNT == 0);

		// Shrinking is not supported.
		check(NumElementsToAlloc >= NumExistingElements);
		uint32 NumNewElements = NumElementsToAlloc - NumExistingElements;

		FRHIComputeShader* ComputeShader = GetComputeShader();
		RHICmdList.SetComputeShader(ComputeShader);

		NewBufferParam.SetBuffer(RHICmdList, ComputeShader, NewBuffer);
		SetSRVParameter(RHICmdList, ComputeShader, ExistingBufferParam, ExistingBuffer);
		SetShaderValue(RHICmdList, ComputeShader, NumNewElementsParam, NumNewElements);
		SetShaderValue(RHICmdList, ComputeShader, NumExistingElementsParam, NumExistingElements);

		DispatchComputeShader(RHICmdList, this, FMath::DivideAndRoundUp(NumElementsToAlloc, THREAD_COUNT), 1, 1);

		RHICmdList.SetUAVParameter(ComputeShader, NewBufferParam.GetUAVIndex(), nullptr);
		RHICmdList.SetShaderResourceViewParameter(ComputeShader, ExistingBufferParam.GetBaseIndex(), nullptr);
	}

private:
	static constexpr uint32 THREAD_COUNT = 64;

	FRWShaderParameter NewBufferParam;
	FShaderResourceParameter ExistingBufferParam;
	FShaderParameter NumNewElementsParam;
	FShaderParameter NumExistingElementsParam;
};

IMPLEMENT_GLOBAL_SHADER(FNiagaraInitFreeIDBufferCS, "/Plugin/FX/Niagara/Private/NiagaraInitFreeIDBuffer.usf", "InitIDBufferCS", SF_Compute);

void NiagaraInitGPUFreeIDList(FRHICommandList& RHICmdList, ERHIFeatureLevel::Type FeatureLevel, uint32 NumElementsToAlloc, FRWBuffer& NewBuffer, uint32 NumExistingElements, FRHIShaderResourceView* ExistingBuffer)
{
	TShaderMapRef<FNiagaraInitFreeIDBufferCS> InitIDBufferCS(GetGlobalShaderMap(FeatureLevel));
	InitIDBufferCS->Execute(RHICmdList, NumElementsToAlloc, NewBuffer, NumExistingElements, ExistingBuffer);
}

class NiagaraComputeFreeIDsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(NiagaraComputeFreeIDsCS);

public:
	NiagaraComputeFreeIDsCS() : FGlobalShader() {}

	NiagaraComputeFreeIDsCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) : FGlobalShader(Initializer)
	{
		IDToIndexTableParam.Bind(Initializer.ParameterMap, TEXT("IDToIndexTable"));
		FreeIDListParam.Bind(Initializer.ParameterMap, TEXT("FreeIDList"));
		FreeIDListSizesParam.Bind(Initializer.ParameterMap, TEXT("FreeIDListSizes"));
		FreeIDListIndexParam.Bind(Initializer.ParameterMap, TEXT("FreeIDListIndex"));
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return RHISupportsComputeShaders(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

	bool Serialize(FArchive& Ar)
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << IDToIndexTableParam;
		Ar << FreeIDListParam;
		Ar << FreeIDListSizesParam;
		Ar << FreeIDListIndexParam;
		return bShaderHasOutdatedParameters;
	}

	void Execute(FRHICommandList& RHICmdList, ERHIFeatureLevel::Type FeatureLevel, uint32 NumIDs, FRHIShaderResourceView* IDToIndexTable, FRWBuffer& FreeIDList, FRWBuffer& FreeIDListSizes, uint32 FreeIDListIndex)
	{
		const EShaderPlatform Platform = GShaderPlatformForFeatureLevel[FeatureLevel];
		const uint32 THREAD_COUNT = (Platform == SP_XBOXONE_D3D12 || Platform == SP_PS4) ? 64 : 128;

		// To simplify the shader code, the size of the ID table must be a multiple of the thread count.
		check(NumIDs % THREAD_COUNT == 0);

		FRHIComputeShader* ComputeShader = GetComputeShader();
		RHICmdList.SetComputeShader(ComputeShader);

		SetSRVParameter(RHICmdList, ComputeShader, IDToIndexTableParam, IDToIndexTable);
		FreeIDListParam.SetBuffer(RHICmdList, ComputeShader, FreeIDList);
		FreeIDListSizesParam.SetBuffer(RHICmdList, ComputeShader, FreeIDListSizes);
		SetShaderValue(RHICmdList, ComputeShader, FreeIDListIndexParam, FreeIDListIndex);

		DispatchComputeShader(RHICmdList, this, NumIDs / THREAD_COUNT, 1, 1);

		RHICmdList.SetShaderResourceViewParameter(ComputeShader, IDToIndexTableParam.GetBaseIndex(), nullptr);
		RHICmdList.SetUAVParameter(ComputeShader, FreeIDListParam.GetUAVIndex(), nullptr);
		RHICmdList.SetUAVParameter(ComputeShader, FreeIDListSizesParam.GetUAVIndex(), nullptr);
	}

private:
	FShaderResourceParameter IDToIndexTableParam;
	FRWShaderParameter FreeIDListParam;
	FRWShaderParameter FreeIDListSizesParam;
	FShaderParameter FreeIDListIndexParam;
};

IMPLEMENT_GLOBAL_SHADER(NiagaraComputeFreeIDsCS, "/Plugin/FX/Niagara/Private/NiagaraComputeFreeIDs.usf", "ComputeFreeIDs", SF_Compute);

void NiagaraComputeGPUFreeIDs(FRHICommandList& RHICmdList, ERHIFeatureLevel::Type FeatureLevel, uint32 NumIDs, FRHIShaderResourceView* IDToIndexTable, FRWBuffer& FreeIDList, FRWBuffer& FreeIDListSizes, uint32 FreeIDListIndex)
{
	TShaderMapRef<NiagaraComputeFreeIDsCS> ComputeFreeIDsCS(GetGlobalShaderMap(FeatureLevel));
	ComputeFreeIDsCS->Execute(RHICmdList, FeatureLevel, NumIDs, IDToIndexTable, FreeIDList, FreeIDListSizes, FreeIDListIndex);
}
