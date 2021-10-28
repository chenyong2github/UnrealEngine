// Copyright Epic Games, Inc. All Rights Reserved.

#include "Niagara/NiagaraDataInterfaceVelocityGrid.h"
#include "NiagaraShader.h"
#include "NiagaraComponent.h"
#include "NiagaraRenderer.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraEmitterInstanceBatcher.h"

#include "ShaderParameterUtils.h"
#include "ClearQuad.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "ShaderParameterStruct.h"
#include "GlobalShader.h"

#define LOCTEXT_NAMESPACE "NiagaraDataInterfaceVelocityGrid"
DEFINE_LOG_CATEGORY_STATIC(LogVelocityGrid, Log, All);

//------------------------------------------------------------------------------------------------------------

static const FName BuildVelocityFieldName(TEXT("BuildVelocityField"));
static const FName SampleVelocityFieldName(TEXT("SampleVelocityField"));
static const FName ComputeGridSizeName(TEXT("ComputeGridSize"));
static const FName UpdateGridTransformName(TEXT("UpdateGridTransform"));
static const FName SetGridDimensionName(TEXT("SetGridDimension"));

//------------------------------------------------------------------------------------------------------------

const FString UNiagaraDataInterfaceVelocityGrid::GridCurrentBufferName(TEXT("GridCurrentBuffer_"));
const FString UNiagaraDataInterfaceVelocityGrid::GridDestinationBufferName(TEXT("GridDestinationBuffer_"));

const FString UNiagaraDataInterfaceVelocityGrid::GridSizeName(TEXT("GridSize_"));
const FString UNiagaraDataInterfaceVelocityGrid::WorldTransformName(TEXT("WorldTransform_"));
const FString UNiagaraDataInterfaceVelocityGrid::WorldInverseName(TEXT("WorldInverse_"));

//------------------------------------------------------------------------------------------------------------

FNDIVelocityGridParametersName::FNDIVelocityGridParametersName(const FString& Suffix)
{
	GridCurrentBufferName = UNiagaraDataInterfaceVelocityGrid::GridCurrentBufferName + Suffix;
	GridDestinationBufferName = UNiagaraDataInterfaceVelocityGrid::GridDestinationBufferName + Suffix;

	GridSizeName = UNiagaraDataInterfaceVelocityGrid::GridSizeName + Suffix;
	WorldTransformName = UNiagaraDataInterfaceVelocityGrid::WorldTransformName + Suffix;
	WorldInverseName = UNiagaraDataInterfaceVelocityGrid::WorldInverseName + Suffix;
}

//------------------------------------------------------------------------------------------------------------

void FNDIVelocityGridBuffer::Initialize(const FIntVector InGridSize, const int32 InNumAttributes)
{
	GridSize = InGridSize;
	NumAttributes = InNumAttributes;
}

void FNDIVelocityGridBuffer::InitRHI()
{
	if (GridSize.X != 0 && GridSize.Y != 0 && GridSize.Z != 0)
	{
		static const uint32 NumComponents = NumAttributes;
		GridDataBuffer.Initialize(sizeof(int32), (GridSize.X + 1)*NumComponents, (GridSize.Y + 1),
			(GridSize.Z + 1), EPixelFormat::PF_R32_SINT);
	}
}

void FNDIVelocityGridBuffer::ReleaseRHI()
{
	GridDataBuffer.Release();
}

//------------------------------------------------------------------------------------------------------------

void FNDIVelocityGridData::Swap()
{
	FNDIVelocityGridBuffer* StoredBufferPointer = CurrentGridBuffer;
	CurrentGridBuffer = DestinationGridBuffer;
	DestinationGridBuffer = StoredBufferPointer;
}

void FNDIVelocityGridData::Release()
{
	if (CurrentGridBuffer)
	{
		BeginReleaseResource(CurrentGridBuffer);
		ENQUEUE_RENDER_COMMAND(DeleteResourceA)(
			[ParamPointerToRelease = CurrentGridBuffer](FRHICommandListImmediate& RHICmdList)
		{
			delete ParamPointerToRelease;
		});
		CurrentGridBuffer = nullptr;
	}
	if (DestinationGridBuffer)
	{
		BeginReleaseResource(DestinationGridBuffer);
		ENQUEUE_RENDER_COMMAND(DeleteResourceB)(
			[ParamPointerToRelease = DestinationGridBuffer](FRHICommandListImmediate& RHICmdList)
		{
			delete ParamPointerToRelease;
		});
		DestinationGridBuffer = nullptr;
	}
}

