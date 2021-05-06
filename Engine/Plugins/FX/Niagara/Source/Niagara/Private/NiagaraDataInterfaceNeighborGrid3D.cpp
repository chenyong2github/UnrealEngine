// Copyright Epic Games, Inc. All Rights Reserved.
#include "NiagaraDataInterfaceNeighborGrid3D.h"
#include "NiagaraShader.h"
#include "ShaderParameterUtils.h"
#include "NiagaraEmitterInstanceBatcher.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraRenderer.h"
#include "NiagaraShaderParticleID.h"

#define LOCTEXT_NAMESPACE "NiagaraDataInterfaceNeighborGrid3D"

static const FString MaxNeighborsPerCellName(TEXT("MaxNeighborsPerCellValue_"));
static const FString ParticleNeighborsName(TEXT("ParticleNeighbors_"));
static const FString ParticleNeighborCountName(TEXT("ParticleNeighborCount_"));
static const FString OutputParticleNeighborsName(TEXT("OutputParticleNeighbors_"));
static const FString OutputParticleNeighborCountName(TEXT("OutputParticleNeighborCount_"));


const FName UNiagaraDataInterfaceNeighborGrid3D::SetNumCellsFunctionName("SetNumCells");


// Global VM function names, also used by the shaders code generation methods.

static const FName MaxNeighborsPerCellFunctionName("MaxNeighborsPerCell");
static const FName NeighborGridIndexToLinearFunctionName("NeighborGridIndexToLinear");
static const FName GetParticleNeighborFunctionName("GetParticleNeighbor");
static const FName SetParticleNeighborFunctionName("SetParticleNeighbor");
static const FName GetParticleNeighborCountFunctionName("GetParticleNeighborCount");
static const FName SetParticleNeighborCountFunctionName("SetParticleNeighborCount");


/*--------------------------------------------------------------------------------------------------------------------------*/
struct FNiagaraDataInterfaceParametersCS_NeighborGrid3D : public FNiagaraDataInterfaceParametersCS
{
	DECLARE_TYPE_LAYOUT(FNiagaraDataInterfaceParametersCS_NeighborGrid3D, NonVirtual);
public:
	void Bind(const FNiagaraDataInterfaceGPUParamInfo& ParameterInfo, const class FShaderParameterMap& ParameterMap)
	{
		NumCellsParam.Bind(ParameterMap, *(UNiagaraDataInterfaceRWBase::NumCellsName + ParameterInfo.DataInterfaceHLSLSymbol));
		UnitToUVParam.Bind(ParameterMap, *(UNiagaraDataInterfaceRWBase::UnitToUVName + ParameterInfo.DataInterfaceHLSLSymbol));
		CellSizeParam.Bind(ParameterMap, *(UNiagaraDataInterfaceRWBase::CellSizeName + ParameterInfo.DataInterfaceHLSLSymbol));

		MaxNeighborsPerCellParam.Bind(ParameterMap, *(MaxNeighborsPerCellName + ParameterInfo.DataInterfaceHLSLSymbol));
		WorldBBoxSizeParam.Bind(ParameterMap, *(UNiagaraDataInterfaceRWBase::WorldBBoxSizeName + ParameterInfo.DataInterfaceHLSLSymbol));
		
		ParticleNeighborsGridParam.Bind(ParameterMap,  *(ParticleNeighborsName + ParameterInfo.DataInterfaceHLSLSymbol));
		ParticleNeighborCountGridParam.Bind(ParameterMap,  *(ParticleNeighborCountName + ParameterInfo.DataInterfaceHLSLSymbol));

		OutputParticleNeighborsGridParam.Bind(ParameterMap, *(OutputParticleNeighborsName + ParameterInfo.DataInterfaceHLSLSymbol));
		OutputParticleNeighborCountGridParam.Bind(ParameterMap, *(OutputParticleNeighborCountName + ParameterInfo.DataInterfaceHLSLSymbol));
	}

