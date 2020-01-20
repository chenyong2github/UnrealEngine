// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraDataInterfacePressureGrid.h"
#include "NiagaraShader.h"
#include "NiagaraComponent.h"
#include "NiagaraRenderer.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraEmitterInstanceBatcher.h"

#include "ShaderParameterUtils.h"
#include "ClearQuad.h"

#define LOCTEXT_NAMESPACE "NiagaraDataInterfacePressureGrid"
DEFINE_LOG_CATEGORY_STATIC(LogPressureGrid, Log, All);

//------------------------------------------------------------------------------------------------------------

static const FName BuildVelocityFieldName(TEXT("BuildVelocityField"));
static const FName ProjectVelocityFieldName(TEXT("ProjectVelocityField"));
static const FName SampleVelocityFieldName(TEXT("SampleVelocityField"));
static const FName GetCellPositionName(TEXT("GetCellPosition"));
static const FName TransferCellVelocityName(TEXT("TransferCellVelocity"));
static const FName SetSolidBoundaryName(TEXT("SetSolidBoundary"));
static const FName ComputeBoundaryWeightsName(TEXT("ComputeBoundaryWeights"));
static const FName BuildGridTopologyName(TEXT("BuildGridTopology"));
static const FName UpdateGridTransformName(TEXT("UpdateGridTransform"));

//------------------------------------------------------------------------------------------------------------

const FString UNiagaraDataInterfacePressureGrid::GridCurrentBufferName(TEXT("GridCurrentBuffer_"));
const FString UNiagaraDataInterfacePressureGrid::GridDestinationBufferName(TEXT("GridDestinationBuffer_"));

const FString UNiagaraDataInterfacePressureGrid::GridSizeName(TEXT("GridSize_"));
const FString UNiagaraDataInterfacePressureGrid::GridOriginName(TEXT("GridOrigin_"));

const FString UNiagaraDataInterfacePressureGrid::WorldTransformName(TEXT("WorldTransform_"));
const FString UNiagaraDataInterfacePressureGrid::WorldInverseName(TEXT("WorldInverse_"));

//------------------------------------------------------------------------------------------------------------

struct FNDIPressureGridParametersName
{
	FNDIPressureGridParametersName(const FString& Suffix)
	{
		GridCurrentBufferName = UNiagaraDataInterfacePressureGrid::GridCurrentBufferName + Suffix;
		GridDestinationBufferName = UNiagaraDataInterfacePressureGrid::GridDestinationBufferName + Suffix;

		GridSizeName = UNiagaraDataInterfacePressureGrid::GridSizeName + Suffix;
		GridOriginName = UNiagaraDataInterfacePressureGrid::GridOriginName + Suffix;

		WorldTransformName = UNiagaraDataInterfacePressureGrid::WorldTransformName + Suffix;
		WorldInverseName = UNiagaraDataInterfacePressureGrid::WorldInverseName + Suffix;
	}

	FString GridCurrentBufferName;
	FString GridDestinationBufferName;

	FString GridSizeName;
	FString GridOriginName;

	FString WorldTransformName;
	FString WorldInverseName;
};

//------------------------------------------------------------------------------------------------------------

void FNDIPressureGridBuffer::SetGridSize(const FUintVector4 InGridSize)
{
	GridSize = InGridSize;
}

void FNDIPressureGridBuffer::InitRHI()
{
	if(GridSize.X != 0 && GridSize.Y != 0 && GridSize.Z != 0)
	{
		static const uint32 NumComponents = 14;
		GridDataBuffer.Initialize(sizeof(int32), GridSize.X*NumComponents, GridSize.Y,
			GridSize.Z, EPixelFormat::PF_R32_SINT);
	}
}

void FNDIPressureGridBuffer::ReleaseRHI()
{
	GridDataBuffer.Release();
}

void FNDIPressureGridBuffer::ClearBuffers(FRHICommandList& RHICmdList)
{
	ClearUAV(RHICmdList, GridDataBuffer, FLinearColor(0, 0, 0, 0));
}

//------------------------------------------------------------------------------------------------------------

void FNDIPressureGridData::SwapBuffers()
{
	FNDIPressureGridBuffer* StoredBufferPointer = CurrentGridBuffer;
	CurrentGridBuffer = DestinationGridBuffer;
	DestinationGridBuffer = StoredBufferPointer;
}

//------------------------------------------------------------------------------------------------------------