void FNDIVelocityGridData::Resize()
{
	if (NeedResize)
	{
		if (CurrentGridBuffer)
		{
			CurrentGridBuffer->Initialize(GridSize, NumAttributes);
			BeginInitResource(CurrentGridBuffer);
		}
		if (DestinationGridBuffer)
		{
			DestinationGridBuffer->Initialize(GridSize, NumAttributes);
			BeginInitResource(DestinationGridBuffer);
		}
		NeedResize = false;
	}
}

bool FNDIVelocityGridData::Init(const FIntVector& InGridSize, const int32 InNumAttributes, FNiagaraSystemInstance* SystemInstance)
{
	CurrentGridBuffer = nullptr;
	DestinationGridBuffer = nullptr;

	GridSize = FIntVector(1, 1, 1);
	NeedResize = true;
	WorldTransform = WorldInverse = FMatrix::Identity;

	if (InGridSize[0] != 0 && InGridSize[1] != 0 && InGridSize[2] != 0)
	{
		GridSize = InGridSize;
		NumAttributes = InNumAttributes;

		CurrentGridBuffer = new FNDIVelocityGridBuffer();
		DestinationGridBuffer = new FNDIVelocityGridBuffer();

		Resize();
	}

	return true;
}

void FNDIVelocityGridParametersCS::Bind(const FNiagaraDataInterfaceGPUParamInfo& ParameterInfo, const class FShaderParameterMap& ParameterMap)
{
	FNDIVelocityGridParametersName ParamNames(ParameterInfo.DataInterfaceHLSLSymbol);

	GridCurrentBuffer.Bind(ParameterMap, *ParamNames.GridCurrentBufferName);
	GridDestinationBuffer.Bind(ParameterMap, *ParamNames.GridDestinationBufferName);

	GridSize.Bind(ParameterMap, *ParamNames.GridSizeName);
	WorldTransform.Bind(ParameterMap, *ParamNames.WorldTransformName);
	WorldInverse.Bind(ParameterMap, *ParamNames.WorldInverseName);

}