	// #todo(dmp): make resource transitions batched
	void Set(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const
	{
		check(IsInRenderingThread());

		// Get shader and DI
		FRHIComputeShader* ComputeShaderRHI = Context.Shader.GetComputeShader();
		FNiagaraDataInterfaceProxyNeighborGrid3D* VFDI = static_cast<FNiagaraDataInterfaceProxyNeighborGrid3D*>(Context.DataInterface);

		NeighborGrid3DRWInstanceData* ProxyData = VFDI->SystemInstancesToProxyData.Find(Context.SystemInstanceID);
		float CellSizeTmp[3];

		if (!ProxyData)
		{
			CellSizeTmp[0] = CellSizeTmp[1] = CellSizeTmp[2] = 1.0f;
			SetShaderValue(RHICmdList, ComputeShaderRHI, NumCellsParam, 0);
			SetShaderValue(RHICmdList, ComputeShaderRHI, UnitToUVParam, FVector::ZeroVector);
			SetShaderValue(RHICmdList, ComputeShaderRHI, CellSizeParam, CellSizeTmp);
			SetShaderValue(RHICmdList, ComputeShaderRHI, MaxNeighborsPerCellParam, 0);
			SetShaderValue(RHICmdList, ComputeShaderRHI, WorldBBoxSizeParam, FVector(0.0f, 0.0f, 0.0f));
			SetSRVParameter(RHICmdList, ComputeShaderRHI, ParticleNeighborsGridParam, FNiagaraRenderer::GetDummyIntBuffer());
			SetSRVParameter(RHICmdList, ComputeShaderRHI, ParticleNeighborCountGridParam, FNiagaraRenderer::GetDummyIntBuffer());
			if (OutputParticleNeighborsGridParam.IsUAVBound())
			{
				RHICmdList.SetUAVParameter(ComputeShaderRHI, OutputParticleNeighborsGridParam.GetUAVIndex(), Context.Batcher->GetEmptyUAVFromPool(RHICmdList, PF_R32_SINT, ENiagaraEmptyUAVType::Buffer));
			}

			if (OutputParticleNeighborCountGridParam.IsUAVBound())
			{
				RHICmdList.SetUAVParameter(ComputeShaderRHI, OutputParticleNeighborCountGridParam.GetUAVIndex(), Context.Batcher->GetEmptyUAVFromPool(RHICmdList, PF_R32_SINT, ENiagaraEmptyUAVType::Buffer));
			}

			return;
		}

		SetShaderValue(RHICmdList, ComputeShaderRHI, NumCellsParam, ProxyData->NumCells);
		SetShaderValue(RHICmdList, ComputeShaderRHI, UnitToUVParam, FVector(1.0f) / FVector(ProxyData->NumCells));

		// #todo(dmp): remove this computation here
		CellSizeTmp[0] = ProxyData->WorldBBoxSize.X / ProxyData->NumCells.X;
		CellSizeTmp[1] = ProxyData->WorldBBoxSize.Y / ProxyData->NumCells.Y;
		CellSizeTmp[2] = ProxyData->WorldBBoxSize.Z / ProxyData->NumCells.Z;
		SetShaderValue(RHICmdList, ComputeShaderRHI, CellSizeParam, CellSizeTmp);
		SetShaderValue(RHICmdList, ComputeShaderRHI, MaxNeighborsPerCellParam, ProxyData->MaxNeighborsPerCell);
		SetShaderValue(RHICmdList, ComputeShaderRHI, WorldBBoxSizeParam, ProxyData->WorldBBoxSize);
		
		if (!Context.IsOutputStage)
		{
			if (ParticleNeighborsGridParam.IsBound())
			{
				RHICmdList.Transition(FRHITransitionInfo(ProxyData->NeighborhoodBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::SRVCompute));
				SetSRVParameter(RHICmdList, ComputeShaderRHI, ParticleNeighborsGridParam, ProxyData->NeighborhoodBuffer.SRV);
			}

			if (ParticleNeighborCountGridParam.IsBound())
			{
				RHICmdList.Transition(FRHITransitionInfo(ProxyData->NeighborhoodCountBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::SRVCompute));
				SetSRVParameter(RHICmdList, ComputeShaderRHI, ParticleNeighborCountGridParam, ProxyData->NeighborhoodCountBuffer.SRV);
			}

			if (OutputParticleNeighborsGridParam.IsUAVBound())
			{
				RHICmdList.SetUAVParameter(ComputeShaderRHI, OutputParticleNeighborsGridParam.GetUAVIndex(), Context.Batcher->GetEmptyUAVFromPool(RHICmdList, PF_R32_SINT, ENiagaraEmptyUAVType::Buffer));
			}

			if (OutputParticleNeighborCountGridParam.IsUAVBound())
			{
				RHICmdList.SetUAVParameter(ComputeShaderRHI, OutputParticleNeighborCountGridParam.GetUAVIndex(), Context.Batcher->GetEmptyUAVFromPool(RHICmdList, PF_R32_SINT, ENiagaraEmptyUAVType::Buffer));
			}
		}
		else
		{
			SetSRVParameter(RHICmdList, ComputeShaderRHI, ParticleNeighborsGridParam, FNiagaraRenderer::GetDummyIntBuffer());
			SetSRVParameter(RHICmdList, ComputeShaderRHI, ParticleNeighborCountGridParam, FNiagaraRenderer::GetDummyIntBuffer());

			if (OutputParticleNeighborsGridParam.IsBound())
			{
				RHICmdList.Transition(FRHITransitionInfo(ProxyData->NeighborhoodBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute));
				OutputParticleNeighborsGridParam.SetBuffer(RHICmdList, ComputeShaderRHI, ProxyData->NeighborhoodBuffer);
			}

			if (OutputParticleNeighborCountGridParam.IsBound())
			{
				RHICmdList.Transition(FRHITransitionInfo(ProxyData->NeighborhoodCountBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute));
				OutputParticleNeighborCountGridParam.SetBuffer(RHICmdList, ComputeShaderRHI, ProxyData->NeighborhoodCountBuffer);
			}
		}
		// Note: There is a flush in PreEditChange to make sure everything is synced up at this point 
	}

	void Unset(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const 
	{
		if (OutputParticleNeighborsGridParam.IsBound())
		{
			OutputParticleNeighborsGridParam.UnsetUAV(RHICmdList, Context.Shader.GetComputeShader());
		}

		if (OutputParticleNeighborCountGridParam.IsBound())
		{
			OutputParticleNeighborCountGridParam.UnsetUAV(RHICmdList, Context.Shader.GetComputeShader());
		}
	}

private:
	LAYOUT_FIELD(FShaderParameter, NumCellsParam);
	LAYOUT_FIELD(FShaderParameter, UnitToUVParam);
	LAYOUT_FIELD(FShaderParameter, CellSizeParam);
	LAYOUT_FIELD(FShaderParameter, MaxNeighborsPerCellParam);
	LAYOUT_FIELD(FShaderParameter, WorldBBoxSizeParam);
	LAYOUT_FIELD(FShaderResourceParameter, ParticleNeighborsGridParam);
	LAYOUT_FIELD(FShaderResourceParameter, ParticleNeighborCountGridParam);
	LAYOUT_FIELD(FRWShaderParameter, OutputParticleNeighborCountGridParam);
	LAYOUT_FIELD(FRWShaderParameter, OutputParticleNeighborsGridParam);
};

IMPLEMENT_TYPE_LAYOUT(FNiagaraDataInterfaceParametersCS_NeighborGrid3D);

IMPLEMENT_NIAGARA_DI_PARAMETER(UNiagaraDataInterfaceNeighborGrid3D, FNiagaraDataInterfaceParametersCS_NeighborGrid3D);

UNiagaraDataInterfaceNeighborGrid3D::UNiagaraDataInterfaceNeighborGrid3D(FObjectInitializer const& ObjectInitializer)
	: Super(ObjectInitializer)
	, MaxNeighborsPerCell(8)
{
	SetResolutionMethod = ESetResolutionMethod::CellSize;

	Proxy.Reset(new FNiagaraDataInterfaceProxyNeighborGrid3D());	
}


void UNiagaraDataInterfaceNeighborGrid3D::GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)
{
	Super::GetFunctions(OutFunctions);

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = SetNumCellsFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("NumCellsX")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("NumCellsY")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("NumCellsZ")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("MaxNeighborsPerCell")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Success")));

		Sig.ModuleUsageBitmask = ENiagaraScriptUsageMask::Emitter | ENiagaraScriptUsageMask::System;
		Sig.bExperimental = true;
		Sig.bMemberFunction = true;
		Sig.bRequiresExecPin = true;
		Sig.bRequiresContext = false;
		Sig.bSupportsCPU = true;
		Sig.bSupportsGPU = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = MaxNeighborsPerCellFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("MaxNeighborsPerCell")));

		Sig.bExperimental = true;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = NeighborGridIndexToLinearFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexX")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexY")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexZ")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Neighbor")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Linear Index")));

		Sig.bExperimental = true;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetParticleNeighborFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Linear")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("NeighborIndex")));

		Sig.bExperimental = true;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = SetParticleNeighborFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Linear")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("NeighborIndex")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IGNORE")));

		Sig.bExperimental = true;
		Sig.bWriteFunction = true;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetParticleNeighborCountFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Linear")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("NeighborCount")));

		Sig.bExperimental = true;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = SetParticleNeighborCountFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Linear")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Increment")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("PrevNeighborCount")));

		Sig.bExperimental = true;
		Sig.bWriteFunction = true;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}
}

DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceNeighborGrid3D, SetNumCells);
void UNiagaraDataInterfaceNeighborGrid3D::GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc)
{
	Super::GetVMExternalFunction(BindingInfo, InstanceData, OutFunc);

	// #todo(dmp): this overrides the empty function set by the super class
	if (BindingInfo.Name == UNiagaraDataInterfaceRWBase::WorldBBoxSizeFunctionName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 3);
		OutFunc = FVMExternalFunction::CreateLambda([&](FVectorVMContext& Context) { GetWorldBBoxSize(Context); });
	}
	else if (BindingInfo.Name == UNiagaraDataInterfaceRWBase::NumCellsFunctionName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 3);
		OutFunc = FVMExternalFunction::CreateLambda([&](FVectorVMContext& Context) { GetNumCells(Context); });
	}
	else if (BindingInfo.Name == MaxNeighborsPerCellFunctionName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 1);
		OutFunc = FVMExternalFunction::CreateLambda([&](FVectorVMContext& Context) { GetMaxNeighborsPerCell(Context); });
	}
	else if (BindingInfo.Name == SetNumCellsFunctionName)
	{
		check(BindingInfo.GetNumInputs() == 5 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceNeighborGrid3D, SetNumCells)::Bind(this, OutFunc);
	}
	//else if (BindingInfo.Name == NeighborGridIndexToLinearFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	//else if (BindingInfo.Name == GetParticleNeighborFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	//else if (BindingInfo.Name == SetParticleNeighborFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	//else if (BindingInfo.Name == GetParticleNeighborCountFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	//else if (BindingInfo.Name == SetParticleNeighborCountFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
}

