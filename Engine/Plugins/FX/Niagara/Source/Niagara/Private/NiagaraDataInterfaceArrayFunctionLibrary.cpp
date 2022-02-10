// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraDataInterfaceArrayFunctionLibrary.h"

#include "NiagaraComponent.h"
#include "NiagaraDataInterfaceArrayFloat.h"
#include "NiagaraDataInterfaceArrayInt.h"
#include "NiagaraDataInterfaceArrayImpl.h"
#include "NiagaraComponent.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraFunctionLibrary.h"

// If / when we share user parameter UObjects we will need to make this per instance which introduces some tricky things about allocating before the instance is active
template<typename TArrayType, typename TDataInterace>
void SetNiagaraArray(UNiagaraComponent* NiagaraSystem, FName OverrideName, const TArray<TArrayType>& InArray)
{
	if (TDataInterace* ArrayDI = UNiagaraFunctionLibrary::GetDataInterface<TDataInterace>(NiagaraSystem, OverrideName))
	{
		auto* ArrayProxy = static_cast<FNDIArrayProxyImpl<TArrayType, TDataInterace>*>(ArrayDI->GetProxy());
		ArrayProxy->SetArrayData(InArray);
	}
}

template<typename TArrayType, typename TDataInterace>
TArray<TArrayType> GetNiagaraArray(UNiagaraComponent* NiagaraSystem, FName OverrideName)
{
	if (TDataInterace* ArrayDI = UNiagaraFunctionLibrary::GetDataInterface<TDataInterace>(NiagaraSystem, OverrideName))
	{
		auto* ArrayProxy = static_cast<FNDIArrayProxyImpl<TArrayType, TDataInterace>*>(ArrayDI->GetProxy());
		return ArrayProxy->GetArrayData();
	}
	return TArray<TArrayType>();
}

template<typename TArrayType, typename TDataInterace>
void SetNiagaraArrayValue(UNiagaraComponent* NiagaraSystem, FName OverrideName, int Index, const TArrayType& Value, bool bSizeToFit)
{
	if (TDataInterace* ArrayDI = UNiagaraFunctionLibrary::GetDataInterface<TDataInterace>(NiagaraSystem, OverrideName))
	{
		auto* ArrayProxy = static_cast<FNDIArrayProxyImpl<TArrayType, TDataInterace>*>(ArrayDI->GetProxy());
		ArrayProxy->SetArrayValue(Index, Value, bSizeToFit);
	}
}

template<typename TArrayType, typename TDataInterace>
TArrayType GetNiagaraArrayValue(UNiagaraComponent* NiagaraSystem, FName OverrideName, int Index)
{
	if (TDataInterace* ArrayDI = UNiagaraFunctionLibrary::GetDataInterface<TDataInterace>(NiagaraSystem, OverrideName))
	{
		auto* ArrayProxy = static_cast<FNDIArrayProxyImpl<TArrayType, TDataInterace>*>(ArrayDI->GetProxy());
		return ArrayProxy->GetArrayValue(Index);
	}
	//-TODO: Should be DefaultValue
	return TArrayType();
}

FNiagaraLWCConverter GetLWCConverter(UNiagaraComponent* NiagaraSystem)
{
	FNiagaraSystemInstanceControllerPtr SystemInstanceController = NiagaraSystem->GetSystemInstanceController();
	if (!SystemInstanceController.IsValid())
	{
		// the instance controller can be invalid if there is no simulation, for example when fx.SuppressNiagaraSystems is set 
		return FNiagaraLWCConverter();
	}
	FNiagaraSystemInstance* SystemInstance = SystemInstanceController->GetSystemInstance_Unsafe();
	return SystemInstance->GetLWCConverter();
}

//////////////////////////////////////////////////////////////////////////

void UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayFloat(UNiagaraComponent* NiagaraSystem, FName OverrideName, const TArray<float>& ArrayData)
{
	SetNiagaraArray<float, UNiagaraDataInterfaceArrayFloat>(NiagaraSystem, OverrideName, ArrayData);
}

void UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector2D(UNiagaraComponent* NiagaraSystem, FName OverrideName, const TArray<FVector2D>& ArrayData)
{
	SetNiagaraArray<FVector2D, UNiagaraDataInterfaceArrayFloat2>(NiagaraSystem, OverrideName, ArrayData);
}

void UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector(UNiagaraComponent* NiagaraSystem, FName OverrideName, const TArray<FVector>& ArrayData)
{
	SetNiagaraArray<FVector, UNiagaraDataInterfaceArrayFloat3>(NiagaraSystem, OverrideName, ArrayData);
}

void UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayPosition(UNiagaraComponent* NiagaraSystem, FName OverrideName, const TArray<FVector>& ArrayData)
{
	FNiagaraLWCConverter LwcConverter = GetLWCConverter(NiagaraSystem);
	TArray<FNiagaraPosition> ConvertedData;
	ConvertedData.SetNumUninitialized(ArrayData.Num());
	for (int i = 0; i < ArrayData.Num(); i++)
	{
		ConvertedData[i] = LwcConverter.ConvertWorldToSimulationPosition(ArrayData[i]);
	}
	SetNiagaraArray<FNiagaraPosition, UNiagaraDataInterfaceArrayPosition>(NiagaraSystem, OverrideName, ConvertedData);
}

void UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector4(UNiagaraComponent* NiagaraSystem, FName OverrideName, const TArray<FVector4>& ArrayData)
{
	SetNiagaraArray<FVector4, UNiagaraDataInterfaceArrayFloat4>(NiagaraSystem, OverrideName, ArrayData);
}

void UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayColor(UNiagaraComponent* NiagaraSystem, FName OverrideName, const TArray<FLinearColor>& ArrayData)
{
	SetNiagaraArray<FLinearColor, UNiagaraDataInterfaceArrayColor>(NiagaraSystem, OverrideName, ArrayData);
}

void UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayQuat(UNiagaraComponent* NiagaraSystem, FName OverrideName, const TArray<FQuat>& ArrayData)
{
	SetNiagaraArray<FQuat, UNiagaraDataInterfaceArrayQuat>(NiagaraSystem, OverrideName, ArrayData);
}

void UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayInt32(UNiagaraComponent* NiagaraSystem, FName OverrideName, const TArray<int32>& ArrayData)
{
	SetNiagaraArray<int32, UNiagaraDataInterfaceArrayInt32>(NiagaraSystem, OverrideName, ArrayData);
}

void UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayBool(UNiagaraComponent* NiagaraSystem, FName OverrideName, const TArray<bool>& ArrayData)
{
	SetNiagaraArray<bool, UNiagaraDataInterfaceArrayBool>(NiagaraSystem, OverrideName, ArrayData);
}

//////////////////////////////////////////////////////////////////////////

TArray<float> UNiagaraDataInterfaceArrayFunctionLibrary::GetNiagaraArrayFloat(UNiagaraComponent* NiagaraSystem, FName OverrideName)
{
	return GetNiagaraArray<float, UNiagaraDataInterfaceArrayFloat>(NiagaraSystem, OverrideName);
}

TArray<FVector2D> UNiagaraDataInterfaceArrayFunctionLibrary::GetNiagaraArrayVector2D(UNiagaraComponent* NiagaraSystem, FName OverrideName)
{
	return GetNiagaraArray<FVector2D, UNiagaraDataInterfaceArrayFloat2>(NiagaraSystem, OverrideName);
}

TArray<FVector> UNiagaraDataInterfaceArrayFunctionLibrary::GetNiagaraArrayVector(UNiagaraComponent* NiagaraSystem, FName OverrideName)
{
	return GetNiagaraArray<FVector, UNiagaraDataInterfaceArrayFloat3>(NiagaraSystem, OverrideName);
}

TArray<FVector> UNiagaraDataInterfaceArrayFunctionLibrary::GetNiagaraArrayPosition(UNiagaraComponent* NiagaraSystem, FName OverrideName)
{
	const TArray<FNiagaraPosition> SimData = GetNiagaraArray<FNiagaraPosition, UNiagaraDataInterfaceArrayPosition>(NiagaraSystem, OverrideName);
	
	FNiagaraLWCConverter LwcConverter = GetLWCConverter(NiagaraSystem);
	TArray<FVector> ConvertedData;
	ConvertedData.SetNumUninitialized(SimData.Num());
	for (int i = 0; i < SimData.Num(); i++)
	{
		ConvertedData[i] = LwcConverter.ConvertSimulationPositionToWorld(SimData[i]);
	}
	
	return ConvertedData;
}