void FNDIVelocityGridParametersCS::Set(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const
{
	check(IsInRenderingThread());

	FRHIComputeShader* ComputeShaderRHI = RHICmdList.GetBoundComputeShader();

	FNDIVelocityGridProxy* InterfaceProxy =
		static_cast<FNDIVelocityGridProxy*>(Context.DataInterface);
	FNDIVelocityGridData* ProxyData =
		InterfaceProxy->SystemInstancesToProxyData.Find(Context.SystemInstanceID);

	if (ProxyData != nullptr && ProxyData->CurrentGridBuffer != nullptr && ProxyData->DestinationGridBuffer != nullptr
		&& ProxyData->CurrentGridBuffer->IsInitialized() && ProxyData->DestinationGridBuffer->IsInitialized())
	{
		FNDIVelocityGridBuffer* CurrentGridBuffer = ProxyData->CurrentGridBuffer;
		FNDIVelocityGridBuffer* DestinationGridBuffer = ProxyData->DestinationGridBuffer;

		SetUAVParameter(RHICmdList, ComputeShaderRHI, GridDestinationBuffer, DestinationGridBuffer->GridDataBuffer.UAV);
		SetSRVParameter(RHICmdList, ComputeShaderRHI, GridCurrentBuffer, CurrentGridBuffer->GridDataBuffer.SRV);

		SetShaderValue(RHICmdList, ComputeShaderRHI, GridSize, ProxyData->GridSize);
		SetShaderValue(RHICmdList, ComputeShaderRHI, WorldTransform, ProxyData->WorldTransform);
		SetShaderValue(RHICmdList, ComputeShaderRHI, WorldInverse, ProxyData->WorldTransform.Inverse());
	}
	else
	{
		SetUAVParameter(RHICmdList, ComputeShaderRHI, GridDestinationBuffer, Context.Batcher->GetEmptyUAVFromPool(RHICmdList, PF_R32_UINT, ENiagaraEmptyUAVType::Buffer));
		SetSRVParameter(RHICmdList, ComputeShaderRHI, GridCurrentBuffer, FNiagaraRenderer::GetDummyUIntBuffer());

		SetShaderValue(RHICmdList, ComputeShaderRHI, GridSize, FIntVector());
		SetShaderValue(RHICmdList, ComputeShaderRHI, WorldTransform, FMatrix::Identity);
		SetShaderValue(RHICmdList, ComputeShaderRHI, WorldInverse, FMatrix::Identity);
	}
}

void FNDIVelocityGridParametersCS::Unset(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const
{
	FRHIComputeShader* ShaderRHI = RHICmdList.GetBoundComputeShader();
	SetUAVParameter(RHICmdList, ShaderRHI, GridDestinationBuffer, nullptr);
}

IMPLEMENT_TYPE_LAYOUT(FNDIVelocityGridParametersCS);

IMPLEMENT_NIAGARA_DI_PARAMETER(UNiagaraDataInterfaceVelocityGrid, FNDIVelocityGridParametersCS);

//------------------------------------------------------------------------------------------------------------

UNiagaraDataInterfaceVelocityGrid::UNiagaraDataInterfaceVelocityGrid(FObjectInitializer const& ObjectInitializer)
	: Super(ObjectInitializer)
	, GridSize(10)
{
	Proxy.Reset(new FNDIVelocityGridProxy());
	NumAttributes = 6;
}

bool UNiagaraDataInterfaceVelocityGrid::InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	const FIntVector ClampedSize = FIntVector(FMath::Clamp(GridSize.X, 0, 50), FMath::Clamp(GridSize.Y, 0, 50), FMath::Clamp(GridSize.Z, 0, 50));

	if (GridSize != ClampedSize)
	{
		UE_LOG(LogVelocityGrid, Warning, TEXT("The grid size is beyond its maximum value (50)"));
	}
	GridSize = ClampedSize;

	FNDIVelocityGridData* InstanceData = new (PerInstanceData) FNDIVelocityGridData();
	check(InstanceData);

	return InstanceData->Init(this->GridSize, NumAttributes, SystemInstance);
}

void UNiagaraDataInterfaceVelocityGrid::DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	FNDIVelocityGridData* InstanceData = static_cast<FNDIVelocityGridData*>(PerInstanceData);

	InstanceData->Release();
	InstanceData->~FNDIVelocityGridData();

	FNDIVelocityGridProxy* ThisProxy = GetProxyAs<FNDIVelocityGridProxy>();
	ENQUEUE_RENDER_COMMAND(FNiagaraDIDestroyInstanceData) (
		[ThisProxy, InstanceID = SystemInstance->GetId(), Batcher = SystemInstance->GetBatcher()](FRHICommandListImmediate& CmdList)
	{
		ThisProxy->SystemInstancesToProxyData.Remove(InstanceID);
	}
	);
}

bool UNiagaraDataInterfaceVelocityGrid::PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float InDeltaSeconds)
{
	FNDIVelocityGridData* InstanceData = static_cast<FNDIVelocityGridData*>(PerInstanceData);

	bool RequireReset = false;
	if (InstanceData)
	{
		InstanceData->WorldTransform = SystemInstance->GetWorldTransform().ToMatrixWithScale();

		if (InstanceData->NeedResize)
		{
			InstanceData->Resize();
		}
	}
	return RequireReset;
}

bool UNiagaraDataInterfaceVelocityGrid::CopyToInternal(UNiagaraDataInterface* Destination) const
{
	if (!Super::CopyToInternal(Destination))
	{
		return false;
	}

	UNiagaraDataInterfaceVelocityGrid* OtherTyped = CastChecked<UNiagaraDataInterfaceVelocityGrid>(Destination);
	OtherTyped->GridSize = GridSize;

	return true;
}

bool UNiagaraDataInterfaceVelocityGrid::Equals(const UNiagaraDataInterface* Other) const
{
	if (!Super::Equals(Other))
	{
		return false;
	}
	const UNiagaraDataInterfaceVelocityGrid* OtherTyped = CastChecked<const UNiagaraDataInterfaceVelocityGrid>(Other);

	return (OtherTyped->GridSize == GridSize);
}

