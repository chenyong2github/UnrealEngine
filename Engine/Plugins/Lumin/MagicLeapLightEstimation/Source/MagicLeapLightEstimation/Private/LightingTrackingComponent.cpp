// Copyright Epic Games, Inc. All Rights Reserved.

#include "LightingTrackingComponent.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"
#include "Engine/TextureCube.h"
#include "RenderUtils.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Components/PostProcessComponent.h"
#include "IMagicLeapLightEstimationPlugin.h"
#include <limits>

#define MAX_NITS 15.0f

class FMagicLeapLightingTrackingImpl
{
public:
	FMagicLeapLightingTrackingImpl(UMagicLeapLightingTrackingComponent* InOwner)
		: Owner(InOwner)
		, PostProcessor(nullptr)
		, AmbientCubeMap(nullptr)
		, LastAmbientIntensityTimeStamp(0)
		, LastAmbientCubeMapTimeStamp(0)
	{
		IMagicLeapLightEstimationPlugin::Get().CreateTracker();

		for (TActorIterator<AActor> ActorItr(Owner->GetWorld()); ActorItr; ++ActorItr)
		{
			PostProcessor = (*ActorItr)->FindComponentByClass<UPostProcessComponent>();
			if (PostProcessor)
			{
				break;
			}
		}

		if (!PostProcessor)
		{
			PostProcessor = NewObject<UPostProcessComponent>(Owner->GetOwner());
			PostProcessor->RegisterComponent();
		}

		PostProcessor->bUnbound = true;
		PostProcessor->Settings.bOverride_AmbientCubemapIntensity = Owner->UseGlobalAmbience;
		PostProcessor->Settings.bOverride_WhiteTemp = Owner->UseColorTemp;
	}

	~FMagicLeapLightingTrackingImpl()
	{
		IMagicLeapLightEstimationPlugin::Get().DestroyTracker();
	}

	void RefreshGlobalAmbience()
	{
		PostProcessor->Settings.bOverride_AmbientCubemapIntensity = Owner->UseGlobalAmbience;

		if (!Owner->UseGlobalAmbience)
		{
			return;
		}

		FMagicLeapLightEstimationAmbientGlobalState AmbientGlobalState;
		bool Result = IMagicLeapLightEstimationPlugin::Get().GetAmbientGlobalState(AmbientGlobalState);
		if (Result)
		{
			if (AmbientGlobalState.Timestamp > LastAmbientIntensityTimeStamp)
			{
				LastAmbientIntensityTimeStamp = AmbientGlobalState.Timestamp;
				float LuminanceSum = 0.0f;
				for (float GlobalLuminance : AmbientGlobalState.AmbientIntensityNits)
				{
					LuminanceSum += GlobalLuminance;
				}
				PostProcessor->Settings.AmbientCubemapIntensity = (float)(LuminanceSum / AmbientGlobalState.AmbientIntensityNits.Num()) / MAX_NITS;// (float)std::numeric_limits<uint16>::max();
			}
		}
	}

	void RefreshColorTemperature()
	{
		PostProcessor->Settings.bOverride_WhiteTemp = Owner->UseColorTemp;

		if (!Owner->UseColorTemp)
		{
			return;
		}

		FMagicLeapLightEstimationColorTemperatureState ColorTemperatureState;
		bool Result = IMagicLeapLightEstimationPlugin::Get().GetColorTemperatureState(ColorTemperatureState);
		if (Result)
		{
			PostProcessor->Settings.WhiteTemp = ColorTemperatureState.ColorTemperatureKelvin;
		}
	}

	void RefreshAmbientCubeMap()
	{
		
	}

	UMagicLeapLightingTrackingComponent* Owner;
	UPostProcessComponent* PostProcessor;
	UTextureCube* AmbientCubeMap;
	FTimespan LastAmbientIntensityTimeStamp;
	FTimespan LastAmbientCubeMapTimeStamp;
};

UMagicLeapLightingTrackingComponent::UMagicLeapLightingTrackingComponent()
	: UseGlobalAmbience(false)
	, UseColorTemp(false)
	, Impl(nullptr)
	//, UseDynamicAmbientCubeMap(false)
{
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.bCanEverTick = true;
}

void UMagicLeapLightingTrackingComponent::BeginPlay()
{
	UActorComponent::BeginPlay();
	Impl = new FMagicLeapLightingTrackingImpl(this);
}

void UMagicLeapLightingTrackingComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UActorComponent::EndPlay(EndPlayReason);
	delete Impl;
	Impl = nullptr;
}

void UMagicLeapLightingTrackingComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	Impl->RefreshGlobalAmbience();

	Impl->RefreshColorTemperature();

	//Impl->RefreshAmbientCubeMap();
}