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

	HasBeenInitialized = true;
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
		}

		// Color Component 
		UDMXFixtureComponentColor* ColorComponent = Cast<UDMXFixtureComponentColor>(DMXComponent);
		if (ColorComponent)
		{
			ColorComponent->SetTargetColor(FLinearColor::White);
			ColorComponent->SetComponent(FLinearColor::White);
		}
	}
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

	// Reset initialized and invalid attributes, they might have changed
	InitializedAttributes.Reset();
	InvalidAttributes.Reset();
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

void ADMXFixtureActor::PushNormalizedValuesPerAttribute(const FDMXNormalizedAttributeValueMap& ValuePerAttribute)
{
	SCOPE_CYCLE_COUNTER(STAT_FixtureActorPushNormalizedValuesPerAttribute);

	for (const TPair<FDMXAttributeName, float>& AttributeValueKvp : ValuePerAttribute.Map)
	{
		ReInitalizeAttributeValueNoInterp(AttributeValueKvp.Key, AttributeValueKvp.Value);
	}

	if (HasBeenInitialized)
	{
		for (UDMXFixtureComponent* DMXComponent : TInlineComponentArray<UDMXFixtureComponent*>(this))
		{
			// Components without matrix data
			if (DMXComponent->IsEnabled && !DMXComponent->UsingMatrixData)
			{
				// SingleChannel Component
				UDMXFixtureComponentSingle* SingleComponent = Cast<UDMXFixtureComponentSingle>(DMXComponent);
				if (SingleComponent)
				{
					const float* TargetValuePtr = ValuePerAttribute.Map.Find(SingleComponent->DMXChannel.Name);
					if (TargetValuePtr)
					{
						float RemappedValue = SingleComponent->RemapValue(*TargetValuePtr);
						 
						if (SingleComponent->IsTargetValid(RemappedValue))
						{
							if (SingleComponent->UseInterpolation)
							{
								SingleComponent->Push(RemappedValue);
							}
							else
							{
								SingleComponent->SetTarget(RemappedValue);
								SingleComponent->SetComponent(RemappedValue);
							}
						}
					}
				}

				// DoubleChannel Component
				UDMXFixtureComponentDouble* DoubleComponent = Cast<UDMXFixtureComponentDouble>(DMXComponent);
				if (DoubleComponent)
				{
					const float* FirstTargetValuePtr = ValuePerAttribute.Map.Find(DoubleComponent->DMXChannel1.Name);
					if (FirstTargetValuePtr)
					{
						float Channel1RemappedValue = DoubleComponent->RemapValue(0, *FirstTargetValuePtr);

						if (DoubleComponent->IsTargetValid(0, Channel1RemappedValue))
						{
							if (DoubleComponent->UseInterpolation)
							{
								DoubleComponent->Push(0, Channel1RemappedValue);
							}
							else
							{
								DoubleComponent->SetTarget(0, Channel1RemappedValue);
								DoubleComponent->SetComponentChannel1(Channel1RemappedValue);
							}
						}
					}

					const float* SecondTargetValuePtr = ValuePerAttribute.Map.Find(DoubleComponent->DMXChannel2.Name);
					if (SecondTargetValuePtr)
					{
						float Channel2RemappedValue = DoubleComponent->RemapValue(1, *SecondTargetValuePtr);

						if (DoubleComponent->IsTargetValid(1, Channel2RemappedValue))
						{
							if (DoubleComponent->UseInterpolation)
							{
								DoubleComponent->Push(1, Channel2RemappedValue);
							}
							else
							{
								DoubleComponent->SetTarget(1, Channel2RemappedValue);
								DoubleComponent->SetComponentChannel2(Channel2RemappedValue);
							}
						}
					}
				}
				
				// Color Component
				UDMXFixtureComponentColor* ColorComponent = Cast<UDMXFixtureComponentColor>(DMXComponent);
				if(ColorComponent)
				{
					if (FLinearColor* CurrentTargetColorPtr = ColorComponent->CurrentTargetColorRef)
					{
						const float* FirstTargetValuePtr = ValuePerAttribute.Map.Find(ColorComponent->ChannelName1);
						const float* SecondTargetValuePtr = ValuePerAttribute.Map.Find(ColorComponent->ChannelName2);
						const float* ThirdTargetValuePtr = ValuePerAttribute.Map.Find(ColorComponent->ChannelName3);
						const float* FourthTargetValuePtr = ValuePerAttribute.Map.Find(ColorComponent->ChannelName4);

						// 1.f if channel not found
						const float r = (FirstTargetValuePtr) ? *FirstTargetValuePtr : CurrentTargetColorPtr->R;
						const float g = (SecondTargetValuePtr) ? *SecondTargetValuePtr : CurrentTargetColorPtr->G;
						const float b = (ThirdTargetValuePtr) ? *ThirdTargetValuePtr : CurrentTargetColorPtr->B;
						const float a = (FourthTargetValuePtr) ? *FourthTargetValuePtr : CurrentTargetColorPtr->A;

						FLinearColor NewTargetColor(r, g, b, a);
						if (ColorComponent->IsColorValid(NewTargetColor))
						{
							ColorComponent->SetTargetColor(NewTargetColor);
							ColorComponent->SetComponent(NewTargetColor);
						}
					}
				}
			}
		}
	}
}