#if WITH_EDITORONLY_DATA
bool UNiagaraDataInterfaceVelocityGrid::AppendCompileHash(FNiagaraCompileHashVisitor* InVisitor) const
{
	if (!Super::AppendCompileHash(InVisitor))
		return false;

	FSHAHash Hash = GetShaderFileHash((TEXT("/Plugin/Runtime/HairStrands/Private/NiagaraDataInterfaceVelocityGrid.ush")), EShaderPlatform::SP_PCD3D_SM5);
	InVisitor->UpdateString(TEXT("NiagaraDataInterfaceVelocityGridHLSLSource"), Hash.ToString());
	return true;
}
#endif

void UNiagaraDataInterfaceVelocityGrid::PostInitProperties()
{
	Super::PostInitProperties();

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		ENiagaraTypeRegistryFlags Flags = ENiagaraTypeRegistryFlags::AllowAnyVariable | ENiagaraTypeRegistryFlags::AllowParameter;
		FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(GetClass()), Flags);
	}
}

void UNiagaraDataInterfaceVelocityGrid::GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)
{
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = BuildVelocityFieldName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.bWriteFunction = true;
		Sig.bSupportsGPU = true;
		Sig.bSupportsCPU = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT(" Velocity Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Grid Origin")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Grid Length")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Particle Position")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Particle Mass")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Particle Velocity")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetMatrix4Def(), TEXT("Velocity Gradient")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Function Status")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = SampleVelocityFieldName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.bSupportsGPU = true;
		Sig.bSupportsCPU = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT(" Velocity Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Grid Origin")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Grid Length")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Particle Position")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Scaled Velocity")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Particle Mass")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Particle Velocity")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetMatrix4Def(), TEXT("Velocity Gradient")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = ComputeGridSizeName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.bSupportsGPU = true;
		Sig.bSupportsCPU = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT(" Velocity Grid")));
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
		Sig.bWriteFunction = true;
		Sig.bSupportsGPU = false;
		Sig.bSupportsCPU = true;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT(" Velocity Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetMatrix4Def(), TEXT("Grid Transform")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Function Status")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = SetGridDimensionName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.bWriteFunction = true;
		Sig.bSupportsGPU = true;
		Sig.bSupportsCPU = true;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT(" Velocity Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Grid Dimension")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Function Status")));

		OutFunctions.Add(Sig);
	}
}

DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceVelocityGrid, BuildVelocityField);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceVelocityGrid, SampleVelocityField);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceVelocityGrid, ComputeGridSize);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceVelocityGrid, UpdateGridTransform);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceVelocityGrid, SetGridDimension);

void UNiagaraDataInterfaceVelocityGrid::GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc)
{
	if (BindingInfo.Name == BuildVelocityFieldName)
	{
		check(BindingInfo.GetNumInputs() == 28 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceVelocityGrid, BuildVelocityField)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == SampleVelocityFieldName)
	{
		check(BindingInfo.GetNumInputs() == 9 && BindingInfo.GetNumOutputs() == 20);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceVelocityGrid, SampleVelocityField)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == ComputeGridSizeName)
	{
		check(BindingInfo.GetNumInputs() == 7 && BindingInfo.GetNumOutputs() == 4);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceVelocityGrid, ComputeGridSize)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == UpdateGridTransformName)
	{
		check(BindingInfo.GetNumInputs() == 17 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceVelocityGrid, UpdateGridTransform)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == SetGridDimensionName)
	{
		check(BindingInfo.GetNumInputs() == 4 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceVelocityGrid, SetGridDimension)::Bind(this, OutFunc);
	}
}

void UNiagaraDataInterfaceVelocityGrid::BuildVelocityField(FVectorVMContext& Context)
{
	// @todo : implement function for cpu 
}

void UNiagaraDataInterfaceVelocityGrid::SampleVelocityField(FVectorVMContext& Context)
{
	// @todo : implement function for cpu 
}

void UNiagaraDataInterfaceVelocityGrid::ComputeGridSize(FVectorVMContext& Context)
{
	// @todo : implement function for cpu
}

