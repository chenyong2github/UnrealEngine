// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "CameraShakeSourceComponent.h"
#include "CameraShake.h"
#include "CinematicCameraModule.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Modules/ModuleManager.h"


UCameraShakeSourceComponent::UCameraShakeSourceComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, Attenuation(ECameraShakeAttenuation::Quadratic)
	, InnerAttenuationRadius(100.f)
	, OuterAttenuationRadius(1000.f)
{
}

void UCameraShakeSourceComponent::PlayCameraShake(TSubclassOf<UCameraShake> CameraShake)
{
	for (FConstPlayerControllerIterator Iterator = GetWorld()->GetPlayerControllerIterator(); Iterator; ++Iterator)
	{
		APlayerController* PlayerController = Iterator->Get();
		if (PlayerController != nullptr && PlayerController->PlayerCameraManager != nullptr)
		{
			PlayerController->ClientPlayCameraShakeFromSource(CameraShake, this);
		}
	}
}

void UCameraShakeSourceComponent::StopAllCameraShakes(bool bImmediately)
{
	for (FConstPlayerControllerIterator Iterator = GetWorld()->GetPlayerControllerIterator(); Iterator; ++Iterator)
	{
		APlayerController* PlayerController = Iterator->Get();
		if (PlayerController != nullptr && PlayerController->PlayerCameraManager != nullptr)
		{
			PlayerController->ClientStopCameraShakesFromSource(this, bImmediately);
		}
	}
}

float UCameraShakeSourceComponent::GetAttenuationFactor(const FVector& Location) const
{
	const FTransform& SourceTransform = GetComponentTransform();
	const FVector LocationToSource = SourceTransform.GetTranslation() - Location;

	float AttFactor = 1.0f;
	switch (Attenuation)
	{
		case ECameraShakeAttenuation::Quadratic:
			AttFactor = 2.0f;
			break;
		default:
			break;
	}

	if (InnerAttenuationRadius < OuterAttenuationRadius)
	{
		float DistFactor = (LocationToSource.Size() - InnerAttenuationRadius) / (OuterAttenuationRadius - InnerAttenuationRadius);
		DistFactor = 1.f - FMath::Clamp(DistFactor, 0.f, 1.f);
		return FMath::Pow(DistFactor, AttFactor);
	}
	else if (OuterAttenuationRadius > 0)
	{
		// Just cut the intensity after the end distance.
		return (LocationToSource.SizeSquared() < FMath::Square(OuterAttenuationRadius)) ? 1.f : 0.f;
	}
	return 1.f;
}
