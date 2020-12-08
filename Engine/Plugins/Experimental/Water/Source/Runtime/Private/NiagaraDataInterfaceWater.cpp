// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraDataInterfaceWater.h"
#include "WaterBodyActor.h"
#include "WaterSplineComponent.h"
#include "WaterModule.h"

#define LOCTEXT_NAMESPACE "NiagaraDataInterfaceWater"


namespace WaterFunctionNames
{
	const FName GetWaterDataAtPointName(TEXT("GetWaterDataAtPoint"));

	const FName GetWaveParamLookupTableName(TEXT("GetWaveParamLookupTableOffset"));
}

void UNiagaraDataInterfaceWater::PostInitProperties()
{
	Super::PostInitProperties();

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		ENiagaraTypeRegistryFlags Flags = ENiagaraTypeRegistryFlags::AllowAnyVariable | ENiagaraTypeRegistryFlags::AllowParameter;
		FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(GetClass()), Flags);
	}
}

void UNiagaraDataInterfaceWater::GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)
{
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = WaterFunctionNames::GetWaterDataAtPointName;

		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Water")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("WorldPosition")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Time")));

		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("WaveHeight")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Depth")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Velocity")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("SurfacePosition")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("SurfaceNormal")));

		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		//Sig.Owner = *GetName();
		Sig.bExperimental = true;
		Sig.SetDescription(LOCTEXT("DataInterfaceWater_GetWaterDataAtPoint", "Get the water data at the provided world position and time"));
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = WaterFunctionNames::GetWaveParamLookupTableName;

		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Water")));

		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Offset")));

		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		//Sig.Owner = *GetName();
		Sig.bExperimental = true;
		Sig.SetDescription(LOCTEXT("DataInterfaceWater_GetWaveParamLookupTableOffset", "Get the lookup table offset into the wave data texture for the data interface's water body"));
		OutFunctions.Add(Sig);
	}
}

DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceWater, GetWaterDataAtPoint);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceWater, GetWaveParamLookupTableOffset);

struct FNDIWater_InstanceData
{
	//Cached ptr to actor we sample from. 
	TWeakObjectPtr<AWaterBody> WaterBodyActor;
};

void UNiagaraDataInterfaceWater::GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc)
{
	FNDIWater_InstanceData* InstData = (FNDIWater_InstanceData*)InstanceData;
	if (BindingInfo.Name == WaterFunctionNames::GetWaterDataAtPointName)
	{
		if(BindingInfo.GetNumInputs() == 5 && BindingInfo.GetNumOutputs() == 11)
		{
			NDI_FUNC_BINDER(UNiagaraDataInterfaceWater, GetWaterDataAtPoint)::Bind(this, OutFunc);
		}
	}
	else if (BindingInfo.Name == WaterFunctionNames::GetWaveParamLookupTableName)
	{
		if (BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 1)
		{
			NDI_FUNC_BINDER(UNiagaraDataInterfaceWater, GetWaveParamLookupTableOffset)::Bind(this, OutFunc);
		}
	}

}

bool UNiagaraDataInterfaceWater::Equals(const UNiagaraDataInterface* Other) const
{
	if (!Super::Equals(Other))
	{
		return false;
	}
	const UNiagaraDataInterfaceWater* OtherTyped = CastChecked<const UNiagaraDataInterfaceWater>(Other);
	return OtherTyped->SourceBody == SourceBody;
}

bool UNiagaraDataInterfaceWater::CopyTo(UNiagaraDataInterface* Destination) const
{
	if (!Super::CopyTo(Destination))
	{
		return false;
	}

	UNiagaraDataInterfaceWater* OtherTyped = CastChecked<UNiagaraDataInterfaceWater>(Destination);
	OtherTyped->SourceBody = SourceBody;

	return true;
}

int32 UNiagaraDataInterfaceWater::PerInstanceDataSize() const
{
	return sizeof(FNDIWater_InstanceData);
}

bool UNiagaraDataInterfaceWater::InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	FNDIWater_InstanceData* InstData = new (PerInstanceData) FNDIWater_InstanceData();

	InstData->WaterBodyActor = SourceBody;

	return true;
}

void UNiagaraDataInterfaceWater::DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	FNDIWater_InstanceData* InstData = (FNDIWater_InstanceData*)PerInstanceData;
	InstData->~FNDIWater_InstanceData();
}

bool UNiagaraDataInterfaceWater::PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds)
{
	check(SystemInstance);
	FNDIWater_InstanceData* InstData = (FNDIWater_InstanceData*)PerInstanceData;

	if (InstData->WaterBodyActor != SourceBody)
	{
		InstData->WaterBodyActor = SourceBody;
	}

	return false;
}