struct FNDIPressureGridParametersCS : public FNiagaraDataInterfaceParametersCS
{
	virtual void Bind(const FNiagaraDataInterfaceParamRef& ParamRef, const class FShaderParameterMap& ParameterMap) override
	{
		FNDIPressureGridParametersName ParamNames(ParamRef.ParameterInfo.DataInterfaceHLSLSymbol);

		GridCurrentBuffer.Bind(ParameterMap, *ParamNames.GridCurrentBufferName);
		GridDestinationBuffer.Bind(ParameterMap, *ParamNames.GridDestinationBufferName);

		GridOrigin.Bind(ParameterMap, *ParamNames.GridOriginName);
		GridSize.Bind(ParameterMap, *ParamNames.GridSizeName);

		WorldTransform.Bind(ParameterMap, *ParamNames.WorldTransformName);
		WorldInverse.Bind(ParameterMap, *ParamNames.WorldInverseName);

		if (!GridCurrentBuffer.IsBound())
		{
			UE_LOG(LogPressureGrid, Warning, TEXT("Binding failed for FNDIPressureGridParametersCS %s. Was it optimized out?"), *ParamNames.GridCurrentBufferName)
		}

		if (!GridDestinationBuffer.IsBound())
		{
			UE_LOG(LogPressureGrid, Warning, TEXT("Binding failed for FNDIPressureGridParametersCS %s. Was it optimized out?"), *ParamNames.GridDestinationBufferName)
		}
	}

	virtual void Serialize(FArchive& Ar) override
	{
		Ar << GridCurrentBuffer;
		Ar << GridDestinationBuffer;

		Ar << GridOrigin;
		Ar << GridSize;

		Ar << WorldTransform;
		Ar << WorldInverse;
	}

	virtual void Set(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const override
	{
		check(IsInRenderingThread());

		FRHIComputeShader* ComputeShaderRHI = Context.Shader->GetComputeShader();

		FNDIPressureGridProxy* InterfaceProxy =
			static_cast<FNDIPressureGridProxy*>(Context.DataInterface);
		FNDIPressureGridData* ProxyData =
			InterfaceProxy->SystemInstancesToProxyData.Find(Context.SystemInstance);

		if (ProxyData != nullptr)
		{
			FNDIPressureGridBuffer* CurrentGridBuffer = ProxyData->CurrentGridBuffer;
			FNDIPressureGridBuffer* DestinationGridBuffer = ProxyData->DestinationGridBuffer;

			RHICmdList.TransitionResource(EResourceTransitionAccess::EWritable, EResourceTransitionPipeline::EComputeToCompute, DestinationGridBuffer->GridDataBuffer.UAV);
			SetUAVParameter(RHICmdList, ComputeShaderRHI, GridDestinationBuffer, DestinationGridBuffer->GridDataBuffer.UAV);

			RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToCompute, CurrentGridBuffer->GridDataBuffer.UAV);
			SetSRVParameter(RHICmdList, ComputeShaderRHI, GridCurrentBuffer, CurrentGridBuffer->GridDataBuffer.SRV);

			//UE_LOG(LogPressureGrid, Warning, TEXT("Binding Grid Origin : %s"), *ProxyData->GridOrigin.ToString());
			SetShaderValue(RHICmdList, ComputeShaderRHI, GridOrigin, ProxyData->GridOrigin);

			//UE_LOG(LogPressureGrid, Warning, TEXT("Binding Grid Size : %d %d %d"), ProxyData->GridSize.X, ProxyData->GridSize.Y, ProxyData->GridSize.Z);
			SetShaderValue(RHICmdList, ComputeShaderRHI, GridSize, ProxyData->GridSize);

			//UE_LOG(LogPressureGrid, Warning, TEXT("Binding Grid Transform : %s"), *ProxyData->WorldTransform.ToString());
			SetShaderValue(RHICmdList, ComputeShaderRHI, WorldTransform, ProxyData->WorldTransform);
			SetShaderValue(RHICmdList, ComputeShaderRHI, WorldInverse, ProxyData->WorldTransform.Inverse());
		}
		else
		{
			SetUAVParameter(RHICmdList, ComputeShaderRHI, GridDestinationBuffer, FNiagaraRenderer::GetDummyUIntBuffer().UAV);
			SetSRVParameter(RHICmdList, ComputeShaderRHI, GridCurrentBuffer, FNiagaraRenderer::GetDummyUIntBuffer().SRV);

			SetShaderValue(RHICmdList, ComputeShaderRHI, GridOrigin, FVector4(0,0,0,0));
			SetShaderValue(RHICmdList, ComputeShaderRHI, GridSize, FUintVector4());

			SetShaderValue(RHICmdList, ComputeShaderRHI, WorldTransform, FMatrix::Identity);
			SetShaderValue(RHICmdList, ComputeShaderRHI, WorldInverse, FMatrix::Identity);
		}
	}

