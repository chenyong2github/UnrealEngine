// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraDataInterfaceArrayFunctionLibrary.h"
#include "NiagaraDataInterfaceArrayFloat.h"
#include "NiagaraDataInterfaceArrayInt.h"
#include "NiagaraFunctionLibrary.h"

void UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayFloat(UNiagaraComponent* NiagaraSystem, FName OverrideName, const TArray<float>& ArrayData)
{
	if (UNiagaraDataInterfaceArrayFloat* ArrayDI = UNiagaraFunctionLibrary::GetDataInterface<UNiagaraDataInterfaceArrayFloat>(NiagaraSystem, OverrideName))
	{
		FRWScopeLock WriteLock(ArrayDI->ArrayRWGuard, SLT_Write);
		ArrayDI->FloatData = ArrayData;
		ArrayDI->MarkRenderDataDirty();
	}
}

void UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector2D(UNiagaraComponent* NiagaraSystem, FName OverrideName, const TArray<FVector2D>& ArrayData)
{
	if (UNiagaraDataInterfaceArrayFloat2* ArrayDI = UNiagaraFunctionLibrary::GetDataInterface<UNiagaraDataInterfaceArrayFloat2>(NiagaraSystem, OverrideName))
	{
		FRWScopeLock WriteLock(ArrayDI->ArrayRWGuard, SLT_Write);
		ArrayDI->FloatData = ArrayData;
		ArrayDI->MarkRenderDataDirty();
	}
}

void UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector(UNiagaraComponent* NiagaraSystem, FName OverrideName, const TArray<FVector>& ArrayData)
{
	if (UNiagaraDataInterfaceArrayFloat3* ArrayDI = UNiagaraFunctionLibrary::GetDataInterface<UNiagaraDataInterfaceArrayFloat3>(NiagaraSystem, OverrideName))
	{
		FRWScopeLock WriteLock(ArrayDI->ArrayRWGuard, SLT_Write);
		ArrayDI->FloatData = ArrayData;
		ArrayDI->MarkRenderDataDirty();
	}
}

void UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector4(UNiagaraComponent* NiagaraSystem, FName OverrideName, const TArray<FVector4>& ArrayData)
{
	if (UNiagaraDataInterfaceArrayFloat4* ArrayDI = UNiagaraFunctionLibrary::GetDataInterface<UNiagaraDataInterfaceArrayFloat4>(NiagaraSystem, OverrideName))
	{
		FRWScopeLock WriteLock(ArrayDI->ArrayRWGuard, SLT_Write);
		ArrayDI->FloatData = ArrayData;
		ArrayDI->MarkRenderDataDirty();
	}
}

void UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayColor(UNiagaraComponent* NiagaraSystem, FName OverrideName, const TArray<FLinearColor>& ArrayData)
{
	if (UNiagaraDataInterfaceArrayColor* ArrayDI = UNiagaraFunctionLibrary::GetDataInterface<UNiagaraDataInterfaceArrayColor>(NiagaraSystem, OverrideName))
	{
		FRWScopeLock WriteLock(ArrayDI->ArrayRWGuard, SLT_Write);
		ArrayDI->ColorData = ArrayData;
		ArrayDI->MarkRenderDataDirty();
	}
}

void UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayQuat(UNiagaraComponent* NiagaraSystem, FName OverrideName, const TArray<FQuat>& ArrayData)
{
	if (UNiagaraDataInterfaceArrayQuat* ArrayDI = UNiagaraFunctionLibrary::GetDataInterface<UNiagaraDataInterfaceArrayQuat>(NiagaraSystem, OverrideName))
	{
		FRWScopeLock WriteLock(ArrayDI->ArrayRWGuard, SLT_Write);
		ArrayDI->QuatData = ArrayData;
		ArrayDI->MarkRenderDataDirty();
	}
}

void UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayInt32(UNiagaraComponent* NiagaraSystem, FName OverrideName, const TArray<int32>& ArrayData)
{
	if (UNiagaraDataInterfaceArrayInt32* ArrayDI = UNiagaraFunctionLibrary::GetDataInterface<UNiagaraDataInterfaceArrayInt32>(NiagaraSystem, OverrideName))
	{
		FRWScopeLock WriteLock(ArrayDI->ArrayRWGuard, SLT_Write);
		ArrayDI->IntData = ArrayData;
		ArrayDI->MarkRenderDataDirty();
	}
}

void UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayBool(UNiagaraComponent* NiagaraSystem, FName OverrideName, const TArray<bool>& ArrayData)
{
	if (UNiagaraDataInterfaceArrayBool* ArrayDI = UNiagaraFunctionLibrary::GetDataInterface<UNiagaraDataInterfaceArrayBool>(NiagaraSystem, OverrideName))
	{
		FRWScopeLock WriteLock(ArrayDI->ArrayRWGuard, SLT_Write);
		ArrayDI->BoolData = ArrayData;
		ArrayDI->MarkRenderDataDirty();
	}
}