void UNiagaraDataInterfaceVelocityGrid::SetGridDimension(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIVelocityGridData> InstData(Context);
	VectorVM::FExternalFuncInputHandler<float> GridDimensionX(Context);
	VectorVM::FExternalFuncInputHandler<float> GridDimensionY(Context);
	VectorVM::FExternalFuncInputHandler<float> GridDimensionZ(Context);

	VectorVM::FExternalFuncRegisterHandler<bool> OutFunctionStatus(Context);

	for (int32 InstanceIdx = 0; InstanceIdx < Context.NumInstances; ++InstanceIdx)
	{
		FIntVector GridDimension;
		GridDimension.X = *GridDimensionX.GetDestAndAdvance();
		GridDimension.Y = *GridDimensionY.GetDestAndAdvance();
		GridDimension.Z = *GridDimensionZ.GetDestAndAdvance();

		InstData->GridSize = GridDimension;
		InstData->NeedResize = true;

		*OutFunctionStatus.GetDestAndAdvance() = true;
	}
}

void UNiagaraDataInterfaceVelocityGrid::UpdateGridTransform(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIVelocityGridData> InstData(Context);

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
}

#if WITH_EDITORONLY_DATA
bool UNiagaraDataInterfaceVelocityGrid::GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL)
{
	FNDIVelocityGridParametersName ParamNames(ParamInfo.DataInterfaceHLSLSymbol);

	TMap<FString, FStringFormatArg> ArgsSample = {
		{TEXT("InstanceFunctionName"), FunctionInfo.InstanceName},
		{TEXT("VelocityGridContextName"), TEXT("DIVelocityGrid_MAKE_CONTEXT(") + ParamInfo.DataInterfaceHLSLSymbol + TEXT(")")},
	};

	if (FunctionInfo.DefinitionName == BuildVelocityFieldName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
				void {InstanceFunctionName} (in float3 GridOrigin, in float GridLength, in float3 ParticlePosition, in float ParticleMass, in float3 ParticleVelocity, in float4x4 VelocityGradient, out bool OutFunctionStatus)
				{
					{VelocityGridContextName} DIVelocityGrid_BuildVelocityField(DIContext,DIContext_GridDestinationBuffer,GridOrigin,GridLength,ParticlePosition,ParticleMass,ParticleVelocity,VelocityGradient,OutFunctionStatus);
				}
				)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == SampleVelocityFieldName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
				void {InstanceFunctionName} (in float3 GridOrigin, in float GridLength, in float3 ParticlePosition, in bool ScaledVelocity, out float OutParticleMass, out float3 OutParticleVelocity, out float4x4 OutVelocityGradient)
				{
					{VelocityGridContextName} DIVelocityGrid_SampleVelocityField(DIContext,GridOrigin,GridLength,ParticlePosition,ScaledVelocity,OutParticleMass,OutParticleVelocity,OutVelocityGradient);
				}
				)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == ComputeGridSizeName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
				void {InstanceFunctionName} (in float3 GridCenter, in float3 GridExtent, out float3 OutGridOrigin, out float OutGridLength)
				{
					{VelocityGridContextName} DIVelocityGrid_ComputeGridSize(DIContext,GridCenter,GridExtent,OutGridOrigin,OutGridLength);
				}
				)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	OutHLSL += TEXT("\n");
	return false;
}

void UNiagaraDataInterfaceVelocityGrid::GetCommonHLSL(FString& OutHLSL)
{
	OutHLSL += TEXT("#include \"/Plugin/Runtime/HairStrands/Private/NiagaraDataInterfaceVelocityGrid.ush\"\n");
}

void UNiagaraDataInterfaceVelocityGrid::GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL)
{
	OutHLSL += TEXT("DIVelocityGrid_DECLARE_CONSTANTS(") + ParamInfo.DataInterfaceHLSLSymbol + TEXT(")\n");
}
#endif

void UNiagaraDataInterfaceVelocityGrid::ProvidePerInstanceDataForRenderThread(void* DataForRenderThread, void* PerInstanceData, const FNiagaraSystemInstanceID& SystemInstance)
{
	FNDIVelocityGridData* GameThreadData = static_cast<FNDIVelocityGridData*>(PerInstanceData);
	FNDIVelocityGridData* RenderThreadData = static_cast<FNDIVelocityGridData*>(DataForRenderThread);

	RenderThreadData->WorldTransform = GameThreadData->WorldTransform;
	RenderThreadData->WorldInverse = GameThreadData->WorldInverse;
	RenderThreadData->CurrentGridBuffer = GameThreadData->CurrentGridBuffer;
	RenderThreadData->DestinationGridBuffer = GameThreadData->DestinationGridBuffer;
	RenderThreadData->GridSize = GameThreadData->GridSize;
}

