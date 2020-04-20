// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraDataInterfaceParticleRead.h"
#include "NiagaraConstants.h"
#include "NiagaraSystemInstance.h"
#include "ShaderParameterUtils.h"
#include "NiagaraRenderer.h"
#include "NiagaraEmitterInstanceBatcher.h"

#define LOCTEXT_NAMESPACE "NiagaraDataInterfaceParticleRead"

static const FName GetNumSpawnedParticlesFunctionName("Get Num Spawned Particles");
static const FName GetSpawnedIDAtIndexFunctionName("Get Spawned ID At Index");
static const FName GetNumParticlesFunctionName("Get Num Particles");
static const FName GetParticleIndexFunctionName("Get Particle Index");

static const FName GetIntByIDFunctionName("Get Int By ID");
static const FName GetBoolByIDFunctionName("Get Bool By ID");
static const FName GetFloatByIDFunctionName("Get Float By ID");
static const FName GetVec2ByIDFunctionName("Get Vector 2D By ID");
static const FName GetVec3ByIDFunctionName("Get Vector By ID");
static const FName GetVec4ByIDFunctionName("Get Vector 4 By ID");
static const FName GetColorByIDFunctionName("Get Color By ID");
static const FName GetQuatByIDFunctionName("Get Quaternion By ID");
static const FName GetIDByIDFunctionName("Get ID By ID");

static const FName GetIntByIndexFunctionName("Get Int By Index");
static const FName GetBoolByIndexFunctionName("Get Bool By Index");
static const FName GetFloatByIndexFunctionName("Get Float By Index");
static const FName GetVec2ByIndexFunctionName("Get Vector 2D By Index");
static const FName GetVec3ByIndexFunctionName("Get Vector By Index");
static const FName GetVec4ByIndexFunctionName("Get Vector 4 By Index");
static const FName GetColorByIndexFunctionName("Get Color By Index");
static const FName GetQuatByIndexFunctionName("Get Quaternion By Index");
static const FName GetIDByIndexFunctionName("Get ID By Index");

static const FString NumSpawnedParticlesBaseName(TEXT("NumSpawnedParticles_"));
static const FString SpawnedParticlesAcquireTagBaseName(TEXT("SpawnedParticlesAcquireTag_"));
static const FString InstanceCountOffsetBaseName(TEXT("InstanceCountOffset_"));
static const FString SpawnedIDsBufferBaseName(TEXT("SpawnedIDsBuffer_"));
static const FString IDToIndexTableBaseName(TEXT("IDToIndexTable_"));
static const FString InputFloatBufferBaseName(TEXT("InputFloatBuffer_"));
static const FString InputIntBufferBaseName(TEXT("InputIntBuffer_"));
static const FString InputHalfBufferBaseName(TEXT("InputHalfBuffer_"));
static const FString ParticleStrideFloatBaseName(TEXT("ParticleStrideFloat_"));
static const FString ParticleStrideIntBaseName(TEXT("ParticleStrideInt_"));
static const FString ParticleStrideHalfBaseName(TEXT("ParticleStrideHalf_"));
static const FString AttributeIndicesBaseName(TEXT("AttributeIndices_"));
static const FString AttributeCompressedBaseName(TEXT("AttributeCompressed_"));
static const FString AcquireTagRegisterIndexBaseName(TEXT("AcquireTagRegisterIndex_"));

enum class ENiagaraParticleDataComponentType : uint8
{
	Float,
	Int,
	Bool,
	ID
};

enum class ENiagaraParticleDataValueType : uint8
{
	Invalid,
	Int,
	Float,
	Vec2,
	Vec3,
	Vec4,
	Bool,
	Color,
	Quat,
	ID
};

DECLARE_INTRINSIC_TYPE_LAYOUT(ENiagaraParticleDataValueType);

static const TCHAR* NiagaraParticleDataValueTypeName(ENiagaraParticleDataValueType Type)
{
	switch (Type)
	{
		case ENiagaraParticleDataValueType::Invalid:	return TEXT("INVALID");
		case ENiagaraParticleDataValueType::Int:		return TEXT("int");
		case ENiagaraParticleDataValueType::Float:		return TEXT("float");
		case ENiagaraParticleDataValueType::Vec2:		return TEXT("vec2");
		case ENiagaraParticleDataValueType::Vec3:		return TEXT("vec3");
		case ENiagaraParticleDataValueType::Vec4:		return TEXT("vec4");
		case ENiagaraParticleDataValueType::Bool:		return TEXT("bool");
		case ENiagaraParticleDataValueType::Color:		return TEXT("color");
		case ENiagaraParticleDataValueType::Quat:		return TEXT("quaternion");
		case ENiagaraParticleDataValueType::ID:			return TEXT("ID");
		default:										return TEXT("UNKNOWN");
	}
}

struct FNDIParticleRead_InstanceData
{
	FNiagaraSystemInstance* SystemInstance;
	FNiagaraEmitterInstance* EmitterInstance;
};

struct FNDIParticleRead_GameToRenderData
{
	FNDIParticleRead_GameToRenderData() : SourceEmitterGPUContext(nullptr) {}

	FNiagaraComputeExecutionContext* SourceEmitterGPUContext;
	FString SourceEmitterName;
};

struct FNDIParticleRead_RenderInstanceData
{
	FNDIParticleRead_RenderInstanceData() :
		SourceEmitterGPUContext(nullptr)
		, CachedDataSet(nullptr)
		, AcquireTagRegisterIndex(-1)
		, bSourceEmitterNotGPUErrorShown(false)
	{
	}

	FNiagaraComputeExecutionContext* SourceEmitterGPUContext;
	FString SourceEmitterName;
	const FNiagaraDataSet* CachedDataSet;
	TArray<int32> AttributeIndices;
	TArray<int32> AttributeCompressed;
	int32 AcquireTagRegisterIndex;
	bool bSourceEmitterNotGPUErrorShown;
};

struct FNiagaraDataInterfaceProxyParticleRead : public FNiagaraDataInterfaceProxy
{
	virtual void ConsumePerInstanceDataFromGameThread(void* Data, const FNiagaraSystemInstanceID& InstanceID) override
	{
		FNDIParticleRead_RenderInstanceData* InstanceData = SystemsRenderData.Find(InstanceID);
		if (!ensure(InstanceData))
		{
			return;
		}

		FNDIParticleRead_GameToRenderData* IncomingData = static_cast<FNDIParticleRead_GameToRenderData*>(Data);
		if (IncomingData)
		{
			InstanceData->SourceEmitterGPUContext = IncomingData->SourceEmitterGPUContext;
			InstanceData->SourceEmitterName = IncomingData->SourceEmitterName;
		}
		else
		{
			InstanceData->SourceEmitterGPUContext = nullptr;
			InstanceData->SourceEmitterName = TEXT("");
		}
	}
	
	virtual int32 PerInstanceDataPassedToRenderThreadSize() const override
	{
		return sizeof(FNDIParticleRead_GameToRenderData);
	}

	void CreateRenderThreadSystemData(const FNiagaraSystemInstanceID& InstanceID)
	{
		check(IsInRenderingThread());
		check(!SystemsRenderData.Contains(InstanceID));
		SystemsRenderData.Add(InstanceID);
	}

	void DestroyRenderThreadSystemData(const FNiagaraSystemInstanceID& InstanceID)
	{
		check(IsInRenderingThread());
		SystemsRenderData.Remove(InstanceID);
	}

	FNDIParticleRead_RenderInstanceData* GetRenderDataForSystem(const FNiagaraSystemInstanceID& InstanceID)
	{
		return SystemsRenderData.Find(InstanceID);
	}

private:
	TMap<FNiagaraSystemInstanceID, FNDIParticleRead_RenderInstanceData> SystemsRenderData;
};

struct FNiagaraDataInterfaceParametersCS_ParticleRead : public FNiagaraDataInterfaceParametersCS
{
	DECLARE_TYPE_LAYOUT(FNiagaraDataInterfaceParametersCS_ParticleRead, NonVirtual);

	ENiagaraParticleDataValueType GetValueTypeFromFuncName(const FName& FuncName)
	{
		if (FuncName == GetIntByIDFunctionName || FuncName == GetIntByIndexFunctionName) return ENiagaraParticleDataValueType::Int;
		if (FuncName == GetBoolByIDFunctionName || FuncName == GetBoolByIndexFunctionName) return ENiagaraParticleDataValueType::Bool;
		if (FuncName == GetFloatByIDFunctionName || FuncName == GetFloatByIndexFunctionName) return ENiagaraParticleDataValueType::Float;
		if (FuncName == GetVec2ByIDFunctionName || FuncName == GetVec2ByIndexFunctionName) return ENiagaraParticleDataValueType::Vec2;
		if (FuncName == GetVec3ByIDFunctionName || FuncName == GetVec3ByIndexFunctionName) return ENiagaraParticleDataValueType::Vec3;
		if (FuncName == GetVec4ByIDFunctionName || FuncName == GetVec4ByIndexFunctionName) return ENiagaraParticleDataValueType::Vec4;
		if (FuncName == GetColorByIDFunctionName || FuncName == GetColorByIndexFunctionName) return ENiagaraParticleDataValueType::Color;
		if (FuncName == GetQuatByIDFunctionName || FuncName == GetQuatByIndexFunctionName) return ENiagaraParticleDataValueType::Quat;
		if (FuncName == GetIDByIDFunctionName || FuncName == GetIDByIndexFunctionName) return ENiagaraParticleDataValueType::ID;
		return ENiagaraParticleDataValueType::Invalid;
	}

	void Bind(const FNiagaraDataInterfaceGPUParamInfo& ParameterInfo, const FShaderParameterMap& ParameterMap)
	{
		NumSpawnedParticlesParam.Bind(ParameterMap, *(NumSpawnedParticlesBaseName + ParameterInfo.DataInterfaceHLSLSymbol));
		SpawnedParticlesAcquireTagParam.Bind(ParameterMap, *(SpawnedParticlesAcquireTagBaseName + ParameterInfo.DataInterfaceHLSLSymbol));
		InstanceCountOffsetParam.Bind(ParameterMap, *(InstanceCountOffsetBaseName + ParameterInfo.DataInterfaceHLSLSymbol));
		SpawnedIDsBufferParam.Bind(ParameterMap, *(SpawnedIDsBufferBaseName + ParameterInfo.DataInterfaceHLSLSymbol));
		IDToIndexTableParam.Bind(ParameterMap, *(IDToIndexTableBaseName + ParameterInfo.DataInterfaceHLSLSymbol));
		InputFloatBufferParam.Bind(ParameterMap, *(InputFloatBufferBaseName + ParameterInfo.DataInterfaceHLSLSymbol));
		InputIntBufferParam.Bind(ParameterMap, *(InputIntBufferBaseName + ParameterInfo.DataInterfaceHLSLSymbol));
		InputHalfBufferParam.Bind(ParameterMap, *(InputHalfBufferBaseName + ParameterInfo.DataInterfaceHLSLSymbol));
		ParticleStrideFloatParam.Bind(ParameterMap, *(ParticleStrideFloatBaseName + ParameterInfo.DataInterfaceHLSLSymbol));
		ParticleStrideIntParam.Bind(ParameterMap, *(ParticleStrideIntBaseName + ParameterInfo.DataInterfaceHLSLSymbol));
		ParticleStrideHalfParam.Bind(ParameterMap, *(ParticleStrideHalfBaseName + ParameterInfo.DataInterfaceHLSLSymbol));
		AttributeIndicesParam.Bind(ParameterMap, *(AttributeIndicesBaseName + ParameterInfo.DataInterfaceHLSLSymbol));
		AttributeCompressedParam.Bind(ParameterMap, *(AttributeCompressedBaseName + ParameterInfo.DataInterfaceHLSLSymbol));
		AcquireTagRegisterIndexParam.Bind(ParameterMap, *(AcquireTagRegisterIndexBaseName + ParameterInfo.DataInterfaceHLSLSymbol));

		int32 NumFuncs = ParameterInfo.GeneratedFunctions.Num();
		AttributeNames.SetNum(NumFuncs);
		AttributeTypes.SetNum(NumFuncs);
		for (int32 FuncIdx = 0; FuncIdx < NumFuncs; ++FuncIdx)
		{
			const FNiagaraDataInterfaceGeneratedFunction& Func = ParameterInfo.GeneratedFunctions[FuncIdx];
			static const FName NAME_Attribute("Attribute");
			const FName* AttributeName = Func.FindSpecifierValue(NAME_Attribute);
			if (AttributeName != nullptr)
			{
				AttributeNames[FuncIdx] = *AttributeName;
				AttributeTypes[FuncIdx] = GetValueTypeFromFuncName(Func.DefinitionName);
			}
			else
			{
				// This is not an error. GetNumSpawnedParticles and GetSpawnedIDAtIndexFunctionName don't use specifiers,
				// but they take up slots in the attribute indices array for simplicity. Just stick NAME_None in here to ignore them.
				AttributeNames[FuncIdx] = NAME_None;
				AttributeTypes[FuncIdx] = ENiagaraParticleDataValueType::Invalid;
			}
		}
	}

