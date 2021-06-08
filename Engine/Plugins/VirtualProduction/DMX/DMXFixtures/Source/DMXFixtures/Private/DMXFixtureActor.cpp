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

	// Initialize components
	for (UDMXFixtureComponent* DMXComponent : TInlineComponentArray<UDMXFixtureComponent*>(this))
	{
		DMXComponent->Initialize();
	}

	// Start with default values for all DMX channels
	SetDefaultFixtureState();

	HasBeenInitialized = true;
}

void ADMXFixtureActor::FeedFixtureData()
{
	// Quality level
	float Quality = 0.0f;
	switch (QualityLevel)
	{
		case(EDMXFixtureQualityLevel::LowQuality): Quality = 0.25f; break;
		case(EDMXFixtureQualityLevel::MediumQuality): Quality = 0.5f; break;
		case(EDMXFixtureQualityLevel::HighQuality): Quality = 1.0f; break;
		case(EDMXFixtureQualityLevel::UltraQuality): Quality = 3.0f; break;
		default: Quality = 1.0f;
	}

	if (DynamicMaterialBeam)
	{
		DynamicMaterialBeam->SetScalarParameterValue("DMX Quality Level", Quality);
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
			if (DMXComponent->bIsEnabled && !DMXComponent->bUsingMatrixData)
			{
				if (UDMXFixtureComponentSingle* SingleComponent = Cast<UDMXFixtureComponentSingle>(DMXComponent))
				{
					// SingleChannel Component
					const float* TargetValuePtr = ValuePerAttribute.Map.Find(SingleComponent->DMXChannel.Name);
					if (TargetValuePtr)
					{
						float RemappedValue = SingleComponent->GetInterpolatedValue(*TargetValuePtr);
						 
						if (SingleComponent->IsTargetValid(RemappedValue))
						{
							if (SingleComponent->bUseInterpolation)
							{
								SingleComponent->SetTargetValue(RemappedValue);
							}
							else
							{
								SingleComponent->SetTargetValue(RemappedValue);
								SingleComponent->SetValueNoInterp(RemappedValue);
							}
						}
					}
				}
				else if (UDMXFixtureComponentDouble* DoubleComponent = Cast<UDMXFixtureComponentDouble>(DMXComponent))
				{	
					// DoubleChannel Component
					const float* FirstTargetValuePtr = ValuePerAttribute.Map.Find(DoubleComponent->DMXChannel1.Name);
					if (FirstTargetValuePtr)
					{
						float Channel1RemappedValue = DoubleComponent->GetInterpolatedValue(0, *FirstTargetValuePtr);

						if (DoubleComponent->IsTargetValid(0, Channel1RemappedValue))
						{
							if (DoubleComponent->bUseInterpolation)
							{
								DoubleComponent->SetTargetValue(0, Channel1RemappedValue);
							}
							else
							{
								DoubleComponent->SetTargetValue(0, Channel1RemappedValue);
								DoubleComponent->SetChannel1ValueNoInterp(Channel1RemappedValue);
							}
						}
					}

					const float* SecondTargetValuePtr = ValuePerAttribute.Map.Find(DoubleComponent->DMXChannel2.Name);
					if (SecondTargetValuePtr)
					{
						float Channel2RemappedValue = DoubleComponent->GetInterpolatedValue(1, *SecondTargetValuePtr);

						if (DoubleComponent->IsTargetValid(1, Channel2RemappedValue))
						{
							if (DoubleComponent->bUseInterpolation)
							{
								DoubleComponent->SetTargetValue(1, Channel2RemappedValue);
							}
							else
							{
								DoubleComponent->SetTargetValue(1, Channel2RemappedValue);
								DoubleComponent->SetChannel2ValueNoInterp(Channel2RemappedValue);
							}
						}
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

						// 1.f if channel not found
						const float r = (FirstTargetValuePtr) ? *FirstTargetValuePtr : CurrentTargetColorPtr->R;
						const float g = (SecondTargetValuePtr) ? *SecondTargetValuePtr : CurrentTargetColorPtr->G;
						const float b = (ThirdTargetValuePtr) ? *ThirdTargetValuePtr : CurrentTargetColorPtr->B;
						const float a = (FourthTargetValuePtr) ? *FourthTargetValuePtr : CurrentTargetColorPtr->A;

						FLinearColor NewTargetColor(r, g, b, a);
						if (ColorComponent->IsColorValid(NewTargetColor))
						{
							ColorComponent->SetTargetColor(NewTargetColor);
							ColorComponent->SetColorNoInterp(NewTargetColor);
						}
					}
				}
			}
		}
	}
}

void ADMXFixtureActor::SetDefaultFixtureState()
{
	// Get current components (supports PIE)
	TInlineComponentArray<UDMXFixtureComponent*> DMXComponents;
	GetComponents<UDMXFixtureComponent>(DMXComponents);

	for (auto& DMXComponent : DMXComponents)
	{
		if (UDMXFixtureComponentSingle* SingleComponent = Cast<UDMXFixtureComponentSingle>(DMXComponent))
		{		
			// SingleChannel Component
			float TargetValue = SingleComponent->DMXChannel.DefaultValue;

			SingleComponent->SetTargetValue(TargetValue);
			SingleComponent->SetValueNoInterp(TargetValue);
		}
		else if (UDMXFixtureComponentDouble* DoubleComponent = Cast<UDMXFixtureComponentDouble>(DMXComponent))
		{
			// DoubleChannel Component
			float Channel1TargetValue = DoubleComponent->DMXChannel1.DefaultValue;
			float Channel2TargetValue = DoubleComponent->DMXChannel2.DefaultValue;

			DoubleComponent->SetTargetValue(0, Channel1TargetValue);
			DoubleComponent->SetTargetValue(1, Channel2TargetValue);
		}
		else if (UDMXFixtureComponentColor* ColorComponent = Cast<UDMXFixtureComponentColor>(DMXComponent))
		{
			// Color Component 
			const FLinearColor& DefaultColor = FLinearColor::White;

			ColorComponent->SetTargetColor(DefaultColor);
			ColorComponent->SetColorNoInterp(DefaultColor);
		}
	}
}
