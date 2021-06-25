// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXFixtureActor.h"

#include "DMXStats.h"

#include "Components/StaticMeshComponent.h"

DECLARE_CYCLE_STAT(TEXT("FixtureActor Push Normalized Values"), STAT_FixtureActorPushNormalizedValuesPerAttribute, STATGROUP_DMX);

ADMXFixtureActor::ADMXFixtureActor()
{
	PrimaryActorTick.bCanEverTick = true;

#if WITH_EDITORONLY_DATA
	bRunConstructionScriptOnDrag = 0;
#endif

	Base = CreateDefaultSubobject<USceneComponent>(TEXT("Base"));
	RootComponent = Base;

	Yoke = CreateDefaultSubobject<USceneComponent>(TEXT("Yoke"));
	Yoke->SetupAttachment(Base);

	Head = CreateDefaultSubobject<USceneComponent>(TEXT("Head"));
	Head->SetupAttachment(Yoke);

	PointLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("Fixture PointLight"));
	PointLight->SetupAttachment(Head);
	PointLight->SetCastShadows(false);
	PointLight->bAffectsWorld = false;

	SpotLight = CreateDefaultSubobject<USpotLightComponent>(TEXT("Fixture SpotLight"));
	SpotLight->SetupAttachment(Head);
	SpotLight->SetCastShadows(false);
	SpotLight->bAffectsWorld = true;

	OcclusionDirection = CreateDefaultSubobject<UArrowComponent>(TEXT("Occlusion"));
	OcclusionDirection->SetupAttachment(Head);

	DMX = CreateDefaultSubobject<UDMXComponent>(TEXT("DMX"));

	// set default values
	LightIntensityMax = 2000;
	LightDistanceMax = 1000;
	LightColorTemp = 6500;
	SpotlightIntensityScale = 1.0f;
	PointlightIntensityScale = 1.0f;
	LightCastShadow = false;
	UseDynamicOcclusion = false;
	LensRadius = 10.0f;
	QualityLevel = EDMXFixtureQualityLevel::HighQuality;
	MinQuality = 1.0f;
	MaxQuality = 1.0f;
	HasBeenInitialized = false;
}

#if WITH_EDITOR
void ADMXFixtureActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	FeedFixtureData();
}
#endif

void ADMXFixtureActor::InitializeFixture(UStaticMeshComponent* StaticMeshLens, UStaticMeshComponent* StaticMeshBeam)
{
	GetComponents<UStaticMeshComponent>(StaticMeshComponents);

	// Create dynamic materials
	DynamicMaterialLens = UMaterialInstanceDynamic::Create(LensMaterialInstance, NULL);
	DynamicMaterialBeam = UMaterialInstanceDynamic::Create(BeamMaterialInstance, NULL);
	DynamicMaterialSpotLight = UMaterialInstanceDynamic::Create(SpotLightMaterialInstance, NULL);
	DynamicMaterialPointLight = UMaterialInstanceDynamic::Create(PointLightMaterialInstance, NULL);

	// Get lens width (support scaling)
	if (StaticMeshLens)
	{
		//FBoxSphereBounds LocalBounds = StaticMeshLens->CalcLocalBounds();
		//FVector Scale = StaticMeshLens->GetRelativeScale3D();
		//float BiggestComponentScale = Scale.GetMax();
		//LensRadius = LocalBounds.SphereRadius * BiggestComponentScale * 0.97f;
		FBoxSphereBounds Bounds = StaticMeshLens->Bounds;
		LensRadius = Bounds.SphereRadius * 0.9f;
	}

	// Feed fixture data into materials and lights
	FeedFixtureData();

	// Assign dynamic materials to static meshes
	if (StaticMeshLens)
	{
		StaticMeshLens->SetMaterial(0, DynamicMaterialLens);
	}

	// Make sure beam doesnt have any scale applied or it wont render correctly
	if (StaticMeshBeam)
	{
		StaticMeshBeam->SetMaterial(0, DynamicMaterialBeam);
		StaticMeshBeam->SetWorldScale3D(FVector(1,1,1));
	}

	// Assign dynamic materials to lights
	SpotLight->SetMaterial(0, DynamicMaterialSpotLight);
	PointLight->SetMaterial(0, DynamicMaterialPointLight);

	// Initialize fixture components
	for (UDMXFixtureComponent* DMXComponent : TInlineComponentArray<UDMXFixtureComponent*>(this))
	{
		DMXComponent->Initialize();
	}

	HasBeenInitialized = true;
}

