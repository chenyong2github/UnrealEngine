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
		return Enum->GetDisplayNameTextByValue(Value).ToString();
	}

	static FString GetName(const UStaticMesh* StaticMesh, int32 LODIndex);
	static FString GetName(const USkeletalMesh* SkeletalMesh, int32 LODIndex);

	static FString GetName(const USceneComponent* Component);

	static FString GetName(const UStaticMeshComponent* Component);
	static FString GetName(const USkeletalMeshComponent* Component);
	static FString GetName(const ULightComponent* Component);
	static FString GetName(const UCameraComponent* Component);
};