void UNiagaraDataInterfaceNeighborGrid3D::GetWorldBBoxSize(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<NeighborGrid3DRWInstanceData> InstData(Context);

	FNDIOutputParam<FVector> OutWorldBounds(Context);

	for (int32 InstanceIdx = 0; InstanceIdx < Context.NumInstances; ++InstanceIdx)
	{
		OutWorldBounds.SetAndAdvance(WorldBBoxSize);
	}
}

void UNiagaraDataInterfaceNeighborGrid3D::GetNumCells(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<NeighborGrid3DRWInstanceData> InstData(Context);

	FNDIOutputParam<int32> NumCellsX(Context);
	FNDIOutputParam<int32> NumCellsY(Context);
	FNDIOutputParam<int32> NumCellsZ(Context);

	for (int32 InstanceIdx = 0; InstanceIdx < Context.NumInstances; ++InstanceIdx)
	{
		NumCellsX.SetAndAdvance(NumCells.X);
		NumCellsY.SetAndAdvance(NumCells.Y);
		NumCellsZ.SetAndAdvance(NumCells.Z);
	}
}

void UNiagaraDataInterfaceNeighborGrid3D::GetMaxNeighborsPerCell(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<NeighborGrid3DRWInstanceData> InstData(Context);

	FNDIOutputParam<int32> OutMaxNeighborsPerCell(Context);

	for (int32 InstanceIdx = 0; InstanceIdx < Context.NumInstances; ++InstanceIdx)
	{
		OutMaxNeighborsPerCell.SetAndAdvance(InstData->MaxNeighborsPerCell);
	}
}

bool UNiagaraDataInterfaceNeighborGrid3D::Equals(const UNiagaraDataInterface* Other) const
{
	if (!Super::Equals(Other))
	{
		return false;
	}
	const UNiagaraDataInterfaceNeighborGrid3D* OtherTyped = CastChecked<const UNiagaraDataInterfaceNeighborGrid3D>(Other);

	return OtherTyped->MaxNeighborsPerCell == MaxNeighborsPerCell;
}

#if WITH_EDITORONLY_DATA
void UNiagaraDataInterfaceNeighborGrid3D::GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL)
{
	Super::GetParameterDefinitionHLSL(ParamInfo, OutHLSL);

	static const TCHAR *FormatDeclarations = TEXT(R"(
		int {MaxNeighborsPerCellName};
		Buffer<int> {ParticleNeighborsName};
		Buffer<int> {ParticleNeighborCountName};
		RWBuffer<int> RW{OutputParticleNeighborsName};
		RWBuffer<int> RW{OutputParticleNeighborCountName};
	)");
	TMap<FString, FStringFormatArg> ArgsDeclarations = {
		{ TEXT("MaxNeighborsPerCellName"),  MaxNeighborsPerCellName + ParamInfo.DataInterfaceHLSLSymbol },
		{ TEXT("ParticleNeighborsName"),    ParticleNeighborsName + ParamInfo.DataInterfaceHLSLSymbol },
		{ TEXT("ParticleNeighborCountName"),    ParticleNeighborCountName + ParamInfo.DataInterfaceHLSLSymbol },
		{ TEXT("OutputParticleNeighborsName"),    OutputParticleNeighborsName + ParamInfo.DataInterfaceHLSLSymbol },
		{ TEXT("OutputParticleNeighborCountName"),    OutputParticleNeighborCountName + ParamInfo.DataInterfaceHLSLSymbol },
	};
	OutHLSL += FString::Format(FormatDeclarations, ArgsDeclarations);
}