	void SetErrorParams(FRHICommandList& RHICmdList, FRHIComputeShader* ComputeShader, bool bSkipSpawnInfo) const
	{
		// Set all the indices to -1, so that any reads return false.
		TArray<int32, TInlineAllocator<32>> AttributeIndices;
		TArray<int32, TInlineAllocator<32>> AttributeCompressed;

		int NumAttrIndices = Align(AttributeNames.Num(), 4);
		AttributeIndices.SetNumUninitialized(NumAttrIndices);
		AttributeCompressed.SetNumUninitialized(NumAttrIndices);

		for (int AttrIdx = 0; AttrIdx < AttributeIndices.Num(); ++AttrIdx)
		{
			AttributeIndices[AttrIdx] = -1;
			AttributeCompressed[AttrIdx] = 0;
		}

		int32 AcquireTagRegisterIndex = -1;

		if (!bSkipSpawnInfo)
		{
			SetShaderValue(RHICmdList, ComputeShader, NumSpawnedParticlesParam, 0);
			SetShaderValue(RHICmdList, ComputeShader, SpawnedParticlesAcquireTagParam, 0);
			SetSRVParameter(RHICmdList, ComputeShader, SpawnedIDsBufferParam, FNiagaraRenderer::GetDummyIntBuffer());
		}

		SetShaderValue(RHICmdList, ComputeShader, InstanceCountOffsetParam, -1);
		SetSRVParameter(RHICmdList, ComputeShader, IDToIndexTableParam, FNiagaraRenderer::GetDummyIntBuffer());
		SetSRVParameter(RHICmdList, ComputeShader, InputFloatBufferParam, FNiagaraRenderer::GetDummyFloatBuffer());
		SetSRVParameter(RHICmdList, ComputeShader, InputIntBufferParam, FNiagaraRenderer::GetDummyIntBuffer());
		SetSRVParameter(RHICmdList, ComputeShader, InputHalfBufferParam, FNiagaraRenderer::GetDummyHalfBuffer());
		SetShaderValue(RHICmdList, ComputeShader, ParticleStrideFloatParam, 0);
		SetShaderValue(RHICmdList, ComputeShader, ParticleStrideIntParam, 0);
		SetShaderValue(RHICmdList, ComputeShader, ParticleStrideHalfParam, 0);
		SetShaderValueArray(RHICmdList, ComputeShader, AttributeIndicesParam, AttributeIndices.GetData(), AttributeIndices.Num());
		SetShaderValueArray(RHICmdList, ComputeShader, AttributeCompressedParam, AttributeCompressed.GetData(), AttributeCompressed.Num());
		SetShaderValue(RHICmdList, ComputeShader, AcquireTagRegisterIndexParam, AcquireTagRegisterIndex);
	}

	bool CheckVariableType(const FNiagaraTypeDefinition& VarType, ENiagaraParticleDataValueType AttributeType) const
	{
		switch (AttributeType)
		{
			case ENiagaraParticleDataValueType::Int: return VarType == FNiagaraTypeDefinition::GetIntDef();
			case ENiagaraParticleDataValueType::Bool: return VarType == FNiagaraTypeDefinition::GetBoolDef();
			case ENiagaraParticleDataValueType::Float: return VarType == FNiagaraTypeDefinition::GetFloatDef();
			case ENiagaraParticleDataValueType::Vec2: return VarType == FNiagaraTypeDefinition::GetVec2Def();
			case ENiagaraParticleDataValueType::Vec3: return VarType == FNiagaraTypeDefinition::GetVec3Def();
			case ENiagaraParticleDataValueType::Vec4: return VarType == FNiagaraTypeDefinition::GetVec4Def();
			case ENiagaraParticleDataValueType::Color: return VarType == FNiagaraTypeDefinition::GetColorDef();
			case ENiagaraParticleDataValueType::Quat: return VarType == FNiagaraTypeDefinition::GetQuatDef();
			case ENiagaraParticleDataValueType::ID: return VarType == FNiagaraTypeDefinition::GetIDDef();
			default: return false;
		}
	}

	bool CheckHalfVariableType(const FNiagaraTypeDefinition& VarType, ENiagaraParticleDataValueType AttributeType) const
	{
		switch (AttributeType)
		{
			case ENiagaraParticleDataValueType::Float: return VarType == FNiagaraTypeDefinition::GetHalfDef();
			case ENiagaraParticleDataValueType::Vec2: return VarType == FNiagaraTypeDefinition::GetHalfVec2Def();
			case ENiagaraParticleDataValueType::Vec3: return VarType == FNiagaraTypeDefinition::GetHalfVec3Def();
			case ENiagaraParticleDataValueType::Vec4: return VarType == FNiagaraTypeDefinition::GetHalfVec4Def();
			case ENiagaraParticleDataValueType::Color: return VarType == FNiagaraTypeDefinition::GetHalfVec4Def();
			case ENiagaraParticleDataValueType::Quat: return VarType == FNiagaraTypeDefinition::GetHalfVec4Def();
			default: return false;
		}
	}

	void FindAttributeIndices(FNDIParticleRead_RenderInstanceData* InstanceData, const FNiagaraDataSet* SourceDataSet) const
	{
		check(AttributeNames.Num() == AttributeTypes.Num());

		int NumAttrIndices = Align(AttributeNames.Num(), 4);
		InstanceData->AttributeIndices.SetNumUninitialized(NumAttrIndices);
		InstanceData->AttributeCompressed.SetNumUninitialized(NumAttrIndices);

		// Find the register index for each named attribute in the source emitter.
		const TArray<FNiagaraVariable>& SourceEmitterVariables = SourceDataSet->GetVariables();
		const TArray<FNiagaraVariableLayoutInfo>& SourceEmitterVariableLayouts = SourceDataSet->GetVariableLayouts();
		for (int AttrNameIdx = 0; AttrNameIdx < AttributeNames.Num(); ++AttrNameIdx)
		{
			const FName& AttrName = AttributeNames[AttrNameIdx];
			if (AttrName == NAME_None)
			{
				InstanceData->AttributeIndices[AttrNameIdx] = -1;
				continue;
			}

			bool FoundVariable = false;
			for (int VarIdx = 0; VarIdx < SourceEmitterVariables.Num(); ++VarIdx)
			{
				const FNiagaraVariable& Var = SourceEmitterVariables[VarIdx];
				if (Var.GetName() == AttrName)
				{
					ENiagaraParticleDataValueType AttributeType = AttributeTypes[AttrNameIdx];
					if (CheckVariableType(Var.GetType(), AttributeType))
					{
						const FNiagaraVariableLayoutInfo& Layout = SourceEmitterVariableLayouts[VarIdx];
						InstanceData->AttributeIndices[AttrNameIdx] = 
							(AttributeType == ENiagaraParticleDataValueType::Int || AttributeType == ENiagaraParticleDataValueType::Bool || AttributeType == ENiagaraParticleDataValueType::ID) ? Layout.Int32ComponentStart : Layout.FloatComponentStart;
						InstanceData->AttributeCompressed[AttrNameIdx] = 0;
					}
					else if (CheckHalfVariableType(Var.GetType(), AttributeType))
					{
						const FNiagaraVariableLayoutInfo& Layout = SourceEmitterVariableLayouts[VarIdx];
						InstanceData->AttributeIndices[AttrNameIdx] = Layout.FloatComponentStart;
						InstanceData->AttributeCompressed[AttrNameIdx] = 1;
					}
					else
					{
						UE_LOG(LogNiagara, Error, TEXT("Variable '%s' in emitter '%s' has type '%s', but particle read DI tried to access it as '%s'."),
							*Var.GetName().ToString(), *InstanceData->SourceEmitterName, *Var.GetType().GetName(), NiagaraParticleDataValueTypeName(AttributeType)
						);
						InstanceData->AttributeIndices[AttrNameIdx] = -1;
						InstanceData->AttributeCompressed[AttrNameIdx] = 0;
					}
					FoundVariable = true;
					break;
				}
			}

			if (!FoundVariable)
			{
				UE_LOG(LogNiagara, Error, TEXT("Particle read DI is trying to access inexistent variable '%s' in emitter '%s'."), *AttrName.ToString(), *InstanceData->SourceEmitterName);
				InstanceData->AttributeIndices[AttrNameIdx] = -1;
				InstanceData->AttributeCompressed[AttrNameIdx] = 0;
			}
		}

		// Find the register index for the AcquireTag part of the particle ID in the source emitter.
		if (AcquireTagRegisterIndexParam.IsBound())
		{
			InstanceData->AcquireTagRegisterIndex = -1;
			for (int VarIdx = 0; VarIdx < SourceEmitterVariables.Num(); ++VarIdx)
			{
				const FNiagaraVariable& Var = SourceEmitterVariables[VarIdx];
				if (Var.GetName().ToString() == TEXT("ID"))
				{
					InstanceData->AcquireTagRegisterIndex = SourceEmitterVariableLayouts[VarIdx].Int32ComponentStart + 1;
					break;
				}
			}
			if (InstanceData->AcquireTagRegisterIndex == -1)
			{
				UE_LOG(LogNiagara, Error, TEXT("Particle read DI cannot find ID variable in emitter '%s'."), *InstanceData->SourceEmitterName);
			}
		}

		// Initialize the buffer padding too, so we don't move garbage around.
		for (int AttrIdx = AttributeNames.Num(); AttrIdx < InstanceData->AttributeIndices.Num(); ++AttrIdx)
		{
			InstanceData->AttributeIndices[AttrIdx] = -1;
			InstanceData->AttributeCompressed[AttrIdx] = 0;
		}
	}

	FRHIShaderResourceView* GetIntSRVWithFallback(FRWBuffer& Buffer) const
	{
		return Buffer.SRV ? Buffer.SRV.GetReference() : FNiagaraRenderer::GetDummyIntBuffer();
	}

	FRHIShaderResourceView* GetFloatSRVWithFallback(FRWBuffer& Buffer) const
	{
		return Buffer.SRV ? Buffer.SRV.GetReference() : FNiagaraRenderer::GetDummyFloatBuffer();
	}