//------------------------------------------------------------------------------------------------------------

inline void ClearBuffer(FRHICommandList& RHICmdList, FNDIVelocityGridBuffer* DestinationGridBuffer)
{
	FRHIUnorderedAccessView* DestinationGridBufferUAV = DestinationGridBuffer->GridDataBuffer.UAV;

	if (DestinationGridBufferUAV != nullptr )
	{
		RHICmdList.ClearUAVUint(DestinationGridBufferUAV, FUintVector4(0, 0, 0, 0));
	}
}

//------------------------------------------------------------------------------------------------------------

void FNDIVelocityGridProxy::ConsumePerInstanceDataFromGameThread(void* PerInstanceData, const FNiagaraSystemInstanceID& Instance)
{
	FNDIVelocityGridData* SourceData = static_cast<FNDIVelocityGridData*>(PerInstanceData);
	FNDIVelocityGridData* TargetData = &(SystemInstancesToProxyData.FindOrAdd(Instance));

	ensure(TargetData);
	if (TargetData)
	{
		TargetData->WorldTransform = SourceData->WorldTransform;
		TargetData->WorldInverse = SourceData->WorldInverse;
		TargetData->GridSize = SourceData->GridSize;
		TargetData->DestinationGridBuffer = SourceData->DestinationGridBuffer;
		TargetData->CurrentGridBuffer = SourceData->CurrentGridBuffer;
	}
	else
	{
		UE_LOG(LogVelocityGrid, Log, TEXT("ConsumePerInstanceDataFromGameThread() ... could not find %s"), *FNiagaraUtilities::SystemInstanceIDToString(Instance));
	}
	SourceData->~FNDIVelocityGridData();
}

//------------------------------------------------------------------------------------------------------------

#define NIAGARA_HAIR_STRANDS_THREAD_COUNT_VELOCITY  4

class FClearVelocityGridCS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FClearVelocityGridCS, Global)

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return RHISupportsComputeShaders(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREAD_COUNT"), NIAGARA_HAIR_STRANDS_THREAD_COUNT_VELOCITY );
	}

	FClearVelocityGridCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		GridSize.Bind(Initializer.ParameterMap, TEXT("GridSize"));
		GridDestinationBuffer.Bind(Initializer.ParameterMap, TEXT("GridDestinationBuffer"));
	}

	FClearVelocityGridCS()
	{}

	void SetParameters(FRHICommandList& RHICmdList, FRHIUnorderedAccessView* InGridDestinationBuffer,
		const FIntVector& InGridSize)
	{
		FRHIComputeShader* ShaderRHI = RHICmdList.GetBoundComputeShader();

		SetUAVParameter(RHICmdList, ShaderRHI, GridDestinationBuffer, InGridDestinationBuffer);
		SetShaderValue(RHICmdList, ShaderRHI, GridSize, InGridSize);
	}

	void UnsetParameters(FRHICommandList& RHICmdList)
	{
		FRHIComputeShader* ShaderRHI = RHICmdList.GetBoundComputeShader();

		SetUAVParameter(RHICmdList, ShaderRHI, GridDestinationBuffer, nullptr);
	}

	LAYOUT_FIELD(FShaderResourceParameter, GridDestinationBuffer);
	LAYOUT_FIELD(FShaderParameter, GridSize);
};

IMPLEMENT_SHADER_TYPE(, FClearVelocityGridCS, TEXT("/Plugin/Runtime/HairStrands/Private/NiagaraClearVelocityGrid.usf"), TEXT("MainCS"), SF_Compute);