bool UNiagaraDataInterfaceNeighborGrid3D::GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL)
{
	bool ParentRet = Super::GetFunctionHLSL(ParamInfo, FunctionInfo, FunctionInstanceIndex, OutHLSL);
	if (ParentRet)
	{
		return true;
	}
	else if (FunctionInfo.DefinitionName == UNiagaraDataInterfaceRWBase::NumCellsFunctionName)
	{
		static const TCHAR* FormatHLSL = TEXT(R"(
			void {FunctionName}(out int OutNumCellsX, out int OutNumCellsY, out int OutNumCellsZ)
			{
				OutNumCellsX = {NumCellsName}.x;
				OutNumCellsY = {NumCellsName}.y;
				OutNumCellsZ = {NumCellsName}.z;
			}
		)");
		TMap<FString, FStringFormatArg> FormatArgs =
		{
			{TEXT("FunctionName"), FunctionInfo.InstanceName},
			{TEXT("NumCellsName"), UNiagaraDataInterfaceRWBase::NumCellsName + ParamInfo.DataInterfaceHLSLSymbol},
			{TEXT("UnitToUVName"), UNiagaraDataInterfaceRWBase::UnitToUVName + ParamInfo.DataInterfaceHLSLSymbol},
		};
		OutHLSL += FString::Format(FormatHLSL, FormatArgs);
	}
	else if (FunctionInfo.DefinitionName == MaxNeighborsPerCellFunctionName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {FunctionName}(out int Out_MaxNeighborsPerCell)
			{
				Out_MaxNeighborsPerCell = {MaxNeighborsPerCellName};
			}
		)");
		TMap<FString, FStringFormatArg> ArgsSample = {
			{TEXT("FunctionName"), FunctionInfo.InstanceName},
			{TEXT("MaxNeighborsPerCellName"), MaxNeighborsPerCellName + ParamInfo.DataInterfaceHLSLSymbol},

		};
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == NeighborGridIndexToLinearFunctionName)
	{
		static const TCHAR *FormatBounds = TEXT(R"(
			void {FunctionName}(int In_IndexX, int In_IndexY, int In_IndexZ, int In_Neighbor, out int Out_Linear)
			{
				Out_Linear = In_Neighbor + In_IndexX * {MaxNeighborsPerCellName} + In_IndexY * {MaxNeighborsPerCellName}*{NumCellsName}.x + In_IndexZ * {MaxNeighborsPerCellName}*{NumCellsName}.x*{NumCellsName}.y;
			}
		)");
		TMap<FString, FStringFormatArg> ArgsBounds = {
			{TEXT("FunctionName"), FunctionInfo.InstanceName},
			{TEXT("MaxNeighborsPerCellName"), MaxNeighborsPerCellName + ParamInfo.DataInterfaceHLSLSymbol},
			{TEXT("NumCellsName"), UNiagaraDataInterfaceRWBase::NumCellsName + ParamInfo.DataInterfaceHLSLSymbol},
			{TEXT("UnitToUVName"), UNiagaraDataInterfaceRWBase::UnitToUVName + ParamInfo.DataInterfaceHLSLSymbol},
		};
		OutHLSL += FString::Format(FormatBounds, ArgsBounds);
		return true;
	}
	else if (FunctionInfo.DefinitionName == GetParticleNeighborFunctionName)
	{
		static const TCHAR *FormatBounds = TEXT(R"(
			void {FunctionName}(int In_Index, out int Out_ParticleNeighborIndex)
			{
				Out_ParticleNeighborIndex = {ParticleNeighbors}[In_Index];				
			}
		)");
		TMap<FString, FStringFormatArg> ArgsBounds = {
			{TEXT("FunctionName"), FunctionInfo.InstanceName},
			{TEXT("ParticleNeighbors"), ParticleNeighborsName + ParamInfo.DataInterfaceHLSLSymbol},
		};
		OutHLSL += FString::Format(FormatBounds, ArgsBounds);
		return true;
	}
	else if (FunctionInfo.DefinitionName == SetParticleNeighborFunctionName)
	{
		static const TCHAR *FormatBounds = TEXT(R"(
			void {FunctionName}(int In_Index, int In_ParticleNeighborIndex, out int Out_Ignore)
			{
				RW{OutputParticleNeighbors}[In_Index] = In_ParticleNeighborIndex;				
				Out_Ignore = 0;
			}
		)");
		TMap<FString, FStringFormatArg> ArgsBounds = {
			{TEXT("FunctionName"), FunctionInfo.InstanceName},
			{TEXT("OutputParticleNeighbors"), OutputParticleNeighborsName + ParamInfo.DataInterfaceHLSLSymbol},
		};
		OutHLSL += FString::Format(FormatBounds, ArgsBounds);
		return true;
	}
	else if (FunctionInfo.DefinitionName == GetParticleNeighborCountFunctionName)
	{
		static const TCHAR *FormatBounds = TEXT(R"(
			void {FunctionName}(int In_Index, out int Out_ParticleNeighborIndex)
			{
				Out_ParticleNeighborIndex = {ParticleNeighborCount}[In_Index];				
			}
		)");
		TMap<FString, FStringFormatArg> ArgsBounds = {
			{TEXT("FunctionName"), FunctionInfo.InstanceName},
			{TEXT("ParticleNeighborCount"), ParticleNeighborCountName + ParamInfo.DataInterfaceHLSLSymbol},
		};
		OutHLSL += FString::Format(FormatBounds, ArgsBounds);
		return true;
	}
	else if (FunctionInfo.DefinitionName == SetParticleNeighborCountFunctionName)
	{
		static const TCHAR *FormatBounds = TEXT(R"(
			void {FunctionName}(int In_Index, int In_Increment, out int PreviousNeighborCount)
			{				
				InterlockedAdd(RW{OutputParticleNeighborCount}[In_Index], In_Increment, PreviousNeighborCount);				
			}
		)");
		TMap<FString, FStringFormatArg> ArgsBounds = {
			{TEXT("FunctionName"), FunctionInfo.InstanceName},
			{TEXT("OutputParticleNeighborCount"), OutputParticleNeighborCountName + ParamInfo.DataInterfaceHLSLSymbol},
		};
		OutHLSL += FString::Format(FormatBounds, ArgsBounds);
		return true;
	}

	return false;
}
#endif