	void Set(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const
	{
		check(IsInRenderingThread());

		FRHIComputeShader* ComputeShader = RHICmdList.GetBoundComputeShader();

		FNiagaraDataInterfaceProxyParticleRead* Proxy = static_cast<FNiagaraDataInterfaceProxyParticleRead*>(Context.DataInterface);
		check(Proxy);

		FNDIParticleRead_RenderInstanceData* InstanceData = Proxy->GetRenderDataForSystem(Context.SystemInstance);
		if (!InstanceData)
		{
			SetErrorParams(RHICmdList, ComputeShader, false);
			return;
		}

		if (InstanceData->SourceEmitterGPUContext == nullptr)
		{
			// This means the source emitter isn't running on GPU.
			if (!InstanceData->bSourceEmitterNotGPUErrorShown)
			{
				UE_LOG(LogNiagara, Error, TEXT("GPU particle read DI is set to access CPU emitter '%s'."), *InstanceData->SourceEmitterName);
				InstanceData->bSourceEmitterNotGPUErrorShown = true;
			}
			SetErrorParams(RHICmdList, ComputeShader, false);
			InstanceData->CachedDataSet = nullptr;
			return;
		}

		InstanceData->bSourceEmitterNotGPUErrorShown = false;

		FNiagaraDataSet* SourceDataSet = InstanceData->SourceEmitterGPUContext->MainDataSet;
		if (!SourceDataSet)
		{
			SetErrorParams(RHICmdList, ComputeShader, false);
			InstanceData->CachedDataSet = nullptr;
			return;
		}

		FNiagaraDataBuffer* SourceData;
		uint32 NumSpawnedInstances = 0, IDAcquireTag = 0, InstanceCountOffset = 0xffffffff;
		const bool bReadingOwnEmitter = Context.ComputeInstanceData->Context == InstanceData->SourceEmitterGPUContext;
		if (bReadingOwnEmitter)
		{
			// If the current execution context is the same as the source emitter's context, it means we're reading from
			// ourselves. We can't use SourceDataSet->GetCurrentData() in that case, because EndSimulate() has already been
			// called on the current emitter, and the current data has been set to the destination data. We need to use the
			// current compute instance data to get to the input buffers.
			const FNiagaraSimStageData& SimStageData = Context.ComputeInstanceData->SimStageData[Context.SimulationStageIndex];
			SourceData = SimStageData.Source;
			InstanceCountOffset = SimStageData.SourceCountOffset;

			// We still want to get the spawn count and ID acquire tag from the destination data, because that's where
			// NiagaraEmitterInstanceBatcher::Run() stores them.
			if (SimStageData.Destination != nullptr)
			{
				NumSpawnedInstances = SimStageData.Destination->GetNumSpawnedInstances();
				IDAcquireTag = SimStageData.Destination->GetIDAcquireTag();
			}
		}
		else
		{
			SourceData = SourceDataSet->GetCurrentData();
			if (SourceData)
			{
				NumSpawnedInstances = SourceData->GetNumSpawnedInstances();
				IDAcquireTag = SourceData->GetIDAcquireTag();
				InstanceCountOffset = SourceData->GetGPUInstanceCountBufferOffset();
			}
		}

		SetShaderValue(RHICmdList, ComputeShader, NumSpawnedParticlesParam, NumSpawnedInstances);
		SetShaderValue(RHICmdList, ComputeShader, SpawnedParticlesAcquireTagParam, IDAcquireTag);
		SetSRVParameter(RHICmdList, ComputeShader, SpawnedIDsBufferParam, GetIntSRVWithFallback(SourceDataSet->GetGPUFreeIDs()));

		if (!SourceData)
		{
			SetErrorParams(RHICmdList, ComputeShader, true);
			return;
		}

		if (InstanceData->CachedDataSet != SourceDataSet)
		{
			FindAttributeIndices(InstanceData, SourceDataSet);
			InstanceData->CachedDataSet = SourceDataSet;
		}

		if (!bReadingOwnEmitter)
		{
			FRHIUnorderedAccessView* InputBuffers[3];
			int32 NumTransitions = 0;
			InputBuffers[NumTransitions] = SourceData->GetGPUBufferFloat().UAV;
			++NumTransitions;
			InputBuffers[NumTransitions] = SourceData->GetGPUBufferInt().UAV;
			++NumTransitions;
			if (SourceData->GetGPUIDToIndexTable().UAV)
			{
				InputBuffers[NumTransitions] = SourceData->GetGPUIDToIndexTable().UAV;
				++NumTransitions;
			}
			checkSlow(NumTransitions <= UE_ARRAY_COUNT(InputBuffers));
			RHICmdList.TransitionResources(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToCompute, InputBuffers, NumTransitions);

			if (InstanceCountOffsetParam.IsBound())
			{
				// If we're reading the instance count from another emitter, we must insert a barrier on the instance count buffer, to make sure the
				// previous dispatch finished writing to it. For D3D11, we need to insert an end overlap / begin overlap pair to break up the current
				// overlap group.
				RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToCompute, const_cast<NiagaraEmitterInstanceBatcher*>(Context.Batcher)->GetGPUInstanceCounterManager().GetInstanceCountBuffer().UAV);
				RHICmdList.EndUAVOverlap();
				RHICmdList.BeginUAVOverlap();
			}
		}

		const uint32 ParticleStrideFloat = SourceData->GetFloatStride() / sizeof(float);
		const uint32 ParticleStrideInt = SourceData->GetInt32Stride() / sizeof(int32);
		const uint32 ParticleStrideHalf = SourceData->GetHalfStride() / sizeof(FFloat16);

		SetShaderValue(RHICmdList, ComputeShader, InstanceCountOffsetParam, InstanceCountOffset);
		SetSRVParameter(RHICmdList, ComputeShader, IDToIndexTableParam, GetIntSRVWithFallback(SourceData->GetGPUIDToIndexTable()));
		SetSRVParameter(RHICmdList, ComputeShader, InputFloatBufferParam, GetFloatSRVWithFallback(SourceData->GetGPUBufferFloat()));
		SetSRVParameter(RHICmdList, ComputeShader, InputIntBufferParam, GetIntSRVWithFallback(SourceData->GetGPUBufferInt()));
		SetSRVParameter(RHICmdList, ComputeShader, InputHalfBufferParam, GetFloatSRVWithFallback(SourceData->GetGPUBufferHalf()));
		SetShaderValue(RHICmdList, ComputeShader, ParticleStrideFloatParam, ParticleStrideFloat);
		SetShaderValue(RHICmdList, ComputeShader, ParticleStrideIntParam, ParticleStrideInt);
		SetShaderValue(RHICmdList, ComputeShader, ParticleStrideHalfParam, ParticleStrideHalf);
		SetShaderValueArray(RHICmdList, ComputeShader, AttributeIndicesParam, InstanceData->AttributeIndices.GetData(), InstanceData->AttributeIndices.Num());
		SetShaderValueArray(RHICmdList, ComputeShader, AttributeCompressedParam, InstanceData->AttributeCompressed.GetData(), InstanceData->AttributeCompressed.Num());
		SetShaderValue(RHICmdList, ComputeShader, AcquireTagRegisterIndexParam, InstanceData->AcquireTagRegisterIndex);
	}
	
private:
	LAYOUT_FIELD(FShaderParameter, NumSpawnedParticlesParam);
	LAYOUT_FIELD(FShaderParameter, SpawnedParticlesAcquireTagParam);
	LAYOUT_FIELD(FShaderParameter, InstanceCountOffsetParam);
	LAYOUT_FIELD(FShaderResourceParameter, SpawnedIDsBufferParam);
	LAYOUT_FIELD(FShaderResourceParameter, IDToIndexTableParam);
	LAYOUT_FIELD(FShaderResourceParameter, InputFloatBufferParam);
	LAYOUT_FIELD(FShaderResourceParameter, InputIntBufferParam);
	LAYOUT_FIELD(FShaderResourceParameter, InputHalfBufferParam);
	LAYOUT_FIELD(FShaderParameter, ParticleStrideFloatParam);
	LAYOUT_FIELD(FShaderParameter, ParticleStrideHalfParam);
	LAYOUT_FIELD(FShaderParameter, ParticleStrideIntParam);
	LAYOUT_FIELD(FShaderParameter, AttributeIndicesParam);
	LAYOUT_FIELD(FShaderParameter, AttributeCompressedParam);
	LAYOUT_FIELD(FShaderParameter, AcquireTagRegisterIndexParam);
	LAYOUT_FIELD(TMemoryImageArray<FName>, AttributeNames);
	LAYOUT_FIELD(TMemoryImageArray<ENiagaraParticleDataValueType>, AttributeTypes);
};

IMPLEMENT_TYPE_LAYOUT(FNiagaraDataInterfaceParametersCS_ParticleRead);

UNiagaraDataInterfaceParticleRead::UNiagaraDataInterfaceParticleRead(FObjectInitializer const& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Proxy.Reset(new FNiagaraDataInterfaceProxyParticleRead());
}

void UNiagaraDataInterfaceParticleRead::PostInitProperties()
{
	Super::PostInitProperties();

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(GetClass()), true, false, false);
	}
}

bool UNiagaraDataInterfaceParticleRead::InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	FNDIParticleRead_InstanceData* PIData = new (PerInstanceData) FNDIParticleRead_InstanceData;
	PIData->SystemInstance = SystemInstance;
	PIData->EmitterInstance = nullptr;
	for (TSharedPtr<FNiagaraEmitterInstance, ESPMode::ThreadSafe> EmitterInstance : SystemInstance->GetEmitters())
	{
		if (EmitterName == EmitterInstance->GetCachedEmitter()->GetUniqueEmitterName())
		{
			PIData->EmitterInstance = EmitterInstance.Get();
			break;
		}
	}

	if (PIData->EmitterInstance == nullptr)
	{
		UE_LOG(LogNiagara, Error, TEXT("Source emitter '%s' not found."), *EmitterName);
		return false;
	}

	FNiagaraDataInterfaceProxyParticleRead* ThisProxy = GetProxyAs<FNiagaraDataInterfaceProxyParticleRead>();
	ENQUEUE_RENDER_COMMAND(FNDIParticleReadCreateRTInstance)(
		[ThisProxy, InstanceID = SystemInstance->GetId()](FRHICommandList& CmdList)
		{
			ThisProxy->CreateRenderThreadSystemData(InstanceID);
		}
	);

	return true;
}

void UNiagaraDataInterfaceParticleRead::DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	FNDIParticleRead_InstanceData* InstData = (FNDIParticleRead_InstanceData*)PerInstanceData;
	InstData->~FNDIParticleRead_InstanceData();

	FNiagaraDataInterfaceProxyParticleRead* ThisProxy = GetProxyAs<FNiagaraDataInterfaceProxyParticleRead>();
	ENQUEUE_RENDER_COMMAND(FNDIParticleReadDestroyRTInstance) (
		[ThisProxy, InstanceID = SystemInstance->GetId()](FRHICommandListImmediate& CmdList)
		{
			ThisProxy->DestroyRenderThreadSystemData(InstanceID);
		}
	);
}

int32 UNiagaraDataInterfaceParticleRead::PerInstanceDataSize() const
{
	return sizeof(FNDIParticleRead_InstanceData);
}

