// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "NiagaraDataInterfaceParticleRead.h"
#include "NiagaraConstants.h"
#include "NiagaraSystemInstance.h"

#define LOCTEXT_NAMESPACE "NiagaraDataInterfaceParticleRead"

static const FName GetIntAttributeFunctionName("Get int Attribute");
static const FName GetFloatAttributeFunctionName("Get float Attribute");
static const FName GetVec2AttributeFunctionName("Get Vector2 Attribute");
static const FName GetVec3AttributeFunctionName("Get Vector3 Attribute");
static const FName GetVec4AttributeFunctionName("Get Vector4 Attribute");
static const FName GetBoolAttributeFunctionName("Get bool Attribute");
static const FName GetColorAttributeFunctionName("Get Color Attribute");
static const FName GetQuatAttributeFunctionName("Get Quaternion Attribute");

UNiagaraDataInterfaceParticleRead::UNiagaraDataInterfaceParticleRead(FObjectInitializer const& ObjectInitializer)
	: Super(ObjectInitializer)
{
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

void UNiagaraDataInterfaceParticleRead::PreEditChange(UProperty* PropertyAboutToChange)
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
	return PIData->EmitterInstance != nullptr;
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
	const FName* AttributeToRead = BindingInfo.Specifiers.Find(FName("Attribute"));

	if (!AttributeToRead)
	{
		UE_LOG(LogNiagara, Error, TEXT("VMExternalFunction '%s' does not have a function specifier 'attribute'!"), *BindingInfo.Name.ToString());
		return;
	}

	if (BindingInfo.Name == GetFloatAttributeFunctionName)
	{
		if (AttributeToRead)
		{
			FNiagaraVariable VariableToRead(FNiagaraTypeDefinition::GetFloatDef(), *AttributeToRead);
			FName Attr = *AttributeToRead;
			if (PIData->EmitterInstance->GetData().GetVariables().Find(VariableToRead) != INDEX_NONE)
			{
				NDI_FUNC_BINDER(UNiagaraDataInterfaceParticleRead, ReadFloat)::Bind(this, OutFunc, Attr);
				bBindSuccessful = true;
			}
		}
	}
	else if (BindingInfo.Name == GetVec2AttributeFunctionName)
	{
		if (AttributeToRead)
		{
			FNiagaraVariable VariableToRead(FNiagaraTypeDefinition::GetVec2Def(), *AttributeToRead);
			FName Attr = *AttributeToRead;
			if (PIData->EmitterInstance->GetData().GetVariables().Find(VariableToRead) != INDEX_NONE)
			{
				NDI_FUNC_BINDER(UNiagaraDataInterfaceParticleRead, ReadVector2)::Bind(this, OutFunc, Attr);
				bBindSuccessful = true;
			}
		}
	}
	else if (BindingInfo.Name == GetVec3AttributeFunctionName)
	{
		if (AttributeToRead)
		{
			FNiagaraVariable VariableToRead(FNiagaraTypeDefinition::GetVec3Def(), *AttributeToRead);
			FName Attr = *AttributeToRead;
			if (PIData->EmitterInstance->GetData().GetVariables().Find(VariableToRead) != INDEX_NONE)
			{
				NDI_FUNC_BINDER(UNiagaraDataInterfaceParticleRead, ReadVector3)::Bind(this, OutFunc, Attr);
				bBindSuccessful = true;
			}
		}
	}
	else if (BindingInfo.Name == GetVec4AttributeFunctionName)
	{
		if (AttributeToRead)
		{
			FNiagaraVariable VariableToRead(FNiagaraTypeDefinition::GetVec4Def(), *AttributeToRead);
			FName Attr = *AttributeToRead;
			if (PIData->EmitterInstance->GetData().GetVariables().Find(VariableToRead) != INDEX_NONE)
			{
				NDI_FUNC_BINDER(UNiagaraDataInterfaceParticleRead, ReadVector4)::Bind(this, OutFunc, Attr);
				bBindSuccessful = true;
			}
		}
	}
	else if (BindingInfo.Name == GetIntAttributeFunctionName)
	{
		if (AttributeToRead)
		{
			FNiagaraVariable VariableToRead(FNiagaraTypeDefinition::GetIntDef(), *AttributeToRead);
			FName Attr = *AttributeToRead;
			if (PIData->EmitterInstance->GetData().GetVariables().Find(VariableToRead) != INDEX_NONE)
			{
				NDI_FUNC_BINDER(UNiagaraDataInterfaceParticleRead, ReadInt)::Bind(this, OutFunc, Attr);
				bBindSuccessful = true;
			}
		}
	}
	else if (BindingInfo.Name == GetBoolAttributeFunctionName)
	{
		if (AttributeToRead)
		{
			FNiagaraVariable VariableToRead(FNiagaraTypeDefinition::GetBoolDef(), *AttributeToRead);
			FName Attr = *AttributeToRead;
			if (PIData->EmitterInstance->GetData().GetVariables().Find(VariableToRead) != INDEX_NONE)
			{
				NDI_FUNC_BINDER(UNiagaraDataInterfaceParticleRead, ReadBool)::Bind(this, OutFunc, Attr);
				bBindSuccessful = true;
			}
		}
	}
	else if (BindingInfo.Name == GetColorAttributeFunctionName)
	{
		if (AttributeToRead)
		{
			FNiagaraVariable VariableToRead(FNiagaraTypeDefinition::GetColorDef(), *AttributeToRead);
			FName Attr = *AttributeToRead;
			if (PIData->EmitterInstance->GetData().GetVariables().Find(VariableToRead) != INDEX_NONE)
			{
				NDI_FUNC_BINDER(UNiagaraDataInterfaceParticleRead, ReadColor)::Bind(this, OutFunc, Attr);
				bBindSuccessful = true;
			}
		}
	}
	else if (BindingInfo.Name == GetQuatAttributeFunctionName)
	{
		if (AttributeToRead)
		{
			FNiagaraVariable VariableToRead(FNiagaraTypeDefinition::GetQuatDef(), *AttributeToRead);
			FName Attr = *AttributeToRead;
			if (PIData->EmitterInstance->GetData().GetVariables().Find(VariableToRead) != INDEX_NONE)
			{
				NDI_FUNC_BINDER(UNiagaraDataInterfaceParticleRead, ReadQuat)::Bind(this, OutFunc, Attr);
				bBindSuccessful = true;
			}
		}
	}


	if (!bBindSuccessful)
	{
		UE_LOG(LogNiagara, Error, TEXT("Failed to bind VMExternalFunction '%s' with attribute '%s'! Check that the attribute is named correctly."), *BindingInfo.Name.ToString(), *(*AttributeToRead).ToString());
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
	return false;
}

bool UNiagaraDataInterfaceParticleRead::GetFunctionHLSL(const FName& DefinitionFunctionName, FString InstanceFunctionName, FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL)
{
	return false;
}

void UNiagaraDataInterfaceParticleRead::GetParameterDefinitionHLSL(FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL)
{

}

FNiagaraDataInterfaceParametersCS* UNiagaraDataInterfaceParticleRead::ConstructComputeParameters() const
{
	return nullptr;
}

#undef LOCTEXT_NAMESPACE