bool UNiagaraDataInterfaceNeighborGrid3D::InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{

	NeighborGrid3DRWInstanceData* InstanceData = new (PerInstanceData) NeighborGrid3DRWInstanceData();

	FNiagaraDataInterfaceProxyNeighborGrid3D* RT_Proxy = GetProxyAs<FNiagaraDataInterfaceProxyNeighborGrid3D>();

	FIntVector RT_NumCells = NumCells;
	uint32 RT_MaxNeighborsPerCell = MaxNeighborsPerCell;	
	FVector RT_WorldBBoxSize = WorldBBoxSize;
	TSet<int> RT_OutputShaderStages = OutputShaderStages;
	TSet<int> RT_IterationShaderStages = IterationShaderStages;

	float TmpCellSize = RT_WorldBBoxSize[0] / RT_NumCells[0];

	if (SetResolutionMethod == ESetResolutionMethod::MaxAxis)
	{
		TmpCellSize = FMath::Max(FMath::Max(WorldBBoxSize.X, WorldBBoxSize.Y), WorldBBoxSize.Z) / NumCellsMaxAxis;
	}
	else if (SetResolutionMethod == ESetResolutionMethod::CellSize)
	{
		TmpCellSize = CellSize;
	}

	// compute world bounds and padding based on cell size
	if (SetResolutionMethod == ESetResolutionMethod::MaxAxis || SetResolutionMethod == ESetResolutionMethod::CellSize)
	{
		RT_NumCells.X = WorldBBoxSize.X / TmpCellSize;
		RT_NumCells.Y = WorldBBoxSize.Y / TmpCellSize;
		RT_NumCells.Z = WorldBBoxSize.Z / TmpCellSize;

		// Pad grid by 1 cell if our computed bounding box is too small
		if (WorldBBoxSize.X > WorldBBoxSize.Y && WorldBBoxSize.X > WorldBBoxSize.Z)
		{
			if (!FMath::IsNearlyEqual(TmpCellSize * RT_NumCells.Y, WorldBBoxSize.Y))
			{
				RT_NumCells.Y++;
			}

			if (!FMath::IsNearlyEqual(TmpCellSize * RT_NumCells.Z, WorldBBoxSize.Z))
			{
				RT_NumCells.Z++;
			}
		}
		else if (WorldBBoxSize.Y > WorldBBoxSize.X && WorldBBoxSize.Y > WorldBBoxSize.Z)
		{
			if (!FMath::IsNearlyEqual(TmpCellSize * RT_NumCells.X, WorldBBoxSize.X))
			{
				RT_NumCells.X++;
			}

			if (!FMath::IsNearlyEqual(TmpCellSize * RT_NumCells.Z, WorldBBoxSize.Z))
			{
				RT_NumCells.Z++;
			}
		}
		else if (WorldBBoxSize.Z > WorldBBoxSize.X && WorldBBoxSize.Z > WorldBBoxSize.Y)
		{
			if (!FMath::IsNearlyEqual(TmpCellSize * RT_NumCells.X, WorldBBoxSize.X))
			{
				RT_NumCells.X++;
			}

			if (!FMath::IsNearlyEqual(TmpCellSize * RT_NumCells.Y, WorldBBoxSize.Y))
			{
				RT_NumCells.Y++;
			}
		}

		RT_WorldBBoxSize = FVector(RT_NumCells.X, RT_NumCells.Y, RT_NumCells.Z) * TmpCellSize;		 
	}
	RT_NumCells.X = FMath::Max(RT_NumCells.X, 1);
	RT_NumCells.Y = FMath::Max(RT_NumCells.Y, 1);
	RT_NumCells.Z = FMath::Max(RT_NumCells.Z, 1);

	InstanceData->CellSize = TmpCellSize;
	InstanceData->WorldBBoxSize = RT_WorldBBoxSize;
	InstanceData->MaxNeighborsPerCell = RT_MaxNeighborsPerCell;	
	InstanceData->NumCells = RT_NumCells;

	// @todo-threadsafety. This would be a race but I'm taking a ref here. Not ideal in the long term.
	// Push Updates to Proxy.
	ENQUEUE_RENDER_COMMAND(FUpdateData)(
		[RT_Proxy, RT_NumCells, RT_MaxNeighborsPerCell, RT_WorldBBoxSize, RT_OutputShaderStages, RT_IterationShaderStages, InstanceID = SystemInstance->GetId()](FRHICommandListImmediate& RHICmdList)
	{
		check(!RT_Proxy->SystemInstancesToProxyData.Contains(InstanceID));
		NeighborGrid3DRWInstanceData* TargetData = &RT_Proxy->SystemInstancesToProxyData.Add(InstanceID);

		TargetData->NumCells = RT_NumCells;
		TargetData->MaxNeighborsPerCell = RT_MaxNeighborsPerCell;		
		TargetData->WorldBBoxSize = RT_WorldBBoxSize;

		RT_Proxy->OutputSimulationStages_DEPRECATED = RT_OutputShaderStages;
		RT_Proxy->IterationSimulationStages_DEPRECATED = RT_IterationShaderStages;

		TargetData->ResizeBuffers();
	});

	return true;
}