void ADMXFixtureActor::ReInitalizeAttributeValueNoInterp(const FDMXAttributeName& AttributeName, float Value)
{
	// Don't need to try and intialized invalid attributes
	if (InitializedAttributes.Contains(AttributeName) || InvalidAttributes.Contains(AttributeName))
	{
		return;
	}

	bool bInitializedAttribute = false;
	for (UDMXFixtureComponent* DMXComponent : TInlineComponentArray<UDMXFixtureComponent*>(this))
	{
		// Components without matrix data
		if (DMXComponent->IsEnabled && !DMXComponent->UsingMatrixData)
		{
			// SingleChannel Component
			UDMXFixtureComponentSingle* SingleComponent = Cast<UDMXFixtureComponentSingle>(DMXComponent);
			if (SingleComponent && SingleComponent->DMXChannel.Name == AttributeName)
			{
				float RemappedValue = SingleComponent->RemapValue(Value);

				if (SingleComponent->IsTargetValid(RemappedValue))
				{
					SingleComponent->SetTarget(RemappedValue);
					SingleComponent->SetComponent(RemappedValue);

					bInitializedAttribute = true;
				}
			}

			// DoubleChannel Component
			UDMXFixtureComponentDouble* DoubleComponent = Cast<UDMXFixtureComponentDouble>(DMXComponent);
			if (DoubleComponent)
			{
				if (DoubleComponent->DMXChannel1.Name == AttributeName)
				{
					float Channel1RemappedValue = DoubleComponent->RemapValue(0, Value);
					DoubleComponent->SetTarget(0, Channel1RemappedValue);
					DoubleComponent->SetComponentChannel1(Channel1RemappedValue);

					bInitializedAttribute = true;
				}
				else if (DoubleComponent->DMXChannel2.Name == AttributeName)
				{
					float Channel2RemappedValue = DoubleComponent->RemapValue(1, Value);
					DoubleComponent->SetTarget(1, Channel2RemappedValue);
					DoubleComponent->SetComponentChannel2(Channel2RemappedValue);

					bInitializedAttribute = true;
				}
			}

			// Color Component
			UDMXFixtureComponentColor* ColorComponent = Cast<UDMXFixtureComponentColor>(DMXComponent);
			if (ColorComponent)
			{
				if (ColorComponent->ChannelName1 == AttributeName)
				{
					FLinearColor* TargetColorPtr = ColorComponent->CurrentTargetColorRef;
					(*TargetColorPtr).R = Value;

					bInitializedAttribute = true;
				}
				else if (ColorComponent->ChannelName2 == AttributeName)
				{
					FLinearColor* TargetColorPtr = ColorComponent->CurrentTargetColorRef;
					(*TargetColorPtr).G = Value;

					bInitializedAttribute = true;
				}
				else if (ColorComponent->ChannelName3 == AttributeName)
				{
					FLinearColor* TargetColorPtr = ColorComponent->CurrentTargetColorRef;
					(*TargetColorPtr).B = Value;

					bInitializedAttribute = true;
				}
				else if (ColorComponent->ChannelName4 == AttributeName)
				{
					FLinearColor* TargetColorPtr = ColorComponent->CurrentTargetColorRef;
					(*TargetColorPtr).A = Value;

					bInitializedAttribute = true;
				}
			}
		}
	}

	if (bInitializedAttribute)
	{
		InitializedAttributes.Add(AttributeName);
	}
	else
	{
		InvalidAttributes.Add(AttributeName);
	}
}

void ADMXFixtureActor::PushDMXData(TMap<FDMXAttributeName, int32> AttributesMap)
{
	ensureMsgf(0, TEXT("PushDMXData is no longer supported. Use PushDMXValuesPerAttribute instead"));
}