void UNiagaraDataInterfaceParticleRead::GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)
{
	//
	// Spawn info and particle count
	//
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetNumSpawnedParticlesFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Particle Reader")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Num Spawned")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.bExperimental = true;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetSpawnedIDAtIndexFunctionName;

		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Particle Reader")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Spawn Index")));

		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Valid")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIDDef(), TEXT("ID")));

		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.bExperimental = true;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetNumParticlesFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Particle Reader")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Num Particles")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetParticleIndexFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Particle Reader")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIDDef(), TEXT("Particle ID")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Index")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.bExperimental = true;
		OutFunctions.Add(Sig);
	}

	//
	// Get attribute by ID
	//
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetIntByIDFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Particle Reader")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIDDef(), TEXT("Particle ID")));

		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Valid")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Value")));

		Sig.FunctionSpecifiers.Add(FName("Attribute"));

		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.bExperimental = true;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetBoolByIDFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Particle Reader")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIDDef(), TEXT("Particle ID")));

		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Valid")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Value")));

		Sig.FunctionSpecifiers.Add(FName("Attribute"));

		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.bExperimental = true;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetFloatByIDFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Particle Reader")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIDDef(), TEXT("Particle ID")));

		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Valid")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Value")));

		Sig.FunctionSpecifiers.Add(FName("Attribute"));

		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.bExperimental = true;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetVec2ByIDFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Particle Reader")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIDDef(), TEXT("Particle ID")));

		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Valid")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec2Def(), TEXT("Value")));

		Sig.FunctionSpecifiers.Add(FName("Attribute"));

		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.bExperimental = true;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetVec3ByIDFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Particle Reader")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIDDef(), TEXT("Particle ID")));

		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Valid")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Value")));

		Sig.FunctionSpecifiers.Add(FName("Attribute"));

		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.bExperimental = true;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetVec4ByIDFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Particle Reader")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIDDef(), TEXT("Particle ID")));

		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Valid")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec4Def(), TEXT("Value")));

		Sig.FunctionSpecifiers.Add(FName("Attribute"));

		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.bExperimental = true;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetColorByIDFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Particle Reader")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIDDef(), TEXT("Particle ID")));

		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Valid")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetColorDef(), TEXT("Value")));

		Sig.FunctionSpecifiers.Add(FName("Attribute"));

		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.bExperimental = true;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetQuatByIDFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Particle Reader")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIDDef(), TEXT("Particle ID")));

		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Valid")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetQuatDef(), TEXT("Value")));

		Sig.FunctionSpecifiers.Add(FName("Attribute"));

		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.bExperimental = true;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetIDByIDFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Particle Reader")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIDDef(), TEXT("Particle ID")));

		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Valid")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIDDef(), TEXT("Value")));

		Sig.FunctionSpecifiers.Add(FName("Attribute"));

		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.bExperimental = true;
		OutFunctions.Add(Sig);
	}

	//
	// Get attribute by index
	//
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetIntByIndexFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Particle Reader")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Particle Index")));

		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Valid")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Value")));

		Sig.FunctionSpecifiers.Add(FName("Attribute"));

		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.bExperimental = true;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetBoolByIndexFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Particle Reader")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Particle Index")));

		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Valid")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Value")));

		Sig.FunctionSpecifiers.Add(FName("Attribute"));

		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.bExperimental = true;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetFloatByIndexFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Particle Reader")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Particle Index")));

		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Valid")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Value")));

		Sig.FunctionSpecifiers.Add(FName("Attribute"));

		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.bExperimental = true;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetVec2ByIndexFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Particle Reader")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Particle Index")));

		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Valid")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec2Def(), TEXT("Value")));

		Sig.FunctionSpecifiers.Add(FName("Attribute"));

		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.bExperimental = true;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetVec3ByIndexFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Particle Reader")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Particle Index")));

		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Valid")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Value")));

		Sig.FunctionSpecifiers.Add(FName("Attribute"));

		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.bExperimental = true;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetVec4ByIndexFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Particle Reader")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Particle Index")));

		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Valid")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec4Def(), TEXT("Value")));

		Sig.FunctionSpecifiers.Add(FName("Attribute"));

		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.bExperimental = true;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetColorByIndexFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Particle Reader")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Particle Index")));

		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Valid")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetColorDef(), TEXT("Value")));

		Sig.FunctionSpecifiers.Add(FName("Attribute"));

		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.bExperimental = true;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetQuatByIndexFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Particle Reader")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Particle Index")));

		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Valid")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetQuatDef(), TEXT("Value")));

		Sig.FunctionSpecifiers.Add(FName("Attribute"));

		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.bExperimental = true;
		OutFunctions.Add(Sig);
	} 

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetIDByIndexFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Particle Reader")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Particle Index")));

		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Valid")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIDDef(), TEXT("Value")));

		Sig.FunctionSpecifiers.Add(FName("Attribute"));

		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.bExperimental = true;
		OutFunctions.Add(Sig);
	}
}

DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceParticleRead, GetNumSpawnedParticles);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceParticleRead, GetSpawnedIDAtIndex);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceParticleRead, GetNumParticles);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceParticleRead, GetParticleIndex);
DEFINE_NDI_DIRECT_FUNC_BINDER_WITH_PAYLOAD(UNiagaraDataInterfaceParticleRead, ReadInt);
DEFINE_NDI_DIRECT_FUNC_BINDER_WITH_PAYLOAD(UNiagaraDataInterfaceParticleRead, ReadBool);
DEFINE_NDI_DIRECT_FUNC_BINDER_WITH_PAYLOAD(UNiagaraDataInterfaceParticleRead, ReadFloat);
DEFINE_NDI_DIRECT_FUNC_BINDER_WITH_PAYLOAD(UNiagaraDataInterfaceParticleRead, ReadVector2);
DEFINE_NDI_DIRECT_FUNC_BINDER_WITH_PAYLOAD(UNiagaraDataInterfaceParticleRead, ReadVector3);
DEFINE_NDI_DIRECT_FUNC_BINDER_WITH_PAYLOAD(UNiagaraDataInterfaceParticleRead, ReadVector4);
DEFINE_NDI_DIRECT_FUNC_BINDER_WITH_PAYLOAD(UNiagaraDataInterfaceParticleRead, ReadColor);
DEFINE_NDI_DIRECT_FUNC_BINDER_WITH_PAYLOAD(UNiagaraDataInterfaceParticleRead, ReadQuat);
DEFINE_NDI_DIRECT_FUNC_BINDER_WITH_PAYLOAD(UNiagaraDataInterfaceParticleRead, ReadID);
DEFINE_NDI_DIRECT_FUNC_BINDER_WITH_PAYLOAD(UNiagaraDataInterfaceParticleRead, ReadIntByIndex);
DEFINE_NDI_DIRECT_FUNC_BINDER_WITH_PAYLOAD(UNiagaraDataInterfaceParticleRead, ReadBoolByIndex);
DEFINE_NDI_DIRECT_FUNC_BINDER_WITH_PAYLOAD(UNiagaraDataInterfaceParticleRead, ReadFloatByIndex);
DEFINE_NDI_DIRECT_FUNC_BINDER_WITH_PAYLOAD(UNiagaraDataInterfaceParticleRead, ReadVector2ByIndex);
DEFINE_NDI_DIRECT_FUNC_BINDER_WITH_PAYLOAD(UNiagaraDataInterfaceParticleRead, ReadVector3ByIndex);
DEFINE_NDI_DIRECT_FUNC_BINDER_WITH_PAYLOAD(UNiagaraDataInterfaceParticleRead, ReadVector4ByIndex);
DEFINE_NDI_DIRECT_FUNC_BINDER_WITH_PAYLOAD(UNiagaraDataInterfaceParticleRead, ReadColorByIndex);
DEFINE_NDI_DIRECT_FUNC_BINDER_WITH_PAYLOAD(UNiagaraDataInterfaceParticleRead, ReadQuatByIndex);
DEFINE_NDI_DIRECT_FUNC_BINDER_WITH_PAYLOAD(UNiagaraDataInterfaceParticleRead, ReadIDByIndex);

static bool HasMatchingVariable(TArrayView<const FNiagaraVariable> Variables, FName AttributeName, const FNiagaraTypeDefinition& ValidType)
{
	return Variables.Find(FNiagaraVariable(ValidType, AttributeName)) != INDEX_NONE;
}

