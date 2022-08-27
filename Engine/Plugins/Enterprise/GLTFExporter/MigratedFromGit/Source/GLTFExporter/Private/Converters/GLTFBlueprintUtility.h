// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FGLTFBlueprintUtility
{
	static FString GetClassPath(const AActor* Actor);

	static bool IsSkySphere(const FString& Path);

	static bool IsHDRIBackdrop(const FString& Path);

	template <class ValueType>
	static bool TryGetPropertyValue(const UObject* Object, const TCHAR* PropertyName, ValueType& Value)
	{
		FProperty* Property = Object->GetClass()->FindPropertyByName(PropertyName);
		if (Property == nullptr)
		{
			return false;
		}

		const ValueType* ValuePtr = Property->ContainerPtrToValuePtr<ValueType>(Object);
		if (ValuePtr == nullptr)
		{
			return false;
		}

		Value = *ValuePtr;
		return true;
	}
};