void UNiagaraDataInterfaceWater::GetWaterDataAtPoint(FVectorVMContext& Context)
{
	QUICK_SCOPE_CYCLE_COUNTER(NiagaraDataInterfaceWater_GetWaterDataAtPoint);

	VectorVM::FUserPtrHandler<FNDIWater_InstanceData> InstData(Context);

	// Inputs
	VectorVM::FExternalFuncInputHandler<float> WorldX(Context);
	VectorVM::FExternalFuncInputHandler<float> WorldY(Context);
	VectorVM::FExternalFuncInputHandler<float> WorldZ(Context);
	VectorVM::FExternalFuncInputHandler<float> Time(Context);

	// Outputs
	VectorVM::FExternalFuncRegisterHandler<float> OutHeight(Context);

	VectorVM::FExternalFuncRegisterHandler<float> OutDepth(Context);

	VectorVM::FExternalFuncRegisterHandler<float> OutVelocityX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutVelocityY(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutVelocityZ(Context);

	VectorVM::FExternalFuncRegisterHandler<float> OutSurfaceX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutSurfaceY(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutSurfaceZ(Context);

	VectorVM::FExternalFuncRegisterHandler<float> OutSurfaceNormalX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutSurfaceNormalY(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutSurfaceNormalZ(Context);

	AWaterBody* Actor = InstData->WaterBodyActor.Get();
	if (Actor == nullptr)
	{
		UE_LOG(LogWater, Warning, TEXT("NiagaraDataInterfaceWater: GetWaterData called with no water body actor set"));
	}

	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		FWaterBodyQueryResult QueryResult;
		
		bool bIsValid = false;
		if (Actor != nullptr)
		{
			FVector WorldPos(WorldX.Get(), WorldY.Get(), WorldZ.Get());
			QueryResult = Actor->QueryWaterInfoClosestToWorldLocation(WorldPos,
				EWaterBodyQueryFlags::ComputeLocation
				| EWaterBodyQueryFlags::ComputeVelocity
				| EWaterBodyQueryFlags::ComputeNormal
				| EWaterBodyQueryFlags::ComputeDepth
				| EWaterBodyQueryFlags::IncludeWaves);
			bIsValid = !QueryResult.IsInExclusionVolume();
		}

		*OutHeight.GetDestAndAdvance() = bIsValid ? QueryResult.GetWaveInfo().Height : 0.0f;
		*OutDepth.GetDestAndAdvance() = bIsValid ? QueryResult.GetWaterSurfaceDepth() : 0.0f;

		const FVector& Velocity = bIsValid ? QueryResult.GetVelocity() : FVector::ZeroVector;
		*OutVelocityX.GetDestAndAdvance() = Velocity.X;
		*OutVelocityY.GetDestAndAdvance() = Velocity.Y;
		*OutVelocityZ.GetDestAndAdvance() = Velocity.Z;

		// Note we assume X and Y are in water by the time this is queried
		const FVector& AdjustedSurfaceLoc = bIsValid ? QueryResult.GetWaterSurfaceLocation() : FVector::ZeroVector;
		*OutSurfaceX.GetDestAndAdvance() =  AdjustedSurfaceLoc.X;
		*OutSurfaceY.GetDestAndAdvance() =  AdjustedSurfaceLoc.Y;
		*OutSurfaceZ.GetDestAndAdvance() =  AdjustedSurfaceLoc.Z;

		const FVector& Normal = bIsValid ? QueryResult.GetWaterSurfaceNormal() : FVector::UpVector;
		*OutSurfaceNormalX.GetDestAndAdvance() = Normal.X;
		*OutSurfaceNormalY.GetDestAndAdvance() = Normal.Y;
		*OutSurfaceNormalZ.GetDestAndAdvance() = Normal.Z;

		WorldX.Advance();
		WorldY.Advance();
		WorldZ.Advance();
		Time.Advance();
	}
}

void UNiagaraDataInterfaceWater::GetWaveParamLookupTableOffset(FVectorVMContext& Context)
{
	// Inputs
	VectorVM::FUserPtrHandler<FNDIWater_InstanceData> InstData(Context);

	// Outputs
	VectorVM::FExternalFuncRegisterHandler<int> OutLookupTableOffset(Context);
	if (AWaterBody* Actor = InstData->WaterBodyActor.Get())
	{
		for (int32 i = 0; i < Context.NumInstances; ++i)
		{
			*OutLookupTableOffset.GetDestAndAdvance() = Actor->WaterBodyIndex;
		}
	}
	else
	{
		for (int32 i = 0; i < Context.NumInstances; ++i)
		{
			*OutLookupTableOffset.GetDestAndAdvance() = 0;
		}
	}
}

#undef LOCTEXT_NAMESPACE