TArray<FVector4> UNiagaraDataInterfaceArrayFunctionLibrary::GetNiagaraArrayVector4(UNiagaraComponent* NiagaraSystem, FName OverrideName)
{
	return GetNiagaraArray<FVector4, UNiagaraDataInterfaceArrayFloat4>(NiagaraSystem, OverrideName);
}

TArray<FLinearColor> UNiagaraDataInterfaceArrayFunctionLibrary::GetNiagaraArrayColor(UNiagaraComponent* NiagaraSystem, FName OverrideName)
{
	return GetNiagaraArray<FLinearColor, UNiagaraDataInterfaceArrayColor>(NiagaraSystem, OverrideName);
}

TArray<FQuat> UNiagaraDataInterfaceArrayFunctionLibrary::GetNiagaraArrayQuat(UNiagaraComponent* NiagaraSystem, FName OverrideName)
{
	return GetNiagaraArray<FQuat, UNiagaraDataInterfaceArrayQuat>(NiagaraSystem, OverrideName);
}

TArray<int32> UNiagaraDataInterfaceArrayFunctionLibrary::GetNiagaraArrayInt32(UNiagaraComponent* NiagaraSystem, FName OverrideName)
{
	return GetNiagaraArray<int32, UNiagaraDataInterfaceArrayInt32>(NiagaraSystem, OverrideName);
}

TArray<bool> UNiagaraDataInterfaceArrayFunctionLibrary::GetNiagaraArrayBool(UNiagaraComponent* NiagaraSystem, FName OverrideName)
{
	return GetNiagaraArray<bool, UNiagaraDataInterfaceArrayBool>(NiagaraSystem, OverrideName);
}

//////////////////////////////////////////////////////////////////////////

void UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayFloatValue(UNiagaraComponent* NiagaraSystem, FName OverrideName, int Index, float Value, bool bSizeToFit)
{
	SetNiagaraArrayValue<float, UNiagaraDataInterfaceArrayFloat>(NiagaraSystem, OverrideName, Index, Value, bSizeToFit);
}

void UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector2DValue(UNiagaraComponent* NiagaraSystem, FName OverrideName, int Index, const FVector2D& Value, bool bSizeToFit)
{
	SetNiagaraArrayValue<FVector2D, UNiagaraDataInterfaceArrayFloat2>(NiagaraSystem, OverrideName, Index, Value, bSizeToFit);
}

void UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVectorValue(UNiagaraComponent* NiagaraSystem, FName OverrideName, int Index, const FVector& Value, bool bSizeToFit)
{
	SetNiagaraArrayValue<FVector, UNiagaraDataInterfaceArrayFloat3>(NiagaraSystem, OverrideName, Index, Value, bSizeToFit);
}

void UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayPositionValue(UNiagaraComponent* NiagaraSystem, FName OverrideName, int Index, const FVector& Value, bool bSizeToFit)
{
	FNiagaraLWCConverter LwcConverter = GetLWCConverter(NiagaraSystem);
	FNiagaraPosition SimulationPosition = LwcConverter.ConvertWorldToSimulationPosition(Value);
	SetNiagaraArrayValue<FNiagaraPosition, UNiagaraDataInterfaceArrayPosition>(NiagaraSystem, OverrideName, Index, SimulationPosition, bSizeToFit);
}

void UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector4Value(UNiagaraComponent* NiagaraSystem, FName OverrideName, int Index, const FVector4& Value, bool bSizeToFit)
{
	SetNiagaraArrayValue<FVector4, UNiagaraDataInterfaceArrayFloat4>(NiagaraSystem, OverrideName, Index, Value, bSizeToFit);
}

void UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayColorValue(UNiagaraComponent* NiagaraSystem, FName OverrideName, int Index, const FLinearColor& Value, bool bSizeToFit)
{
	SetNiagaraArrayValue<FLinearColor, UNiagaraDataInterfaceArrayColor>(NiagaraSystem, OverrideName, Index, Value, bSizeToFit);
}

void UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayQuatValue(UNiagaraComponent* NiagaraSystem, FName OverrideName, int Index, const FQuat& Value, bool bSizeToFit)
{
	SetNiagaraArrayValue<FQuat, UNiagaraDataInterfaceArrayQuat>(NiagaraSystem, OverrideName, Index, Value, bSizeToFit);
}

void UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayInt32Value(UNiagaraComponent* NiagaraSystem, FName OverrideName, int Index, int32 Value, bool bSizeToFit)
{
	SetNiagaraArrayValue<int32, UNiagaraDataInterfaceArrayInt32>(NiagaraSystem, OverrideName, Index, Value, bSizeToFit);
}

void UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayBoolValue(UNiagaraComponent* NiagaraSystem, FName OverrideName, int Index, const bool& Value, bool bSizeToFit)
{
	SetNiagaraArrayValue<bool, UNiagaraDataInterfaceArrayBool>(NiagaraSystem, OverrideName, Index, Value, bSizeToFit);
}

//////////////////////////////////////////////////////////////////////////

float UNiagaraDataInterfaceArrayFunctionLibrary::GetNiagaraArrayFloatValue(UNiagaraComponent* NiagaraSystem, FName OverrideName, int Index)
{
	return GetNiagaraArrayValue<float, UNiagaraDataInterfaceArrayFloat>(NiagaraSystem, OverrideName, Index);
}

FVector2D UNiagaraDataInterfaceArrayFunctionLibrary::GetNiagaraArrayVector2DValue(UNiagaraComponent* NiagaraSystem, FName OverrideName, int Index)
{
	return GetNiagaraArrayValue<FVector2D, UNiagaraDataInterfaceArrayFloat2>(NiagaraSystem, OverrideName, Index);
}

FVector UNiagaraDataInterfaceArrayFunctionLibrary::GetNiagaraArrayVectorValue(UNiagaraComponent* NiagaraSystem, FName OverrideName, int Index)
{
	return GetNiagaraArrayValue<FVector, UNiagaraDataInterfaceArrayFloat3>(NiagaraSystem, OverrideName, Index);
}

FVector UNiagaraDataInterfaceArrayFunctionLibrary::GetNiagaraArrayPositionValue(UNiagaraComponent* NiagaraSystem, FName OverrideName, int Index)
{
	FNiagaraPosition SimPosition = GetNiagaraArrayValue<FNiagaraPosition, UNiagaraDataInterfaceArrayPosition>(NiagaraSystem, OverrideName, Index);
	FNiagaraLWCConverter LwcConverter = GetLWCConverter(NiagaraSystem);
	return LwcConverter.ConvertSimulationPositionToWorld(SimPosition);
}

FVector4 UNiagaraDataInterfaceArrayFunctionLibrary::GetNiagaraArrayVector4Value(UNiagaraComponent* NiagaraSystem, FName OverrideName, int Index)
{
	return GetNiagaraArrayValue<FVector4, UNiagaraDataInterfaceArrayFloat4>(NiagaraSystem, OverrideName, Index);
}

FLinearColor UNiagaraDataInterfaceArrayFunctionLibrary::GetNiagaraArrayColorValue(UNiagaraComponent* NiagaraSystem, FName OverrideName, int Index)
{
	return GetNiagaraArrayValue<FLinearColor, UNiagaraDataInterfaceArrayColor>(NiagaraSystem, OverrideName, Index);
}

FQuat UNiagaraDataInterfaceArrayFunctionLibrary::GetNiagaraArrayQuatValue(UNiagaraComponent* NiagaraSystem, FName OverrideName, int Index)
{
	return GetNiagaraArrayValue<FQuat, UNiagaraDataInterfaceArrayQuat>(NiagaraSystem, OverrideName, Index);
}

int32 UNiagaraDataInterfaceArrayFunctionLibrary::GetNiagaraArrayInt32Value(UNiagaraComponent* NiagaraSystem, FName OverrideName, int Index)
{
	return GetNiagaraArrayValue<int32, UNiagaraDataInterfaceArrayInt32>(NiagaraSystem, OverrideName, Index);
}

bool UNiagaraDataInterfaceArrayFunctionLibrary::GetNiagaraArrayBoolValue(UNiagaraComponent* NiagaraSystem, FName OverrideName, int Index)
{
	return GetNiagaraArrayValue<bool, UNiagaraDataInterfaceArrayBool>(NiagaraSystem, OverrideName, Index);
}