	virtual void Unset(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const override
	{
		SetUAVParameter(RHICmdList, Context.Shader->GetComputeShader(), GridDestinationBuffer, nullptr);
	}

private:

	FShaderResourceParameter GridCurrentBuffer;
	FShaderResourceParameter GridDestinationBuffer;

	FShaderParameter GridSize;
	FShaderParameter GridOrigin;

	FShaderParameter WorldTransform;
	FShaderParameter WorldInverse;
};

//------------------------------------------------------------------------------------------------------------

void FNDIPressureGridProxy::DeferredDestroy()
{
	for (const FNiagaraSystemInstanceID& Sys : DeferredDestroyList)
	{
		SystemInstancesToProxyData.Remove(Sys);
	}

	DeferredDestroyList.Empty();
}

void FNDIPressureGridProxy::ConsumePerInstanceDataFromGameThread(void* PerInstanceData, const FNiagaraSystemInstanceID& Instance)
{
	FNDIPressureGridData* SourceData = static_cast<FNDIPressureGridData*>(PerInstanceData);
	FNDIPressureGridData& TargetData = SystemInstancesToProxyData.FindOrAdd(Instance);

	TargetData.WorldTransform = SourceData->WorldTransform;
	TargetData.GridOrigin = SourceData->GridOrigin;
}

void FNDIPressureGridProxy::InitializePerInstanceData(const FNiagaraSystemInstanceID& SystemInstance, FNDIPressureGridBuffer* CurrentGridBuffer,
FNDIPressureGridBuffer* DestinationGridBuffer, const FVector4& GridOrigin, const FUintVector4& GridSize)
{
	check(IsInRenderingThread());

	FNDIPressureGridData& TargetData = SystemInstancesToProxyData.FindOrAdd(SystemInstance);
	
	TargetData.CurrentGridBuffer = CurrentGridBuffer;
	TargetData.DestinationGridBuffer = DestinationGridBuffer;
	TargetData.GridOrigin = GridOrigin;
	TargetData.GridSize = GridSize;
}

void FNDIPressureGridProxy::DestroyPerInstanceData(NiagaraEmitterInstanceBatcher* Batcher, const FNiagaraSystemInstanceID& SystemInstance)
{
	check(IsInRenderingThread());

	DeferredDestroyList.Add(SystemInstance);
	Batcher->EnqueueDeferredDeletesForDI_RenderThread(this->AsShared());
}

//------------------------------------------------------------------------------------------------------------

UNiagaraDataInterfacePressureGrid::UNiagaraDataInterfacePressureGrid(FObjectInitializer const& ObjectInitializer)
	: Super(ObjectInitializer)
	, GridSize(10)
	, GridOrigin(0,0,0)
	, GridLength(1)
{

	Proxy = MakeShared<FNDIPressureGridProxy, ESPMode::ThreadSafe>();
}

bool UNiagaraDataInterfacePressureGrid::InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	FNDIPressureGridData* InstanceData = new (PerInstanceData) FNDIPressureGridData();
	check(InstanceData);
	
	FNDIPressureGridBuffer* CurrentGridBuffer = new FNDIPressureGridBuffer;
	FNDIPressureGridBuffer* DestinationGridBuffer = new FNDIPressureGridBuffer;
		