void UNiagaraDataInterfaceNeighborGrid3D::SetNumCells(FVectorVMContext& Context)
{
	// This should only be called from a system or emitter script due to a need for only setting up initially.
	VectorVM::FUserPtrHandler<NeighborGrid3DRWInstanceData> InstData(Context);
	VectorVM::FExternalFuncInputHandler<int> InNumCellsX(Context);
	VectorVM::FExternalFuncInputHandler<int> InNumCellsY(Context);
	VectorVM::FExternalFuncInputHandler<int> InNumCellsZ(Context);
	VectorVM::FExternalFuncInputHandler<int> InMaxNeighborsPerCell(Context);
	VectorVM::FExternalFuncRegisterHandler<FNiagaraBool> OutSuccess(Context);

	for (int32 InstanceIdx = 0; InstanceIdx < Context.NumInstances; ++InstanceIdx)
	{
		int NewNumCellsX = InNumCellsX.GetAndAdvance();
		int NewNumCellsY = InNumCellsY.GetAndAdvance();
		int NewNumCellsZ = InNumCellsZ.GetAndAdvance();
		int NewMaxNeighborsPerCell = InMaxNeighborsPerCell.GetAndAdvance();
		bool bSuccess = (InstData.Get() != nullptr && Context.NumInstances == 1 && NumCells.X >= 0 && NumCells.Y >= 0 && NumCells.Z >= 0 && MaxNeighborsPerCell >= 0);
		*OutSuccess.GetDestAndAdvance() = bSuccess;
		if (bSuccess)
		{
			FIntVector OldNumCells = InstData->NumCells;
			int OldMaxNeighborsPerCell = InstData->MaxNeighborsPerCell;

			InstData->NumCells.X = NewNumCellsX;
			InstData->NumCells.Y = NewNumCellsY;
			InstData->NumCells.Z = NewNumCellsZ;
			InstData->MaxNeighborsPerCell = NewMaxNeighborsPerCell;
		
			InstData->NeedsRealloc = OldNumCells != InstData->NumCells && OldMaxNeighborsPerCell != InstData->MaxNeighborsPerCell;
		}
	}
}

