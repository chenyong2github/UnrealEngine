// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraDataInterfaceParticleRead.h"
#include "NiagaraConstants.h"
#include "NiagaraSystemInstance.h"
#include "ShaderParameterUtils.h"
#include "NiagaraRenderer.h"

#define LOCTEXT_NAMESPACE "NiagaraDataInterfaceParticleRead"

static const FName GetIntAttributeFunctionName("Get int Attribute");
static const FName GetFloatAttributeFunctionName("Get float Attribute");
static const FName GetVec2AttributeFunctionName("Get Vector2 Attribute");
static const FName GetVec3AttributeFunctionName("Get Vector3 Attribute");
static const FName GetVec4AttributeFunctionName("Get Vector4 Attribute");
static const FName GetBoolAttributeFunctionName("Get bool Attribute");
static const FName GetColorAttributeFunctionName("Get Color Attribute");
static const FName GetQuatAttributeFunctionName("Get Quaternion Attribute");

static const FString IDToIndexTableBaseName(TEXT("IDToIndexTable_"));
static const FString InputFloatBufferBaseName(TEXT("InputFloatBuffer_"));
static const FString InputIntBufferBaseName(TEXT("InputIntBuffer_"));
static const FString ParticleStrideFloatBaseName(TEXT("ParticleStrideFloat_"));
static const FString ParticleStrideIntBaseName(TEXT("ParticleStrideInt_"));
static const FString AttributeIndicesBaseName(TEXT("AttributeIndices_"));
static const FString AcquireTagRegisterIndexBaseName(TEXT("AcquireTagRegisterIndex_"));

enum class ENiagaraParticleDataComponentType : uint8
{
	Float,
	Int,
	Bool
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
	Quat
};

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
		default:										return TEXT("UNKNOWN");
	}
}

struct FNDIParticleRead_InstanceDataGPU
{
	FNiagaraComputeExecutionContext* SourceEmitterGPUContext;
	FString SourceEmitterName;
};

struct FNiagaraDataInterfaceProxyParticleRead : public FNiagaraDataInterfaceProxy
{
	FNiagaraDataInterfaceProxyParticleRead() : SourceEmitterGPUContext(nullptr)
	{
	}

	virtual void ConsumePerInstanceDataFromGameThread(void* PerInstanceData, const FNiagaraSystemInstanceID& Instance) override
	{
		FNDIParticleRead_InstanceDataGPU* InstanceData = static_cast<FNDIParticleRead_InstanceDataGPU*>(PerInstanceData);
		if (InstanceData)
		{
			SourceEmitterGPUContext = InstanceData->SourceEmitterGPUContext;
			SourceEmitterName = InstanceData->SourceEmitterName;
		}
	}
	
	virtual int32 PerInstanceDataPassedToRenderThreadSize() const override
	{
		return sizeof(FNDIParticleRead_InstanceDataGPU);
	}

	FNiagaraComputeExecutionContext* SourceEmitterGPUContext;
	FString SourceEmitterName;
};

struct FNiagaraDataInterfaceParametersCS_ParticleRead : public FNiagaraDataInterfaceParametersCS
{
	FNiagaraDataInterfaceParametersCS_ParticleRead() : 
		CachedDataSet(nullptr),
		bSourceEmitterNotGPUErrorShown(false)
	{
	}

	ENiagaraParticleDataValueType GetValueTypeFromFuncName(const FName& FuncName)
	{
		if (FuncName == GetIntAttributeFunctionName) return ENiagaraParticleDataValueType::Int;
		if (FuncName == GetFloatAttributeFunctionName) return ENiagaraParticleDataValueType::Float;
		if (FuncName == GetVec2AttributeFunctionName) return ENiagaraParticleDataValueType::Vec2;
		if (FuncName == GetVec3AttributeFunctionName) return ENiagaraParticleDataValueType::Vec3;
		if (FuncName == GetVec4AttributeFunctionName) return ENiagaraParticleDataValueType::Vec4;
		if (FuncName == GetBoolAttributeFunctionName) return ENiagaraParticleDataValueType::Bool;
		if (FuncName == GetColorAttributeFunctionName) return ENiagaraParticleDataValueType::Color;
		if (FuncName == GetQuatAttributeFunctionName) return ENiagaraParticleDataValueType::Quat;
		return ENiagaraParticleDataValueType::Invalid;
	}

	virtual void Bind(const FNiagaraDataInterfaceParamRef& ParamRef, const class FShaderParameterMap& ParameterMap) override
	{
		IDToIndexTableParam.Bind(ParameterMap, *(IDToIndexTableBaseName + ParamRef.ParameterInfo.DataInterfaceHLSLSymbol));
		InputFloatBufferParam.Bind(ParameterMap, *(InputFloatBufferBaseName + ParamRef.ParameterInfo.DataInterfaceHLSLSymbol));
		InputIntBufferParam.Bind(ParameterMap, *(InputIntBufferBaseName + ParamRef.ParameterInfo.DataInterfaceHLSLSymbol));
		ParticleStrideFloatParam.Bind(ParameterMap, *(ParticleStrideFloatBaseName + ParamRef.ParameterInfo.DataInterfaceHLSLSymbol));
		ParticleStrideIntParam.Bind(ParameterMap, *(ParticleStrideIntBaseName + ParamRef.ParameterInfo.DataInterfaceHLSLSymbol));
		AttributeIndicesParam.Bind(ParameterMap, *(AttributeIndicesBaseName + ParamRef.ParameterInfo.DataInterfaceHLSLSymbol));
		AcquireTagRegisterIndexParam.Bind(ParameterMap, *(AcquireTagRegisterIndexBaseName + ParamRef.ParameterInfo.DataInterfaceHLSLSymbol));

		int32 NumFuncs = ParamRef.ParameterInfo.GeneratedFunctions.Num();
		AttributeNames.SetNum(NumFuncs);
		AttributeTypes.SetNum(NumFuncs);
		for (int32 FuncIdx = 0; FuncIdx < NumFuncs; ++FuncIdx)
		{
			const FNiagaraDataInterfaceGeneratedFunction& Func = ParamRef.ParameterInfo.GeneratedFunctions[FuncIdx];
			static const FName NAME_Attribute("Attribute");
			const FName* AttributeName = Func.FindSpecifierValue(NAME_Attribute);
			if (AttributeName != nullptr)
			{
				AttributeNames[FuncIdx] = *AttributeName;
				AttributeTypes[FuncIdx] = GetValueTypeFromFuncName(Func.DefinitionName);
			}
			else
			{
				AttributeNames[FuncIdx] = NAME_None;
				AttributeTypes[FuncIdx] = ENiagaraParticleDataValueType::Invalid;
			}
		}

		AttributeIndices.SetNum(AttributeNames.Num());
	}

