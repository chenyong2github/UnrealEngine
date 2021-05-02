// Copyright Epic Games, Inc. All Rights Reserved.

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

	void Execute(FRHICommandList& RHICmdList, FRHIComputeShader* ComputeShader, uint32 NumElementsToAlloc, FRWBuffer& NewBuffer, uint32 NumExistingElements, FRHIShaderResourceView* ExistingBuffer)
	{
		// To simplify the shader code, the size of the ID table must be a multiple of the thread count.
		check(NumElementsToAlloc % THREAD_COUNT == 0);

		// Shrinking is not supported.
		check(NumElementsToAlloc >= NumExistingElements);
		uint32 NumNewElements = NumElementsToAlloc - NumExistingElements;

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

	LAYOUT_FIELD(FRWShaderParameter, NewBufferParam);
	LAYOUT_FIELD(FShaderResourceParameter, ExistingBufferParam);
	LAYOUT_FIELD(FShaderParameter, NumNewElementsParam);
	LAYOUT_FIELD(FShaderParameter, NumExistingElementsParam);
};

IMPLEMENT_GLOBAL_SHADER(FNiagaraInitFreeIDBufferCS, "/Plugin/FX/Niagara/Private/NiagaraInitFreeIDBuffer.usf", "InitIDBufferCS", SF_Compute);

void NiagaraInitGPUFreeIDList(FRHICommandList& RHICmdList, ERHIFeatureLevel::Type FeatureLevel, uint32 NumElementsToAlloc, FRWBuffer& NewBuffer, uint32 NumExistingElements, FRHIShaderResourceView* ExistingBuffer)
{
	TShaderMapRef<FNiagaraInitFreeIDBufferCS> InitIDBufferCS(GetGlobalShaderMap(FeatureLevel));
	FRHIComputeShader* ComputeShader = InitIDBufferCS.GetComputeShader();
	InitIDBufferCS->Execute(RHICmdList, ComputeShader, NumElementsToAlloc, NewBuffer, NumExistingElements, ExistingBuffer);
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
		OutEnvironment.SetDefine(TEXT("THREAD_COUNT"), GetThreadCount(Parameters.Platform));
	}

	static uint32 GetThreadCount(EShaderPlatform Platform)
	{
		//-TODO: Pull from shader platform info
		return 64;
	}

	void Execute(FRHICommandList& RHICmdList, ERHIFeatureLevel::Type FeatureLevel, FRHIComputeShader* ComputeShader, uint32 NumIDs, FRHIShaderResourceView* IDToIndexTable, FRWBuffer& FreeIDList, FRWBuffer& FreeIDListSizes, uint32 FreeIDListIndex)
	{
		const EShaderPlatform Platform = GShaderPlatformForFeatureLevel[FeatureLevel];
		const uint32 NumThreadGroups = NumIDs / GetThreadCount(Platform);

		// To simplify the shader code, the size of the ID table must be a multiple of the thread count.
		check((NumIDs - (NumThreadGroups * GetThreadCount(Platform))) == 0);

		RHICmdList.SetComputeShader(ComputeShader);

		SetSRVParameter(RHICmdList, ComputeShader, IDToIndexTableParam, IDToIndexTable);
		FreeIDListParam.SetBuffer(RHICmdList, ComputeShader, FreeIDList);
		FreeIDListSizesParam.SetBuffer(RHICmdList, ComputeShader, FreeIDListSizes);
		SetShaderValue(RHICmdList, ComputeShader, FreeIDListIndexParam, FreeIDListIndex);

		DispatchComputeShader(RHICmdList, this, NumThreadGroups, 1, 1);

		RHICmdList.SetShaderResourceViewParameter(ComputeShader, IDToIndexTableParam.GetBaseIndex(), nullptr);
		RHICmdList.SetUAVParameter(ComputeShader, FreeIDListParam.GetUAVIndex(), nullptr);
		RHICmdList.SetUAVParameter(ComputeShader, FreeIDListSizesParam.GetUAVIndex(), nullptr);
	}

