// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraDataInterfacePhysicsField.h"
#include "NiagaraShader.h"
#include "NiagaraComponent.h"
#include "NiagaraRenderer.h"
#include "NiagaraSystemInstance.h"
#include "ShaderParameterUtils.h"
#include "Field/FieldSystemNodes.h"
#include "PhysicsField/PhysicsFieldComponent.h"
#include "ChaosStats.h"

#define LOCTEXT_NAMESPACE "NiagaraDataInterfacePhysicsField"
DEFINE_LOG_CATEGORY_STATIC(LogPhysicsField, Log, All);

struct FNiagaraPhysicsFieldDIFunctionVersion
{
	enum Type
	{
		InitialVersion = 0,
		LargeWorldCoordinates = 1,

		VersionPlusOne,
		LatestVersion = VersionPlusOne - 1
	};
};

//------------------------------------------------------------------------------------------------------------

static const FName SamplePhysicsVectorFieldName(TEXT("SamplePhysicsVectorField"));
static const FName SamplePhysicsScalarFieldName(TEXT("SamplePhysicsScalarField"));
static const FName SamplePhysicsIntegerFieldName(TEXT("SamplePhysicsIntegerField"));

static const FName GetPhysicsFieldResolutionName(TEXT("GetPhysicsFieldResolution"));
static const FName GetPhysicsFieldBoundsName(TEXT("GetPhysicsFieldBounds"));

//------------------------------------------------------------------------------------------------------------

const FString UNiagaraDataInterfacePhysicsField::ClipmapBufferName(TEXT("ClipmapBuffer_"));
const FString UNiagaraDataInterfacePhysicsField::ClipmapCenterName(TEXT("ClipmapCenter_"));
const FString UNiagaraDataInterfacePhysicsField::ClipmapDistanceName(TEXT("ClipmapDistance_"));
const FString UNiagaraDataInterfacePhysicsField::ClipmapResolutionName(TEXT("ClipmapResolution_"));
const FString UNiagaraDataInterfacePhysicsField::ClipmapExponentName(TEXT("ClipmapExponent_"));
const FString UNiagaraDataInterfacePhysicsField::ClipmapCountName(TEXT("ClipmapCount_"));
const FString UNiagaraDataInterfacePhysicsField::TargetCountName(TEXT("TargetCount_"));
const FString UNiagaraDataInterfacePhysicsField::FieldTargetsName(TEXT("FieldTargets_"));
const FString UNiagaraDataInterfacePhysicsField::SystemLWCTileName(TEXT("SystemLWCTile_"));

//------------------------------------------------------------------------------------------------------------

struct FNDIPhysicsFieldParametersName
{
	FNDIPhysicsFieldParametersName(const FString& Suffix)
	{
		ClipmapBufferName = UNiagaraDataInterfacePhysicsField::ClipmapBufferName + Suffix;
		ClipmapCenterName = UNiagaraDataInterfacePhysicsField::ClipmapCenterName + Suffix;
		ClipmapDistanceName = UNiagaraDataInterfacePhysicsField::ClipmapDistanceName + Suffix;
		ClipmapResolutionName = UNiagaraDataInterfacePhysicsField::ClipmapResolutionName + Suffix;
		ClipmapExponentName = UNiagaraDataInterfacePhysicsField::ClipmapExponentName + Suffix;
		ClipmapCountName = UNiagaraDataInterfacePhysicsField::ClipmapCountName + Suffix;
		TargetCountName = UNiagaraDataInterfacePhysicsField::TargetCountName + Suffix;
		FieldTargetsName = UNiagaraDataInterfacePhysicsField::FieldTargetsName + Suffix;
		SystemLWCTileName = UNiagaraDataInterfacePhysicsField::SystemLWCTileName + Suffix;
	}

	FString ClipmapBufferName;
	FString ClipmapCenterName;
	FString ClipmapDistanceName;
	FString ClipmapResolutionName;
	FString ClipmapExponentName;
	FString ClipmapCountName;
	FString TargetCountName;
	FString FieldTargetsName;
	FString SystemLWCTileName;
};

//------------------------------------------------------------------------------------------------------------

void FNDIPhysicsFieldData::Release()
{
	FieldResource = nullptr;
	FieldCommands.Empty();
}