void UNiagaraDataInterfaceParticleRead::GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction& OutFunc)
{
	//
	// Spawn info and particle count
	//
	if (BindingInfo.Name == GetNumSpawnedParticlesFunctionName)
	{
		NDI_FUNC_BINDER(UNiagaraDataInterfaceParticleRead, GetNumSpawnedParticles)::Bind(this, OutFunc);
		return;
	}

	if (BindingInfo.Name == GetSpawnedIDAtIndexFunctionName)
	{
		NDI_FUNC_BINDER(UNiagaraDataInterfaceParticleRead, GetSpawnedIDAtIndex)::Bind(this, OutFunc);
		return;
	}

	if (BindingInfo.Name == GetNumParticlesFunctionName)
	{
		NDI_FUNC_BINDER(UNiagaraDataInterfaceParticleRead, GetNumParticles)::Bind(this, OutFunc);
		return;
	}

	if (BindingInfo.Name == GetParticleIndexFunctionName)
	{
		NDI_FUNC_BINDER(UNiagaraDataInterfaceParticleRead, GetParticleIndex)::Bind(this, OutFunc);
		return;
	}

	bool bBindSuccessful = false;
	FNDIParticleRead_InstanceData* PIData = static_cast<FNDIParticleRead_InstanceData*>(InstanceData);
	static const FName NAME_Attribute("Attribute");

	const FVMFunctionSpecifier* FunctionSpecifier = BindingInfo.FindSpecifier(NAME_Attribute);
	if (FunctionSpecifier == nullptr)
	{
		UE_LOG(LogNiagara, Error, TEXT("VMExternalFunction '%s' does not have a function specifier 'attribute'!"), *BindingInfo.Name.ToString());
		return;
	}

	TArrayView<const FNiagaraVariable> EmitterVariables = PIData->EmitterInstance->GetData().GetVariables();

	const FName AttributeToRead = FunctionSpecifier->Value;

	//
	// Get attribute by ID
	//
	if (BindingInfo.Name == GetIntByIDFunctionName)
	{
		if (HasMatchingVariable(EmitterVariables, AttributeToRead, FNiagaraTypeDefinition::GetIntDef()))
		{
			NDI_FUNC_BINDER(UNiagaraDataInterfaceParticleRead, ReadInt)::Bind(this, OutFunc, AttributeToRead);
			bBindSuccessful = true;
		}
	}
	else if (BindingInfo.Name == GetBoolByIDFunctionName)
	{
		if (HasMatchingVariable(EmitterVariables, AttributeToRead, FNiagaraTypeDefinition::GetBoolDef()))
		{
			NDI_FUNC_BINDER(UNiagaraDataInterfaceParticleRead, ReadBool)::Bind(this, OutFunc, AttributeToRead);
			bBindSuccessful = true;
		}
	}
	else if (BindingInfo.Name == GetFloatByIDFunctionName)
	{
		if (HasMatchingVariable(EmitterVariables, AttributeToRead, FNiagaraTypeDefinition::GetFloatDef())
			|| HasMatchingVariable(EmitterVariables, AttributeToRead, FNiagaraTypeDefinition::GetHalfDef()))
		{
			NDI_FUNC_BINDER(UNiagaraDataInterfaceParticleRead, ReadFloat)::Bind(this, OutFunc, AttributeToRead);
			bBindSuccessful = true;
		}
	}
	else if (BindingInfo.Name == GetVec2ByIDFunctionName)
	{
		if (HasMatchingVariable(EmitterVariables, AttributeToRead, FNiagaraTypeDefinition::GetVec2Def())
			|| HasMatchingVariable(EmitterVariables, AttributeToRead, FNiagaraTypeDefinition::GetHalfVec2Def()))
		{
			NDI_FUNC_BINDER(UNiagaraDataInterfaceParticleRead, ReadVector2)::Bind(this, OutFunc, AttributeToRead);
			bBindSuccessful = true;
		}
	}
	else if (BindingInfo.Name == GetVec3ByIDFunctionName)
	{
		if (HasMatchingVariable(EmitterVariables, AttributeToRead, FNiagaraTypeDefinition::GetVec3Def())
			|| HasMatchingVariable(EmitterVariables, AttributeToRead, FNiagaraTypeDefinition::GetHalfVec3Def()))
		{
			NDI_FUNC_BINDER(UNiagaraDataInterfaceParticleRead, ReadVector3)::Bind(this, OutFunc, AttributeToRead);
			bBindSuccessful = true;
		}
	}
	else if (BindingInfo.Name == GetVec4ByIDFunctionName)
	{
		if (HasMatchingVariable(EmitterVariables, AttributeToRead, FNiagaraTypeDefinition::GetVec4Def())
			|| HasMatchingVariable(EmitterVariables, AttributeToRead, FNiagaraTypeDefinition::GetHalfVec4Def()))
		{
			NDI_FUNC_BINDER(UNiagaraDataInterfaceParticleRead, ReadVector4)::Bind(this, OutFunc, AttributeToRead);
			bBindSuccessful = true;
		}
	}
	else if (BindingInfo.Name == GetColorByIDFunctionName)
	{
		if (HasMatchingVariable(EmitterVariables, AttributeToRead, FNiagaraTypeDefinition::GetColorDef())
			|| HasMatchingVariable(EmitterVariables, AttributeToRead, FNiagaraTypeDefinition::GetHalfVec4Def()))
		{
			NDI_FUNC_BINDER(UNiagaraDataInterfaceParticleRead, ReadColor)::Bind(this, OutFunc, AttributeToRead);
			bBindSuccessful = true;
		}
	}
	else if (BindingInfo.Name == GetQuatByIDFunctionName)
	{
		if (HasMatchingVariable(EmitterVariables, AttributeToRead, FNiagaraTypeDefinition::GetQuatDef())
			|| HasMatchingVariable(EmitterVariables, AttributeToRead, FNiagaraTypeDefinition::GetHalfVec4Def()))
		{
			NDI_FUNC_BINDER(UNiagaraDataInterfaceParticleRead, ReadQuat)::Bind(this, OutFunc, AttributeToRead);
			bBindSuccessful = true;
		}
	}
	else if (BindingInfo.Name == GetIDByIDFunctionName)
	{
		FNiagaraVariable VariableToRead(FNiagaraTypeDefinition::GetIDDef(), AttributeToRead);
		if (PIData->EmitterInstance->GetData().GetVariables().Find(VariableToRead) != INDEX_NONE)
		{
			NDI_FUNC_BINDER(UNiagaraDataInterfaceParticleRead, ReadID)::Bind(this, OutFunc, AttributeToRead);
			bBindSuccessful = true;
		}
	}
	//
	// Get attribute by index
	//
	else if (BindingInfo.Name == GetIntByIndexFunctionName)
	{
		if (HasMatchingVariable(EmitterVariables, AttributeToRead, FNiagaraTypeDefinition::GetIntDef()))
		{
			NDI_FUNC_BINDER(UNiagaraDataInterfaceParticleRead, ReadIntByIndex)::Bind(this, OutFunc, AttributeToRead);
			bBindSuccessful = true;
		}
	}
	else if (BindingInfo.Name == GetBoolByIndexFunctionName)
	{
		if (HasMatchingVariable(EmitterVariables, AttributeToRead, FNiagaraTypeDefinition::GetBoolDef()))
		{
			NDI_FUNC_BINDER(UNiagaraDataInterfaceParticleRead, ReadBoolByIndex)::Bind(this, OutFunc, AttributeToRead);
			bBindSuccessful = true;
		}
	}
	else if (BindingInfo.Name == GetFloatByIndexFunctionName)
	{
		if (HasMatchingVariable(EmitterVariables, AttributeToRead, FNiagaraTypeDefinition::GetFloatDef())
			|| HasMatchingVariable(EmitterVariables, AttributeToRead, FNiagaraTypeDefinition::GetHalfDef()))
		{
			NDI_FUNC_BINDER(UNiagaraDataInterfaceParticleRead, ReadFloatByIndex)::Bind(this, OutFunc, AttributeToRead);
			bBindSuccessful = true;
		}
	}
	else if (BindingInfo.Name == GetVec2ByIndexFunctionName)
	{
		if (HasMatchingVariable(EmitterVariables, AttributeToRead, FNiagaraTypeDefinition::GetVec2Def())
			|| HasMatchingVariable(EmitterVariables, AttributeToRead, FNiagaraTypeDefinition::GetHalfVec2Def()))
		{
			NDI_FUNC_BINDER(UNiagaraDataInterfaceParticleRead, ReadVector2ByIndex)::Bind(this, OutFunc, AttributeToRead);
			bBindSuccessful = true;
		}
	}
	else if (BindingInfo.Name == GetVec3ByIndexFunctionName)
	{
		if (HasMatchingVariable(EmitterVariables, AttributeToRead, FNiagaraTypeDefinition::GetVec3Def())
			|| HasMatchingVariable(EmitterVariables, AttributeToRead, FNiagaraTypeDefinition::GetHalfVec3Def()))
		{
			NDI_FUNC_BINDER(UNiagaraDataInterfaceParticleRead, ReadVector3ByIndex)::Bind(this, OutFunc, AttributeToRead);
			bBindSuccessful = true;
		}
	}
	else if (BindingInfo.Name == GetVec4ByIndexFunctionName)
	{
		if (HasMatchingVariable(EmitterVariables, AttributeToRead, FNiagaraTypeDefinition::GetVec4Def())
			|| HasMatchingVariable(EmitterVariables, AttributeToRead, FNiagaraTypeDefinition::GetHalfVec4Def()))
		{
			NDI_FUNC_BINDER(UNiagaraDataInterfaceParticleRead, ReadVector4ByIndex)::Bind(this, OutFunc, AttributeToRead);
			bBindSuccessful = true;
		}
	}
	else if (BindingInfo.Name == GetColorByIndexFunctionName)
	{
		if (HasMatchingVariable(EmitterVariables, AttributeToRead, FNiagaraTypeDefinition::GetColorDef())
			|| HasMatchingVariable(EmitterVariables, AttributeToRead, FNiagaraTypeDefinition::GetHalfVec4Def()))
		{
			NDI_FUNC_BINDER(UNiagaraDataInterfaceParticleRead, ReadColorByIndex)::Bind(this, OutFunc, AttributeToRead);
			bBindSuccessful = true;
		}
	}
	else if (BindingInfo.Name == GetQuatByIndexFunctionName)
	{
		if (HasMatchingVariable(EmitterVariables, AttributeToRead, FNiagaraTypeDefinition::GetQuatDef())
			|| HasMatchingVariable(EmitterVariables, AttributeToRead, FNiagaraTypeDefinition::GetHalfVec4Def()))
		{
			NDI_FUNC_BINDER(UNiagaraDataInterfaceParticleRead, ReadQuatByIndex)::Bind(this, OutFunc, AttributeToRead);
			bBindSuccessful = true;
		}
	}
	else if (BindingInfo.Name == GetIDByIndexFunctionName)
	{
	FNiagaraVariable VariableToRead(FNiagaraTypeDefinition::GetIDDef(), AttributeToRead);
	if (PIData->EmitterInstance->GetData().GetVariables().Find(VariableToRead) != INDEX_NONE)
	{
		NDI_FUNC_BINDER(UNiagaraDataInterfaceParticleRead, ReadIDByIndex)::Bind(this, OutFunc, AttributeToRead);
		bBindSuccessful = true;
	}
	}

	if (!bBindSuccessful)
	{
		UE_LOG(LogNiagara, Error, TEXT("Failed to bind VMExternalFunction '%s' with attribute '%s'! Check that the attribute is named correctly."), *BindingInfo.Name.ToString(), *AttributeToRead.ToString());
	}
}

void UNiagaraDataInterfaceParticleRead::GetNumSpawnedParticles(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIParticleRead_InstanceData> InstData(Context);
	VectorVM::FExternalFuncRegisterHandler<int32> OutNumSpawned(Context);

	const FNiagaraEmitterInstance* EmitterInstance = InstData.Get()->EmitterInstance;
	const FNiagaraDataBuffer* CurrentData = EmitterInstance->GetData().GetCurrentData();
	const int32 NumSpawned = CurrentData ? CurrentData->GetNumSpawnedInstances() : 0;

	for (int32 InstanceIdx = 0; InstanceIdx < Context.NumInstances; ++InstanceIdx)
	{
		*OutNumSpawned.GetDestAndAdvance() = NumSpawned;
	}
}

void UNiagaraDataInterfaceParticleRead::GetSpawnedIDAtIndex(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIParticleRead_InstanceData> InstData(Context);
	VectorVM::FExternalFuncInputHandler<int32> InIndex(Context);

	VectorVM::FExternalFuncRegisterHandler<FNiagaraBool> OutValid(Context);
	VectorVM::FExternalFuncRegisterHandler<int32> OutIDIndex(Context);
	VectorVM::FExternalFuncRegisterHandler<int32> OutIDAcquireTag(Context);

	FNiagaraEmitterInstance* EmitterInstance = InstData.Get()->EmitterInstance;
	const TArray<int32>& SpawnedIDsTable = EmitterInstance->GetData().GetSpawnedIDsTable();
	int32 NumSpawned = SpawnedIDsTable.Num();
	int32 IDAcquireTag = EmitterInstance->GetData().GetIDAcquireTag();

	for (int32 InstanceIdx = 0; InstanceIdx < Context.NumInstances; ++InstanceIdx)
	{
		FNiagaraBool ValidValue;
		FNiagaraID IDValue;

		int32 SpawnIndex = InIndex.GetAndAdvance();
		if (SpawnIndex >= 0 && SpawnIndex < NumSpawned)
		{
			ValidValue.SetValue(true);
			IDValue.Index = SpawnedIDsTable[SpawnIndex];
			IDValue.AcquireTag = IDAcquireTag;
		}
		else
		{
			ValidValue.SetValue(false);
			IDValue.Index = 0;
			IDValue.AcquireTag = 0;
		}

		*OutValid.GetDestAndAdvance() = ValidValue;
		*OutIDIndex.GetDestAndAdvance() = IDValue.Index;
		*OutIDAcquireTag.GetDestAndAdvance() = IDValue.AcquireTag;
	}
}

void UNiagaraDataInterfaceParticleRead::GetNumParticles(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIParticleRead_InstanceData> InstData(Context);
	VectorVM::FExternalFuncRegisterHandler<int32> OutNumParticles (Context);

	const FNiagaraEmitterInstance* EmitterInstance = InstData.Get()->EmitterInstance;
	const FNiagaraDataBuffer* CurrentData = EmitterInstance->GetData().GetCurrentData();
	const int32 NumParticles = CurrentData ? CurrentData->GetNumInstances() : 0;

	for (int32 InstanceIdx = 0; InstanceIdx < Context.NumInstances; ++InstanceIdx)
	{
		*OutNumParticles.GetDestAndAdvance() = NumParticles;
	}
}

void UNiagaraDataInterfaceParticleRead::GetParticleIndex(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIParticleRead_InstanceData> InstData(Context);
	VectorVM::FExternalFuncInputHandler<int32> ParticleIDIndexParam(Context);
	VectorVM::FExternalFuncInputHandler<int32> ParticleIDAcquireTagParam(Context);
	VectorVM::FExternalFuncRegisterHandler<int32> OutIndex(Context);

	const FNiagaraEmitterInstance* EmitterInstance = InstData.Get()->EmitterInstance;
	const FNiagaraDataBuffer* CurrentData = EmitterInstance->GetData().GetCurrentData();

	if (!CurrentData)
	{
		for (int32 InstanceIdx = 0; InstanceIdx < Context.NumInstances; ++InstanceIdx)
		{
			*OutIndex.GetDestAndAdvance() = -1;
		}
		return;
	}

	FNiagaraVariable IDVar(FNiagaraTypeDefinition::GetIDDef(), "ID");
	FNiagaraDataSetAccessor<FNiagaraID> IDData(EmitterInstance->GetData(), IDVar);
	const TArray<int32>& IDTable = CurrentData->GetIDTable();

	for (int32 InstanceIdx = 0; InstanceIdx < Context.NumInstances; ++InstanceIdx)
	{
		FNiagaraID ParticleID = { ParticleIDIndexParam.GetAndAdvance(), ParticleIDAcquireTagParam.GetAndAdvance() };

		int32 ParticleIndex = -1;
		if (ParticleID.Index >= 0 && ParticleID.Index < IDTable.Num())
		{
			ParticleIndex = IDTable[ParticleID.Index];
			if (ParticleIndex >= 0)
			{
				FNiagaraID ActualID = IDData.GetSafe(ParticleIndex, NIAGARA_INVALID_ID);
				if (ActualID != ParticleID)
				{
					ParticleIndex = -1;
				}
			}
		}

		*OutIndex.GetDestAndAdvance() = ParticleIndex;
	}
}

//////////////////////////////////////////////////////////////////////////

template<typename T>
struct FDirectReadParamHandler
{
	FDirectReadParamHandler(FVectorVMContext& Context)
		: InstanceData(Context)
		, ParticleIDIndexParam(Context)
		, ParticleIDAcquireTagParam(Context)
		, OutValid(Context)
		, OutValue(Context)
	{
	}

	FORCEINLINE FNiagaraEmitterInstance* GetEmitterInstance() { return InstanceData->EmitterInstance; }
	FORCEINLINE FNiagaraSystemInstance* GetSysteInstance() { return InstanceData->SystemInstance; }