	virtual void Serialize(FArchive& Ar) override
	{
		Ar << IDToIndexTableParam;
		Ar << InputFloatBufferParam;
		Ar << InputIntBufferParam;
		Ar << ParticleStrideFloatParam;
		Ar << ParticleStrideIntParam;
		Ar << AttributeIndicesParam;
		Ar << AcquireTagRegisterIndexParam;
		Ar << AttributeNames;
		Ar << AttributeTypes;

		AttributeIndices.SetNum(AttributeNames.Num());
	}

	void SetErrorParams(FRHICommandList& RHICmdList, FRHIComputeShader* ComputeShader) const
	{
		CachedDataSet = nullptr;

		for (int AttrIdx = 0; AttrIdx < AttributeIndices.Num(); ++AttrIdx)
		{
			AttributeIndices[AttrIdx] = -1;
		}
		AcquireTagRegisterIndex = -1;

		SetSRVParameter(RHICmdList, ComputeShader, IDToIndexTableParam, FNiagaraRenderer::GetDummyIntBuffer().SRV);
		SetSRVParameter(RHICmdList, ComputeShader, InputFloatBufferParam, FNiagaraRenderer::GetDummyFloatBuffer().SRV);
		SetSRVParameter(RHICmdList, ComputeShader, InputIntBufferParam, FNiagaraRenderer::GetDummyIntBuffer().SRV);
		SetShaderValue(RHICmdList, ComputeShader, ParticleStrideFloatParam, 0);
		SetShaderValue(RHICmdList, ComputeShader, ParticleStrideIntParam, 0);
		SetShaderValueArray(RHICmdList, ComputeShader, AttributeIndicesParam, AttributeIndices.GetData(), AttributeIndices.Num());
		SetShaderValue(RHICmdList, ComputeShader, AcquireTagRegisterIndexParam, AcquireTagRegisterIndex);
	}

	bool CheckVariableType(const FNiagaraTypeDefinition& VarType, ENiagaraParticleDataValueType AttributeType) const
	{
		switch (AttributeType)
		{
			case ENiagaraParticleDataValueType::Int: return VarType == FNiagaraTypeDefinition::GetIntDef();
			case ENiagaraParticleDataValueType::Float: return VarType == FNiagaraTypeDefinition::GetFloatDef();
			case ENiagaraParticleDataValueType::Vec2: return VarType == FNiagaraTypeDefinition::GetVec2Def();
			case ENiagaraParticleDataValueType::Vec3: return VarType == FNiagaraTypeDefinition::GetVec3Def();
			case ENiagaraParticleDataValueType::Vec4: return VarType == FNiagaraTypeDefinition::GetVec4Def();
			case ENiagaraParticleDataValueType::Bool: return VarType == FNiagaraTypeDefinition::GetBoolDef();
			case ENiagaraParticleDataValueType::Color: return VarType == FNiagaraTypeDefinition::GetColorDef();
			case ENiagaraParticleDataValueType::Quat: return VarType == FNiagaraTypeDefinition::GetQuatDef();
			default: return false;
		}
	}

	void FindAttributeIndices(FNiagaraDataSet* SourceDataSet, const TCHAR* SourceEmitterName) const
	{
		check(AttributeIndices.Num() == AttributeNames.Num());

		const TArray<FNiagaraVariable>& SourceEmitterVariables = SourceDataSet->GetVariables();
		const TArray<FNiagaraVariableLayoutInfo>& SourceEmitterVariableLayouts = SourceDataSet->GetVariableLayouts();
		for (int AttrNameIdx = 0; AttrNameIdx < AttributeNames.Num(); ++AttrNameIdx)
		{
			bool FoundVariable = false;
			for (int VarIdx = 0; VarIdx < SourceEmitterVariables.Num(); ++VarIdx)
			{
				const FNiagaraVariable& Var = SourceEmitterVariables[VarIdx];
				if (Var.GetName() == AttributeNames[AttrNameIdx])
				{
					ENiagaraParticleDataValueType AttributeType = AttributeTypes[AttrNameIdx];
					if (CheckVariableType(Var.GetType(), AttributeType))
					{
						const FNiagaraVariableLayoutInfo& Layout = SourceEmitterVariableLayouts[VarIdx];
						AttributeIndices[AttrNameIdx] = (AttributeType == ENiagaraParticleDataValueType::Int || AttributeType == ENiagaraParticleDataValueType::Bool) ? Layout.Int32ComponentStart : Layout.FloatComponentStart;
					}
					else
					{
						UE_LOG(LogNiagara, Error, TEXT("Variable '%s' in emitter '%s' has type '%s', but particle read DI tried to access it as '%s'."),
							*Var.GetName().ToString(), SourceEmitterName, *Var.GetType().GetName(), NiagaraParticleDataValueTypeName(AttributeType)
						);
						AttributeIndices[AttrNameIdx] = -1;
					}
					FoundVariable = true;
					break;
				}
			}

			if (!FoundVariable)
			{
				UE_LOG(LogNiagara, Error, TEXT("Particle read DI is trying to access inexistent variable '%s' in emitter '%s'."), *AttributeNames[AttrNameIdx].ToString(), SourceEmitterName);
				AttributeIndices[AttrNameIdx] = -1;
			}
		}

		AcquireTagRegisterIndex = -1;
		for (int VarIdx = 0; VarIdx < SourceEmitterVariables.Num(); ++VarIdx)
		{
			const FNiagaraVariable& Var = SourceEmitterVariables[VarIdx];
			if (Var.GetName().ToString() == TEXT("ID"))
			{
				AcquireTagRegisterIndex = SourceEmitterVariableLayouts[VarIdx].Int32ComponentStart + 1;
				break;
			}
		}

		if (AcquireTagRegisterIndex == -1)
		{
			UE_LOG(LogNiagara, Error, TEXT("Particle read DI cannot find ID variable in emitter '%s'."), SourceEmitterName);
		}
	}