void FNDIPhysicsFieldData::Init(FNiagaraSystemInstance* SystemInstance)
{
	Release();
	if (SystemInstance != nullptr)
	{
		UWorld* World = SystemInstance->GetWorld();
		if (World)
		{
			TimeSeconds = World->GetTimeSeconds();

			UPhysicsFieldComponent* FieldComponent = World->PhysicsField;
			if (FieldComponent && FieldComponent->FieldInstance && FieldComponent->FieldInstance->FieldResource)
			{
				FieldResource = FieldComponent->FieldInstance->FieldResource;
			}
		}
		LWCConverter = SystemInstance->GetLWCConverter();
	}
}

void FNDIPhysicsFieldData::Update(FNiagaraSystemInstance* SystemInstance)
{
	if (SystemInstance != nullptr)
	{
		UWorld* World = SystemInstance->GetWorld();
		if (World)
		{	
			TimeSeconds = World->GetTimeSeconds();

			UPhysicsFieldComponent* FieldComponent = World->PhysicsField;
			if (FieldComponent && FieldComponent->FieldInstance)
			{
				FieldCommands = FieldComponent->FieldInstance->FieldCommands;
			}
		}
	}
}

//------------------------------------------------------------------------------------------------------------

struct FNDIPhysicsFieldParametersCS : public FNiagaraDataInterfaceParametersCS
{
	DECLARE_TYPE_LAYOUT(FNDIPhysicsFieldParametersCS, NonVirtual);
public:
	void Bind(const FNiagaraDataInterfaceGPUParamInfo& ParameterInfo, const class FShaderParameterMap& ParameterMap)
	{
		FNDIPhysicsFieldParametersName ParamNames(*ParameterInfo.DataInterfaceHLSLSymbol);

		ClipmapBuffer.Bind(ParameterMap, *ParamNames.ClipmapBufferName);
		ClipmapCenter.Bind(ParameterMap, *ParamNames.ClipmapCenterName);
		ClipmapDistance.Bind(ParameterMap, *ParamNames.ClipmapDistanceName);
		ClipmapResolution.Bind(ParameterMap, *ParamNames.ClipmapResolutionName);
		ClipmapExponent.Bind(ParameterMap, *ParamNames.ClipmapExponentName);
		ClipmapCount.Bind(ParameterMap, *ParamNames.ClipmapCountName);
		TargetCount.Bind(ParameterMap, *ParamNames.TargetCountName);
		FieldTargets.Bind(ParameterMap, *ParamNames.FieldTargetsName);
		SystemLWCTile.Bind(ParameterMap, *ParamNames.SystemLWCTileName);
	}