	FORCEINLINE FNiagaraID GetID() { return FNiagaraID(ParticleIDIndexParam.GetAndAdvance(), ParticleIDAcquireTagParam.GetAndAdvance()); }
	FORCEINLINE void SetValid(bool bValid) { OutValid.SetAndAdvance(FNiagaraBool(bValid)); }
	FORCEINLINE void SetValue(T Val) { OutValue.SetAndAdvance(Val); }

	VectorVM::FUserPtrHandler<FNDIParticleRead_InstanceData> InstanceData;
	VectorVM::FExternalFuncInputHandler<int32> ParticleIDIndexParam;
	VectorVM::FExternalFuncInputHandler<int32> ParticleIDAcquireTagParam;
	FNDIOutputParam<FNiagaraBool> OutValid;
	FNDIOutputParam<T> OutValue;
};

template <typename T>
FORCEINLINE void ReadWithCheck(FVectorVMContext& Context, FName AttributeToRead, T Default)
{
	using AccessorType = FNiagaraDataConversions<T>;//TODO: Route template calls in here so we can use a regular accessor rather than a converatable accessor.
	FDirectReadParamHandler<T> Params(Context);
	if (FNiagaraEmitterInstance* EmitterInstance = Params.GetEmitterInstance())
	{
		const FNiagaraDataBuffer* CurrentData = EmitterInstance->GetData().GetCurrentData();//TODO: We should really be grabbing this in the instance data tick and adding a read ref to it.
		if (CurrentData && EmitterInstance->GetGPUContext() == nullptr)
		{
			static const FNiagaraVariable IDVar(FNiagaraTypeDefinition::GetIDDef(), "ID");
			const TArray<int32>& IDTable = CurrentData->GetIDTable();
			int32 NumSourceInstances = CurrentData->GetNumInstances();

			if (IDTable.Num() > 0)
			{
				FNiagaraVariable ReadVar(FNiagaraTypeDefinition::Get<T>(), AttributeToRead);
				FNiagaraDataSetAccessor<AccessorType> ValueData(EmitterInstance->GetData(), ReadVar);

				FNiagaraDataSetAccessor<FNiagaraID> IDData(EmitterInstance->GetData(), IDVar);

				for (int32 InstanceIdx = 0; InstanceIdx < Context.NumInstances; ++InstanceIdx)
				{
					FNiagaraID ParticleID = Params.GetID();
					bool bValid = false;
					T Value = Default;

					if (ParticleID.Index >= 0 && ParticleID.Index < IDTable.Num())
					{
						int32 ParticleIndex = IDTable[ParticleID.Index];
						if (ParticleIndex >= 0 && ParticleIndex < NumSourceInstances)
						{
							FNiagaraID ActualID = IDData.GetSafe(ParticleIndex, NIAGARA_INVALID_ID);
							if (ActualID == ParticleID)
							{
								Value = ValueData.GetSafe(ParticleIndex, T());
								bValid = true;
							}
						}
					}

					Params.SetValid(bValid);
					Params.SetValue(Value);
				}
			}

			return;
		}
	}

	//If we get there there's been a problem so just fill the output with default;
	for (int32 InstanceIdx = 0; InstanceIdx < Context.NumInstances; ++InstanceIdx)
	{
		Params.SetValid(false);
		Params.SetValue(Default);
	}
}

//TODO: We can remove all this boiler plate and bind directly to ReadWithCheck.
void UNiagaraDataInterfaceParticleRead::ReadInt(FVectorVMContext& Context, FName AttributeToRead)
{
	ReadWithCheck<int32>(Context, AttributeToRead, 0);
}

void UNiagaraDataInterfaceParticleRead::ReadBool(FVectorVMContext& Context, FName AttributeToRead)
{
	ReadWithCheck<FNiagaraBool>(Context, AttributeToRead, FNiagaraBool(false));
}

void UNiagaraDataInterfaceParticleRead::ReadFloat(FVectorVMContext& Context, FName AttributeToRead)
{
	ReadWithCheck<float>(Context, AttributeToRead, 0.0f);
}

void UNiagaraDataInterfaceParticleRead::ReadVector2(FVectorVMContext& Context, FName AttributeToRead)
{
	ReadWithCheck<FVector2D>(Context, AttributeToRead, FVector2D::ZeroVector);
}

void UNiagaraDataInterfaceParticleRead::ReadVector3(FVectorVMContext& Context, FName AttributeToRead)
{
	ReadWithCheck<FVector>(Context, AttributeToRead, FVector::ZeroVector);
}

void UNiagaraDataInterfaceParticleRead::ReadVector4(FVectorVMContext& Context, FName AttributeToRead)
{
	ReadWithCheck<FVector4>(Context, AttributeToRead, FVector4(ForceInit));
}

void UNiagaraDataInterfaceParticleRead::ReadColor(FVectorVMContext& Context, FName AttributeToRead)
{
	ReadWithCheck<FLinearColor>(Context, AttributeToRead, FLinearColor::White);
}

void UNiagaraDataInterfaceParticleRead::ReadQuat(FVectorVMContext& Context, FName AttributeToRead)
{
	ReadWithCheck<FQuat>(Context, AttributeToRead, FQuat::Identity);
}

void UNiagaraDataInterfaceParticleRead::ReadID(FVectorVMContext& Context, FName AttributeToRead)
{
	ReadWithCheck<FNiagaraID>(Context, AttributeToRead, FNiagaraID());
}

//////////////////////////////////////////////////////////////////////////

template<typename T>
struct FDirectReadByIndexParamHandler
{
	FDirectReadByIndexParamHandler(FVectorVMContext& Context)
		: InstanceData(Context)
		, ParticleIndexParam(Context)
		, OutValid(Context)
		, OutValue(Context)
	{
	}

	FORCEINLINE FNiagaraEmitterInstance* GetEmitterInstance() { return InstanceData->EmitterInstance; }
	FORCEINLINE FNiagaraSystemInstance* GetSysteInstance() { return InstanceData->SystemInstance; }

	FORCEINLINE int32 GetIndex() { return ParticleIndexParam.GetAndAdvance(); }
	FORCEINLINE void SetValid(bool bValid) { OutValid.SetAndAdvance(FNiagaraBool(bValid)); }
	FORCEINLINE void SetValue(T Val) { OutValue.SetAndAdvance(Val); }

	VectorVM::FUserPtrHandler<FNDIParticleRead_InstanceData> InstanceData;
	VectorVM::FExternalFuncInputHandler<int32> ParticleIndexParam;
	FNDIOutputParam<FNiagaraBool> OutValid;
	FNDIOutputParam<T> OutValue;
};

template<typename T>
FORCEINLINE void ReadByIndexWithCheck(FVectorVMContext& Context, FName AttributeToRead, T Default)
{
	using AccessorType = FNiagaraDataConversions<T>;//TODO: Route template calls in here so we can use a regular accessor rather than a converatable accessor.
	FDirectReadByIndexParamHandler<T> Params(Context);

	if (FNiagaraEmitterInstance* EmitterInstance = Params.GetEmitterInstance())
	{
		const FNiagaraDataBuffer* CurrentData = EmitterInstance->GetData().GetCurrentData();//TODO: We should really be grabbing these during instance data tick and adding a read ref. Releasing that on PostTick.
		if (CurrentData && CurrentData->GetNumInstances() > 0 && EmitterInstance->GetGPUContext() == nullptr)
		{
			static const FNiagaraVariable IDVar(FNiagaraTypeDefinition::GetIDDef(), "ID");
			const TArray<int32>& IDTable = CurrentData->GetIDTable();
			int32 NumSourceInstances = (int32)CurrentData->GetNumInstances();

			FNiagaraVariable ReadVar(FNiagaraTypeDefinition::Get<T>(), AttributeToRead);
			FNiagaraDataSetAccessor<AccessorType> ValueData(EmitterInstance->GetData(), ReadVar);

			FNiagaraDataSetAccessor<FNiagaraID> IDData(EmitterInstance->GetData(), IDVar);

			for (int32 InstanceIdx = 0; InstanceIdx < Context.NumInstances; ++InstanceIdx)
			{
				int32 ParticleIndex = Params.GetIndex();

				T Value = Default;
				bool bValid = false;
				if (ParticleIndex >= 0 && ParticleIndex < NumSourceInstances)
				{
					Value = ValueData.GetSafe(ParticleIndex, Default);
					bValid = true;					
				}

				Params.SetValid(bValid);
				Params.SetValue(Value);
			}

			return;
		}
	}

	//If we get there there's been a problem so just fill the output with default;
	for (int32 InstanceIdx = 0; InstanceIdx < Context.NumInstances; ++InstanceIdx)
	{
		Params.SetValid(false);
		Params.SetValue(Default);
	}
}

//TODO: We can remove all this boiler plate and bind directly to ReadByIndexWithCheck.
void UNiagaraDataInterfaceParticleRead::ReadIntByIndex(FVectorVMContext& Context, FName AttributeToRead)
{
	ReadByIndexWithCheck<int32>(Context, AttributeToRead, 0);
}

void UNiagaraDataInterfaceParticleRead::ReadBoolByIndex(FVectorVMContext& Context, FName AttributeToRead)
{
	ReadByIndexWithCheck<FNiagaraBool>(Context, AttributeToRead, FNiagaraBool(false));
}

void UNiagaraDataInterfaceParticleRead::ReadFloatByIndex(FVectorVMContext& Context, FName AttributeToRead)
{
	ReadByIndexWithCheck<float>(Context, AttributeToRead, 0.0f);
}

void UNiagaraDataInterfaceParticleRead::ReadVector2ByIndex(FVectorVMContext& Context, FName AttributeToRead)
{
	ReadByIndexWithCheck<FVector2D>(Context, AttributeToRead, FVector2D::ZeroVector);
}

void UNiagaraDataInterfaceParticleRead::ReadVector3ByIndex(FVectorVMContext& Context, FName AttributeToRead)
{
	ReadByIndexWithCheck<FVector>(Context, AttributeToRead, FVector::ZeroVector);
}

void UNiagaraDataInterfaceParticleRead::ReadVector4ByIndex(FVectorVMContext& Context, FName AttributeToRead)
{
	ReadByIndexWithCheck<FVector4>(Context, AttributeToRead, FVector4(ForceInit));
}

void UNiagaraDataInterfaceParticleRead::ReadColorByIndex(FVectorVMContext& Context, FName AttributeToRead)
{
	ReadByIndexWithCheck<FLinearColor>(Context, AttributeToRead, FLinearColor::White);
}

void UNiagaraDataInterfaceParticleRead::ReadQuatByIndex(FVectorVMContext& Context, FName AttributeToRead)
{
	ReadByIndexWithCheck<FQuat>(Context, AttributeToRead, FQuat::Identity);
}

void UNiagaraDataInterfaceParticleRead::ReadIDByIndex(FVectorVMContext& Context, FName AttributeToRead)
{
	ReadByIndexWithCheck<FNiagaraID>(Context, AttributeToRead, FNiagaraID());
}

bool UNiagaraDataInterfaceParticleRead::Equals(const UNiagaraDataInterface* Other) const
{
	if (!Super::Equals(Other))
	{
		return false;
	}
	return CastChecked<UNiagaraDataInterfaceParticleRead>(Other)->EmitterName == EmitterName;
}

bool UNiagaraDataInterfaceParticleRead::CopyToInternal(UNiagaraDataInterface* Destination) const
{
	if (!Super::CopyToInternal(Destination))
	{
		return false;
	}
	CastChecked<UNiagaraDataInterfaceParticleRead>(Destination)->EmitterName = EmitterName;
	return true;
}

