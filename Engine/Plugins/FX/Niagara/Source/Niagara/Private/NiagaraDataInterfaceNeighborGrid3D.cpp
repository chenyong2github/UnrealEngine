// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "NiagaraDataInterfaceNeighborGrid3D.h"
#include "NiagaraShader.h"
#include "ShaderParameterUtils.h"
#include "NiagaraEmitterInstanceBatcher.h"
#include "NiagaraSystemInstance.h"


#define LOCTEXT_NAMESPACE "NiagaraDataInterfaceNeighborGrid3D"

static const FString MaxNeighborsPerVoxelName(TEXT("MaxNeighborsPerVoxel_"));
static const FString ParticleNeighborsName(TEXT("ParticleNeighbors_"));
static const FString ParticleNeighborCountName(TEXT("ParticleNeighborCount_"));
static const FString OutputParticleNeighborsName(TEXT("OutputParticleNeighbors_"));
static const FString OutputParticleNeighborCountName(TEXT("OutputParticleNeighborCount_"));


// Global VM function names, also used by the shaders code generation methods.

static const FName MaxNeighborsPerVoxelFunctionName("MaxNeighborsPerVoxel");
static const FName NeighborGridIndexToLinearFunctionName("NeighborGridIndexToLinear");
static const FName GetParticleNeighborFunctionName("GetParticleNeighbor");
static const FName SetParticleNeighborFunctionName("SetParticleNeighbor");
static const FName GetParticleNeighborCountFunctionName("GetParticleNeighborCount");
static const FName SetParticleNeighborCountFunctionName("SetParticleNeighborCount");


/*--------------------------------------------------------------------------------------------------------------------------*/
struct FNiagaraDataInterfaceParametersCS_NeighborGrid3D : public FNiagaraDataInterfaceParametersCS
{
	virtual void Bind(const FNiagaraDataInterfaceParamRef& ParamRef, const class FShaderParameterMap& ParameterMap) override
	{
		NumVoxelsParam.Bind(ParameterMap, *(NumVoxelsName + ParamRef.ParameterInfo.DataInterfaceHLSLSymbol));		
		VoxelSizeParam.Bind(ParameterMap, *(VoxelSizeName + ParamRef.ParameterInfo.DataInterfaceHLSLSymbol));

		MaxNeighborsPerVoxelParam.Bind(ParameterMap, *(MaxNeighborsPerVoxelName + ParamRef.ParameterInfo.DataInterfaceHLSLSymbol));		
		WorldBBoxSizeParam.Bind(ParameterMap, *(WorldBBoxSizeName + ParamRef.ParameterInfo.DataInterfaceHLSLSymbol));
		
		ParticleNeighborsGridParam.Bind(ParameterMap,  *(ParticleNeighborsName + ParamRef.ParameterInfo.DataInterfaceHLSLSymbol));
		ParticleNeighborCountGridParam.Bind(ParameterMap,  *(ParticleNeighborCountName + ParamRef.ParameterInfo.DataInterfaceHLSLSymbol));

		OutputParticleNeighborsGridParam.Bind(ParameterMap, *(OutputParticleNeighborsName + ParamRef.ParameterInfo.DataInterfaceHLSLSymbol));
		OutputParticleNeighborCountGridParam.Bind(ParameterMap, *(OutputParticleNeighborCountName + ParamRef.ParameterInfo.DataInterfaceHLSLSymbol));		
	}

	virtual void Serialize(FArchive& Ar)override
	{
		Ar << NumVoxelsParam;
		Ar << VoxelSizeParam;
		Ar << MaxNeighborsPerVoxelParam;		
		Ar << WorldBBoxSizeParam;
		
		Ar << ParticleNeighborsGridParam;
		Ar << ParticleNeighborCountGridParam;
		Ar << OutputParticleNeighborsGridParam;
		Ar << OutputParticleNeighborCountGridParam;		
	}