	// Push instance data to RT
	{
		const FUintVector4 LocalGridSize(GridSize.X, GridSize.Y, GridSize.Z,0);
		const FVector4 LocalGridOrigin(GridOrigin.X,GridOrigin.Y,GridOrigin.Z,GridLength);
		CurrentGridBuffer->SetGridSize(LocalGridSize);
		DestinationGridBuffer->SetGridSize(LocalGridSize);

		TSet<int> RT_OutputShaderStages = OutputShaderStages;
		TSet<int> RT_IterationShaderStages = IterationShaderStages;
		
		InstanceData->CurrentGridBuffer = CurrentGridBuffer;
		InstanceData->DestinationGridBuffer = DestinationGridBuffer;
		InstanceData->GridSize = LocalGridSize;
		InstanceData->GridOrigin = LocalGridOrigin;

		InstanceData->WorldTransform = FMatrix::Identity;// SystemInstance->GetComponent()->GetComponentToWorld().ToMatrixWithScale();

		FNDIPressureGridProxy* ThisProxy = GetProxyAs<FNDIPressureGridProxy>();
		ENQUEUE_RENDER_COMMAND(FNiagaraDIPushInitialInstanceDataToRT) (
			[ThisProxy, RT_OutputShaderStages, RT_IterationShaderStages, InstanceID = SystemInstance->GetId(), CurrentGridBuffer, DestinationGridBuffer, LocalGridSize, LocalGridOrigin](FRHICommandListImmediate& CmdList)
		{
			ThisProxy->OutputShaderStages = RT_OutputShaderStages;
			ThisProxy->IterationShaderStages = RT_IterationShaderStages;
			ThisProxy->SetElementCount(LocalGridSize.X*LocalGridSize.Y*LocalGridSize.Z);

			CurrentGridBuffer->InitResource();
			DestinationGridBuffer->InitResource();

			ThisProxy->InitializePerInstanceData(InstanceID, CurrentGridBuffer, DestinationGridBuffer, LocalGridOrigin, LocalGridSize);
		}
		);
	}
	return true;
}

void UNiagaraDataInterfacePressureGrid::DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	FNDIPressureGridData* InstanceData = static_cast<FNDIPressureGridData*>(PerInstanceData);
	InstanceData->~FNDIPressureGridData();

	if (InstanceData->CurrentGridBuffer && InstanceData->DestinationGridBuffer)
	{
		FNDIPressureGridProxy* ThisProxy = GetProxyAs<FNDIPressureGridProxy>();
		FNDIPressureGridBuffer* CurrentGridBuffer = InstanceData->CurrentGridBuffer;
		FNDIPressureGridBuffer* DestinationGridBuffer = InstanceData->DestinationGridBuffer;
		ENQUEUE_RENDER_COMMAND(FNiagaraDIDestroyInstanceData) (
			[ThisProxy, CurrentGridBuffer, DestinationGridBuffer, InstanceID = SystemInstance->GetId(), Batcher = SystemInstance->GetBatcher()](FRHICommandListImmediate& CmdList)
		{
			CurrentGridBuffer->ReleaseResource();
			DestinationGridBuffer->ReleaseResource();
			ThisProxy->SystemInstancesToProxyData.Remove(InstanceID);
			delete CurrentGridBuffer;
			delete DestinationGridBuffer;
		}
		);
		InstanceData->CurrentGridBuffer = nullptr;
		InstanceData->DestinationGridBuffer = nullptr;
	}
}

bool UNiagaraDataInterfacePressureGrid::PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float InDeltaSeconds)
{
	FNDIPressureGridData* InstanceData = static_cast<FNDIPressureGridData*>(PerInstanceData);

	bool RequireReset = false;
	if (InstanceData)
	{
		InstanceData->WorldTransform = SystemInstance->GetComponent()->GetComponentToWorld().ToMatrixWithScale();
	}
	
	return RequireReset;
}

bool UNiagaraDataInterfacePressureGrid::CopyToInternal(UNiagaraDataInterface* Destination) const
{
	if (!Super::CopyToInternal(Destination))
	{
		return false;
	}

	UNiagaraDataInterfacePressureGrid* OtherTyped = CastChecked<UNiagaraDataInterfacePressureGrid>(Destination);
	OtherTyped->GridSize = GridSize;
	OtherTyped->GridOrigin = GridOrigin;
	OtherTyped->GridLength= GridLength;

	return true;
}

bool UNiagaraDataInterfacePressureGrid::Equals(const UNiagaraDataInterface* Other) const
{
	if (!Super::Equals(Other))
	{
		return false;
	}
	const UNiagaraDataInterfacePressureGrid* OtherTyped = CastChecked<const UNiagaraDataInterfacePressureGrid>(Other);

	return (OtherTyped->GridSize == GridSize) && (OtherTyped->GridOrigin == GridOrigin) && (OtherTyped->GridLength == GridLength);
}

void UNiagaraDataInterfacePressureGrid::PostInitProperties()
{
	Super::PostInitProperties();

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(GetClass()), true, false, false);
	}
}