void UNiagaraDataInterfaceParticleRead::GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL)
{
	static const TCHAR *FormatDeclarations = TEXT(
		"int {NumSpawnedParticlesName};\n"
		"int {SpawnedParticlesAcquireTagName};\n"
		"uint {InstanceCountOffsetName};\n"
		"uint {ParticleStrideFloatName};\n"
		"uint {ParticleStrideIntName};\n"
		"uint {ParticleStrideHalfName};\n"
		"int {AcquireTagRegisterIndexName};\n"
		"Buffer<int> {SpawnedIDsBufferName};\n"
		"Buffer<int> {IDToIndexTableName};\n"
		"Buffer<float> {InputFloatBufferName};\n"
		"Buffer<int> {InputIntBufferName};\n"
		"Buffer<float> {InputHalfBufferName};\n"
		"int4 {AttributeIndicesName}[{AttributeInt4Count}];\n"
		"int4 {AttributeCompressedName}[{AttributeInt4Count}];\n\n"
	);

	// If we use an int array for the attribute indices, the shader compiler will actually use int4 due to the packing rules,
	// and leave 3 elements unused. Besides being wasteful, this means that the array we send to the CS would need to be padded,
	// which is a hassle. Instead, use int4 explicitly, and access individual components in the generated code.
	// Note that we have to have at least one here because hlsl doesn't support arrays of size 0.
	const int AttributeInt4Count = FMath::Max(1, FMath::DivideAndRoundUp(ParamInfo.GeneratedFunctions.Num(), 4));

	TMap<FString, FStringFormatArg> ArgsDeclarations;
	ArgsDeclarations.Add(TEXT("NumSpawnedParticlesName"), NumSpawnedParticlesBaseName + ParamInfo.DataInterfaceHLSLSymbol);
	ArgsDeclarations.Add(TEXT("SpawnedParticlesAcquireTagName"), SpawnedParticlesAcquireTagBaseName + ParamInfo.DataInterfaceHLSLSymbol);
	ArgsDeclarations.Add(TEXT("InstanceCountOffsetName"), InstanceCountOffsetBaseName + ParamInfo.DataInterfaceHLSLSymbol);
	ArgsDeclarations.Add(TEXT("ParticleStrideFloatName"), ParticleStrideFloatBaseName + ParamInfo.DataInterfaceHLSLSymbol);
	ArgsDeclarations.Add(TEXT("ParticleStrideIntName"), ParticleStrideIntBaseName + ParamInfo.DataInterfaceHLSLSymbol);
	ArgsDeclarations.Add(TEXT("ParticleStrideHalfName"), ParticleStrideHalfBaseName + ParamInfo.DataInterfaceHLSLSymbol);
	ArgsDeclarations.Add(TEXT("AcquireTagRegisterIndexName"), AcquireTagRegisterIndexBaseName + ParamInfo.DataInterfaceHLSLSymbol);
	ArgsDeclarations.Add(TEXT("SpawnedIDsBufferName"), SpawnedIDsBufferBaseName + ParamInfo.DataInterfaceHLSLSymbol);
	ArgsDeclarations.Add(TEXT("IDToIndexTableName"), IDToIndexTableBaseName + ParamInfo.DataInterfaceHLSLSymbol);
	ArgsDeclarations.Add(TEXT("InputFloatBufferName"), InputFloatBufferBaseName + ParamInfo.DataInterfaceHLSLSymbol);
	ArgsDeclarations.Add(TEXT("InputIntBufferName"), InputIntBufferBaseName + ParamInfo.DataInterfaceHLSLSymbol);
	ArgsDeclarations.Add(TEXT("InputHalfBufferName"), InputHalfBufferBaseName + ParamInfo.DataInterfaceHLSLSymbol);
	ArgsDeclarations.Add(TEXT("AttributeIndicesName"), AttributeIndicesBaseName + ParamInfo.DataInterfaceHLSLSymbol);
	ArgsDeclarations.Add(TEXT("AttributeCompressedName"), AttributeCompressedBaseName + ParamInfo.DataInterfaceHLSLSymbol);
	ArgsDeclarations.Add(TEXT("AttributeInt4Count"), AttributeInt4Count);

	OutHLSL += FString::Format(FormatDeclarations, ArgsDeclarations);
}

static FString GenerateFetchValueHLSL(int NumComponents, const TCHAR* ComponentNames[], const TCHAR* ComponentTypeName, const FString& InputBufferName, const FString& ParticleStrideName, bool bExtraIndent)
{
	FString Code;

	for (int ComponentIndex = 0; ComponentIndex < NumComponents; ++ComponentIndex)
	{
		const TCHAR* ComponentName = (NumComponents > 1) ? ComponentNames[ComponentIndex] : TEXT("");
		const FString FetchComponentCode = FString::Printf(TEXT("            Out_Value%s = %s(%s[(RegisterIndex + %d)*%s + ParticleIndex]);\n"), ComponentName, ComponentTypeName, *InputBufferName, ComponentIndex, *ParticleStrideName);
		if (bExtraIndent)
		{
			Code += TEXT("    ");
		}
		Code += FetchComponentCode;
	}

	return Code;
}

static bool GenerateGetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, ENiagaraParticleDataComponentType ComponentType, int NumComponents, bool bByIndex, FString& OutHLSL)
{
	FString FuncTemplate;

	if (bByIndex)
	{
		FuncTemplate = TEXT("void {FunctionName}(int ParticleIndex, out bool Out_Valid, out {ValueType} Out_Value)\n");
	}
	else
	{
		FuncTemplate = TEXT("void {FunctionName}(NiagaraID In_ParticleID, out bool Out_Valid, out {ValueType} Out_Value)\n");
	}

	FuncTemplate += TEXT(
		"{\n"
		"    int RegisterIndex = {AttributeIndicesName}[{AttributeIndexGroup}]{AttributeIndexComponent};\n"
	);

	if (bByIndex)
	{
		FuncTemplate += TEXT(
			"    int NumParticles = {InstanceCountOffsetName} != 0xffffffff ? RWInstanceCounts[{InstanceCountOffsetName}] : 0;\n"
			"    if(RegisterIndex != -1 && ParticleIndex >= 0 && ParticleIndex < NumParticles)\n"
		);
	}
	else
	{
		FuncTemplate += TEXT(
			"    int ParticleIndex = (RegisterIndex != -1) && (In_ParticleID.Index >= 0) ? {IDToIndexTableName}[In_ParticleID.Index] : -1;\n"
			"    int AcquireTag = (ParticleIndex != -1) ? {InputIntBufferName}[{AcquireTagRegisterIndexName}*{ParticleStrideIntName} + ParticleIndex] : 0;\n"
			"    if(ParticleIndex != -1 && In_ParticleID.AcquireTag == AcquireTag)\n"
		);
	}

	static const TCHAR* VectorComponentNames[] = { TEXT(".x"), TEXT(".y"), TEXT(".z"), TEXT(".w") };
	static const TCHAR* IDComponentNames[] = { TEXT(".Index"), TEXT(".AcquireTag") };

	const FString ParticleStrideFloatName = ParticleStrideFloatBaseName + ParamInfo.DataInterfaceHLSLSymbol;
	const FString ParticleStrideIntName = ParticleStrideIntBaseName + ParamInfo.DataInterfaceHLSLSymbol;
	const FString ParticleStrideHalfName = ParticleStrideHalfBaseName + ParamInfo.DataInterfaceHLSLSymbol;
	const FString InputFloatBufferName = InputFloatBufferBaseName + ParamInfo.DataInterfaceHLSLSymbol;
	const FString InputIntBufferName = InputIntBufferBaseName + ParamInfo.DataInterfaceHLSLSymbol;
	const FString InputHalfBufferName = InputHalfBufferBaseName + ParamInfo.DataInterfaceHLSLSymbol;

	const TCHAR* ComponentTypeName;
	FString FetchValueCode;
	if (ComponentType != ENiagaraParticleDataComponentType::Float)
	{
		const TCHAR** ValueComponentNames = VectorComponentNames;
		switch (ComponentType)
		{
			case ENiagaraParticleDataComponentType::Int:
				ComponentTypeName = TEXT("int");
				break;
			case ENiagaraParticleDataComponentType::Bool:
				ComponentTypeName = TEXT("bool");
				break;
			case ENiagaraParticleDataComponentType::ID:
				ComponentTypeName = TEXT("int");
				// IDs are structs with 2 components and specific field names.
				NumComponents = 2;
				ValueComponentNames = IDComponentNames;
				break;
			default:
				UE_LOG(LogNiagara, Error, TEXT("Unknown component type %d while generating function %s"), ComponentType, *FunctionInfo.InstanceName);
				return false;
		}

		FetchValueCode = GenerateFetchValueHLSL(NumComponents, ValueComponentNames, ComponentTypeName, InputIntBufferName, ParticleStrideIntName, false);
	}
	else
	{
		ComponentTypeName = TEXT("float");
		// Floats and vectors can be compressed, so we need to add extra code which checks.
		FString FetchFloatCode = GenerateFetchValueHLSL(NumComponents, VectorComponentNames, ComponentTypeName, InputFloatBufferName, ParticleStrideFloatName, true);
		FString FetchHalfCode = GenerateFetchValueHLSL(NumComponents, VectorComponentNames, ComponentTypeName, InputHalfBufferName, ParticleStrideHalfName, true);
		FetchValueCode = FString(
			TEXT(
			"        BRANCH\n"
			"        if (!{AttributeCompressedName}[{AttributeIndexGroup}]{AttributeIndexComponent})\n"
			"        {\n"
			)) +
			FetchFloatCode + TEXT(
			"        }\n"
			"        else\n"
			"        {\n"
			) +
			FetchHalfCode + TEXT(
			"        }\n"
			);
	}

	FString ValueTypeName;
	if (ComponentType == ENiagaraParticleDataComponentType::ID)
	{
		ValueTypeName = TEXT("NiagaraID");
	}
	else if (NumComponents == 1)
	{
		ValueTypeName = ComponentTypeName;
	}
	else
	{
		ValueTypeName = FString::Printf(TEXT("%s%d"), ComponentTypeName, NumComponents);
	}

	FuncTemplate += TEXT(
		"    {\n"
		"        Out_Valid = true;\n"
	);

	FuncTemplate += FetchValueCode;

	FuncTemplate += TEXT(
		"    }\n"
		"    else\n"
		"    {\n"
		"        Out_Valid = false;\n"
		"        Out_Value = ({ValueType})0;\n"
		"    }\n"
		"}\n\n"
	);

	TMap<FString, FStringFormatArg> FuncTemplateArgs;
	FuncTemplateArgs.Add(TEXT("FunctionName"), FunctionInfo.InstanceName);
	FuncTemplateArgs.Add(TEXT("ValueType"), ValueTypeName);
	FuncTemplateArgs.Add(TEXT("AttributeIndicesName"), AttributeIndicesBaseName + ParamInfo.DataInterfaceHLSLSymbol);
	FuncTemplateArgs.Add(TEXT("AttributeCompressedName"), AttributeCompressedBaseName + ParamInfo.DataInterfaceHLSLSymbol);
	FuncTemplateArgs.Add(TEXT("AttributeIndexGroup"), FunctionInstanceIndex / 4);
	FuncTemplateArgs.Add(TEXT("AttributeIndexComponent"), VectorComponentNames[FunctionInstanceIndex % 4]);
	FuncTemplateArgs.Add(TEXT("IDToIndexTableName"), IDToIndexTableBaseName + ParamInfo.DataInterfaceHLSLSymbol);
	FuncTemplateArgs.Add(TEXT("InputIntBufferName"), InputIntBufferName);
	FuncTemplateArgs.Add(TEXT("AcquireTagRegisterIndexName"), AcquireTagRegisterIndexBaseName + ParamInfo.DataInterfaceHLSLSymbol);
	FuncTemplateArgs.Add(TEXT("ParticleStrideIntName"), ParticleStrideIntName);
	FuncTemplateArgs.Add(TEXT("FetchValueCode"), FetchValueCode);
	FuncTemplateArgs.Add(TEXT("InstanceCountOffsetName"), InstanceCountOffsetBaseName + ParamInfo.DataInterfaceHLSLSymbol);

	OutHLSL += FString::Format(*FuncTemplate, FuncTemplateArgs);

	return true;
}