bool UNiagaraDataInterfaceNeighborGrid3D::PerInstanceTickPostSimulate(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds)
{
	NeighborGrid3DRWInstanceData* InstanceData = static_cast<NeighborGrid3DRWInstanceData*>(PerInstanceData);
	bool bNeedsReset = false;

	if (InstanceData->NeedsRealloc && InstanceData->NumCells.X > 0 && InstanceData->NumCells.Y > 0 && InstanceData->NumCells.Z > 0 && InstanceData->MaxNeighborsPerCell > 0)
	{
		InstanceData->NeedsRealloc = false;

		InstanceData->CellSize = (InstanceData->WorldBBoxSize / FVector(InstanceData->NumCells.X, InstanceData->NumCells.Y, InstanceData->NumCells.Z))[0];

		FNiagaraDataInterfaceProxyNeighborGrid3D* RT_Proxy = GetProxyAs<FNiagaraDataInterfaceProxyNeighborGrid3D>();
		ENQUEUE_RENDER_COMMAND(FUpdateData)(
			[RT_Proxy, RT_NumCells = InstanceData->NumCells, RT_MaxNeighborsPerCell = InstanceData->MaxNeighborsPerCell, RT_CellSize = InstanceData->CellSize, InstanceID = SystemInstance->GetId()](FRHICommandListImmediate& RHICmdList)
		{
			check(RT_Proxy->SystemInstancesToProxyData.Contains(InstanceID));
			NeighborGrid3DRWInstanceData* TargetData = &RT_Proxy->SystemInstancesToProxyData.Add(InstanceID);

			TargetData->NumCells = RT_NumCells;
			TargetData->MaxNeighborsPerCell = RT_MaxNeighborsPerCell;			
			TargetData->CellSize = RT_CellSize;
			TargetData->ResizeBuffers();
		});
	}

	return false;
}


void UNiagaraDataInterfaceNeighborGrid3D::DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{		
	NeighborGrid3DRWInstanceData* InstanceData = static_cast<NeighborGrid3DRWInstanceData*>(PerInstanceData);
	InstanceData->~NeighborGrid3DRWInstanceData();

	FNiagaraDataInterfaceProxyNeighborGrid3D* ThisProxy = GetProxyAs<FNiagaraDataInterfaceProxyNeighborGrid3D>();
	if (!ThisProxy)
		return;

	ENQUEUE_RENDER_COMMAND(FNiagaraDIDestroyInstanceData) (
		[ThisProxy, InstanceID = SystemInstance->GetId(), Batcher = SystemInstance->GetBatcher()](FRHICommandListImmediate& CmdList)
	{
		//check(ThisProxy->SystemInstancesToProxyData.Contains(InstanceID));
		ThisProxy->SystemInstancesToProxyData.Remove(InstanceID);
	}
	);
}

void FNiagaraDataInterfaceProxyNeighborGrid3D::PreStage(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceStageArgs& Context)
{
	if (Context.IsOutputStage)
	{
		NeighborGrid3DRWInstanceData* ProxyData = SystemInstancesToProxyData.Find(Context.SystemInstanceID);

		SCOPED_DRAW_EVENT(RHICmdList, NiagaraNeighborGrid3DClearNeighborInfo);
		ERHIFeatureLevel::Type FeatureLevel = Context.Batcher->GetFeatureLevel();

		FRHITransitionInfo TransitionInfos[] =
		{
			FRHITransitionInfo(ProxyData->NeighborhoodBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute),
			FRHITransitionInfo(ProxyData->NeighborhoodCountBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute),
		};
		RHICmdList.Transition(MakeArrayView(TransitionInfos, UE_ARRAY_COUNT(TransitionInfos)));
		NiagaraFillGPUIntBuffer(RHICmdList, FeatureLevel, ProxyData->NeighborhoodBuffer, -1);
		NiagaraFillGPUIntBuffer(RHICmdList, FeatureLevel, ProxyData->NeighborhoodCountBuffer, 0);
	}
}

FIntVector FNiagaraDataInterfaceProxyNeighborGrid3D::GetElementCount(FNiagaraSystemInstanceID SystemInstanceID) const
{
	if ( const NeighborGrid3DRWInstanceData* TargetData = SystemInstancesToProxyData.Find(SystemInstanceID) )
	{
		return TargetData->NumCells;
	}
	return FIntVector::ZeroValue;
}

bool UNiagaraDataInterfaceNeighborGrid3D::CopyToInternal(UNiagaraDataInterface* Destination) const
{
	if (!Super::CopyToInternal(Destination))
	{
		return false;
	}

	UNiagaraDataInterfaceNeighborGrid3D* OtherTyped = CastChecked<UNiagaraDataInterfaceNeighborGrid3D>(Destination);


	OtherTyped->MaxNeighborsPerCell = MaxNeighborsPerCell;


	return true;
}

#undef LOCTEXT_NAMESPACE