inline void ClearTexture(FRHICommandList& RHICmdList, FNDIVelocityGridBuffer* DestinationGridBuffer, const FIntVector& InGridSize)
{
	FRHIUnorderedAccessView* DestinationGridBufferUAV = DestinationGridBuffer->GridDataBuffer.UAV;

	if (DestinationGridBufferUAV != nullptr)
	{
		TShaderMapRef<FClearVelocityGridCS> ComputeShader(GetGlobalShaderMap(ERHIFeatureLevel::SM5));
		RHICmdList.SetComputeShader(ComputeShader.GetComputeShader());

		FRHITransitionInfo Transitions[] = {
			FRHITransitionInfo(DestinationGridBufferUAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute)
		};
		RHICmdList.Transition(MakeArrayView(Transitions, UE_ARRAY_COUNT(Transitions)));

		const uint32 GroupSize = NIAGARA_HAIR_STRANDS_THREAD_COUNT_VELOCITY ;
		const FIntVector GridSize( (InGridSize.X + 1) * DestinationGridBuffer->NumAttributes, InGridSize.Y + 1, InGridSize.Z + 1);

		const uint32 DispatchCountX = FMath::DivideAndRoundUp((uint32)GridSize.X, GroupSize);
		const uint32 DispatchCountY = FMath::DivideAndRoundUp((uint32)GridSize.Y, GroupSize);
		const uint32 DispatchCountZ = FMath::DivideAndRoundUp((uint32)GridSize.Z, GroupSize);

		ComputeShader->SetParameters(RHICmdList, DestinationGridBufferUAV, GridSize);
		DispatchComputeShader(RHICmdList, ComputeShader.GetShader(), DispatchCountX, DispatchCountY, DispatchCountZ);
		ComputeShader->UnsetParameters(RHICmdList);
	}
}

//------------------------------------------------------------------------------------------------------------

class FCopyVelocityGridCS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FCopyVelocityGridCS, Global)

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return RHISupportsComputeShaders(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREAD_COUNT"), NIAGARA_HAIR_STRANDS_THREAD_COUNT_VELOCITY );
	}

	FCopyVelocityGridCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		GridSize.Bind(Initializer.ParameterMap, TEXT("GridSize"));
		GridDestinationBuffer.Bind(Initializer.ParameterMap, TEXT("GridDestinationBuffer"));
		GridCurrentBuffer.Bind(Initializer.ParameterMap, TEXT("GridCurrentBuffer"));
	}

	FCopyVelocityGridCS()
	{}

	void SetParameters(FRHICommandList& RHICmdList, FRHIShaderResourceView* InGridCurrentBuffer,
		FRHIUnorderedAccessView* InGridDestinationBuffer,
		const FIntVector& InGridSize)
	{
		FRHIComputeShader* ShaderRHI = RHICmdList.GetBoundComputeShader();

		SetUAVParameter(RHICmdList, ShaderRHI, GridDestinationBuffer, InGridDestinationBuffer);
		SetShaderValue(RHICmdList, ShaderRHI, GridSize, InGridSize);
		SetSRVParameter(RHICmdList, ShaderRHI, GridCurrentBuffer, InGridCurrentBuffer);
	}

	void UnsetParameters(FRHICommandList& RHICmdList)
	{
		FRHIComputeShader* ShaderRHI = RHICmdList.GetBoundComputeShader();

		SetUAVParameter(RHICmdList, ShaderRHI, GridDestinationBuffer, nullptr);
	}

	LAYOUT_FIELD(FShaderResourceParameter, GridDestinationBuffer);
	LAYOUT_FIELD(FShaderParameter, GridSize);
	LAYOUT_FIELD(FShaderResourceParameter, GridCurrentBuffer);
};

IMPLEMENT_SHADER_TYPE(, FCopyVelocityGridCS, TEXT("/Plugin/Runtime/HairStrands/Private/NiagaraCopyVelocityGrid.usf"), TEXT("MainCS"), SF_Compute);