bool UNiagaraDataInterfaceParticleRead::GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL)
{
	//
	// Spawn info and particle count
	//
	if (FunctionInfo.DefinitionName == GetNumSpawnedParticlesFunctionName)
	{
		static const TCHAR* FuncTemplate = TEXT(
			"void {FunctionName}(out int Out_NumSpawned)\n"
			"{\n"
			"    Out_NumSpawned = {NumSpawnedParticlesName};\n"
			"}\n\n"
		);

		TMap<FString, FStringFormatArg> FuncTemplateArgs;
		FuncTemplateArgs.Add(TEXT("FunctionName"), FunctionInfo.InstanceName);
		FuncTemplateArgs.Add(TEXT("NumSpawnedParticlesName"), NumSpawnedParticlesBaseName + ParamInfo.DataInterfaceHLSLSymbol);

		OutHLSL += FString::Format(FuncTemplate, FuncTemplateArgs);
		return true;
	}
	
	if (FunctionInfo.DefinitionName == GetSpawnedIDAtIndexFunctionName)
	{
		static const TCHAR* FuncTemplate = TEXT(
			"void {FunctionName}(int In_SpawnIndex, out bool Out_Valid, out NiagaraID Out_ID)\n"
			"{\n"
			"    if(In_SpawnIndex >= 0 && In_SpawnIndex < {NumSpawnedParticlesName})\n"
			"    {\n"
			"        Out_Valid = true;\n"
			"        Out_ID.Index = {SpawnedIDsBufferName}[In_SpawnIndex];\n"
			"        Out_ID.AcquireTag = {SpawnedParticlesAcquireTagName};\n"
			"    }\n"
			"    else\n"
			"    {\n"
			"        Out_Valid = false;\n"
			"        Out_ID.Index = 0;\n"
			"        Out_ID.AcquireTag = 0;\n"
			"    }\n"
			"}\n\n"
		);

		TMap<FString, FStringFormatArg> FuncTemplateArgs;
		FuncTemplateArgs.Add(TEXT("FunctionName"), FunctionInfo.InstanceName);
		FuncTemplateArgs.Add(TEXT("NumSpawnedParticlesName"), NumSpawnedParticlesBaseName + ParamInfo.DataInterfaceHLSLSymbol);
		FuncTemplateArgs.Add(TEXT("SpawnedParticlesAcquireTagName"), SpawnedParticlesAcquireTagBaseName + ParamInfo.DataInterfaceHLSLSymbol);
		FuncTemplateArgs.Add(TEXT("SpawnedIDsBufferName"), SpawnedIDsBufferBaseName + ParamInfo.DataInterfaceHLSLSymbol);

		OutHLSL += FString::Format(FuncTemplate, FuncTemplateArgs);
		return true;
	}

	if (FunctionInfo.DefinitionName == GetNumParticlesFunctionName)
	{
		static const TCHAR* FuncTemplate = TEXT(
			"void {FunctionName}(out int Out_NumParticles)\n"
			"{\n"
			"    if({InstanceCountOffsetName} != 0xffffffff)\n"
			"    {\n"
			"        Out_NumParticles = RWInstanceCounts[{InstanceCountOffsetName}];\n"
			"    }\n"
			"    else\n"
			"    {\n"
			"        Out_NumParticles = 0;\n"
			"    }\n"
			"}\n\n"
		);

		TMap<FString, FStringFormatArg> FuncTemplateArgs;
		FuncTemplateArgs.Add(TEXT("FunctionName"), FunctionInfo.InstanceName);
		FuncTemplateArgs.Add(TEXT("InstanceCountOffsetName"), InstanceCountOffsetBaseName + ParamInfo.DataInterfaceHLSLSymbol);

		OutHLSL += FString::Format(FuncTemplate, FuncTemplateArgs);
		return true;
	}

	if (FunctionInfo.DefinitionName == GetParticleIndexFunctionName)
	{
		static const TCHAR* FuncTemplate = TEXT(
			"void {FunctionName}(NiagaraID In_ParticleID, out int Out_Index)\n"
			"{\n"
			"    Out_Index = (In_ParticleID.Index >= 0) ? {IDToIndexTableName}[In_ParticleID.Index] : -1;\n"
			"    int AcquireTag = (Out_Index != -1) ? {InputIntBufferName}[{AcquireTagRegisterIndexName}*{ParticleStrideIntName} + Out_Index] : 0;\n"
			"    if(In_ParticleID.AcquireTag != AcquireTag)\n"
			"    {\n"
			"        Out_Index = -1;\n"
			"    }\n"
			"}\n\n"
		);

		TMap<FString, FStringFormatArg> FuncTemplateArgs;
		FuncTemplateArgs.Add(TEXT("FunctionName"), FunctionInfo.InstanceName);
		FuncTemplateArgs.Add(TEXT("IDToIndexTableName"), IDToIndexTableBaseName + ParamInfo.DataInterfaceHLSLSymbol);
		FuncTemplateArgs.Add(TEXT("InputIntBufferName"), InputIntBufferBaseName + ParamInfo.DataInterfaceHLSLSymbol);
		FuncTemplateArgs.Add(TEXT("AcquireTagRegisterIndexName"), AcquireTagRegisterIndexBaseName + ParamInfo.DataInterfaceHLSLSymbol);
		FuncTemplateArgs.Add(TEXT("ParticleStrideIntName"), ParticleStrideIntBaseName + ParamInfo.DataInterfaceHLSLSymbol);

		OutHLSL += FString::Format(FuncTemplate, FuncTemplateArgs);
		return true;
	}

	//
	// Get attribute by ID
	//
	if (FunctionInfo.DefinitionName == GetIntByIDFunctionName)
	{
		return GenerateGetFunctionHLSL(ParamInfo, FunctionInfo, FunctionInstanceIndex, ENiagaraParticleDataComponentType::Int, 1, false, OutHLSL);
	}

	if (FunctionInfo.DefinitionName == GetBoolByIDFunctionName)
	{
		return GenerateGetFunctionHLSL(ParamInfo, FunctionInfo, FunctionInstanceIndex, ENiagaraParticleDataComponentType::Bool, 1, false, OutHLSL);
	}

	if (FunctionInfo.DefinitionName == GetFloatByIDFunctionName)
	{
		return GenerateGetFunctionHLSL(ParamInfo, FunctionInfo, FunctionInstanceIndex, ENiagaraParticleDataComponentType::Float, 1, false, OutHLSL);
	}

	if (FunctionInfo.DefinitionName == GetVec2ByIDFunctionName)
	{
		return GenerateGetFunctionHLSL(ParamInfo, FunctionInfo, FunctionInstanceIndex, ENiagaraParticleDataComponentType::Float, 2, false, OutHLSL);
	}

	if (FunctionInfo.DefinitionName == GetVec3ByIDFunctionName)
	{
		return GenerateGetFunctionHLSL(ParamInfo, FunctionInfo, FunctionInstanceIndex, ENiagaraParticleDataComponentType::Float, 3, false, OutHLSL);
	}

	if (FunctionInfo.DefinitionName == GetVec4ByIDFunctionName)
	{
		return GenerateGetFunctionHLSL(ParamInfo, FunctionInfo, FunctionInstanceIndex, ENiagaraParticleDataComponentType::Float, 4, false, OutHLSL);
	}

	if (FunctionInfo.DefinitionName == GetColorByIDFunctionName)
	{
		return GenerateGetFunctionHLSL(ParamInfo, FunctionInfo, FunctionInstanceIndex, ENiagaraParticleDataComponentType::Float, 4, false, OutHLSL);
	}

	if (FunctionInfo.DefinitionName == GetQuatByIDFunctionName)
	{
		return GenerateGetFunctionHLSL(ParamInfo, FunctionInfo, FunctionInstanceIndex, ENiagaraParticleDataComponentType::Float, 4, false, OutHLSL);
	}

	if (FunctionInfo.DefinitionName == GetIDByIDFunctionName)
	{
		return GenerateGetFunctionHLSL(ParamInfo, FunctionInfo, FunctionInstanceIndex, ENiagaraParticleDataComponentType::ID, 1, false, OutHLSL);
	}

	//
	// Get attribute by index
	//
	if (FunctionInfo.DefinitionName == GetIntByIndexFunctionName)
	{
		return GenerateGetFunctionHLSL(ParamInfo, FunctionInfo, FunctionInstanceIndex, ENiagaraParticleDataComponentType::Int, 1, true, OutHLSL);
	}

	if (FunctionInfo.DefinitionName == GetBoolByIndexFunctionName)
	{
		return GenerateGetFunctionHLSL(ParamInfo, FunctionInfo, FunctionInstanceIndex, ENiagaraParticleDataComponentType::Bool, 1, true, OutHLSL);
	}

	if (FunctionInfo.DefinitionName == GetFloatByIndexFunctionName)
	{
		return GenerateGetFunctionHLSL(ParamInfo, FunctionInfo, FunctionInstanceIndex, ENiagaraParticleDataComponentType::Float, 1, true, OutHLSL);
	}

	if (FunctionInfo.DefinitionName == GetVec2ByIndexFunctionName)
	{
		return GenerateGetFunctionHLSL(ParamInfo, FunctionInfo, FunctionInstanceIndex, ENiagaraParticleDataComponentType::Float, 2, true, OutHLSL);
	}

	if (FunctionInfo.DefinitionName == GetVec3ByIndexFunctionName)
	{
		return GenerateGetFunctionHLSL(ParamInfo, FunctionInfo, FunctionInstanceIndex, ENiagaraParticleDataComponentType::Float, 3, true, OutHLSL);
	}

	if (FunctionInfo.DefinitionName == GetVec4ByIndexFunctionName)
	{
		return GenerateGetFunctionHLSL(ParamInfo, FunctionInfo, FunctionInstanceIndex, ENiagaraParticleDataComponentType::Float, 4, true, OutHLSL);
	}

	if (FunctionInfo.DefinitionName == GetColorByIndexFunctionName)
	{
		return GenerateGetFunctionHLSL(ParamInfo, FunctionInfo, FunctionInstanceIndex, ENiagaraParticleDataComponentType::Float, 4, true, OutHLSL);
	}

	if (FunctionInfo.DefinitionName == GetQuatByIndexFunctionName)
	{
		return GenerateGetFunctionHLSL(ParamInfo, FunctionInfo, FunctionInstanceIndex, ENiagaraParticleDataComponentType::Float, 4, true, OutHLSL);
	}

	if (FunctionInfo.DefinitionName == GetIDByIndexFunctionName)
	{
		return GenerateGetFunctionHLSL(ParamInfo, FunctionInfo, FunctionInstanceIndex, ENiagaraParticleDataComponentType::ID, 1, true, OutHLSL);
	}

	return false;
}

void UNiagaraDataInterfaceParticleRead::ProvidePerInstanceDataForRenderThread(void* DataForRenderThread, void* PerInstanceData, const FNiagaraSystemInstanceID& SystemInstance)
{
	FNDIParticleRead_GameToRenderData* RTData = new (DataForRenderThread) FNDIParticleRead_GameToRenderData;
	FNDIParticleRead_InstanceData* PIData = static_cast<FNDIParticleRead_InstanceData*>(PerInstanceData);
	if (PIData && PIData->EmitterInstance)
	{
		RTData->SourceEmitterGPUContext = PIData->EmitterInstance->GetGPUContext();
		RTData->SourceEmitterName = PIData->EmitterInstance->GetCachedEmitter()->GetUniqueEmitterName();
	}
}

void UNiagaraDataInterfaceParticleRead::GetEmitterDependencies(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, TArray<FNiagaraEmitterInstance*>& Dependencies) const
{
	FNDIParticleRead_InstanceData* PIData = static_cast<FNDIParticleRead_InstanceData*>(PerInstanceData);
	if (PIData && PIData->EmitterInstance)
	{
		Dependencies.Add(PIData->EmitterInstance);
	}
}

IMPLEMENT_NIAGARA_DI_PARAMETER(UNiagaraDataInterfaceParticleRead, FNiagaraDataInterfaceParametersCS_ParticleRead);

#undef LOCTEXT_NAMESPACE
