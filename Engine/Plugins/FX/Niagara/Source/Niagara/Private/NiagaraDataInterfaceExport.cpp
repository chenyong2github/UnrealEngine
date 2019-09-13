// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "NiagaraDataInterfaceExport.h"
#include "NiagaraTypes.h"
#include "NiagaraCustomVersion.h"
#include "Internationalization/Internationalization.h"
#include "NiagaraSystemInstance.h"

const FName UNiagaraDataInterfaceExport::StoreDataName(TEXT("StoreParticleData"));

UNiagaraDataInterfaceExport::UNiagaraDataInterfaceExport(FObjectInitializer const& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UNiagaraDataInterfaceExport::PostInitProperties()
{
	Super::PostInitProperties();

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(GetClass()), true, false, false);
	}
}

bool UNiagaraDataInterfaceExport::InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	ExportInterface_InstanceData* PIData = new (PerInstanceData) ExportInterface_InstanceData;
	return true;
}

bool UNiagaraDataInterfaceExport::PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds)
{
	ExportInterface_InstanceData* PIData = (ExportInterface_InstanceData*)PerInstanceData;
	if (!PIData)
	{
		return true;
	}

	if (CallbackHandlerParameter.Parameter.IsValid() && SystemInstance)
	{
		PIData->CallbackHandler = PIData->UserParamBinding.Init(SystemInstance->GetInstanceParameters(), CallbackHandlerParameter.Parameter);
	}
	else
	{
		PIData->CallbackHandler = nullptr;
	}
	return false;
}

bool UNiagaraDataInterfaceExport::PerInstanceTickPostSimulate(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds)
{
	ExportInterface_InstanceData* PIData = (ExportInterface_InstanceData*) PerInstanceData;
	if (!PIData->GatheredData.IsEmpty() && PIData->CallbackHandler && PIData->CallbackHandler->GetClass()->ImplementsInterface(UNiagaraParticleCallbackHandler::StaticClass()))
	{
		//Drain the queue into an array here
		TArray<FBasicParticleData> Data;
		FBasicParticleData Value;
		while (PIData->GatheredData.Dequeue(Value))
		{
			Data.Add(Value);
		}
		INiagaraParticleCallbackHandler::Execute_ReceiveParticleData(PIData->CallbackHandler, Data, SystemInstance->GetSystem());
	}
	return false;
}

bool UNiagaraDataInterfaceExport::Equals(const UNiagaraDataInterface* Other) const
{
	if (!Super::Equals(Other))
	{
		return false;
	}

	return CastChecked<UNiagaraDataInterfaceExport>(Other)->CallbackHandlerParameter == CallbackHandlerParameter;
}

void UNiagaraDataInterfaceExport::GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)
{
	FNiagaraFunctionSignature Sig;
	Sig.Name = StoreDataName;
#if WITH_EDITORONLY_DATA
	Sig.Description = NSLOCTEXT("Niagara", "ExportDataFunctionDescription", "This function takes the particle data and stores it to be exported to the registered callback handler after the simulation has ticked.");
#endif
	Sig.bMemberFunction = true;
	Sig.bRequiresContext = false;
	Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Export interface")));
	Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Store Data")));
	Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Position")));
	Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Size")));
	Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Velocity")));
	Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Success")));
	OutFunctions.Add(Sig);
}

bool UNiagaraDataInterfaceExport::GetFunctionHLSL(const FName& DefinitionFunctionName, FString InstanceFunctionName, FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL)
{
	return false;
}

DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceExport, StoreData);
void UNiagaraDataInterfaceExport::GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc)
{
	if (BindingInfo.Name == StoreDataName)
	{
		NDI_FUNC_BINDER(UNiagaraDataInterfaceExport, StoreData)::Bind(this, OutFunc);
	}
	else
	{
		UE_LOG(LogNiagara, Error, TEXT("Could not find data interface external function. Expected Name: %s  Actual Name: %s"), *StoreDataName.ToString(), *BindingInfo.Name.ToString());
	}
}

void UNiagaraDataInterfaceExport::StoreData(FVectorVMContext& Context)
{
	VectorVM::FExternalFuncInputHandler<FNiagaraBool> StoreDataParam(Context);

	VectorVM::FExternalFuncInputHandler<float> PositionParamX(Context);
	VectorVM::FExternalFuncInputHandler<float> PositionParamY(Context);
	VectorVM::FExternalFuncInputHandler<float> PositionParamZ(Context);

	VectorVM::FExternalFuncInputHandler<float> SizeParam(Context);
	
	VectorVM::FExternalFuncInputHandler<float> VelocityParamX(Context);
	VectorVM::FExternalFuncInputHandler<float> VelocityParamY(Context);
	VectorVM::FExternalFuncInputHandler<float> VelocityParamZ(Context);

	VectorVM::FUserPtrHandler<ExportInterface_InstanceData> InstData(Context);

	VectorVM::FExternalFuncRegisterHandler<FNiagaraBool> OutSample(Context);

	checkfSlow(InstData.Get(), TEXT("Export data interface has invalid instance data. %s"), *GetPathName());
	bool ValidHandlerData = InstData->UserParamBinding.BoundVariable.IsValid() && InstData->CallbackHandler;

	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		FNiagaraBool ShouldStore = StoreDataParam.GetAndAdvance();
		FBasicParticleData Data;
		Data.Position = FVector(PositionParamX.GetAndAdvance(), PositionParamY.GetAndAdvance(), PositionParamZ.GetAndAdvance());
		Data.Size = SizeParam.GetAndAdvance();
		Data.Position = FVector(VelocityParamX.GetAndAdvance(), VelocityParamY.GetAndAdvance(), VelocityParamZ.GetAndAdvance());

		FNiagaraBool Valid;
		if (ValidHandlerData && ShouldStore)
		{
			Valid.SetValue(InstData->GatheredData.Enqueue(Data));
		}
		*OutSample.GetDestAndAdvance() = Valid;
	}
}

bool UNiagaraDataInterfaceExport::CopyToInternal(UNiagaraDataInterface* Destination) const
{
	if (!Super::CopyToInternal(Destination))
	{
		return false;
	}

	UNiagaraDataInterfaceExport* OtherTyped = CastChecked<UNiagaraDataInterfaceExport>(Destination);
	OtherTyped->CallbackHandlerParameter = CallbackHandlerParameter;
	return true;
}