	// #todo(dmp): make resource transitions batched
	virtual void Set(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const override
	{
		check(IsInRenderingThread());

		// Get shader and DI
		FRHIComputeShader* ComputeShaderRHI = Context.Shader->GetComputeShader();
		FNiagaraDataInterfaceProxyNeighborGrid3D* VFDI = static_cast<FNiagaraDataInterfaceProxyNeighborGrid3D*>(Context.DataInterface);

		NeighborGrid3DRWInstanceData* ProxyData = VFDI->SystemInstancesToProxyData.Find(Context.SystemInstance);
		check(ProxyData);

		SetShaderValue(RHICmdList, ComputeShaderRHI, NumVoxelsParam, ProxyData->NumVoxels);

		// #todo(dmp): remove this computation here
		float VoxelSizeTmp[3];
		VoxelSizeTmp[0] = ProxyData->WorldBBoxSize.X / ProxyData->NumVoxels.X;
		VoxelSizeTmp[1] = ProxyData->WorldBBoxSize.Y / ProxyData->NumVoxels.Y;
		VoxelSizeTmp[2] = ProxyData->WorldBBoxSize.Z / ProxyData->NumVoxels.Z;
		SetShaderValue(RHICmdList, ComputeShaderRHI, VoxelSizeParam, VoxelSizeTmp);

		SetShaderValue(RHICmdList, ComputeShaderRHI, MaxNeighborsPerVoxelParam, ProxyData->MaxNeighborsPerVoxel);		
		SetShaderValue(RHICmdList, ComputeShaderRHI, WorldBBoxSizeParam, ProxyData->WorldBBoxSize);
		
		if (!Context.IsOutputStage)
		{
			if (ParticleNeighborsGridParam.IsBound())
			{
				RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToCompute, ProxyData->NeighborhoodBuffer.UAV);
				SetSRVParameter(RHICmdList, Context.Shader->GetComputeShader(), ParticleNeighborsGridParam, ProxyData->NeighborhoodBuffer.SRV);
			}

			if (ParticleNeighborCountGridParam.IsBound())
			{
				RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToCompute, ProxyData->NeighborhoodCountBuffer.UAV);
				SetSRVParameter(RHICmdList, Context.Shader->GetComputeShader(), ParticleNeighborCountGridParam, ProxyData->NeighborhoodCountBuffer.SRV);
			}
		}
		else
		{
			if (OutputParticleNeighborsGridParam.IsBound())
			{
				RHICmdList.TransitionResource(EResourceTransitionAccess::EWritable, EResourceTransitionPipeline::EComputeToCompute, ProxyData->NeighborhoodBuffer.UAV);
				OutputParticleNeighborsGridParam.SetBuffer(RHICmdList, Context.Shader->GetComputeShader(), ProxyData->NeighborhoodBuffer);
			}

			if (OutputParticleNeighborCountGridParam.IsBound())
			{
				RHICmdList.TransitionResource(EResourceTransitionAccess::EWritable, EResourceTransitionPipeline::EComputeToCompute, ProxyData->NeighborhoodCountBuffer.UAV);
				OutputParticleNeighborCountGridParam.SetBuffer(RHICmdList, Context.Shader->GetComputeShader(), ProxyData->NeighborhoodCountBuffer);
			}
		}
		// Note: There is a flush in PreEditChange to make sure everything is synced up at this point 
	}

	virtual void Unset(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const 
	{
		if (OutputParticleNeighborsGridParam.IsBound())
		{
			OutputParticleNeighborsGridParam.UnsetUAV(RHICmdList, Context.Shader->GetComputeShader());
		}

		if (OutputParticleNeighborCountGridParam.IsBound())
		{
			OutputParticleNeighborCountGridParam.UnsetUAV(RHICmdList, Context.Shader->GetComputeShader());
		}
	}

private:
	FShaderParameter NumVoxelsParam;
	FShaderParameter VoxelSizeParam;
	FShaderParameter MaxNeighborsPerVoxelParam;	
	FShaderParameter WorldBBoxSizeParam;
	FShaderResourceParameter ParticleNeighborsGridParam;
	FShaderResourceParameter ParticleNeighborCountGridParam;
	FRWShaderParameter OutputParticleNeighborCountGridParam;
	FRWShaderParameter OutputParticleNeighborsGridParam;
};


