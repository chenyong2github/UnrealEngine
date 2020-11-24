// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SceneTypes.h"
#include "UObject/NameTypes.h"

/** Structure extending EMaterialProperty to allow detailed information about custom output */
struct FMaterialPropertyEx
{
	FMaterialPropertyEx(EMaterialProperty Type = MP_MAX, const FName& CustomOutput = NAME_None)
		: Type(Type)
		, CustomOutput(CustomOutput)
	{}

	FMaterialPropertyEx(const FName& CustomOutput)
		: Type(MP_CustomOutput)
		, CustomOutput(CustomOutput)
	{}

	FMaterialPropertyEx(const TCHAR* CustomOutput)
		: Type(MP_CustomOutput)
		, CustomOutput(CustomOutput)
	{}

	FORCEINLINE bool operator ==(const FMaterialPropertyEx& Other) const
	{
		return Type == Other.Type && (Type != MP_CustomOutput || CustomOutput == Other.CustomOutput);
	}

	FORCEINLINE bool operator !=(const FMaterialPropertyEx& Other) const
	{
		return !(*this == Other);
	}

	friend FORCEINLINE uint32 GetTypeHash(const FMaterialPropertyEx& Other)
	{
		return Other.Type != MP_CustomOutput ? GetTypeHash(Other.Type) : GetTypeHash(Other.CustomOutput);
	}

	FString ToString() const
	{
		if (Type != MP_CustomOutput)
		{
			const UEnum* Enum = StaticEnum<EMaterialProperty>();
			FName Name = Enum->GetNameByValue(Type);
			FString TrimmedName = Name.ToString();
			TrimmedName.RemoveFromStart(TEXT("MP_"), ESearchCase::CaseSensitive);
			return TrimmedName;
		}
		else
		{
			return CustomOutput.ToString();
		}
	}
	
	/** The material property */
	EMaterialProperty Type;
	
	/** The name of a specific custom output. Only used if property is MP_CustomOutput */
	FName CustomOutput;
};