TArray<float> UNiagaraDataInterfaceArrayFunctionLibrary::GetNiagaraArrayFloat(UNiagaraComponent* NiagaraSystem, FName OverrideName)
{
	if (UNiagaraDataInterfaceArrayFloat* ArrayDI = UNiagaraFunctionLibrary::GetDataInterface<UNiagaraDataInterfaceArrayFloat>(NiagaraSystem, OverrideName))
	{
		FRWScopeLock ReadLock(ArrayDI->ArrayRWGuard, SLT_ReadOnly);
		return ArrayDI->FloatData;
	}
	return TArray<float>();
}

TArray<FVector2D> UNiagaraDataInterfaceArrayFunctionLibrary::GetNiagaraArrayVector2D(UNiagaraComponent* NiagaraSystem, FName OverrideName)
{
	if (UNiagaraDataInterfaceArrayFloat2* ArrayDI = UNiagaraFunctionLibrary::GetDataInterface<UNiagaraDataInterfaceArrayFloat2>(NiagaraSystem, OverrideName))
	{
		FRWScopeLock ReadLock(ArrayDI->ArrayRWGuard, SLT_ReadOnly);
		return ArrayDI->FloatData;
	}
	return TArray<FVector2D>();
}

TArray<FVector> UNiagaraDataInterfaceArrayFunctionLibrary::GetNiagaraArrayVector(UNiagaraComponent* NiagaraSystem, FName OverrideName)
{
	if (UNiagaraDataInterfaceArrayFloat3* ArrayDI = UNiagaraFunctionLibrary::GetDataInterface<UNiagaraDataInterfaceArrayFloat3>(NiagaraSystem, OverrideName))
	{
		FRWScopeLock ReadLock(ArrayDI->ArrayRWGuard, SLT_ReadOnly);
		return ArrayDI->FloatData;
	}
	return TArray<FVector>();
}

TArray<FVector4> UNiagaraDataInterfaceArrayFunctionLibrary::GetNiagaraArrayVector4(UNiagaraComponent* NiagaraSystem, FName OverrideName)
{
	if (UNiagaraDataInterfaceArrayFloat4* ArrayDI = UNiagaraFunctionLibrary::GetDataInterface<UNiagaraDataInterfaceArrayFloat4>(NiagaraSystem, OverrideName))
	{
		FRWScopeLock ReadLock(ArrayDI->ArrayRWGuard, SLT_ReadOnly);
		return ArrayDI->FloatData;
	}
	return TArray<FVector4>();
}

TArray<FLinearColor> UNiagaraDataInterfaceArrayFunctionLibrary::GetNiagaraArrayColor(UNiagaraComponent* NiagaraSystem, FName OverrideName)
{
	if (UNiagaraDataInterfaceArrayColor* ArrayDI = UNiagaraFunctionLibrary::GetDataInterface<UNiagaraDataInterfaceArrayColor>(NiagaraSystem, OverrideName))
	{
		FRWScopeLock ReadLock(ArrayDI->ArrayRWGuard, SLT_ReadOnly);
		return ArrayDI->ColorData;
	}
	return TArray<FLinearColor>();
}

TArray<FQuat> UNiagaraDataInterfaceArrayFunctionLibrary::GetNiagaraArrayQuat(UNiagaraComponent* NiagaraSystem, FName OverrideName)
{
	if (UNiagaraDataInterfaceArrayQuat* ArrayDI = UNiagaraFunctionLibrary::GetDataInterface<UNiagaraDataInterfaceArrayQuat>(NiagaraSystem, OverrideName))
	{
		FRWScopeLock ReadLock(ArrayDI->ArrayRWGuard, SLT_ReadOnly);
		return ArrayDI->QuatData;
	}
	return TArray<FQuat>();
}

TArray<int32> UNiagaraDataInterfaceArrayFunctionLibrary::GetNiagaraArrayInt32(UNiagaraComponent* NiagaraSystem, FName OverrideName)
{
	if (UNiagaraDataInterfaceArrayInt32* ArrayDI = UNiagaraFunctionLibrary::GetDataInterface<UNiagaraDataInterfaceArrayInt32>(NiagaraSystem, OverrideName))
	{
		FRWScopeLock ReadLock(ArrayDI->ArrayRWGuard, SLT_ReadOnly);
		return ArrayDI->IntData;
	}
	return TArray<int32>();
}

TArray<bool> UNiagaraDataInterfaceArrayFunctionLibrary::GetNiagaraArrayBool(UNiagaraComponent* NiagaraSystem, FName OverrideName)
{
	if (UNiagaraDataInterfaceArrayBool* ArrayDI = UNiagaraFunctionLibrary::GetDataInterface<UNiagaraDataInterfaceArrayBool>(NiagaraSystem, OverrideName))
	{
		FRWScopeLock ReadLock(ArrayDI->ArrayRWGuard, SLT_ReadOnly);
		return ArrayDI->BoolData;
	}
	return TArray<bool>();
}

