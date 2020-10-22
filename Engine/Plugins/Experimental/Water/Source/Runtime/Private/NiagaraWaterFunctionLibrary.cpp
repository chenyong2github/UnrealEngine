// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraWaterFunctionLibrary.h"
#include "NiagaraComponent.h"
#include "WaterBodyActor.h"
#include "NiagaraDataInterfaceWater.h"
#include "WaterModule.h"

void UNiagaraWaterFunctionLibrary::SetWaterBody(UNiagaraComponent* NiagaraSystem, const FString& OverrideName, AWaterBody* WaterBody)
{
	if (!NiagaraSystem)
	{
		UE_LOG(LogWater, Warning, TEXT("NiagaraSystem in \"Set Water Body\" is NULL, OverrideName \"%s\" and WaterBody \"%s\", skipping."), *OverrideName, WaterBody ? *WaterBody->GetName() : TEXT("NULL"));
		return;
	}

	if (!WaterBody)
	{
		UE_LOG(LogWater, Warning, TEXT("WaterBody in \"Set Water Body\" is NULL, OverrideName \"%s\" and NiagaraSystem \"%s\", skipping."), *OverrideName, *NiagaraSystem->GetOwner()->GetName());
		return;
	}


	const FNiagaraParameterStore& OverrideParameters = NiagaraSystem->GetOverrideParameters();

	FNiagaraVariable Variable(FNiagaraTypeDefinition(UNiagaraDataInterfaceWater::StaticClass()), *OverrideName);

	int32 Index = OverrideParameters.IndexOf(Variable);
	if (Index == INDEX_NONE)
	{
		UE_LOG(LogWater, Warning, TEXT("Could not find index of variable \"%s\" in the OverrideParameters map of NiagaraSystem \"%s\"."), *OverrideName, *NiagaraSystem->GetOwner()->GetName());
		return;
	}

	UNiagaraDataInterfaceWater* WaterInterface = Cast<UNiagaraDataInterfaceWater>(OverrideParameters.GetDataInterface(Index));
	if (!WaterInterface)
	{
		UE_LOG(LogWater, Warning, TEXT("Did not find a matching Water Data Interface variable named \"%s\" in the User variables of NiagaraSystem \"%s\" ."), *OverrideName, *NiagaraSystem->GetOwner()->GetName());
		return;
	}

	WaterInterface->SetWaterBody(WaterBody);
}