UNiagaraDataInterfaceNeighborGrid3D::UNiagaraDataInterfaceNeighborGrid3D(FObjectInitializer const& ObjectInitializer)
	: Super(ObjectInitializer)
	, MaxNeighborsPerVoxel(8)
{
	Proxy = MakeShared<FNiagaraDataInterfaceProxyNeighborGrid3D, ESPMode::ThreadSafe>();
	PushToRenderThread();
}


void UNiagaraDataInterfaceNeighborGrid3D::GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)
{
	Super::GetFunctions(OutFunctions);

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = MaxNeighborsPerVoxelFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("MaxNeighborsPerVoxel")));

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

		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}
}

DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceNeighborGrid3D, GetWorldBBoxSize);

void UNiagaraDataInterfaceNeighborGrid3D::GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc)
{
	Super::GetVMExternalFunction(BindingInfo, InstanceData, OutFunc);

	// #todo(dmp): this overrides the empty function set by the super class
	if (BindingInfo.Name == WorldBBoxSizeFunctionName) {
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 3);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceNeighborGrid3D, GetWorldBBoxSize)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == MaxNeighborsPerVoxelFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }	
	else if (BindingInfo.Name == NeighborGridIndexToLinearFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	else if (BindingInfo.Name == GetParticleNeighborFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	else if (BindingInfo.Name == SetParticleNeighborFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	else if (BindingInfo.Name == GetParticleNeighborCountFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	else if (BindingInfo.Name == SetParticleNeighborCountFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
}

void UNiagaraDataInterfaceNeighborGrid3D::GetWorldBBoxSize(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<NeighborGrid3DRWInstanceData> InstData(Context);

	VectorVM::FExternalFuncRegisterHandler<float> OutWorldBoundsX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutWorldBoundsY(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutWorldBoundsZ(Context);

	for (int32 InstanceIdx = 0; InstanceIdx < Context.NumInstances; ++InstanceIdx)
	{
		*OutWorldBoundsX.GetDestAndAdvance() = WorldBBoxSize.X;
		*OutWorldBoundsY.GetDestAndAdvance() = WorldBBoxSize.Y;
		*OutWorldBoundsZ.GetDestAndAdvance() = WorldBBoxSize.Z;
	}
}

bool UNiagaraDataInterfaceNeighborGrid3D::Equals(const UNiagaraDataInterface* Other) const
{
	if (!Super::Equals(Other))
	{
		return false;
	}
	const UNiagaraDataInterfaceNeighborGrid3D* OtherTyped = CastChecked<const UNiagaraDataInterfaceNeighborGrid3D>(Other);

	return OtherTyped->MaxNeighborsPerVoxel == MaxNeighborsPerVoxel;
}

void UNiagaraDataInterfaceNeighborGrid3D::GetParameterDefinitionHLSL(FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL)
{
	Super::GetParameterDefinitionHLSL(ParamInfo, OutHLSL);

	static const TCHAR *FormatDeclarations = TEXT(R"(
		int {MaxNeighborsPerVoxelName};
		Buffer<int> {ParticleNeighborsName};
		Buffer<int> {ParticleNeighborCountName};
		RWBuffer<int> RW{OutputParticleNeighborsName};
		RWBuffer<int> RW{OutputParticleNeighborCountName};
	)");
	TMap<FString, FStringFormatArg> ArgsDeclarations = {
		{ TEXT("MaxNeighborsPerVoxelName"),  MaxNeighborsPerVoxelName + ParamInfo.DataInterfaceHLSLSymbol },
		{ TEXT("ParticleNeighborsName"),    ParticleNeighborsName + ParamInfo.DataInterfaceHLSLSymbol },
		{ TEXT("ParticleNeighborCountName"),    ParticleNeighborCountName + ParamInfo.DataInterfaceHLSLSymbol },
		{ TEXT("OutputParticleNeighborsName"),    OutputParticleNeighborsName + ParamInfo.DataInterfaceHLSLSymbol },
		{ TEXT("OutputParticleNeighborCountName"),    OutputParticleNeighborCountName + ParamInfo.DataInterfaceHLSLSymbol },
	};
	OutHLSL += FString::Format(FormatDeclarations, ArgsDeclarations);
}

bool UNiagaraDataInterfaceNeighborGrid3D::GetFunctionHLSL(const FName& DefinitionFunctionName, FString InstanceFunctionName, FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL)
{
	bool ParentRet = Super::GetFunctionHLSL(DefinitionFunctionName, InstanceFunctionName, ParamInfo, OutHLSL);
	if (ParentRet)
	{
		return true;
	} else if (DefinitionFunctionName == MaxNeighborsPerVoxelFunctionName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {FunctionName}(out int Out_MaxNeighborsPerVoxel)
			{
				Out_MaxNeighborsPerVoxel = {MaxNeighborsPerVoxelName};
			}
		)");
		TMap<FString, FStringFormatArg> ArgsSample = {
			{TEXT("FunctionName"), InstanceFunctionName},
			{TEXT("MaxNeighborsPerVoxelName"), MaxNeighborsPerVoxelName + ParamInfo.DataInterfaceHLSLSymbol},

		};
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (DefinitionFunctionName == NeighborGridIndexToLinearFunctionName)
	{
		static const TCHAR *FormatBounds = TEXT(R"(
			void {FunctionName}(int In_IndexX, int In_IndexY, int In_IndexZ, int In_Neighbor, out int Out_Linear)
			{
				Out_Linear = In_Neighbor + In_IndexX * {MaxNeighborsPerVoxelName} + In_IndexY * {MaxNeighborsPerVoxelName}*{NumVoxelsName}.x + In_IndexZ * {MaxNeighborsPerVoxelName}*{NumVoxelsName}.x*{NumVoxelsName}.y;
			}
		)");
		TMap<FString, FStringFormatArg> ArgsBounds = {
			{TEXT("FunctionName"), InstanceFunctionName},
			{TEXT("MaxNeighborsPerVoxelName"), MaxNeighborsPerVoxelName + ParamInfo.DataInterfaceHLSLSymbol},
			{TEXT("NumVoxelsName"), NumVoxelsName + ParamInfo.DataInterfaceHLSLSymbol},
		};
		OutHLSL += FString::Format(FormatBounds, ArgsBounds);
		return true;
	}
	else if (DefinitionFunctionName == GetParticleNeighborFunctionName)
	{
		static const TCHAR *FormatBounds = TEXT(R"(
			void {FunctionName}(int In_Index, out int Out_ParticleNeighborIndex)
			{
				Out_ParticleNeighborIndex = {ParticleNeighbors}[In_Index];				
			}
		)");
		TMap<FString, FStringFormatArg> ArgsBounds = {
			{TEXT("FunctionName"), InstanceFunctionName},
			{TEXT("ParticleNeighbors"), ParticleNeighborsName + ParamInfo.DataInterfaceHLSLSymbol},
		};
		OutHLSL += FString::Format(FormatBounds, ArgsBounds);
		return true;
	}
	else if (DefinitionFunctionName == SetParticleNeighborFunctionName)
	{
		static const TCHAR *FormatBounds = TEXT(R"(
			void {FunctionName}(int In_Index, int In_ParticleNeighborIndex, out int Out_Ignore)
			{
				RW{OutputParticleNeighbors}[In_Index] = In_ParticleNeighborIndex;				
				Out_Ignore = 0;
			}
		)");
		TMap<FString, FStringFormatArg> ArgsBounds = {
			{TEXT("FunctionName"), InstanceFunctionName},
			{TEXT("OutputParticleNeighbors"), OutputParticleNeighborsName + ParamInfo.DataInterfaceHLSLSymbol},
		};
		OutHLSL += FString::Format(FormatBounds, ArgsBounds);
		return true;
	}
	else if (DefinitionFunctionName == GetParticleNeighborCountFunctionName)
	{
		static const TCHAR *FormatBounds = TEXT(R"(
			void {FunctionName}(int In_Index, out int Out_ParticleNeighborIndex)
			{
				Out_ParticleNeighborIndex = {ParticleNeighborCount}[In_Index];				
			}
		)");
		TMap<FString, FStringFormatArg> ArgsBounds = {
			{TEXT("FunctionName"), InstanceFunctionName},
			{TEXT("ParticleNeighborCount"), ParticleNeighborCountName + ParamInfo.DataInterfaceHLSLSymbol},
		};
		OutHLSL += FString::Format(FormatBounds, ArgsBounds);
		return true;
	}
	else if (DefinitionFunctionName == SetParticleNeighborCountFunctionName)
	{
		static const TCHAR *FormatBounds = TEXT(R"(
			void {FunctionName}(int In_Index, int In_Increment, out int PreviousNeighborCount)
			{				
				InterlockedAdd(RW{OutputParticleNeighborCount}[In_Index], In_Increment, PreviousNeighborCount);				
			}
		)");
		TMap<FString, FStringFormatArg> ArgsBounds = {
			{TEXT("FunctionName"), InstanceFunctionName},
			{TEXT("OutputParticleNeighborCount"), OutputParticleNeighborCountName + ParamInfo.DataInterfaceHLSLSymbol},
		};
		OutHLSL += FString::Format(FormatBounds, ArgsBounds);
		return true;
	}

	return false;
}

FNiagaraDataInterfaceParametersCS* UNiagaraDataInterfaceNeighborGrid3D::ConstructComputeParameters()const
{
	return new FNiagaraDataInterfaceParametersCS_NeighborGrid3D();
}



bool UNiagaraDataInterfaceNeighborGrid3D::InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{

	NeighborGrid3DRWInstanceData* InstanceData = new (PerInstanceData) NeighborGrid3DRWInstanceData();

	FNiagaraDataInterfaceProxyNeighborGrid3D* RT_Proxy = GetProxyAs<FNiagaraDataInterfaceProxyNeighborGrid3D>();

	FIntVector RT_NumVoxels = NumVoxels;
	uint32 RT_MaxNeighborsPerVoxel = MaxNeighborsPerVoxel;	
	FVector RT_WorldBBoxSize = WorldBBoxSize;
	TSet<int> RT_OutputShaderStages = OutputShaderStages;
	TSet<int> RT_IterationShaderStages = IterationShaderStages;

	// If we are setting the grid from the voxel size, then recompute NumVoxels and change bbox
	if (SetGridFromVoxelSize)
	{
		RT_NumVoxels = FIntVector(WorldBBoxSize / VoxelSize);

		// Pad grid by 1 voxel if our computed bounding box is too small
		if (!FMath::IsNearlyEqual(VoxelSize * RT_NumVoxels.X, WorldBBoxSize.X))
		{
			RT_NumVoxels += FIntVector(1, 1, 1);
			RT_WorldBBoxSize = FVector(RT_NumVoxels) * VoxelSize;
		}
	}

	InstanceData->NumVoxels = RT_NumVoxels;
	InstanceData->MaxNeighborsPerVoxel = RT_MaxNeighborsPerVoxel;	
	InstanceData->WorldBBoxSize = RT_WorldBBoxSize;

	// @todo-threadsafety. This would be a race but I'm taking a ref here. Not ideal in the long term.
	// Push Updates to Proxy.
	ENQUEUE_RENDER_COMMAND(FUpdateData)(
		[RT_Proxy, RT_NumVoxels, RT_MaxNeighborsPerVoxel, RT_WorldBBoxSize, RT_OutputShaderStages, RT_IterationShaderStages, InstanceID = SystemInstance->GetId()](FRHICommandListImmediate& RHICmdList)
	{
		NeighborGrid3DRWInstanceData* TargetData = RT_Proxy->SystemInstancesToProxyData.Find(InstanceID);
		if (TargetData != nullptr)
		{
			RT_Proxy->DeferredDestroyList.Remove(InstanceID);
		}
		else
		{
			TargetData = &RT_Proxy->SystemInstancesToProxyData.Add(InstanceID);
		}

		TargetData->NumVoxels = RT_NumVoxels;
		TargetData->MaxNeighborsPerVoxel = RT_MaxNeighborsPerVoxel;		
		TargetData->WorldBBoxSize = RT_WorldBBoxSize;

		RT_Proxy->OutputShaderStages = RT_OutputShaderStages;
		RT_Proxy->IterationShaderStages = RT_IterationShaderStages;

		// #todo(dmp): element count is still defined on the proxy and not the instance data
		RT_Proxy->SetElementCount(RT_NumVoxels.X *  RT_NumVoxels.Y * RT_NumVoxels.Z);

		TargetData->NeighborhoodCountBuffer.Initialize(sizeof(int32), TargetData->NumVoxels.X*TargetData->NumVoxels.Y*TargetData->NumVoxels.Z, EPixelFormat::PF_R32_SINT, BUF_Static);
		TargetData->NeighborhoodBuffer.Initialize(sizeof(int32), TargetData->NumVoxels.X*TargetData->NumVoxels.Y*TargetData->NumVoxels.Z*TargetData->MaxNeighborsPerVoxel, EPixelFormat::PF_R32_SINT, BUF_Static);

	});

	return true;
}

void UNiagaraDataInterfaceNeighborGrid3D::DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{		
	NeighborGrid3DRWInstanceData* InstanceData = static_cast<NeighborGrid3DRWInstanceData*>(PerInstanceData);
	InstanceData->~NeighborGrid3DRWInstanceData();

	FNiagaraDataInterfaceProxyNeighborGrid3D* ThisProxy = GetProxyAs<FNiagaraDataInterfaceProxyNeighborGrid3D>();

	ENQUEUE_RENDER_COMMAND(FNiagaraDIDestroyInstanceData) (
		[ThisProxy, InstanceID = SystemInstance->GetId(), Batcher = SystemInstance->GetBatcher()](FRHICommandListImmediate& CmdList)
	{
		ThisProxy->DestroyPerInstanceData(Batcher, InstanceID);
	}
	);
}

void FNiagaraDataInterfaceProxyNeighborGrid3D::PreStage(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context)
{
	if (Context.IsOutputStage)
	{
		NeighborGrid3DRWInstanceData* ProxyData = SystemInstancesToProxyData.Find(Context.SystemInstance);

		ClearUAV(RHICmdList, ProxyData->NeighborhoodBuffer, -1);
		ClearUAV(RHICmdList, ProxyData->NeighborhoodCountBuffer, 0);
	}
}

// #todo(dmp): move these to super class
void FNiagaraDataInterfaceProxyNeighborGrid3D::DestroyPerInstanceData(NiagaraEmitterInstanceBatcher* Batcher, const FNiagaraSystemInstanceID& SystemInstance)
{
	check(IsInRenderingThread());

	DeferredDestroyList.Add(SystemInstance);
	Batcher->EnqueueDeferredDeletesForDI_RenderThread(this->AsShared());
}

// #todo(dmp): move these to super class
void FNiagaraDataInterfaceProxyNeighborGrid3D::DeferredDestroy()
{
	for (const FNiagaraSystemInstanceID& Sys : DeferredDestroyList)
	{
		SystemInstancesToProxyData.Remove(Sys);
	}

	DeferredDestroyList.Empty();
}

bool UNiagaraDataInterfaceNeighborGrid3D::CopyToInternal(UNiagaraDataInterface* Destination) const
{
	if (!Super::CopyToInternal(Destination))
	{
		return false;
	}

	UNiagaraDataInterfaceNeighborGrid3D* OtherTyped = CastChecked<UNiagaraDataInterfaceNeighborGrid3D>(Destination);


	OtherTyped->MaxNeighborsPerVoxel = MaxNeighborsPerVoxel;


	return true;
}

#undef LOCTEXT_NAMESPACE