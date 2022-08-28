// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class ULightComponent;
class UCameraComponent;
class AGLTFHotspotActor;

struct FGLTFNameUtility
{
	template <typename EnumType, typename = typename TEnableIf<TIsEnum<EnumType>::Value>::Type>
	static FString GetName(EnumType Value)
	{
		const UEnum* Enum = StaticEnum<EnumType>();
		check(Enum != nullptr);
		const int32 NumericValue = static_cast<int32>(Value);
		const FString DisplayName = Enum->GetDisplayNameTextByValue(NumericValue).ToString();
		return DisplayName.IsEmpty() ? FString::FromInt(NumericValue) : DisplayName;
	}

	static FString GetName(const USceneComponent* Component);

	static FString GetName(const UStaticMeshComponent* Component);
	static FString GetName(const USkeletalMeshComponent* Component);
	static FString GetName(const ULightComponent* Component);
	static FString GetName(const UCameraComponent* Component);
};