void UNiagaraDataInterfacePressureGrid::GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)
{
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = BuildVelocityFieldName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Pressure Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Strands Size")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Node Position")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Node Mass")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Node Velocity")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Velocity GradientX")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Velocity GradientY")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Velocity GradientZ")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Grid Origin")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Grid Length")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Build Status")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = SampleVelocityFieldName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Pressure Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Node Position")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Grid Origin")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Grid Length")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Node Velocity")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Node Density")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Velocity GradientX")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Velocity GradientY")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Velocity GradientZ")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = ProjectVelocityFieldName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Pressure Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Grid Cell")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Project Status")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Cell Divergence")));
		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetCellPositionName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Pressure Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Grid Cell")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Grid Origin")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Grid Length")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Grid Position")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = SetSolidBoundaryName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Pressure Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Grid Cell")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Cell Distance")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Cell Velocity")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Boundary Status")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = ComputeBoundaryWeightsName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Pressure Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Grid Cell")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Weights Status")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = TransferCellVelocityName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Pressure Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Grid Cell")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Transfer Status")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = BuildGridTopologyName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Pressure Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Grid Center")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Grid Extent")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Grid Origin")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Grid Length")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = UpdateGridTransformName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Pressure Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetMatrix4Def(), TEXT("Grid Transform")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Transform Status")));

		OutFunctions.Add(Sig);
	}
}

DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfacePressureGrid, BuildVelocityField);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfacePressureGrid, ProjectVelocityField);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfacePressureGrid, GetCellPosition);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfacePressureGrid, SetSolidBoundary);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfacePressureGrid, TransferCellVelocity);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfacePressureGrid, ComputeBoundaryWeights);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfacePressureGrid, SampleVelocityField);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfacePressureGrid, BuildGridTopology);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfacePressureGrid, UpdateGridTransform);

void UNiagaraDataInterfacePressureGrid::GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc)
{
	if (BindingInfo.Name == BuildVelocityFieldName)
	{
		check(BindingInfo.GetNumInputs() == 23 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfacePressureGrid, BuildVelocityField)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == ProjectVelocityFieldName)
	{
		check(BindingInfo.GetNumInputs() == 2 && BindingInfo.GetNumOutputs() == 2);
		NDI_FUNC_BINDER(UNiagaraDataInterfacePressureGrid, ProjectVelocityField)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == GetCellPositionName)
	{
		check(BindingInfo.GetNumInputs() == 6 && BindingInfo.GetNumOutputs() == 3);
		NDI_FUNC_BINDER(UNiagaraDataInterfacePressureGrid, GetCellPosition)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == SampleVelocityFieldName)
	{
		check(BindingInfo.GetNumInputs() == 8 && BindingInfo.GetNumOutputs() == 13);
		NDI_FUNC_BINDER(UNiagaraDataInterfacePressureGrid, SampleVelocityField)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == SetSolidBoundaryName)
	{
		check(BindingInfo.GetNumInputs() == 6 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfacePressureGrid, SetSolidBoundary)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == ComputeBoundaryWeightsName)
	{
		check(BindingInfo.GetNumInputs() == 2 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfacePressureGrid, ComputeBoundaryWeights)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name ==TransferCellVelocityName)
	{
		check(BindingInfo.GetNumInputs() == 2 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfacePressureGrid, TransferCellVelocity)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == BuildGridTopologyName)
	{
		check(BindingInfo.GetNumInputs() == 7 && BindingInfo.GetNumOutputs() == 4);
		NDI_FUNC_BINDER(UNiagaraDataInterfacePressureGrid, BuildGridTopology)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == UpdateGridTransformName)
	{
		check(BindingInfo.GetNumInputs() == 17 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfacePressureGrid, UpdateGridTransform)::Bind(this, OutFunc);
	}
}

void UNiagaraDataInterfacePressureGrid::BuildVelocityField(FVectorVMContext& Context)
{
	// @todo : implement function for cpu 
}

void UNiagaraDataInterfacePressureGrid::ProjectVelocityField(FVectorVMContext& Context)
{
	// @todo : implement function for cpu 
}

void UNiagaraDataInterfacePressureGrid::GetCellPosition(FVectorVMContext& Context)
{
	// @todo : implement function for cpu 
}

void UNiagaraDataInterfacePressureGrid::SampleVelocityField(FVectorVMContext& Context)
{
	// @todo : implement function for cpu 
}

void UNiagaraDataInterfacePressureGrid::ComputeBoundaryWeights(FVectorVMContext& Context)
{
	// @todo : implement function for cpu 
}

void UNiagaraDataInterfacePressureGrid::SetSolidBoundary(FVectorVMContext& Context)
{
	// @todo : implement function for cpu 
}

void UNiagaraDataInterfacePressureGrid::TransferCellVelocity(FVectorVMContext& Context)
{
	// @todo : implement function for cpu 
}