void ADMXFixtureActor::FeedFixtureData()
{
	// Note: MinQuality and MaxQuality are used in conjonction with the zoom angle when zoom component is used
	switch (QualityLevel)
	{
		case(EDMXFixtureQualityLevel::LowQuality): MinQuality = 4.0f; MaxQuality = 4.0f; break;
		case(EDMXFixtureQualityLevel::MediumQuality): MinQuality = 2.0f; MaxQuality = 2.0f; break;
		case(EDMXFixtureQualityLevel::HighQuality): MinQuality = 1.0f; MaxQuality = 1.0f; break;
		case(EDMXFixtureQualityLevel::UltraQuality): MinQuality = 0.33f; MaxQuality = 0.33f; break;
	}

	// Note:fallback when fixture doesnt use zoom component
	float QualityFallback = 1.0f;
	switch (QualityLevel)
	{
		case(EDMXFixtureQualityLevel::LowQuality): QualityFallback = 4.0f; break;
		case(EDMXFixtureQualityLevel::MediumQuality): QualityFallback = 2.0f; break;
		case(EDMXFixtureQualityLevel::HighQuality): QualityFallback = 1.0f; break;
		case(EDMXFixtureQualityLevel::UltraQuality): QualityFallback = 0.33f; break;
	}
	
	if (QualityLevel == EDMXFixtureQualityLevel::Custom)
	{
		MinQuality = FMath::Clamp(MinQuality, 0.2f, 4.0f);
		MaxQuality = FMath::Clamp(MaxQuality, 0.2f, 4.0f);
		QualityFallback = MaxQuality;
	}

	if (DynamicMaterialBeam)
	{
		DynamicMaterialBeam->SetScalarParameterValue("DMX Quality Level", QualityFallback);
		DynamicMaterialBeam->SetScalarParameterValue("DMX Max Light Distance", LightDistanceMax);
		DynamicMaterialBeam->SetScalarParameterValue("DMX Max Light Intensity", LightIntensityMax * SpotlightIntensityScale);
	}

	if (DynamicMaterialLens)
	{
		DynamicMaterialLens->SetScalarParameterValue("DMX Max Light Intensity", LightIntensityMax * SpotlightIntensityScale);
		DynamicMaterialBeam->SetScalarParameterValue("DMX Lens Radius", LensRadius);
	}

	// Set lights
	SpotLight->SetIntensity(LightIntensityMax * SpotlightIntensityScale);
	SpotLight->SetTemperature(LightColorTemp);
	SpotLight->SetCastShadows(LightCastShadow);
	SpotLight->SetAttenuationRadius(LightDistanceMax);

	PointLight->SetIntensity(LightIntensityMax * PointlightIntensityScale);
	PointLight->SetTemperature(LightColorTemp);
	PointLight->SetCastShadows(LightCastShadow);
	PointLight->SetAttenuationRadius(LightDistanceMax);
}

void ADMXFixtureActor::InterpolateDMXComponents(float DeltaSeconds)
{
	// Get current components (supports PIE)
	TInlineComponentArray<UDMXFixtureComponent*> DMXComponents;
	GetComponents<UDMXFixtureComponent>(DMXComponents);

	for (auto& DMXComponent : DMXComponents)
	{
		if (DMXComponent->bIsEnabled && DMXComponent->bUseInterpolation && DMXComponent->IsRegistered())
		{
			for (auto& Cell : DMXComponent->Cells)
			{
				DMXComponent->CurrentCell = &Cell;
				for (auto& ChannelInterp : Cell.ChannelInterpolation)
				{
					if (ChannelInterp.IsUpdating)
					{
						// Update
						ChannelInterp.Travel(DeltaSeconds);

						// Run BP event
						DMXComponent->InterpolateComponent(DeltaSeconds);

						// Is done?
						if (ChannelInterp.IsInterpolationDone())
						{
							ChannelInterp.EndInterpolation();
						}
					}
				}
			}
		}
	}
}

