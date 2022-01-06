// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraTypes.h"

void FNiagaraVariableBase::SetNamespacedName(const FString& InNamespace, FName InVariableName)
{
	TStringBuilder<128> NameBuilder;
	NameBuilder.Append(InNamespace);
	NameBuilder.AppendChar(TEXT('.'));
	InVariableName.AppendString(NameBuilder);
	Name = FName(NameBuilder.ToString());
}

void FNiagaraVariableMetaData::CopyUserEditableMetaData(const FNiagaraVariableMetaData& OtherMetaData)
{
	for (const FProperty* ChildProperty : TFieldRange<FProperty>(StaticStruct()))
	{
		if (ChildProperty->HasAnyPropertyFlags(CPF_Edit))
		{
			int32 PropertyOffset = ChildProperty->GetOffset_ForInternal();
			ChildProperty->CopyCompleteValue((uint8*)this + PropertyOffset, (uint8*)&OtherMetaData + PropertyOffset);
		};
	}
}

FNiagaraLWCConverter::FNiagaraLWCConverter(FVector InSystemWorldPos)
{
	SystemWorldPos = InSystemWorldPos;
}

FVector3f FNiagaraLWCConverter::ConvertWorldToSimulationVector(FVector WorldPosition) const
{
	return FVector3f(WorldPosition - SystemWorldPos);
}

FNiagaraPosition FNiagaraLWCConverter::ConvertWorldToSimulationPosition(FVector WorldPosition) const
{
	return FNiagaraPosition(ConvertWorldToSimulationVector(WorldPosition));
}

FVector FNiagaraLWCConverter::ConvertSimulationPositionToWorld(FNiagaraPosition SimulationPosition) const
{
	return ConvertSimulationVectorToWorld(SimulationPosition);
}

FVector FNiagaraLWCConverter::ConvertSimulationVectorToWorld(FVector3f SimulationPosition) const
{
	return FVector(SimulationPosition) + SystemWorldPos;
}