void UNiagaraDataInterfacePressureGrid::BuildGridTopology(FVectorVMContext& Context)
{
	VectorVM::FExternalFuncInputHandler<float> CenterXParam(Context);
	VectorVM::FExternalFuncInputHandler<float> CenterYParam(Context);
	VectorVM::FExternalFuncInputHandler<float> CenterZParam(Context);
	VectorVM::FExternalFuncInputHandler<float> ExtentXParam(Context);
	VectorVM::FExternalFuncInputHandler<float> ExtentYParam(Context);
	VectorVM::FExternalFuncInputHandler<float> ExtentZParam(Context);

	VectorVM::FUserPtrHandler<FNDIPressureGridData> InstData(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutGridOriginX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutGridOriginY(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutGridOriginZ(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutGridLength(Context);

	for (int32 InstanceIdx = 0; InstanceIdx < Context.NumInstances; ++InstanceIdx)
	{
		const FVector GridCenter(*CenterXParam.GetDestAndAdvance(), *CenterYParam.GetDestAndAdvance(), *CenterZParam.GetDestAndAdvance());
		const FVector GridExtent(*ExtentXParam.GetDestAndAdvance(), *ExtentYParam.GetDestAndAdvance(), *ExtentZParam.GetDestAndAdvance());

		const FVector GridLengths(2.0 * GridExtent.X / (InstData->GridSize.X - 1),
								  2.0 * GridExtent.Y / (InstData->GridSize.Y - 1),
								  2.0 * GridExtent.Z / (InstData->GridSize.Z - 1));
		const float MaxLength = GridLengths.GetMax();

		const FVector RegularExtent((InstData->GridSize.X - 1)*MaxLength, 
								    (InstData->GridSize.Y - 1)*MaxLength, 
									(InstData->GridSize.Z - 1)*MaxLength);
		const FVector BoxOrigin = GridCenter - 0.5 * RegularExtent;
		InstData->GridOrigin = FVector4(BoxOrigin.X, BoxOrigin.Y, BoxOrigin.Z, MaxLength);

		*OutGridOriginX.GetDestAndAdvance() = BoxOrigin.X;
		*OutGridOriginY.GetDestAndAdvance() = BoxOrigin.Y;
		*OutGridOriginZ.GetDestAndAdvance() = BoxOrigin.Z;
		*OutGridLength.GetDestAndAdvance() = MaxLength;
		//UE_LOG(LogPressureGrid, Warning, TEXT("Get Grid Extent : %f %f %f | Grid Lengths : %f %f %f | Regular Extent : %f %f %f | Box Origin : %f %f %f | Grid Center : %f %f %f"), 
		//				GridExtent.X, GridExtent.Y, GridExtent.Z, GridLengths.X, GridLengths.Y, GridLengths.Z, RegularExtent.X, RegularExtent.Y, RegularExtent.Z,
		//	InstData->GridOrigin.X, InstData->GridOrigin.Y, InstData->GridOrigin.Z, GridCenter.X, GridCenter.Y, GridCenter.Z);
	}
}

void UNiagaraDataInterfacePressureGrid::UpdateGridTransform(FVectorVMContext& Context)
{
	VectorVM::FExternalFuncInputHandler<float> Out00(Context);
	VectorVM::FExternalFuncInputHandler<float> Out01(Context);
	VectorVM::FExternalFuncInputHandler<float> Out02(Context);
	VectorVM::FExternalFuncInputHandler<float> Out03(Context);

	VectorVM::FExternalFuncInputHandler<float> Out10(Context);
	VectorVM::FExternalFuncInputHandler<float> Out11(Context);
	VectorVM::FExternalFuncInputHandler<float> Out12(Context);
	VectorVM::FExternalFuncInputHandler<float> Out13(Context);

	VectorVM::FExternalFuncInputHandler<float> Out20(Context);
	VectorVM::FExternalFuncInputHandler<float> Out21(Context);
	VectorVM::FExternalFuncInputHandler<float> Out22(Context);
	VectorVM::FExternalFuncInputHandler<float> Out23(Context);

	VectorVM::FExternalFuncInputHandler<float> Out30(Context);
	VectorVM::FExternalFuncInputHandler<float> Out31(Context);
	VectorVM::FExternalFuncInputHandler<float> Out32(Context);
	VectorVM::FExternalFuncInputHandler<float> Out33(Context);

	VectorVM::FUserPtrHandler<FNDIPressureGridData> InstData(Context);
	VectorVM::FExternalFuncRegisterHandler<bool> OutTransformStatus(Context);

	for (int32 InstanceIdx = 0; InstanceIdx < Context.NumInstances; ++InstanceIdx)
	{
		FMatrix Transform;
		Transform.M[0][0] = *Out00.GetDestAndAdvance();
		Transform.M[0][1] = *Out01.GetDestAndAdvance();
		Transform.M[0][2] = *Out02.GetDestAndAdvance();
		Transform.M[0][3] = *Out03.GetDestAndAdvance();

		Transform.M[1][0] = *Out10.GetDestAndAdvance();
		Transform.M[1][1] = *Out11.GetDestAndAdvance();
		Transform.M[1][2] = *Out12.GetDestAndAdvance();
		Transform.M[1][3] = *Out13.GetDestAndAdvance();

		Transform.M[2][0] = *Out20.GetDestAndAdvance();
		Transform.M[2][1] = *Out21.GetDestAndAdvance();
		Transform.M[2][2] = *Out22.GetDestAndAdvance();
		Transform.M[2][3] = *Out23.GetDestAndAdvance();

		Transform.M[3][0] = *Out30.GetDestAndAdvance();
		Transform.M[3][1] = *Out31.GetDestAndAdvance();
		Transform.M[3][2] = *Out32.GetDestAndAdvance();
		Transform.M[3][3] = *Out33.GetDestAndAdvance();

		InstData->WorldTransform = Transform;
		InstData->WorldInverse = Transform.Inverse();

		*OutTransformStatus.GetDestAndAdvance() = true;
	}
	//UE_LOG(LogPressureGrid, Warning, TEXT("Get Grid Transform : %s"), *InstData->WorldTransform.ToString() );
}

bool UNiagaraDataInterfacePressureGrid::GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL)
{
	FNDIPressureGridParametersName ParamNames(ParamInfo.DataInterfaceHLSLSymbol);

	TMap<FString, FStringFormatArg> ArgsSample = {
		{TEXT("InstanceFunctionName"), FunctionInfo.InstanceName},
		{TEXT("GridCurrentBufferName"), ParamNames.GridCurrentBufferName},
		{TEXT("GridDestinationBufferName"), ParamNames.GridDestinationBufferName},
		{TEXT("GridOriginName"), ParamNames.GridOriginName},
		{TEXT("GridSizeName"), ParamNames.GridSizeName},
		{TEXT("WorldTransformName"), ParamNames.WorldTransformName},
		{TEXT("WorldInverseName"), ParamNames.WorldInverseName},
		{TEXT("PressureGridContextName"), TEXT("DIPRESSUREGRID_MAKE_CONTEXT(") + ParamInfo.DataInterfaceHLSLSymbol + TEXT(")")},
	};

	if (FunctionInfo.DefinitionName == BuildVelocityFieldName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
				void {InstanceFunctionName} (in int StrandsSize, in float3 NodePosition, in float NodeMass, in float3 NodeVelocity, in float3 VelocityGradientX, in float3 VelocityGradientY, in float3 VelocityGradientZ, 
							in float3 GridOrigin, in float GridLength, out bool OutBuildStatus)
				{
					{PressureGridContextName} DIPressureGrid_BuildVelocityField(DIContext,StrandsSize,NodePosition,NodeMass,NodeVelocity,VelocityGradientX,VelocityGradientY,VelocityGradientZ,GridOrigin,GridLength,OutBuildStatus);
				}
				)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == SampleVelocityFieldName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
				void {InstanceFunctionName} (in float3 NodePosition, in float3 GridVelocity, in float GridLength, out float3 OutGridVelocity, out float OutGridDensity, out float3 OutGridGradientX, out float3 OutGridGradientY, out float3 OutGridGradientZ )
				{
					{PressureGridContextName} DIPressureGrid_SampleVelocityField(DIContext,NodePosition,GridVelocity,GridLength,OutGridVelocity,OutGridDensity,OutGridGradientX,OutGridGradientY,OutGridGradientZ);
				}
				)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == ProjectVelocityFieldName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
				void {InstanceFunctionName} (in int GridCell, out bool OutProjectStatus, out float OutCellDivergence)
				{
					{PressureGridContextName} DIPressureGrid_ProjectVelocityField(DIContext,GridCell,OutProjectStatus,OutCellDivergence);
				}
				)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == GetCellPositionName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
				void {InstanceFunctionName} (in int GridCell, in float3 GridOrigin, in float GridLength, out float3 OutGridPosition)
				{
					{PressureGridContextName} DIPressureGrid_GetCellPosition(DIContext,GridCell,GridOrigin,GridLength,OutGridPosition);
				}
				)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == SetSolidBoundaryName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
				void {InstanceFunctionName} (in int GridCell, in float SolidDistance, in float3 SolidVelocity, out bool OutBoundaryStatus)
				{
					{PressureGridContextName} DIPressureGrid_SetSolidBoundary(DIContext,GridCell,SolidDistance,SolidVelocity,OutBoundaryStatus);
				}
				)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == ComputeBoundaryWeightsName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
				void {InstanceFunctionName} (in int GridCell, out bool OutWeightsStatus)
				{
					{PressureGridContextName} DIPressureGrid_ComputeBoundaryWeights(DIContext,GridCell,OutWeightsStatus);
				}
				)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == TransferCellVelocityName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
				void {InstanceFunctionName} (in int GridCell, out bool OutTransferStatus)
				{
					{PressureGridContextName} DIPressureGrid_TransferCellVelocity(DIContext,GridCell,OutTransferStatus);
				}
				)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == BuildGridTopologyName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
				void {InstanceFunctionName} (in float3 GridCenter, in float3 GridExtent, out float3 OutGridOrigin, out float OutGridLength)
				{
					{PressureGridContextName} DIPressureGrid_BuildGridTopology(DIContext,GridCenter,GridExtent,OutGridOrigin,OutGridLength);
				}
				)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}

	OutHLSL += TEXT("\n");
	return false;
}