	virtual void Set(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const override
	{
		check(IsInRenderingThread());

		FRHIComputeShader* ComputeShader = Context.Shader->GetComputeShader();

		if (!InputFloatBufferParam.IsBound() && !InputIntBufferParam.IsBound())
		{
			// This DI instance didn't generate any reachable code, probably because all the values it sets are overwritten by
			// other DIs. Don't bother with it.
			SetErrorParams(RHICmdList, ComputeShader);
			return;
		}

		FNiagaraDataInterfaceProxyParticleRead* Proxy = static_cast<FNiagaraDataInterfaceProxyParticleRead*>(Context.DataInterface);
		check(Proxy);
		if (Proxy->SourceEmitterGPUContext == nullptr)
		{
			// This means the source emitter isn't running on GPU.
			if (!bSourceEmitterNotGPUErrorShown)
			{
				UE_LOG(LogNiagara, Error, TEXT("GPU particle read DI is set to access CPU emitter '%s'."), *Proxy->SourceEmitterName);
				bSourceEmitterNotGPUErrorShown = true;
			}
			SetErrorParams(RHICmdList, ComputeShader);
			return;
		}

		bSourceEmitterNotGPUErrorShown = false;

		FNiagaraDataSet* SourceDataSet = Proxy->SourceEmitterGPUContext->MainDataSet;
		if (!SourceDataSet)
		{
			SetErrorParams(RHICmdList, ComputeShader);
			return;
		}

		FNiagaraDataBuffer* SourceData;
		if (Context.ComputeInstanceData->Context == Proxy->SourceEmitterGPUContext)
		{
			// If the current execution context is the same as the source emitter's context, it means we're reading from
			// ourselves. We can't use SourceDataSet->GetCurrentData() in that case, because EndSimulate() has already been
			// called on the current emitter, and the current data has been set to the destination data. We need to use the
			// current compute instance data to get to the input buffers.
			SourceData = Context.ComputeInstanceData->CurrentData;
		}
		else
		{
			SourceData = SourceDataSet->GetCurrentData();
		}

		if (!SourceData)
		{
			SetErrorParams(RHICmdList, ComputeShader);
			return;
		}

		if (CachedDataSet != SourceDataSet)
		{
			FindAttributeIndices(SourceDataSet, *Proxy->SourceEmitterName);
			CachedDataSet = SourceDataSet;
		}

		if (!SourceData->GetGPUIDToIndexTable().Buffer)
		{
			// This can happen in the first frame, when there's no previous data yet. The DI shouldn't be
			// queried in this case, because there's no way to have particle IDs (since there are no particles),
			// but if it is it will just return failure and default values.
			SetErrorParams(RHICmdList, ComputeShader);
			return;
		}

		const uint32 ParticleStrideFloat = SourceData->GetFloatStride() / sizeof(float);
		const uint32 ParticleStrideInt = SourceData->GetInt32Stride() / sizeof(int32);

		SetSRVParameter(RHICmdList, ComputeShader, IDToIndexTableParam, SourceData->GetGPUIDToIndexTable().SRV);
		SetSRVParameter(RHICmdList, ComputeShader, InputFloatBufferParam, SourceData->GetGPUBufferFloat().SRV);
		SetSRVParameter(RHICmdList, ComputeShader, InputIntBufferParam, SourceData->GetGPUBufferInt().SRV);
		SetShaderValue(RHICmdList, ComputeShader, ParticleStrideFloatParam, ParticleStrideFloat);
		SetShaderValue(RHICmdList, ComputeShader, ParticleStrideIntParam, ParticleStrideInt);
		SetShaderValueArray(RHICmdList, ComputeShader, AttributeIndicesParam, AttributeIndices.GetData(), AttributeIndices.Num());
		SetShaderValue(RHICmdList, ComputeShader, AcquireTagRegisterIndexParam, AcquireTagRegisterIndex);
	}

private:
	FShaderResourceParameter IDToIndexTableParam;
	FShaderResourceParameter InputFloatBufferParam;
	FShaderResourceParameter InputIntBufferParam;
	FShaderParameter ParticleStrideFloatParam;
	FShaderParameter ParticleStrideIntParam;
	FShaderParameter AttributeIndicesParam;
	FShaderParameter AcquireTagRegisterIndexParam;
	TArray<FName> AttributeNames;
	TArray<ENiagaraParticleDataValueType> AttributeTypes;
	mutable TArray<int32> AttributeIndices;
	mutable int32 AcquireTagRegisterIndex;
	mutable FNiagaraDataSet* CachedDataSet;
	mutable bool bSourceEmitterNotGPUErrorShown;
};

UNiagaraDataInterfaceParticleRead::UNiagaraDataInterfaceParticleRead(FObjectInitializer const& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Proxy = MakeShared<FNiagaraDataInterfaceProxyParticleRead, ESPMode::ThreadSafe>();
}

void UNiagaraDataInterfaceParticleRead::PostInitProperties()
{
	Super::PostInitProperties();

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(GetClass()), true, false, false);
	}
}

void UNiagaraDataInterfaceParticleRead::PostLoad()
{
	Super::PostLoad();
}

#if WITH_EDITOR

void UNiagaraDataInterfaceParticleRead::PreEditChange(FProperty* PropertyAboutToChange)
{
	Super::PreEditChange(PropertyAboutToChange);
}

void UNiagaraDataInterfaceParticleRead::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
}

#endif

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
	return (PIData->EmitterInstance != nullptr);
}

void UNiagaraDataInterfaceParticleRead::DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{

}

bool UNiagaraDataInterfaceParticleRead::PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds)
{
	return false;
}

bool UNiagaraDataInterfaceParticleRead::PerInstanceTickPostSimulate(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds)
{
	return false;
}