private:
	LAYOUT_FIELD(FShaderResourceParameter, IDToIndexTableParam);
	LAYOUT_FIELD(FRWShaderParameter, FreeIDListParam);
	LAYOUT_FIELD(FRWShaderParameter, FreeIDListSizesParam);
	LAYOUT_FIELD(FShaderParameter, FreeIDListIndexParam);
};

IMPLEMENT_GLOBAL_SHADER(NiagaraComputeFreeIDsCS, "/Plugin/FX/Niagara/Private/NiagaraComputeFreeIDs.usf", "ComputeFreeIDs", SF_Compute);

void NiagaraComputeGPUFreeIDs(FRHICommandList& RHICmdList, ERHIFeatureLevel::Type FeatureLevel, uint32 NumIDs, FRHIShaderResourceView* IDToIndexTable, FRWBuffer& FreeIDList, FRWBuffer& FreeIDListSizes, uint32 FreeIDListIndex)
{
	TShaderMapRef<NiagaraComputeFreeIDsCS> ComputeFreeIDsCS(GetGlobalShaderMap(FeatureLevel));
	FRHIComputeShader* ComputeShader = ComputeFreeIDsCS.GetComputeShader();
	ComputeFreeIDsCS->Execute(RHICmdList, FeatureLevel, ComputeShader, NumIDs, IDToIndexTable, FreeIDList, FreeIDListSizes, FreeIDListIndex);
}

class NiagaraFillIntBufferCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(NiagaraFillIntBufferCS);

public:
	NiagaraFillIntBufferCS() : FGlobalShader() {}

	NiagaraFillIntBufferCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) : FGlobalShader(Initializer)
	{
		TargetBufferParam.Bind(Initializer.ParameterMap, TEXT("TargetBuffer"));
		FillValueParam.Bind(Initializer.ParameterMap, TEXT("FillValue"));
		BufferSizeParam.Bind(Initializer.ParameterMap, TEXT("BufferSize"));
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return RHISupportsComputeShaders(Parameters.Platform);
	}

	void Execute(FRHICommandList& RHICmdList, FRHIComputeShader* ComputeShader, FRWBuffer& Buffer, int Value)
	{
		const uint32 THREAD_COUNT = 64;
		const uint32 NumInts = Buffer.NumBytes / sizeof(int);
		const uint32 ThreadGroups = FMath::DivideAndRoundUp(NumInts, THREAD_COUNT);

		RHICmdList.SetComputeShader(ComputeShader);

		SetUAVParameter(RHICmdList, ComputeShader, TargetBufferParam, Buffer.UAV);
		SetShaderValue(RHICmdList, ComputeShader, FillValueParam, Value);
		SetShaderValue(RHICmdList, ComputeShader, BufferSizeParam, NumInts);

		DispatchComputeShader(RHICmdList, this, ThreadGroups, 1, 1);

		SetUAVParameter(RHICmdList, ComputeShader, TargetBufferParam, nullptr);
	}

private:
	LAYOUT_FIELD(FShaderResourceParameter, TargetBufferParam);
	LAYOUT_FIELD(FShaderParameter, FillValueParam);
	LAYOUT_FIELD(FShaderParameter, BufferSizeParam);
};

IMPLEMENT_GLOBAL_SHADER(NiagaraFillIntBufferCS, "/Plugin/FX/Niagara/Private/NiagaraFillIntBuffer.usf", "FillIntBuffer", SF_Compute);

void NiagaraFillGPUIntBuffer(FRHICommandList& RHICmdList, ERHIFeatureLevel::Type FeatureLevel, FRWBuffer& Buffer, int Value)
{
	TShaderMapRef<NiagaraFillIntBufferCS> FillCS(GetGlobalShaderMap(FeatureLevel));
	FRHIComputeShader* ComputeShader = FillCS.GetComputeShader();
	FillCS->Execute(RHICmdList, ComputeShader, Buffer, Value);
}