void UNiagaraDataInterfacePressureGrid::GetCommonHLSL(FString& OutHLSL)
{
	OutHLSL += TEXT("#include \"/Plugin/Experimental/HairStrands/Private/NiagaraDataInterfacePressureGrid.ush\"\n");
}

void UNiagaraDataInterfacePressureGrid::GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL)
{
	OutHLSL += TEXT("DIPRESSUREGRID_DECLARE_CONSTANTS(") + ParamInfo.DataInterfaceHLSLSymbol + TEXT(")\n");
}

void UNiagaraDataInterfacePressureGrid::ProvidePerInstanceDataForRenderThread(void* DataForRenderThread, void* PerInstanceData, const FNiagaraSystemInstanceID& SystemInstance)
{
	FNDIPressureGridData* GameThreadData = static_cast<FNDIPressureGridData*>(PerInstanceData);
	FNDIPressureGridData* RenderThreadData = static_cast<FNDIPressureGridData*>(DataForRenderThread);

	RenderThreadData->WorldTransform = GameThreadData->WorldTransform;
	RenderThreadData->GridOrigin = GameThreadData->GridOrigin;
}

FNiagaraDataInterfaceParametersCS*
UNiagaraDataInterfacePressureGrid::ConstructComputeParameters() const
{
	return new FNDIPressureGridParametersCS();
}

