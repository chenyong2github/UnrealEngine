// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FPropertyPath;
class FProperty;
class UStruct;

namespace PropertyHelpers
{

	struct FPropertyAddress
	{
		FProperty* Property;
		void* Address;

		FPropertyAddress()
			: Property(nullptr)
			, Address(nullptr)
		{}
	};

	struct FPropertyAndIndex
	{
		FPropertyAndIndex() : Property(nullptr), ArrayIndex(INDEX_NONE) {}

		FProperty* Property;
		int32 ArrayIndex;
	};

	FPropertyAndIndex FindPropertyAndArrayIndex(UStruct* InStruct, const FString& PropertyName);

	FPropertyAddress FindPropertyRecursive(void* BasePointer, UStruct* InStruct, TArray<FString>& InPropertyNames, uint32 Index, TArray<FProperty*>& InOutPropertyChain, bool bAllowArrayResize);

	FPropertyAddress FindProperty(void* BasePointer, UStruct* InStruct, const FString& InPropertyPath, TArray<FProperty*>& InOutPropertyChain, bool bAllowArrayResize);

}