	void Set(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const
	{
		check(IsInRenderingThread());

		FRHIComputeShader* ComputeShaderRHI = RHICmdList.GetBoundComputeShader();

		FNDIPhysicsFieldProxy* InterfaceProxy =
			static_cast<FNDIPhysicsFieldProxy*>(Context.DataInterface);
		FNDIFieldRenderData* ProxyData =
			InterfaceProxy->SystemInstancesToProxyData.Find(Context.SystemInstanceID);

		TStaticArray<FIntVector4, MAX_PHYSICS_FIELD_TARGETS,16> LocalTargets;
		if (ProxyData != nullptr && ProxyData->FieldResource)
		{
			FPhysicsFieldResource* FieldResource = ProxyData->FieldResource;

			for (int32 Index = 0; Index < MAX_PHYSICS_FIELD_TARGETS; ++Index)
			{
				LocalTargets[Index].X = FieldResource->FieldInfos.VectorTargets[Index];
				LocalTargets[Index].Y = FieldResource->FieldInfos.ScalarTargets[Index];
				LocalTargets[Index].Z = FieldResource->FieldInfos.IntegerTargets[Index];
				LocalTargets[Index].W = 0; // Padding
			}
			SetSRVParameter(RHICmdList, ComputeShaderRHI, ClipmapBuffer, FieldResource->ClipmapBuffer.SRV);
			SetShaderValue(RHICmdList, ComputeShaderRHI, ClipmapCenter, (FVector3f)FieldResource->FieldInfos.ClipmapCenter);
			SetShaderValue(RHICmdList, ComputeShaderRHI, ClipmapDistance, FieldResource->FieldInfos.ClipmapDistance);
			SetShaderValue(RHICmdList, ComputeShaderRHI, ClipmapResolution, FieldResource->FieldInfos.ClipmapResolution);
			SetShaderValue(RHICmdList, ComputeShaderRHI, ClipmapExponent, FieldResource->FieldInfos.ClipmapExponent);
			SetShaderValue(RHICmdList, ComputeShaderRHI, ClipmapCount, FieldResource->FieldInfos.ClipmapCount);

			SetShaderValue(RHICmdList, ComputeShaderRHI, TargetCount, FieldResource->FieldInfos.TargetCount);
			SetShaderValue(RHICmdList, ComputeShaderRHI, FieldTargets, LocalTargets);
		}
		else
		{
			SetSRVParameter(RHICmdList, ComputeShaderRHI, ClipmapBuffer, FNiagaraRenderer::GetDummyFloatBuffer());
			SetShaderValue(RHICmdList, ComputeShaderRHI, ClipmapCenter, FVector3f::ZeroVector);
			SetShaderValue(RHICmdList, ComputeShaderRHI, ClipmapDistance, 1);
			SetShaderValue(RHICmdList, ComputeShaderRHI, ClipmapResolution, 2);
			SetShaderValue(RHICmdList, ComputeShaderRHI, ClipmapExponent, 1);
			SetShaderValue(RHICmdList, ComputeShaderRHI, ClipmapCount, 1);
			SetShaderValue(RHICmdList, ComputeShaderRHI, TargetCount, 0);
			SetShaderValue(RHICmdList, ComputeShaderRHI, FieldTargets, LocalTargets);
		}
		SetShaderValue(RHICmdList, ComputeShaderRHI, SystemLWCTile, Context.SystemLWCTile);
	}

	void Unset(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const
	{
	}

private:

	LAYOUT_FIELD(FShaderResourceParameter, ClipmapBuffer);
	LAYOUT_FIELD(FShaderParameter, ClipmapCenter);
	LAYOUT_FIELD(FShaderParameter, ClipmapDistance);
	LAYOUT_FIELD(FShaderParameter, ClipmapResolution);
	LAYOUT_FIELD(FShaderParameter, ClipmapExponent);
	LAYOUT_FIELD(FShaderParameter, ClipmapCount);
	LAYOUT_FIELD(FShaderParameter, TargetCount);
	LAYOUT_FIELD(FShaderParameter, FieldTargets);
	LAYOUT_FIELD(FShaderParameter, SystemLWCTile);
};

IMPLEMENT_TYPE_LAYOUT(FNDIPhysicsFieldParametersCS);

IMPLEMENT_NIAGARA_DI_PARAMETER(UNiagaraDataInterfacePhysicsField, FNDIPhysicsFieldParametersCS);


//------------------------------------------------------------------------------------------------------------

void FNDIPhysicsFieldProxy::ConsumePerInstanceDataFromGameThread(void* PerInstanceData, const FNiagaraSystemInstanceID& Instance)
{
	FNDIFieldRenderData* SourceData = static_cast<FNDIFieldRenderData*>(PerInstanceData);
	FNDIFieldRenderData* TargetData = &(SystemInstancesToProxyData.FindOrAdd(Instance));

	ensure(TargetData);
	if (TargetData && SourceData && SourceData->FieldResource)
	{
		TargetData->FieldResource = SourceData->FieldResource;
	}
	SourceData->~FNDIFieldRenderData();
}

void FNDIPhysicsFieldProxy::InitializePerInstanceData(const FNiagaraSystemInstanceID& SystemInstance)
{
	check(IsInRenderingThread());
	SystemInstancesToProxyData.Add(SystemInstance);
}

void FNDIPhysicsFieldProxy::DestroyPerInstanceData(const FNiagaraSystemInstanceID& SystemInstance)
{
	check(IsInRenderingThread());
	SystemInstancesToProxyData.Remove(SystemInstance);
}

//------------------------------------------------------------------------------------------------------------

UNiagaraDataInterfacePhysicsField::UNiagaraDataInterfacePhysicsField()
{
	Proxy.Reset(new FNDIPhysicsFieldProxy());
}

bool UNiagaraDataInterfacePhysicsField::InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	FNDIPhysicsFieldData* InstanceData = new (PerInstanceData) FNDIPhysicsFieldData();

	check(InstanceData);
	if (SystemInstance)
	{
		InstanceData->Init(SystemInstance);
	}

	return true;
}

void UNiagaraDataInterfacePhysicsField::DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	FNDIPhysicsFieldData* InstanceData = static_cast<FNDIPhysicsFieldData*>(PerInstanceData);

	InstanceData->Release();
	InstanceData->~FNDIPhysicsFieldData();