inline void CopyTexture(FRHICommandList& RHICmdList, FNDIVelocityGridBuffer* CurrentGridBuffer, FNDIVelocityGridBuffer* DestinationGridBuffer, const FIntVector& InGridSize)
{
	FRHIUnorderedAccessView* DestinationGridBufferUAV = DestinationGridBuffer->GridDataBuffer.UAV;
	FRHIShaderResourceView* CurrentGridBufferSRV = CurrentGridBuffer->GridDataBuffer.SRV;
	FRHIUnorderedAccessView* CurrentGridBufferUAV = CurrentGridBuffer->GridDataBuffer.UAV;

	if (DestinationGridBufferUAV != nullptr && CurrentGridBufferSRV != nullptr && CurrentGridBufferUAV != nullptr)
	{
		TShaderMapRef<FCopyVelocityGridCS> ComputeShader(GetGlobalShaderMap(ERHIFeatureLevel::SM5));
		RHICmdList.SetComputeShader(ComputeShader.GetComputeShader());

		FRHITransitionInfo Transitions[] = {
			FRHITransitionInfo(CurrentGridBufferUAV, ERHIAccess::Unknown, ERHIAccess::SRVCompute),
			FRHITransitionInfo(DestinationGridBufferUAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute)
		};
		RHICmdList.Transition(MakeArrayView(Transitions, UE_ARRAY_COUNT(Transitions)));

		const uint32 GroupSize = NIAGARA_HAIR_STRANDS_THREAD_COUNT_VELOCITY ;
		const FIntVector GridSize((InGridSize.X + 1) * DestinationGridBuffer->NumAttributes,InGridSize.Y + 1, InGridSize.Z + 1);

		const uint32 DispatchCountX = FMath::DivideAndRoundUp((uint32)GridSize.X, GroupSize);
		const uint32 DispatchCountY = FMath::DivideAndRoundUp((uint32)GridSize.Y, GroupSize);
		const uint32 DispatchCountZ = FMath::DivideAndRoundUp((uint32)GridSize.Z, GroupSize);

		ComputeShader->SetParameters(RHICmdList, CurrentGridBufferSRV, DestinationGridBufferUAV, GridSize);
		DispatchComputeShader(RHICmdList, ComputeShader.GetShader(), DispatchCountX, DispatchCountY, DispatchCountZ);
		ComputeShader->UnsetParameters(RHICmdList);
	}
}

void FNDIVelocityGridProxy::PreStage(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceStageArgs& Context)
{
	FNDIVelocityGridData* ProxyData =
		SystemInstancesToProxyData.Find(Context.SystemInstanceID);

	if (ProxyData != nullptr)
	{
		if (Context.SimStageData->bFirstStage)
		{
			ClearTexture(RHICmdList, ProxyData->DestinationGridBuffer, ProxyData->GridSize);
		}

		FRHITransitionInfo Transitions[] = {
			// FIXME: what's the source state for these?
			FRHITransitionInfo(ProxyData->CurrentGridBuffer->GridDataBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::SRVCompute),
			FRHITransitionInfo(ProxyData->DestinationGridBuffer->GridDataBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute)
		};
		RHICmdList.Transition(MakeArrayView(Transitions, UE_ARRAY_COUNT(Transitions)));
	}
}

void FNDIVelocityGridProxy::PostStage(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceStageArgs& Context)
{
	FNDIVelocityGridData* ProxyData =
		SystemInstancesToProxyData.Find(Context.SystemInstanceID);

	if (ProxyData != nullptr)
	{
		//ProxyData->Swap();
		CopyTexture(RHICmdList, ProxyData->DestinationGridBuffer, ProxyData->CurrentGridBuffer,  ProxyData->GridSize);
		//FRHICopyTextureInfo CopyInfo;
		//RHICmdList.CopyTexture(ProxyData->DestinationGridBuffer->GridDataBuffer.Buffer,
		//	ProxyData->CurrentGridBuffer->GridDataBuffer.Buffer, CopyInfo);
	}
}

void FNDIVelocityGridProxy::ResetData(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceArgs& Context)
{
	FNDIVelocityGridData* ProxyData = SystemInstancesToProxyData.Find(Context.SystemInstanceID);

	if (ProxyData != nullptr && ProxyData->DestinationGridBuffer != nullptr && ProxyData->CurrentGridBuffer != nullptr)
	{
		ClearTexture(RHICmdList, ProxyData->DestinationGridBuffer, ProxyData->GridSize);
		ClearTexture(RHICmdList, ProxyData->CurrentGridBuffer, ProxyData->GridSize);
	}
}

// Get the element count for this instance
FIntVector FNDIVelocityGridProxy::GetElementCount(FNiagaraSystemInstanceID SystemInstanceID) const
{
	if  ( const FNDIVelocityGridData* ProxyData = SystemInstancesToProxyData.Find(SystemInstanceID) )
	{
		return FIntVector(ProxyData->GridSize.X + 1, ProxyData->GridSize.Y + 1, ProxyData->GridSize.Z + 1);
	}
	return FIntVector::ZeroValue;
}

#undef LOCTEXT_NAMESPACE