void UNiagaraDataInterfaceParticleRead::GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)
{
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetFloatAttributeFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Particle Reader")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIDDef(), TEXT("Particle ID")));

		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Valid")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Value")));

		Sig.FunctionSpecifiers.Add(FName("Attribute"));

		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetVec2AttributeFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Particle Reader")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIDDef(), TEXT("Particle ID")));

		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Valid")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec2Def(), TEXT("Value")));

		Sig.FunctionSpecifiers.Add(FName("Attribute"));

		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetVec3AttributeFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Particle Reader")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIDDef(), TEXT("Particle ID")));

		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Valid")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Value")));

		Sig.FunctionSpecifiers.Add(FName("Attribute"));

		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetVec4AttributeFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Particle Reader")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIDDef(), TEXT("Particle ID")));

		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Valid")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec4Def(), TEXT("Value")));

		Sig.FunctionSpecifiers.Add(FName("Attribute"));

		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetIntAttributeFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Particle Reader")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIDDef(), TEXT("Particle ID")));

		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Valid")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Value")));

		Sig.FunctionSpecifiers.Add(FName("Attribute"));

		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetBoolAttributeFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Particle Reader")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIDDef(), TEXT("Particle ID")));

		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Valid")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Value")));

		Sig.FunctionSpecifiers.Add(FName("Attribute"));

		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetColorAttributeFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Particle Reader")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIDDef(), TEXT("Particle ID")));

		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Valid")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetColorDef(), TEXT("Value")));

		Sig.FunctionSpecifiers.Add(FName("Attribute"));

		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetQuatAttributeFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Particle Reader")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIDDef(), TEXT("Particle ID")));

		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Valid")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetQuatDef(), TEXT("Value")));

		Sig.FunctionSpecifiers.Add(FName("Attribute"));

		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}
}
DEFINE_NDI_DIRECT_FUNC_BINDER_WITH_PAYLOAD(UNiagaraDataInterfaceParticleRead, ReadFloat);
DEFINE_NDI_DIRECT_FUNC_BINDER_WITH_PAYLOAD(UNiagaraDataInterfaceParticleRead, ReadVector2);
DEFINE_NDI_DIRECT_FUNC_BINDER_WITH_PAYLOAD(UNiagaraDataInterfaceParticleRead, ReadVector3);
DEFINE_NDI_DIRECT_FUNC_BINDER_WITH_PAYLOAD(UNiagaraDataInterfaceParticleRead, ReadVector4);
DEFINE_NDI_DIRECT_FUNC_BINDER_WITH_PAYLOAD(UNiagaraDataInterfaceParticleRead, ReadInt);
DEFINE_NDI_DIRECT_FUNC_BINDER_WITH_PAYLOAD(UNiagaraDataInterfaceParticleRead, ReadBool);
DEFINE_NDI_DIRECT_FUNC_BINDER_WITH_PAYLOAD(UNiagaraDataInterfaceParticleRead, ReadColor);
DEFINE_NDI_DIRECT_FUNC_BINDER_WITH_PAYLOAD(UNiagaraDataInterfaceParticleRead, ReadQuat);
void UNiagaraDataInterfaceParticleRead::GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc)
{
	bool bBindSuccessful = false;
	FNDIParticleRead_InstanceData* PIData = static_cast<FNDIParticleRead_InstanceData*>(InstanceData);
	static const FName NAME_Attribute("Attribute");

	const FVMFunctionSpecifier* FunctionSpecifier = BindingInfo.FindSpecifier(NAME_Attribute);
	if (FunctionSpecifier == nullptr)
	{
		UE_LOG(LogNiagara, Error, TEXT("VMExternalFunction '%s' does not have a function specifier 'attribute'!"), *BindingInfo.Name.ToString());
		return;
	}

	const FName AttributeToRead = FunctionSpecifier->Value;
	if (BindingInfo.Name == GetFloatAttributeFunctionName)
	{
		FNiagaraVariable VariableToRead(FNiagaraTypeDefinition::GetFloatDef(), AttributeToRead);
		if (PIData->EmitterInstance->GetData().GetVariables().Find(VariableToRead) != INDEX_NONE)
		{
			NDI_FUNC_BINDER(UNiagaraDataInterfaceParticleRead, ReadFloat)::Bind(this, OutFunc, AttributeToRead);
			bBindSuccessful = true;
		}
	}
	else if (BindingInfo.Name == GetVec2AttributeFunctionName)
	{
		FNiagaraVariable VariableToRead(FNiagaraTypeDefinition::GetVec2Def(), AttributeToRead);
		if (PIData->EmitterInstance->GetData().GetVariables().Find(VariableToRead) != INDEX_NONE)
		{
			NDI_FUNC_BINDER(UNiagaraDataInterfaceParticleRead, ReadVector2)::Bind(this, OutFunc, AttributeToRead);
			bBindSuccessful = true;
		}
	}
	else if (BindingInfo.Name == GetVec3AttributeFunctionName)
	{
		FNiagaraVariable VariableToRead(FNiagaraTypeDefinition::GetVec3Def(), AttributeToRead);
		if (PIData->EmitterInstance->GetData().GetVariables().Find(VariableToRead) != INDEX_NONE)
		{
			NDI_FUNC_BINDER(UNiagaraDataInterfaceParticleRead, ReadVector3)::Bind(this, OutFunc, AttributeToRead);
			bBindSuccessful = true;
		}
	}
	else if (BindingInfo.Name == GetVec4AttributeFunctionName)
	{
		FNiagaraVariable VariableToRead(FNiagaraTypeDefinition::GetVec4Def(), AttributeToRead);
		if (PIData->EmitterInstance->GetData().GetVariables().Find(VariableToRead) != INDEX_NONE)
		{
			NDI_FUNC_BINDER(UNiagaraDataInterfaceParticleRead, ReadVector4)::Bind(this, OutFunc, AttributeToRead);
			bBindSuccessful = true;
		}
	}
	else if (BindingInfo.Name == GetIntAttributeFunctionName)
	{
		FNiagaraVariable VariableToRead(FNiagaraTypeDefinition::GetIntDef(), AttributeToRead);
		if (PIData->EmitterInstance->GetData().GetVariables().Find(VariableToRead) != INDEX_NONE)
		{
			NDI_FUNC_BINDER(UNiagaraDataInterfaceParticleRead, ReadInt)::Bind(this, OutFunc, AttributeToRead);
			bBindSuccessful = true;
		}
	}
	else if (BindingInfo.Name == GetBoolAttributeFunctionName)
	{
		FNiagaraVariable VariableToRead(FNiagaraTypeDefinition::GetBoolDef(), AttributeToRead);
		if (PIData->EmitterInstance->GetData().GetVariables().Find(VariableToRead) != INDEX_NONE)
		{
			NDI_FUNC_BINDER(UNiagaraDataInterfaceParticleRead, ReadBool)::Bind(this, OutFunc, AttributeToRead);
			bBindSuccessful = true;
		}
	}
	else if (BindingInfo.Name == GetColorAttributeFunctionName)
	{
		FNiagaraVariable VariableToRead(FNiagaraTypeDefinition::GetColorDef(), AttributeToRead);
		if (PIData->EmitterInstance->GetData().GetVariables().Find(VariableToRead) != INDEX_NONE)
		{
			NDI_FUNC_BINDER(UNiagaraDataInterfaceParticleRead, ReadColor)::Bind(this, OutFunc, AttributeToRead);
			bBindSuccessful = true;
		}
	}
	else if (BindingInfo.Name == GetQuatAttributeFunctionName)
	{
		FNiagaraVariable VariableToRead(FNiagaraTypeDefinition::GetQuatDef(), AttributeToRead);
		if (PIData->EmitterInstance->GetData().GetVariables().Find(VariableToRead) != INDEX_NONE)
		{
			NDI_FUNC_BINDER(UNiagaraDataInterfaceParticleRead, ReadQuat)::Bind(this, OutFunc, AttributeToRead);
			bBindSuccessful = true;
		}
	}

	if (!bBindSuccessful)
	{
		UE_LOG(LogNiagara, Error, TEXT("Failed to bind VMExternalFunction '%s' with attribute '%s'! Check that the attribute is named correctly."), *BindingInfo.Name.ToString(), *AttributeToRead.ToString());
	}
}