	FNDIPhysicsFieldProxy* ThisProxy = GetProxyAs<FNDIPhysicsFieldProxy>();
	ENQUEUE_RENDER_COMMAND(FNiagaraDIDestroyInstanceData) (
		[ThisProxy, InstanceID = SystemInstance->GetId()](FRHICommandListImmediate& CmdList)
	{
		ThisProxy->SystemInstancesToProxyData.Remove(InstanceID);
	}
	);
}

bool UNiagaraDataInterfacePhysicsField::PerInstanceTickPostSimulate(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float InDeltaSeconds)
{
	FNDIPhysicsFieldData* InstanceData = static_cast<FNDIPhysicsFieldData*>(PerInstanceData);
	if (InstanceData && SystemInstance)
	{
		InstanceData->Update(SystemInstance);
	}
	return false;
}

bool UNiagaraDataInterfacePhysicsField::CopyToInternal(UNiagaraDataInterface* Destination) const
{
	if (!Super::CopyToInternal(Destination))
	{
		return false;
	}

	return true;
}

bool UNiagaraDataInterfacePhysicsField::Equals(const UNiagaraDataInterface* Other) const
{
	if (!Super::Equals(Other))
	{
		return false;
	}
	return true;
}

void UNiagaraDataInterfacePhysicsField::PostInitProperties()
{
	Super::PostInitProperties();

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		ENiagaraTypeRegistryFlags DIFlags =
			ENiagaraTypeRegistryFlags::AllowAnyVariable |
			ENiagaraTypeRegistryFlags::AllowParameter;
		FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(GetClass()), DIFlags);

		ENiagaraTypeRegistryFlags FieldFlags =
			ENiagaraTypeRegistryFlags::AllowAnyVariable |
			ENiagaraTypeRegistryFlags::AllowParameter;
		
		FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(StaticEnum<EFieldVectorType>()), FieldFlags);
		FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(StaticEnum<EFieldScalarType>()), FieldFlags);
		FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(StaticEnum<EFieldIntegerType>()), FieldFlags);
	}
}

void UNiagaraDataInterfacePhysicsField::GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)
{
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = SamplePhysicsVectorFieldName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Physics Field")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetPositionDef(), TEXT("World Position")));
		Sig.Inputs.Add(FNiagaraVariable(StaticEnum<EFieldVectorType>(), TEXT("Target Type")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Vector Value")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = SamplePhysicsScalarFieldName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Physics Field")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetPositionDef(), TEXT("World Position")));
		Sig.Inputs.Add(FNiagaraVariable(StaticEnum<EFieldScalarType>(), TEXT("Target Type")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Scalar Value")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = SamplePhysicsIntegerFieldName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Physics Field")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetPositionDef(), TEXT("World Position")));
		Sig.Inputs.Add(FNiagaraVariable(StaticEnum<EFieldIntegerType>(), TEXT("Target Type")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Integer Value")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetPhysicsFieldResolutionName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Physics Field")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Field Resolution")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetPhysicsFieldBoundsName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Physics Field")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Min Bounds"))); //TODO (LWC) not sure what to do with these bounds, should they be converted as well?
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Max Bounds")));

		OutFunctions.Add(Sig);
	}
}

DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfacePhysicsField, SamplePhysicsVectorField);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfacePhysicsField, SamplePhysicsScalarField);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfacePhysicsField, SamplePhysicsIntegerField);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfacePhysicsField, GetPhysicsFieldResolution);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfacePhysicsField, GetPhysicsFieldBounds);

void UNiagaraDataInterfacePhysicsField::GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction& OutFunc)
{
	if (BindingInfo.Name == SamplePhysicsVectorFieldName)
	{
		check(BindingInfo.GetNumInputs() == 5 && BindingInfo.GetNumOutputs() == 3);
		NDI_FUNC_BINDER(UNiagaraDataInterfacePhysicsField, SamplePhysicsVectorField)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == SamplePhysicsScalarFieldName)
	{
		check(BindingInfo.GetNumInputs() == 5 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfacePhysicsField, SamplePhysicsScalarField)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == SamplePhysicsIntegerFieldName)
	{
		check(BindingInfo.GetNumInputs() == 5 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfacePhysicsField, SamplePhysicsIntegerField)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == GetPhysicsFieldResolutionName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 3);
		NDI_FUNC_BINDER(UNiagaraDataInterfacePhysicsField, GetPhysicsFieldResolution)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == GetPhysicsFieldBoundsName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 6);
		NDI_FUNC_BINDER(UNiagaraDataInterfacePhysicsField, GetPhysicsFieldBounds)::Bind(this, OutFunc);
	}
}