void FNDIPressureGridProxy::PreStage(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context)
{
	FNDIPressureGridData* ProxyData =
		SystemInstancesToProxyData.Find(Context.SystemInstance);

	if (ProxyData != nullptr)
	{
		if (!Context.IsIterationStage)
		{
			ProxyData->DestinationGridBuffer->ClearBuffers(RHICmdList);
		}
		else
		{
			ENQUEUE_RENDER_COMMAND(CopyPressureGrid)(
				[ProxyData](FRHICommandListImmediate& RHICmdList)
			{
				FRHICopyTextureInfo CopyInfo;
				RHICmdList.CopyTexture(ProxyData->CurrentGridBuffer->GridDataBuffer.Buffer,
					ProxyData->DestinationGridBuffer->GridDataBuffer.Buffer, CopyInfo);
			});
		}
	}
}

void FNDIPressureGridProxy::PostStage(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context)
{
	FNDIPressureGridData* ProxyData =
		SystemInstancesToProxyData.Find(Context.SystemInstance);

	if (Context.IsOutputStage)
	{
		if (ProxyData != nullptr)
		{
			ProxyData->SwapBuffers();
		}
	}
}

void FNDIPressureGridProxy::ResetData(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context)
{
	FNDIPressureGridData* ProxyData = SystemInstancesToProxyData.Find(Context.SystemInstance);

	if (ProxyData != nullptr)
	{
		ProxyData->CurrentGridBuffer->ClearBuffers(RHICmdList);
		ProxyData->DestinationGridBuffer->ClearBuffers(RHICmdList);
	}
}


#undef LOCTEXT_NAMESPACE