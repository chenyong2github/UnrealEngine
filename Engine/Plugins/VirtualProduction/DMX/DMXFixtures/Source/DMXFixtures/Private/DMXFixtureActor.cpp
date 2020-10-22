// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXFixtureActor.h"
#include "Components/StaticMeshComponent.h"

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
	ForceInitialFixtureState = false;
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

	// Get lens width
	if (StaticMeshLens)
	{
		FBoxSphereBounds Bounds = StaticMeshLens->Bounds;
		LensRadius = Bounds.SphereRadius;
	}

	// Feed fixture data into materials and lights
	FeedFixtureData();

	// Assign dynamic materials to static meshes
	if (StaticMeshLens)
	{
		StaticMeshLens->SetMaterial(0, DynamicMaterialLens);
	}

	if (StaticMeshBeam)
	{
		StaticMeshBeam->SetMaterial(0, DynamicMaterialBeam);
	}

	// Assign dynamic materials to lights
	SpotLight->SetMaterial(0, DynamicMaterialSpotLight);
	PointLight->SetMaterial(0, DynamicMaterialPointLight);

	// Initialize components
	TInlineComponentArray<UDMXFixtureComponent*> DMXComponents;
	GetComponents<UDMXFixtureComponent>(DMXComponents);
	for (auto& DMXComponent : DMXComponents)
	{
		DMXComponent->Initialize();
	}

	// Start with default values for all DMX channels
	SetDefaultFixtureState();
}

void ADMXFixtureActor::SetDefaultFixtureState()
{
	// Get current components (supports PIE)
	TInlineComponentArray<UDMXFixtureComponent*> DMXComponents;
	GetComponents<UDMXFixtureComponent>(DMXComponents);

	for (auto& DMXComponent : DMXComponents)
	{
		// SingleChannel Component
		UDMXFixtureComponentSingle* SingleComponent = Cast<UDMXFixtureComponentSingle>(DMXComponent);
		if (SingleComponent)
		{
			float TargetValue = SingleComponent->DMXChannel.DefaultValue;
			SingleComponent->SetTarget(TargetValue);
			SingleComponent->SetComponent(TargetValue);
		}

		// DoubleChannel Component
		UDMXFixtureComponentDouble* DoubleComponent = Cast<UDMXFixtureComponentDouble>(DMXComponent);
		if (DoubleComponent)
		{
			float Channel1TargetValue = DoubleComponent->DMXChannel1.DefaultValue;
			float Channel2TargetValue = DoubleComponent->DMXChannel2.DefaultValue;
			DoubleComponent->SetTarget(0, Channel1TargetValue);
			DoubleComponent->SetTarget(1, Channel2TargetValue);
			DoubleComponent->SetComponent(Channel1TargetValue, Channel2TargetValue);
		}

		// Color Component 
		UDMXFixtureComponentColor* ColorComponent = Cast<UDMXFixtureComponentColor>(DMXComponent);
		if (ColorComponent)
		{
			ColorComponent->SetTargetColor(FLinearColor::White);
			ColorComponent->SetComponent(FLinearColor::White);
		}
	}

	HasBeenInitialized = true;
	ForceInitialFixtureState = true;
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
		if (DMXComponent->IsEnabled && DMXComponent->UseInterpolation && DMXComponent->IsRegistered())
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


bool IsAttributesMapValid(TMap<FDMXAttributeName, int32> AttributesMap)
{
	for (const TPair<FDMXAttributeName, int32>& pair : AttributesMap)
	{
		if (pair.Value != 0)
		{
			return true;
		}
	}
	return false;
}

void ADMXFixtureActor::PushDMXData(TMap<FDMXAttributeName, int32> AttributesMap)
{
	// Avoid attributes map containing all zeros at startup
	bool IsWarmedUp = true;
	if (ForceInitialFixtureState)
	{
		IsWarmedUp = IsAttributesMapValid(AttributesMap);
	}

	if (HasBeenInitialized && IsWarmedUp)
	{
		// Get current components (supports PIE)
		TInlineComponentArray<UDMXFixtureComponent*> DMXComponents;
		GetComponents<UDMXFixtureComponent>(DMXComponents);

		for (auto& DMXComponent : DMXComponents)
		{
			// Components without matrix data
			if (DMXComponent->IsEnabled && !DMXComponent->UsingMatrixData)
			{
				// SingleChannel Component
				UDMXFixtureComponentSingle* SingleComponent = Cast<UDMXFixtureComponentSingle>(DMXComponent);
				if (SingleComponent)
				{
					int* d1 = AttributesMap.Find(SingleComponent->DMXChannel.Name);
					if (d1)
					{
						float TargetValue = SingleComponent->RemapValue(*d1);
						if (SingleComponent->IsTargetValid(TargetValue))
						{
							if (SingleComponent->UseInterpolation && !ForceInitialFixtureState)
							{
								SingleComponent->Push(TargetValue);
							}
							else
							{
								SingleComponent->SetTarget(TargetValue);
								SingleComponent->SetComponent(TargetValue);
							}
						}
					}
				}

				// DoubleChannel Component
				UDMXFixtureComponentDouble* DoubleComponent = Cast<UDMXFixtureComponentDouble>(DMXComponent);
				if (DoubleComponent)
				{
					int* d1 = AttributesMap.Find(DoubleComponent->DMXChannel1.Name);
					int* d2 = AttributesMap.Find(DoubleComponent->DMXChannel2.Name);
					if (d1 && d2)
					{
						float Channel1TargetValue = DoubleComponent->RemapValue(0, *d1);
						float Channel2TargetValue = DoubleComponent->RemapValue(1, *d2);
						if (DoubleComponent->IsTargetValid(0, Channel1TargetValue) && DoubleComponent->IsTargetValid(1, Channel2TargetValue))
						{
							if (DoubleComponent->UseInterpolation && !ForceInitialFixtureState)
							{
								DoubleComponent->Push(0, Channel1TargetValue);
								DoubleComponent->Push(1, Channel2TargetValue);
							}
							else
							{
								DoubleComponent->SetTarget(0, Channel1TargetValue);
								DoubleComponent->SetTarget(1, Channel2TargetValue);
								DoubleComponent->SetComponent(Channel1TargetValue, Channel2TargetValue);
							}
						}
					}
				}
				
				// Color Component
				UDMXFixtureComponentColor* ColorComponent = Cast<UDMXFixtureComponentColor>(DMXComponent);
				if(ColorComponent)
				{
					int* d1 = AttributesMap.Find(ColorComponent->ChannelName1);
					int* d2 = AttributesMap.Find(ColorComponent->ChannelName2);
					int* d3 = AttributesMap.Find(ColorComponent->ChannelName3);
					int* d4 = AttributesMap.Find(ColorComponent->ChannelName4);

					// 255 if channel not found
					int r = (d1) ? *d1 : ColorComponent->BitResolution;
					int g = (d2) ? *d2 : ColorComponent->BitResolution;
					int b = (d3) ? *d3 : ColorComponent->BitResolution;
					int a = (d4) ? *d4 : ColorComponent->BitResolution;

					FLinearColor NewTargetColor = ColorComponent->RemapColor(r, g, b, a);
					if (ColorComponent->IsColorValid(NewTargetColor))
					{
						ColorComponent->SetTargetColor(NewTargetColor);
						ColorComponent->SetComponent(NewTargetColor);
					}
				}
			}
		}

		ForceInitialFixtureState = false;
	}
}

