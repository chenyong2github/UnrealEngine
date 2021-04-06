// Copyright Epic Games, Inc. All Rights Reserved.

#include "Models/LensModel.h"

#include "Logging/LogMacros.h"


DEFINE_LOG_CATEGORY_STATIC(LogLensModel, Log, All);


uint32 ULensModel::GetNumParameters() const
{
	UScriptStruct* TypeStruct = GetParameterStruct();

	uint32 NumParameters = 0;
	for (TFieldIterator<FProperty> It(TypeStruct); It; ++It)
	{
		if (FFloatProperty* Prop = CastField<FFloatProperty>(*It))
		{
			++NumParameters;
		}
		else
		{
			UE_LOG(LogLensModel, Warning, TEXT("Property '%s' was skipped because its type was not float"), *(It->GetNameCPP()));
		}
	}

	return NumParameters;
}

void ULensModel::ToArray_Internal(UScriptStruct* TypeStruct, void* SrcData, TArray<float>& DstArray) const
{
	if (TypeStruct != GetParameterStruct())
	{
		UE_LOG(LogLensModel, Error, TEXT("TypeStruct does not match the distortion parameter struct supported by this model"));
		return;
	}

	DstArray.Reserve(GetNumParameters());
	for (TFieldIterator<FProperty> It(TypeStruct); It; ++It)
	{
		if (FFloatProperty* Prop = CastField<FFloatProperty>(*It))
		{
			const float* Tmp = Prop->ContainerPtrToValuePtr<float>(SrcData);
			DstArray.Add(*Tmp);
		}
		else
		{
			UE_LOG(LogLensModel, Warning, TEXT("Property '%s' was skipped because its type was not float"), *(It->GetNameCPP()));
		}
	}
}

void ULensModel::FromArray_Internal(UScriptStruct* TypeStruct, const TArray<float>& SrcArray, void* DstData)
{
	if (TypeStruct != GetParameterStruct())
	{
		UE_LOG(LogLensModel, Error, TEXT("TypeStruct does not match the distortion parameter struct supported by this model"));
		return;
	}

	if (SrcArray.Num() != GetNumParameters())
	{
		UE_LOG(LogLensModel, Error, TEXT("SrcArray size (%d) does not match the expected number of parameters (%d)."), SrcArray.Num(), GetNumParameters());
		return;
	}

	uint32 Index = 0;
	for (TFieldIterator<FProperty> It(TypeStruct); It; ++It)
	{
		if (FFloatProperty* Prop = CastField<FFloatProperty>(*It))
		{
			Prop->SetPropertyValue_InContainer(DstData, SrcArray[Index]);
			++Index;
		}
		else
		{
			UE_LOG(LogLensModel, Warning, TEXT("Property '%s' was skipped because its type was not float"), *(It->GetNameCPP()));
		}
	}
}