void UNiagaraDataInterfaceParticleRead::ReadFloat(FVectorVMContext& Context, FName AttributeToRead)
{
	VectorVM::FExternalFuncInputHandler<int32> ParticleIDIndexParam(Context);
	VectorVM::FExternalFuncInputHandler<int32> ParticleIDAcquireTagParam(Context);

	VectorVM::FUserPtrHandler<FNDIParticleRead_InstanceData> InstanceData(Context);

	VectorVM::FExternalFuncRegisterHandler<FNiagaraBool> OutValid(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutValue(Context);
	
	for (int32 InstanceIdx = 0; InstanceIdx < Context.NumInstances; ++InstanceIdx)
	{
		FNiagaraID ParticleID = { ParticleIDIndexParam.GetAndAdvance(), ParticleIDAcquireTagParam.GetAndAdvance() };
		bool bValid;
		float Value = RetrieveValueWithCheck<float>(InstanceData->EmitterInstance, FNiagaraTypeDefinition::GetFloatDef(), AttributeToRead, ParticleID, bValid);
		FNiagaraBool ValidValue;
		ValidValue.SetValue(bValid);
		*OutValid.GetDestAndAdvance() = ValidValue;
		*OutValue.GetDestAndAdvance() = Value;
	}
}

void UNiagaraDataInterfaceParticleRead::ReadVector2(FVectorVMContext& Context, FName AttributeToRead)
{
	VectorVM::FExternalFuncInputHandler<int32> ParticleIDIndexParam(Context);
	VectorVM::FExternalFuncInputHandler<int32> ParticleIDAcquireTagParam(Context);

	VectorVM::FUserPtrHandler<FNDIParticleRead_InstanceData> InstanceData(Context);

	VectorVM::FExternalFuncRegisterHandler<FNiagaraBool> OutValid(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutY(Context);

	for (int32 InstanceIdx = 0; InstanceIdx < Context.NumInstances; ++InstanceIdx)
	{
		FNiagaraID ParticleID = { ParticleIDIndexParam.GetAndAdvance(), ParticleIDAcquireTagParam.GetAndAdvance() };
		bool bValid;

		FVector2D Value = RetrieveValueWithCheck<FVector2D>(InstanceData->EmitterInstance, FNiagaraTypeDefinition::GetVec2Def(), AttributeToRead, ParticleID, bValid);

		FNiagaraBool ValidValue;
		ValidValue.SetValue(bValid);
		*OutValid.GetDestAndAdvance() = ValidValue;
		*OutX.GetDestAndAdvance() = Value.X;
		*OutY.GetDestAndAdvance() = Value.Y;
	}
}

void UNiagaraDataInterfaceParticleRead::ReadVector3(FVectorVMContext& Context, FName AttributeToRead)
{
	VectorVM::FExternalFuncInputHandler<int32> ParticleIDIndexParam(Context);
	VectorVM::FExternalFuncInputHandler<int32> ParticleIDAcquireTagParam(Context);

	VectorVM::FUserPtrHandler<FNDIParticleRead_InstanceData> InstanceData(Context);

	VectorVM::FExternalFuncRegisterHandler<FNiagaraBool> OutValid(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutY(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutZ(Context);

	for (int32 InstanceIdx = 0; InstanceIdx < Context.NumInstances; ++InstanceIdx)
	{
		FNiagaraID ParticleID = { ParticleIDIndexParam.GetAndAdvance(), ParticleIDAcquireTagParam.GetAndAdvance() };
		bool bValid;

		FVector Value = RetrieveValueWithCheck<FVector>(InstanceData->EmitterInstance, FNiagaraTypeDefinition::GetVec3Def(), AttributeToRead, ParticleID, bValid);

		FNiagaraBool ValidValue;
		ValidValue.SetValue(bValid);
		*OutValid.GetDestAndAdvance() = ValidValue;
		*OutX.GetDestAndAdvance() = Value.X;
		*OutY.GetDestAndAdvance() = Value.Y;
		*OutZ.GetDestAndAdvance() = Value.Z;
	}
}

void UNiagaraDataInterfaceParticleRead::ReadVector4(FVectorVMContext& Context, FName AttributeToRead)
{
	VectorVM::FExternalFuncInputHandler<int32> ParticleIDIndexParam(Context);
	VectorVM::FExternalFuncInputHandler<int32> ParticleIDAcquireTagParam(Context);

	VectorVM::FUserPtrHandler<FNDIParticleRead_InstanceData> InstanceData(Context);

	VectorVM::FExternalFuncRegisterHandler<FNiagaraBool> OutValid(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutY(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutZ(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutW(Context);

	for (int32 InstanceIdx = 0; InstanceIdx < Context.NumInstances; ++InstanceIdx)
	{
		FNiagaraID ParticleID = { ParticleIDIndexParam.GetAndAdvance(), ParticleIDAcquireTagParam.GetAndAdvance() };
		bool bValid;

		FVector4 Value = RetrieveValueWithCheck<FVector4>(InstanceData->EmitterInstance, FNiagaraTypeDefinition::GetVec4Def(), AttributeToRead, ParticleID, bValid);

		FNiagaraBool ValidValue;
		ValidValue.SetValue(bValid);
		*OutValid.GetDestAndAdvance() = ValidValue;
		*OutX.GetDestAndAdvance() = Value.X;
		*OutY.GetDestAndAdvance() = Value.Y;
		*OutZ.GetDestAndAdvance() = Value.Z;
		*OutW.GetDestAndAdvance() = Value.W;
	}
}

void UNiagaraDataInterfaceParticleRead::ReadInt(FVectorVMContext& Context, FName AttributeToRead)
{
	VectorVM::FExternalFuncInputHandler<int32> ParticleIDIndexParam(Context);
	VectorVM::FExternalFuncInputHandler<int32> ParticleIDAcquireTagParam(Context);

	VectorVM::FUserPtrHandler<FNDIParticleRead_InstanceData> InstanceData(Context);

	VectorVM::FExternalFuncRegisterHandler<FNiagaraBool> OutValid(Context);
	VectorVM::FExternalFuncRegisterHandler<int32> OutValue(Context);

	for (int32 InstanceIdx = 0; InstanceIdx < Context.NumInstances; ++InstanceIdx)
	{
		FNiagaraID ParticleID = { ParticleIDIndexParam.GetAndAdvance(), ParticleIDAcquireTagParam.GetAndAdvance() };
		bool bValid;

		int32 Value = RetrieveValueWithCheck<int32>(InstanceData->EmitterInstance, FNiagaraTypeDefinition::GetIntDef(), AttributeToRead, ParticleID, bValid);

		FNiagaraBool ValidValue;
		ValidValue.SetValue(bValid);
		*OutValid.GetDestAndAdvance() = ValidValue;
		*OutValue.GetDestAndAdvance() = Value;
	}
}

void UNiagaraDataInterfaceParticleRead::ReadBool(FVectorVMContext& Context, FName AttributeToRead)
{
	VectorVM::FExternalFuncInputHandler<int32> ParticleIDIndexParam(Context);
	VectorVM::FExternalFuncInputHandler<int32> ParticleIDAcquireTagParam(Context);

	VectorVM::FUserPtrHandler<FNDIParticleRead_InstanceData> InstanceData(Context);

	VectorVM::FExternalFuncRegisterHandler<FNiagaraBool> OutValid(Context);
	VectorVM::FExternalFuncRegisterHandler<FNiagaraBool> OutValue(Context);

	for (int32 InstanceIdx = 0; InstanceIdx < Context.NumInstances; ++InstanceIdx)
	{
		FNiagaraID ParticleID = { ParticleIDIndexParam.GetAndAdvance(), ParticleIDAcquireTagParam.GetAndAdvance() };
		bool bValid;

		FNiagaraBool Value = RetrieveValueWithCheck<FNiagaraBool>(InstanceData->EmitterInstance, FNiagaraTypeDefinition::GetBoolDef(), AttributeToRead, ParticleID, bValid);

		FNiagaraBool ValidValue;
		ValidValue.SetValue(bValid);
		*OutValid.GetDestAndAdvance() = ValidValue;
		*OutValue.GetDestAndAdvance() = Value;
	}
}

void UNiagaraDataInterfaceParticleRead::ReadColor(FVectorVMContext& Context, FName AttributeToRead)
{
	VectorVM::FExternalFuncInputHandler<int32> ParticleIDIndexParam(Context);
	VectorVM::FExternalFuncInputHandler<int32> ParticleIDAcquireTagParam(Context);

	VectorVM::FUserPtrHandler<FNDIParticleRead_InstanceData> InstanceData(Context);

	VectorVM::FExternalFuncRegisterHandler<FNiagaraBool> OutValid(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutR(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutG(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutB(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutA(Context);

	for (int32 InstanceIdx = 0; InstanceIdx < Context.NumInstances; ++InstanceIdx)
	{
		FNiagaraID ParticleID = { ParticleIDIndexParam.GetAndAdvance(), ParticleIDAcquireTagParam.GetAndAdvance() };
		bool bValid;

		FLinearColor Value = RetrieveValueWithCheck<FLinearColor>(InstanceData->EmitterInstance, FNiagaraTypeDefinition::GetColorDef(), AttributeToRead, ParticleID, bValid);

		FNiagaraBool ValidValue;
		ValidValue.SetValue(bValid);
		*OutValid.GetDestAndAdvance() = ValidValue;
		*OutR.GetDestAndAdvance() = Value.R;
		*OutG.GetDestAndAdvance() = Value.G;
		*OutB.GetDestAndAdvance() = Value.B;
		*OutA.GetDestAndAdvance() = Value.A;
	}
}

void UNiagaraDataInterfaceParticleRead::ReadQuat(FVectorVMContext& Context, FName AttributeToRead)
{
	VectorVM::FExternalFuncInputHandler<int32> ParticleIDIndexParam(Context);
	VectorVM::FExternalFuncInputHandler<int32> ParticleIDAcquireTagParam(Context);

	VectorVM::FUserPtrHandler<FNDIParticleRead_InstanceData> InstanceData(Context);

	VectorVM::FExternalFuncRegisterHandler<FNiagaraBool> OutValid(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutY(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutZ(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutW(Context);

	for (int32 InstanceIdx = 0; InstanceIdx < Context.NumInstances; ++InstanceIdx)
	{
		FNiagaraID ParticleID = { ParticleIDIndexParam.GetAndAdvance(), ParticleIDAcquireTagParam.GetAndAdvance() };
		bool bValid;

		FQuat Value = RetrieveValueWithCheck<FQuat>(InstanceData->EmitterInstance, FNiagaraTypeDefinition::GetQuatDef(), AttributeToRead, ParticleID, bValid);

		FNiagaraBool ValidValue;
		ValidValue.SetValue(bValid);
		*OutValid.GetDestAndAdvance() = ValidValue;
		*OutX.GetDestAndAdvance() = Value.X;
		*OutY.GetDestAndAdvance() = Value.Y;
		*OutZ.GetDestAndAdvance() = Value.Z;
		*OutW.GetDestAndAdvance() = Value.W;
	}
}

template <typename T>
T UNiagaraDataInterfaceParticleRead::RetrieveValueWithCheck(FNiagaraEmitterInstance* EmitterInstance, const FNiagaraTypeDefinition& Type, const FName& Attr, const FNiagaraID& ParticleID, bool& bValid)
{
	TArray<int32>& IDTable = EmitterInstance->GetData().GetCurrentData()->GetIDTable();
	if (ParticleID.Index < 0 || ParticleID.Index >= IDTable.Num())
	{
		bValid = false;
		return T();
	}
	else
	{
		FNiagaraVariable ReadVar(Type, Attr);
		FNiagaraDataSetAccessor<T> ValueData(EmitterInstance->GetData(), ReadVar);

		FNiagaraVariable IDVar(FNiagaraTypeDefinition::GetIDDef(), "ID");
		FNiagaraDataSetAccessor<FNiagaraID> IDData(EmitterInstance->GetData(), IDVar);

		int32 CorrectIndex = IDTable[ParticleID.Index];
		T Value = T();
		FNiagaraID ID = NIAGARA_INVALID_ID;
		if (CorrectIndex >= 0)
		{
			ID = IDData.GetSafe(CorrectIndex, NIAGARA_INVALID_ID);
			Value = ValueData.GetSafe(CorrectIndex, T());
		}
		bValid = (ID != NIAGARA_INVALID_ID);
		return Value;
	}
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

void UNiagaraDataInterfaceParticleRead::GetCommonHLSL(FString& OutHLSL)
{
}

void UNiagaraDataInterfaceParticleRead::GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL)
{
	static const TCHAR *FormatDeclarations = TEXT(
		"Buffer<int> {IDToIndexTableName};\n"
		"Buffer<float> {InputFloatBufferName};\n"
		"Buffer<int> {InputIntBufferName};\n"
		"uint {ParticleStrideFloatName};\n"
		"uint {ParticleStrideIntName};\n"
		"int {AttributeIndicesName}[{AttributeCount}];\n"
		"int {AcquireTagRegisterIndexName};\n\n"
	);

	TMap<FString, FStringFormatArg> ArgsDeclarations;
	ArgsDeclarations.Add(TEXT("IDToIndexTableName"), IDToIndexTableBaseName + ParamInfo.DataInterfaceHLSLSymbol);
	ArgsDeclarations.Add(TEXT("InputFloatBufferName"), InputFloatBufferBaseName + ParamInfo.DataInterfaceHLSLSymbol);
	ArgsDeclarations.Add(TEXT("InputIntBufferName"), InputIntBufferBaseName + ParamInfo.DataInterfaceHLSLSymbol);
	ArgsDeclarations.Add(TEXT("ParticleStrideFloatName"), ParticleStrideFloatBaseName + ParamInfo.DataInterfaceHLSLSymbol);
	ArgsDeclarations.Add(TEXT("ParticleStrideIntName"), ParticleStrideIntBaseName + ParamInfo.DataInterfaceHLSLSymbol);
	ArgsDeclarations.Add(TEXT("AttributeIndicesName"), AttributeIndicesBaseName + ParamInfo.DataInterfaceHLSLSymbol);
	ArgsDeclarations.Add(TEXT("AttributeCount"), ParamInfo.GeneratedFunctions.Num());
	ArgsDeclarations.Add(TEXT("AcquireTagRegisterIndexName"), AcquireTagRegisterIndexBaseName + ParamInfo.DataInterfaceHLSLSymbol);

	OutHLSL += FString::Format(FormatDeclarations, ArgsDeclarations);
}

static bool GenerateGetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, ENiagaraParticleDataComponentType ComponentType, int NumComponents, FString& OutHLSL)
{
	static const TCHAR* FuncTemplate = TEXT(
		"void {FunctionName}(NiagaraID In_ParticleID, out bool Out_Valid, out {ValueType} Out_Value)\n"
		"{\n"
		"    int RegisterIndex = {AttributeIndicesName}[{FunctionInstanceIndex}];\n"
		"    int ParticleIndex = (RegisterIndex != -1) && (In_ParticleID.Index >= 0) ? {IDToIndexTableName}[In_ParticleID.Index] : -1;\n"
		"    int AcquireTag = (ParticleIndex != -1) ? {InputIntBufferName}[{AcquireTagRegisterIndexName}*{ParticleStrideIntName} + ParticleIndex] : 0;\n"
		"    if(ParticleIndex != -1 && In_ParticleID.AcquireTag == AcquireTag)\n"
		"    {\n"
		"        Out_Valid = true;\n"
		"{FetchValueCode}"
		"    }\n"
		"    else\n"
		"    {\n"
		"        Out_Valid = false;\n"
		"        Out_Value = {ValueType}(0{ExtraDefaultValues});\n"
		"    }\n"
		"}\n\n"
	);

	const FString ParticleStrideFloatName = ParticleStrideFloatBaseName + ParamInfo.DataInterfaceHLSLSymbol;
	const FString ParticleStrideIntName = ParticleStrideIntBaseName + ParamInfo.DataInterfaceHLSLSymbol;
	const FString InputFloatBufferName = InputFloatBufferBaseName + ParamInfo.DataInterfaceHLSLSymbol;
	const FString InputIntBufferName = InputIntBufferBaseName + ParamInfo.DataInterfaceHLSLSymbol;

	const TCHAR* ComponentTypeName;
	const TCHAR* InputBufferName;
	const TCHAR* InputBufferStrideName;
	switch (ComponentType)
	{
		case ENiagaraParticleDataComponentType::Float:
			ComponentTypeName = TEXT("float");
			InputBufferName = *InputFloatBufferName;
			InputBufferStrideName = *ParticleStrideFloatName;
			break;
		case ENiagaraParticleDataComponentType::Int:
			ComponentTypeName = TEXT("int");
			InputBufferName = *InputIntBufferName;
			InputBufferStrideName = *ParticleStrideIntName;
			break;
		case ENiagaraParticleDataComponentType::Bool:
			ComponentTypeName = TEXT("bool");
			InputBufferName = *InputIntBufferName;
			InputBufferStrideName = *ParticleStrideIntName;
			break;
		default:
			UE_LOG(LogNiagara, Error, TEXT("Unknown component type %d while generating function %s"), ComponentType, *FunctionInfo.InstanceName);
			return false;
	}

	FString ExtraDefaultValues;
	for (int ComponentIndex = 1; ComponentIndex < NumComponents; ++ComponentIndex)
	{
		ExtraDefaultValues += TEXT(", 0");
	}

	FString FetchValueCode;
	for (int ComponentIndex = 0; ComponentIndex < NumComponents; ++ComponentIndex)
	{
		static const TCHAR* ComponentNames[] = { TEXT(".x"), TEXT(".y"), TEXT(".z"), TEXT(".w") };
		const TCHAR* ComponentName = (NumComponents > 1) ? ComponentNames[ComponentIndex] : TEXT("");
		const FString FetchComponentCode = FString::Printf(TEXT("        Out_Value%s = %s(%s[(RegisterIndex + %d)*%s + ParticleIndex]);\n"), ComponentName, ComponentTypeName, InputBufferName, ComponentIndex, InputBufferStrideName);
		FetchValueCode += FetchComponentCode;
	}

	FString ValueTypeName = (NumComponents > 1) ? FString::Printf(TEXT("%s%d"), ComponentTypeName, NumComponents) : FString(ComponentTypeName);

	TMap<FString, FStringFormatArg> FuncTemplateArgs;
	FuncTemplateArgs.Add(TEXT("FunctionName"), FunctionInfo.InstanceName);
	FuncTemplateArgs.Add(TEXT("ValueType"), ValueTypeName);
	FuncTemplateArgs.Add(TEXT("AttributeIndicesName"), AttributeIndicesBaseName + ParamInfo.DataInterfaceHLSLSymbol);
	FuncTemplateArgs.Add(TEXT("FunctionInstanceIndex"), FunctionInstanceIndex);
	FuncTemplateArgs.Add(TEXT("IDToIndexTableName"), IDToIndexTableBaseName + ParamInfo.DataInterfaceHLSLSymbol);
	FuncTemplateArgs.Add(TEXT("InputIntBufferName"), InputIntBufferName);
	FuncTemplateArgs.Add(TEXT("AcquireTagRegisterIndexName"), AcquireTagRegisterIndexBaseName + ParamInfo.DataInterfaceHLSLSymbol);
	FuncTemplateArgs.Add(TEXT("ParticleStrideIntName"), ParticleStrideIntName);
	FuncTemplateArgs.Add(TEXT("FetchValueCode"), FetchValueCode);
	FuncTemplateArgs.Add(TEXT("ExtraDefaultValues"), ExtraDefaultValues);
	
	OutHLSL += FString::Format(FuncTemplate, FuncTemplateArgs);

	return true;
}

bool UNiagaraDataInterfaceParticleRead::GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL)
{
	if (FunctionInfo.DefinitionName == GetIntAttributeFunctionName)
	{
		return GenerateGetFunctionHLSL(ParamInfo, FunctionInfo, FunctionInstanceIndex, ENiagaraParticleDataComponentType::Int, 1, OutHLSL);
	}

	if (FunctionInfo.DefinitionName == GetFloatAttributeFunctionName)
	{
		return GenerateGetFunctionHLSL(ParamInfo, FunctionInfo, FunctionInstanceIndex, ENiagaraParticleDataComponentType::Float, 1, OutHLSL);
	}

	if (FunctionInfo.DefinitionName == GetVec2AttributeFunctionName)
	{
		return GenerateGetFunctionHLSL(ParamInfo, FunctionInfo, FunctionInstanceIndex, ENiagaraParticleDataComponentType::Float, 2, OutHLSL);
	}

	if (FunctionInfo.DefinitionName == GetVec3AttributeFunctionName)
	{
		return GenerateGetFunctionHLSL(ParamInfo, FunctionInfo, FunctionInstanceIndex, ENiagaraParticleDataComponentType::Float, 3, OutHLSL);
	}

	if (FunctionInfo.DefinitionName == GetVec4AttributeFunctionName)
	{
		return GenerateGetFunctionHLSL(ParamInfo, FunctionInfo, FunctionInstanceIndex, ENiagaraParticleDataComponentType::Float, 4, OutHLSL);
	}

	if (FunctionInfo.DefinitionName == GetBoolAttributeFunctionName)
	{
		return GenerateGetFunctionHLSL(ParamInfo, FunctionInfo, FunctionInstanceIndex, ENiagaraParticleDataComponentType::Bool, 1, OutHLSL);
	}

	if (FunctionInfo.DefinitionName == GetColorAttributeFunctionName)
	{
		return GenerateGetFunctionHLSL(ParamInfo, FunctionInfo, FunctionInstanceIndex, ENiagaraParticleDataComponentType::Float, 4, OutHLSL);
	}

	if (FunctionInfo.DefinitionName == GetQuatAttributeFunctionName)
	{
		return GenerateGetFunctionHLSL(ParamInfo, FunctionInfo, FunctionInstanceIndex, ENiagaraParticleDataComponentType::Float, 4, OutHLSL);
	}

	return false;
}

FNiagaraDataInterfaceParametersCS* UNiagaraDataInterfaceParticleRead::ConstructComputeParameters() const
{
	return new FNiagaraDataInterfaceParametersCS_ParticleRead();
}

void UNiagaraDataInterfaceParticleRead::ProvidePerInstanceDataForRenderThread(void* DataForRenderThread, void* PerInstanceData, const FNiagaraSystemInstanceID& SystemInstance)
{
	FNDIParticleRead_InstanceDataGPU* DataToPass = new (DataForRenderThread) FNDIParticleRead_InstanceDataGPU;
	FNDIParticleRead_InstanceData* PIData = static_cast<FNDIParticleRead_InstanceData*>(PerInstanceData);
	if (PIData && PIData->EmitterInstance)
	{
		DataToPass->SourceEmitterGPUContext = PIData->EmitterInstance->GetGPUContext();
		DataToPass->SourceEmitterName = PIData->EmitterInstance->GetCachedEmitter()->GetUniqueEmitterName();
	}
}

#undef LOCTEXT_NAMESPACE
