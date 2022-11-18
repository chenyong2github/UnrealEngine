// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFNameUtility.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"

FString FGLTFNameUtility::GetName(const UEnum* Enum, int32 Value)
{
	check(Enum != nullptr);
	const FString DisplayName = Enum->GetDisplayNameTextByValue(Value).ToString();
	return DisplayName.IsEmpty() ? FString::FromInt(Value) : DisplayName;
}

FString FGLTFNameUtility::GetName(const USceneComponent* Component)
{
	if (const AActor* Owner = Component->GetOwner())
	{
		if (Component == Owner->GetRootComponent())
		{
#if WITH_EDITOR
			return Owner->GetActorLabel();
#else
			return Owner->GetName();
#endif
		}
	}

	return Component->GetName();
}