void UNiagaraDataInterfacePhysicsField::GetPhysicsFieldResolution(FVectorVMExternalFunctionContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIPhysicsFieldData> InstData(Context);

	VectorVM::FExternalFuncRegisterHandler<float> OutDimensionX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutDimensionY(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutDimensionZ(Context);

	const FIntVector FieldDimension = (InstData && InstData->FieldResource) ? FIntVector(InstData->FieldResource->FieldInfos.ClipmapResolution, 
		InstData->FieldResource->FieldInfos.ClipmapResolution, InstData->FieldResource->FieldInfos.ClipmapResolution) : FIntVector(1, 1, 1);

	for (int32 i = 0; i < Context.GetNumInstances(); ++i)
	{
		*OutDimensionX.GetDest() = FieldDimension.X;
		*OutDimensionY.GetDest() = FieldDimension.Y;
		*OutDimensionZ.GetDest() = FieldDimension.Z;

		OutDimensionX.Advance();
		OutDimensionY.Advance();
		OutDimensionZ.Advance();
	}
}

void UNiagaraDataInterfacePhysicsField::GetPhysicsFieldBounds(FVectorVMExternalFunctionContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIPhysicsFieldData> InstData(Context);

	VectorVM::FExternalFuncRegisterHandler<float> OutMinX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutMinY(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutMinZ(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutMaxX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutMaxY(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutMaxZ(Context);

	const FVector MinBound = (InstData && InstData->FieldResource) ? InstData->FieldResource->FieldInfos.ClipmapCenter -
		FVector(InstData->FieldResource->FieldInfos.ClipmapDistance) : FVector(0, 0, 0);
	const FVector MaxBound = (InstData && InstData->FieldResource) ? InstData->FieldResource->FieldInfos.ClipmapCenter +
		FVector(InstData->FieldResource->FieldInfos.ClipmapDistance) : FVector(0, 0, 0);

	for (int32 i = 0; i < Context.GetNumInstances(); ++i)
	{
		*OutMinX.GetDest() = MinBound.X;
		*OutMinY.GetDest() = MinBound.Y;
		*OutMinZ.GetDest() = MinBound.Z;
		*OutMaxX.GetDest() = MaxBound.X;
		*OutMaxY.GetDest() = MaxBound.Y;
		*OutMaxZ.GetDest() = MaxBound.Z;

		OutMinX.Advance();
		OutMinY.Advance();
		OutMinZ.Advance();
		OutMaxX.Advance();
		OutMaxY.Advance();
		OutMaxZ.Advance();
	}
}

struct FVectorFieldOperator
{
	static void BlendValues(const FVector& VectorValueA, const FVector& VectorValueB, FVector& VectorValueC)
	{
		VectorValueC = VectorValueA + VectorValueB;
	}
};

struct FScalarFieldOperator
{
	static void BlendValues(const float& VectorValueA, const float& VectorValueB, float& VectorValueC)
	{
		VectorValueC = VectorValueA + VectorValueB;
	}
};

struct FIntegerFieldOperator
{
	static void BlendValues(const int32& VectorValueA, const int32& VectorValueB, int32& VectorValueC)
	{
		VectorValueC = VectorValueA + VectorValueB;
	}
};

template<typename DataType, typename BlendOperator>
void EvaluateFieldNodes(TArray<FFieldSystemCommand>& FieldCommands, const EFieldPhysicsType FieldType, FFieldContext& FieldContext, 
	TArray<DataType>& ResultsArray, TArray<DataType>& MaxArray)
{
	bool HasMatchingCommand = false;
	if (FieldCommands.Num() > 0 && ResultsArray.Num() == MaxArray.Num())
	{
		SCOPE_CYCLE_COUNTER(STAT_NiagaraUpdateField_Object);
		TFieldArrayView<DataType> ResultsView(ResultsArray, 0, ResultsArray.Num());

		const float TimeSeconds = FieldContext.TimeSeconds;
		for (int32 CommandIndex = 0; CommandIndex < FieldCommands.Num(); ++CommandIndex)
		{
			const FName AttributeName = FieldCommands[CommandIndex].TargetAttribute;
			FieldContext.TimeSeconds = TimeSeconds - FieldCommands[CommandIndex].TimeCreation;

			const EFieldPhysicsType CommandType = GetFieldPhysicsType(AttributeName);
			if (CommandType == FieldType && FieldCommands[CommandIndex].RootNode.Get())
			{
				FFieldNode<DataType>* RootNode = static_cast<FFieldNode<DataType>*>(
					FieldCommands[CommandIndex].RootNode.Get());

				RootNode->Evaluate(FieldContext, ResultsView);
				HasMatchingCommand = true;

				for (int32 InstanceIdx = 0; InstanceIdx < MaxArray.Num(); ++InstanceIdx)
				{
					// TODO : First version with the add. will probably have to include an operator as a template argument 
					BlendOperator::BlendValues(MaxArray[InstanceIdx], ResultsArray[InstanceIdx], MaxArray[InstanceIdx]);
				} 
			}
		}
	}
	if (!HasMatchingCommand)
	{
		MaxArray.Init(DataType(0), ResultsArray.Num());
	}
}

void UNiagaraDataInterfacePhysicsField::SamplePhysicsVectorField(FVectorVMExternalFunctionContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIPhysicsFieldData> InstData(Context);

	// Inputs 
	FNDIInputParam<FNiagaraPosition> SamplePositionParam(Context);
	FNDIInputParam<EFieldVectorType> VectorTargetParam(Context);

	// Outputs...
	FNDIOutputParam<FVector3f> OutVectorFieldParam(Context);

	if (InstData)
	{
		FFieldExecutionDatas ExecutionDatas;
		ExecutionDatas.SamplePositions.Init(FVector(0, 0, 0), Context.GetNumInstances());

		EFieldVectorType VectorTarget = Vector_TargetMax;

		for (int32 InstanceIdx = 0; InstanceIdx < Context.GetNumInstances(); ++InstanceIdx)
		{
			ExecutionDatas.SamplePositions[InstanceIdx] = InstData->LWCConverter.ConvertSimulationPositionToWorld(SamplePositionParam.GetAndAdvance());
			VectorTarget = VectorTargetParam.GetAndAdvance();
		}
		FFieldContextIndex::ContiguousIndices(ExecutionDatas.SampleIndices, Context.GetNumInstances());

		TArray<FVector>& SampleResults = ExecutionDatas.VectorResults[(uint8)EFieldCommandResultType::FinalResult];
		SampleResults.Init(FVector::ZeroVector, Context.GetNumInstances());

		TArray<FVector> SampleMax;
		SampleMax.Init(FVector::ZeroVector, Context.GetNumInstances());

		FFieldContext FieldContext{
			ExecutionDatas,
			FFieldContext::UniquePointerMap(),
			InstData->TimeSeconds
		};

		const EFieldPhysicsType PhysicsType = GetFieldTargetTypes(Field_Output_Vector)[VectorTarget];
		EvaluateFieldNodes<FVector, FVectorFieldOperator>(InstData->FieldCommands, PhysicsType, FieldContext, SampleResults, SampleMax);

		for (int32 InstanceIdx = 0; InstanceIdx < Context.GetNumInstances(); ++InstanceIdx)
		{
			OutVectorFieldParam.SetAndAdvance(FVector3f(SampleMax[InstanceIdx]));
		}
	}
	else
	{
		for (int32 InstanceIdx = 0; InstanceIdx < Context.GetNumInstances(); ++InstanceIdx)
		{
			OutVectorFieldParam.SetAndAdvance(FVector3f::ZeroVector);
		}
	}
}

void UNiagaraDataInterfacePhysicsField::SamplePhysicsIntegerField(FVectorVMExternalFunctionContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIPhysicsFieldData> InstData(Context);

	// Inputs 
	FNDIInputParam<FNiagaraPosition> SamplePositionParam(Context);
	FNDIInputParam<EFieldIntegerType> IntegerTargetParam(Context);

	// Outputs...
	FNDIOutputParam<int32> OutIntegerFieldParam(Context);

	if (InstData)
	{
		FFieldExecutionDatas ExecutionDatas;
		ExecutionDatas.SamplePositions.Init(FVector(0, 0, 0), Context.GetNumInstances());

		EFieldIntegerType IntegerTarget = Integer_TargetMax;

		for (int32 InstanceIdx = 0; InstanceIdx < Context.GetNumInstances(); ++InstanceIdx)
		{
			ExecutionDatas.SamplePositions[InstanceIdx] = InstData->LWCConverter.ConvertSimulationPositionToWorld(SamplePositionParam.GetAndAdvance());
			IntegerTarget = IntegerTargetParam.GetAndAdvance();
		}
		FFieldContextIndex::ContiguousIndices(ExecutionDatas.SampleIndices, Context.GetNumInstances());

		TArray<int32>& SampleResults = ExecutionDatas.IntegerResults[(uint8)EFieldCommandResultType::FinalResult];
		SampleResults.Init(0, Context.GetNumInstances());

		TArray<int32> SampleMax;
		SampleMax.Init(0, Context.GetNumInstances());

		FFieldContext FieldContext{
			ExecutionDatas,
			FFieldContext::UniquePointerMap(),
			InstData->TimeSeconds
		};

		const EFieldPhysicsType PhysicsType = GetFieldTargetTypes(Field_Output_Integer)[IntegerTarget];
		EvaluateFieldNodes<int32, FIntegerFieldOperator>(InstData->FieldCommands, PhysicsType, FieldContext, SampleResults, SampleMax);

		for (int32 InstanceIdx = 0; InstanceIdx < Context.GetNumInstances(); ++InstanceIdx)
		{
			OutIntegerFieldParam.SetAndAdvance(SampleMax[InstanceIdx]);
		}
	}
	else
	{
		for (int32 InstanceIdx = 0; InstanceIdx < Context.GetNumInstances(); ++InstanceIdx)
		{
			OutIntegerFieldParam.SetAndAdvance(0.0);
		}
	}
}


void UNiagaraDataInterfacePhysicsField::SamplePhysicsScalarField(FVectorVMExternalFunctionContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIPhysicsFieldData> InstData(Context);

	// Inputs 
	FNDIInputParam<FNiagaraPosition> SamplePositionParam(Context);
	FNDIInputParam<EFieldScalarType> ScalarTargetParam(Context);

	// Outputs...
	FNDIOutputParam<float> OutScalarFieldParam(Context);

	if (InstData)
	{
		FFieldExecutionDatas ExecutionDatas;
		ExecutionDatas.SamplePositions.Init(FVector(0, 0, 0), Context.GetNumInstances());

		EFieldScalarType ScalarTarget = Scalar_TargetMax;

		for (int32 InstanceIdx = 0; InstanceIdx < Context.GetNumInstances(); ++InstanceIdx)
		{
			ExecutionDatas.SamplePositions[InstanceIdx] = InstData->LWCConverter.ConvertSimulationPositionToWorld(SamplePositionParam.GetAndAdvance());
			ScalarTarget = ScalarTargetParam.GetAndAdvance();
		}
		FFieldContextIndex::ContiguousIndices(ExecutionDatas.SampleIndices, Context.GetNumInstances());

		TArray<float>& SampleResults = ExecutionDatas.ScalarResults[(uint8)EFieldCommandResultType::FinalResult];
		SampleResults.Init(0.0, Context.GetNumInstances());

		TArray<float> SampleMax;
		SampleMax.Init(0, Context.GetNumInstances());

		FFieldContext FieldContext{
			ExecutionDatas,
			FFieldContext::UniquePointerMap(),
			InstData->TimeSeconds
		};

		const EFieldPhysicsType PhysicsType = GetFieldTargetTypes(Field_Output_Scalar)[ScalarTarget];
		EvaluateFieldNodes<float, FScalarFieldOperator>(InstData->FieldCommands, PhysicsType, FieldContext, SampleResults, SampleMax);

		for (int32 InstanceIdx = 0; InstanceIdx < Context.GetNumInstances(); ++InstanceIdx)
		{
			OutScalarFieldParam.SetAndAdvance(SampleMax[InstanceIdx]);
		}
	}
	else
	{
		for (int32 InstanceIdx = 0; InstanceIdx < Context.GetNumInstances(); ++InstanceIdx)
		{
			OutScalarFieldParam.SetAndAdvance(0.0);
		}
	}
}

#if WITH_EDITORONLY_DATA
bool UNiagaraDataInterfacePhysicsField::GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL)
{
	FNDIPhysicsFieldParametersName ParamNames(ParamInfo.DataInterfaceHLSLSymbol);

	TMap<FString, FStringFormatArg> ArgsSample = {
		{TEXT("InstanceFunctionName"), FunctionInfo.InstanceName},
		{TEXT("PhysicsFieldContextName"), TEXT("DIPhysicsField_MAKE_CONTEXT(") + ParamInfo.DataInterfaceHLSLSymbol + TEXT(")")},
	};

	if (FunctionInfo.DefinitionName == SamplePhysicsVectorFieldName)
	{
		static const TCHAR* FormatSample = TEXT(R"(
		void {InstanceFunctionName}(in float3 WorldPosition, in int TargetIndex, out float3 OutFieldVector)
		{
			{PhysicsFieldContextName}
			OutFieldVector = DIPhysicsField_SamplePhysicsVectorField(DIContext,WorldPosition,TargetIndex);
		}
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == SamplePhysicsScalarFieldName)
	{
		static const TCHAR* FormatSample = TEXT(R"(
		void {InstanceFunctionName}(in float3 WorldPosition, in int TargetIndex, out float OutFieldScalar)
		{
			{PhysicsFieldContextName}
			OutFieldScalar = DIPhysicsField_SamplePhysicsScalarField(DIContext,WorldPosition,TargetIndex);
		}
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == SamplePhysicsIntegerFieldName)
	{
		static const TCHAR* FormatSample = TEXT(R"(
		void {InstanceFunctionName}(in float3 WorldPosition, in int TargetIndex, out int OutFieldInteger)
		{
			{PhysicsFieldContextName}
			OutFieldInteger = DIPhysicsField_SamplePhysicsIntegerField(DIContext,WorldPosition,TargetIndex);
		}
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == GetPhysicsFieldResolutionName)
	{
		static const TCHAR* FormatSample = TEXT(R"(
		void {InstanceFunctionName}(out float3 OutTextureSize)
		{
			{PhysicsFieldContextName}
			OutTextureSize = DIContext.ClipmapResolution;
		}
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == GetPhysicsFieldBoundsName)
	{
		static const TCHAR* FormatSample = TEXT(R"(
		void {InstanceFunctionName}(out float3 OutMinBounds, out float3 OutMaxBounds)
		{
			{PhysicsFieldContextName}
			OutMinBounds = DIContext.ClipmapCenter - DIContext.ClipmapDistance;
			OutMaxBounds = DIContext.ClipmapCenter + DIContext.ClipmapDistance;
		}
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}

	OutHLSL += TEXT("\n");
	return false;
}

bool UNiagaraDataInterfacePhysicsField::UpgradeFunctionCall(FNiagaraFunctionSignature& FunctionSignature)
{
	bool bChanged = false;
	
	// upgrade from lwc changes, only parameter types changed there
	if (FunctionSignature.FunctionVersion < FNiagaraPhysicsFieldDIFunctionVersion::LargeWorldCoordinates)
	{
		if (FunctionSignature.Name == SamplePhysicsVectorFieldName && ensure(FunctionSignature.Inputs.Num() == 3))
		{
			FunctionSignature.Inputs[1].SetType(FNiagaraTypeDefinition::GetPositionDef());
			bChanged = true;
		}
		if (FunctionSignature.Name == SamplePhysicsScalarFieldName && ensure(FunctionSignature.Inputs.Num() == 3))
		{
			FunctionSignature.Inputs[1].SetType(FNiagaraTypeDefinition::GetPositionDef());
			bChanged = true;
		}
		if (FunctionSignature.Name == SamplePhysicsIntegerFieldName && ensure(FunctionSignature.Inputs.Num() == 3))
		{
			FunctionSignature.Inputs[1].SetType(FNiagaraTypeDefinition::GetPositionDef());
			bChanged = true;
		}
	}
	FunctionSignature.FunctionVersion = FNiagaraPhysicsFieldDIFunctionVersion::LatestVersion;

	return bChanged;
}

void UNiagaraDataInterfacePhysicsField::GetCommonHLSL(FString& OutHLSL)
{
	OutHLSL += TEXT("#include \"/Plugin/Experimental/ChaosNiagara/NiagaraDataInterfacePhysicsField.ush\"\n");
}

void UNiagaraDataInterfacePhysicsField::GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL)
{
	OutHLSL += TEXT("DIPhysicsField_DECLARE_CONSTANTS(") + ParamInfo.DataInterfaceHLSLSymbol + TEXT(")\n");
}
#endif

void UNiagaraDataInterfacePhysicsField::ProvidePerInstanceDataForRenderThread(void* DataForRenderThread, void* PerInstanceData, const FNiagaraSystemInstanceID& SystemInstance)
{
	FNDIPhysicsFieldData* GameThreadData = static_cast<FNDIPhysicsFieldData*>(PerInstanceData);
	FNDIFieldRenderData* RenderThreadData = static_cast<FNDIFieldRenderData*>(DataForRenderThread);

	if (GameThreadData != nullptr && RenderThreadData != nullptr)
	{
		RenderThreadData->FieldResource = GameThreadData->FieldResource;
	}
	check(Proxy);
}

#undef LOCTEXT_NAMESPACE