void ADMXFixtureActor::PushNormalizedValuesPerAttribute(const FDMXNormalizedAttributeValueMap& ValuePerAttribute)
{
	SCOPE_CYCLE_COUNTER(STAT_FixtureActorPushNormalizedValuesPerAttribute);

	if (HasBeenInitialized)
	{
		for (UDMXFixtureComponent* DMXComponent : TInlineComponentArray<UDMXFixtureComponent*>(this))
		{
			// Components without matrix data
			if (DMXComponent->bIsEnabled)
			{
				if (UDMXFixtureComponentSingle* SingleComponent = Cast<UDMXFixtureComponentSingle>(DMXComponent))
				{
					// SingleChannel Component
					const float* TargetValuePtr = ValuePerAttribute.Map.Find(SingleComponent->DMXChannel.Name);
					if (TargetValuePtr)
					{
						const float RemappedValue = SingleComponent->NormalizedToAbsoluteValue(*TargetValuePtr);
						SingleComponent->SetTargetValue(RemappedValue);
					}
				}
				else if (UDMXFixtureComponentDouble* DoubleComponent = Cast<UDMXFixtureComponentDouble>(DMXComponent))
				{	
					// DoubleChannel Component
					const float* FirstTargetValuePtr = ValuePerAttribute.Map.Find(DoubleComponent->DMXChannel1.Name);
					if (FirstTargetValuePtr)
					{
						float Channel1RemappedValue = DoubleComponent->NormalizedToAbsoluteValue(0, *FirstTargetValuePtr);

						constexpr int32 FirstChannelIndex = 0;
						DoubleComponent->SetTargetValue(FirstChannelIndex, *FirstTargetValuePtr);
					}

					const float* SecondTargetValuePtr = ValuePerAttribute.Map.Find(DoubleComponent->DMXChannel2.Name);
					if (SecondTargetValuePtr)
					{
						float Channel2RemappedValue = DoubleComponent->NormalizedToAbsoluteValue(1, *SecondTargetValuePtr);

						constexpr int32 SecondChannelIndex = 1;
						DoubleComponent->SetTargetValue(SecondChannelIndex, *SecondTargetValuePtr);
					}
				}
				else if(UDMXFixtureComponentColor* ColorComponent = Cast<UDMXFixtureComponentColor>(DMXComponent))
				{
					// Color Component
					if (FLinearColor* CurrentTargetColorPtr = ColorComponent->CurrentTargetColorRef)
					{
						const float* FirstTargetValuePtr = ValuePerAttribute.Map.Find(ColorComponent->DMXChannel1);
						const float* SecondTargetValuePtr = ValuePerAttribute.Map.Find(ColorComponent->DMXChannel2);
						const float* ThirdTargetValuePtr = ValuePerAttribute.Map.Find(ColorComponent->DMXChannel3);
						const float* FourthTargetValuePtr = ValuePerAttribute.Map.Find(ColorComponent->DMXChannel4);

						// Current color if channel not found
						const float r = (FirstTargetValuePtr) ? *FirstTargetValuePtr : CurrentTargetColorPtr->R;
						const float g = (SecondTargetValuePtr) ? *SecondTargetValuePtr : CurrentTargetColorPtr->G;
						const float b = (ThirdTargetValuePtr) ? *ThirdTargetValuePtr : CurrentTargetColorPtr->B;
						const float a = (FourthTargetValuePtr) ? *FourthTargetValuePtr : CurrentTargetColorPtr->A;

						FLinearColor NewTargetColor(r, g, b, a);
						if (ColorComponent->IsColorValid(NewTargetColor))
						{
							ColorComponent->SetTargetColor(NewTargetColor);
						}
					}
				}
			}
		}
	}
}
