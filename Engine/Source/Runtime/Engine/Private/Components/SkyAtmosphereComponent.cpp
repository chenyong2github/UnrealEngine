// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Components/SkyAtmosphereComponent.h"

#include "Atmosphere/AtmosphericFogComponent.h"
#include "Components/ArrowComponent.h"
#include "Components/BillboardComponent.h"
#include "Engine/MapBuildDataRegistry.h"
#include "Internationalization/Text.h"
#include "Logging/MessageLog.h"
#include "Logging/TokenizedMessage.h"
#include "Misc/MapErrors.h"
#include "Misc/UObjectToken.h"
#include "UObject/UObjectIterator.h"
#include "UObject/ConstructorHelpers.h"

#if WITH_EDITOR
#include "ObjectEditorUtils.h"
#endif

#define LOCTEXT_NAMESPACE "SkyAtmosphereComponent"

//#pragma optimize( "", off )



/*=============================================================================
	USkyAtmosphereComponent implementation.
=============================================================================*/

USkyAtmosphereComponent::USkyAtmosphereComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// All distance here are in kilometer and scattering/absorptions coefficient in 1/kilometers.
	const float EarthBottomRadius = 6360.0f;
	const float EarthTopRadius = 6420.0f;
	const float EarthRayleighScaleHeight = 8.0f;
	const float EarthMieScaleHeight = 1.2f;
	
	// Default: Earth like atmosphere
	BottomRadius = EarthBottomRadius;
	AtmosphereHeight = EarthTopRadius - EarthBottomRadius;
	GroundAlbedo = FColor(0.0f, 0.0f, 0.0f);

	// FLoat to a u8 rgb + float length can lose some precision but it is better UI wise.
	const FLinearColor RayleightScatteringRaw = FLinearColor(0.005802f, 0.013558f, 0.033100f);
	RayleighScattering = (RayleightScatteringRaw * (1.0f / RayleightScatteringRaw.B)).ToFColor(false);
	RayleighScatteringScale = RayleightScatteringRaw.B;
	RayleighExponentialDistribution = EarthRayleighScaleHeight;

	MieScattering = FColor(FColor::White);
	MieScatteringScale = 0.003996f;
	MieAbsorption = FColor(FColor::White);
	MieAbsorptionScale = 0.000444f;
	MieAnisotropy = 0.8f;
	MieExponentialDistribution = EarthMieScaleHeight;

	// Absorption tent distribution representing ozone distribution in Earth atmosphere.
	const FLinearColor OtherAbsorptionRaw = FLinearColor(0.000650f, 0.001881f, 0.000085f);
	OtherAbsorptionScale = OtherAbsorptionRaw.G;
	OtherAbsorption = (OtherAbsorptionRaw * (1.0f / OtherAbsorptionRaw.G)).ToFColor(false);
	OtherTentDistribution.TipAltitude = 25.0f;
	OtherTentDistribution.TipValue    =  1.0f;
	OtherTentDistribution.Width       = 15.0f;

	SkyLuminanceFactor = FLinearColor(FLinearColor::White);
	MultiScatteringFactor = 1.0f;
	AerialPespectiveViewDistanceScale = 1.0f;

	ValidateStaticLightingGUIDs();
}

USkyAtmosphereComponent::~USkyAtmosphereComponent()
{
}

static bool SkyAtmosphereComponentStaticLightingBuilt(const USkyAtmosphereComponent* Component)
{
	UMapBuildDataRegistry* Registry = Component->GetOwner() && Component->GetOwner()->GetLevel() ? Component->GetOwner()->GetLevel()->GetOrCreateMapBuildData() : nullptr;
	const FSkyAtmosphereMapBuildData* SkyAtmosphereFogBuildData = Registry ? Registry->GetSkyAtmosphereBuildData(Component->GetStaticLightingBuiltGuid()) : nullptr;
	class FSceneInterface* Scene = Component->GetWorld()->Scene;

	// Only require building if there is a Sky or Sun light requiring lighting builds, i.e. non movable.
	const bool StaticLightingDependsOnAtmosphere = Scene->HasSkyLightRequiringLightingBuild() || Scene->HasAtmosphereLightRequiringLightingBuild();
	// Built data is available or static lighting does not depend any sun/sky components.
	return (SkyAtmosphereFogBuildData != nullptr && StaticLightingDependsOnAtmosphere) || !StaticLightingDependsOnAtmosphere;
}

void USkyAtmosphereComponent::AddToRenderScene() const
{
	if (ShouldComponentAddToScene() && ShouldRender() && IsRegistered() && (GetOuter() == NULL || !GetOuter()->HasAnyFlags(RF_ClassDefaultObject)))
	{
		GetWorld()->Scene->AddSkyAtmosphere(this, SkyAtmosphereComponentStaticLightingBuilt(this));
	}
}

void USkyAtmosphereComponent::CreateRenderState_Concurrent()
{
	Super::CreateRenderState_Concurrent();
	// If one day we need to look up lightmass built data, lookup it up here using the guid from the correct MapBuildData.
	AddToRenderScene();
}

void USkyAtmosphereComponent::DestroyRenderState_Concurrent()
{
	Super::DestroyRenderState_Concurrent();
	GetWorld()->Scene->RemoveSkyAtmosphere(this);

	// Search for new fog component
	for (TObjectIterator<USkyAtmosphereComponent> It; It; ++It)
	{
		USkyAtmosphereComponent* Component = *It;
		checkSlow(Component);
		if (Component != this && Component->IsRegistered())
		{
			Component->AddToRenderScene();
			break;
		}
	}
}

void USkyAtmosphereComponent::ValidateStaticLightingGUIDs()
{
	// Validate light guids.
	if (!bStaticLightingBuiltGUID.IsValid())
	{
		UpdateStaticLightingGUIDs();
	}
}

void USkyAtmosphereComponent::UpdateStaticLightingGUIDs()
{
	bStaticLightingBuiltGUID = FGuid::NewGuid();
}

#if WITH_EDITOR

void USkyAtmosphereComponent::CheckForErrors()
{
	AActor* Owner = GetOwner();
	if (Owner && bVisible)
	{
		UWorld* ThisWorld = Owner->GetWorld();
		bool bMultipleFound = false;
		bool bLegacyAtmosphericFogFound = false;

		if (ThisWorld)
		{
			for (TObjectIterator<USkyAtmosphereComponent> ComponentIt; ComponentIt; ++ComponentIt)
			{
				USkyAtmosphereComponent* Component = *ComponentIt;

				if (Component != this
					&& !Component->IsPendingKill()
					&& Component->bVisible
					&& Component->GetOwner()
					&& ThisWorld->ContainsActor(Component->GetOwner())
					&& !Component->GetOwner()->IsPendingKill())
				{
					bMultipleFound = true;
					break;
				}
			}
			for (TObjectIterator<UAtmosphericFogComponent> ComponentIt; ComponentIt; ++ComponentIt)
			{
				UAtmosphericFogComponent* Component = *ComponentIt;

				if (!Component->IsPendingKill()
					&& Component->bVisible
					&& Component->GetOwner()
					&& ThisWorld->ContainsActor(Component->GetOwner())
					&& !Component->GetOwner()->IsPendingKill())
				{
					bLegacyAtmosphericFogFound = true;
					break;
				}
			}
		}

		if (bMultipleFound)
		{
			FMessageLog("MapCheck").Error()
				->AddToken(FUObjectToken::Create(Owner))
				->AddToken(FTextToken::Create(LOCTEXT("MapCheck_Message_MultipleSkyAtmosphere", "Multiple sky atmosphere are active, only one can be enabled per world.")))
				->AddToken(FMapErrorToken::Create(FMapErrors::MultipleSkyAtmospheres));
		}
		if (bLegacyAtmosphericFogFound)
		{
			FMessageLog("MapCheck").Error()
				->AddToken(FUObjectToken::Create(Owner))
				->AddToken(FTextToken::Create(LOCTEXT("MapCheck_Message_MultipleSkyAtmosphereType", "A SkyAtmosphere and a legacy AtmosphericFog components are both active, we recommend to have only one enabled per world.")))
				->AddToken(FMapErrorToken::Create(FMapErrors::MultipleSkyAtmosphereTypes));
		}
	}
}

void USkyAtmosphereComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// If any properties have been changed in the atmosphere category, it means the sky look will change and lighting needs to be rebuild.
	const FName CategoryName = FObjectEditorUtils::GetCategoryFName(PropertyChangedEvent.Property);
	if (CategoryName == FName(TEXT("Planet")) ||
		CategoryName == FName(TEXT("Atmosphere")) ||
		CategoryName == FName(TEXT("Atmosphere - Raleigh")) ||
		CategoryName == FName(TEXT("Atmosphere - Mie")) ||
		CategoryName == FName(TEXT("Atmosphere - Absorption")) ||
		CategoryName == FName(TEXT("Art direction")))
	{
		UMapBuildDataRegistry* Registry = GetOwner() && GetOwner()->GetLevel() ? GetOwner()->GetLevel()->GetOrCreateMapBuildData() : nullptr;
		if (SkyAtmosphereComponentStaticLightingBuilt(this))
		{
			// If we have changed an atmosphere property and the lighyting has already been built, we need to ask for a rebuild by updating the static lighting GUIDs.
			UpdateStaticLightingGUIDs();
		}
	}
}

#endif // WITH_EDITOR

void USkyAtmosphereComponent::PostInterpChange(UProperty* PropertyThatChanged)
{
	Super::PostInterpChange(PropertyThatChanged);
	MarkRenderStateDirty();
}

void USkyAtmosphereComponent::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
	Ar << bStaticLightingBuiltGUID;
}


/*=============================================================================
	ASkyAtmosphere implementation.
=============================================================================*/

#if WITH_EDITOR
#include "ObjectEditorUtils.h"
#endif

ASkyAtmosphere::ASkyAtmosphere(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SkyAtmosphereComponent = CreateDefaultSubobject<USkyAtmosphereComponent>(TEXT("SkyAtmosphereComponent"));
	RootComponent = SkyAtmosphereComponent;

#if WITH_EDITORONLY_DATA
	ArrowComponent = CreateEditorOnlyDefaultSubobject<UArrowComponent>(TEXT("ArrowComponent"));

	if (!IsRunningCommandlet())
	{
		// Structure to hold one-time initialization
		struct FConstructorStatics
		{
			ConstructorHelpers::FObjectFinderOptional<UTexture2D> SkyAtmosphereTextureObject;
			FName ID_SkyAtmosphere;
			FText NAME_SkyAtmosphere;
			FConstructorStatics()
				: SkyAtmosphereTextureObject(TEXT("/Engine/EditorResources/S_ExpoHeightFog"))
				, ID_SkyAtmosphere(TEXT("Fog"))
				, NAME_SkyAtmosphere(NSLOCTEXT("SpriteCategory", "Fog", "Fog"))
			{
			}
		};
		static FConstructorStatics ConstructorStatics;

		if (GetSpriteComponent())
		{
			GetSpriteComponent()->Sprite = ConstructorStatics.SkyAtmosphereTextureObject.Get();
			GetSpriteComponent()->RelativeScale3D = FVector(0.5f, 0.5f, 0.5f);
			GetSpriteComponent()->SpriteInfo.Category = ConstructorStatics.ID_SkyAtmosphere;
			GetSpriteComponent()->SpriteInfo.DisplayName = ConstructorStatics.NAME_SkyAtmosphere;
			GetSpriteComponent()->SetupAttachment(SkyAtmosphereComponent);
		}

		if (ArrowComponent)
		{
			ArrowComponent->ArrowColor = FColor(150, 200, 255);

			ArrowComponent->bTreatAsASprite = true;
			ArrowComponent->SpriteInfo.Category = ConstructorStatics.ID_SkyAtmosphere;
			ArrowComponent->SpriteInfo.DisplayName = ConstructorStatics.NAME_SkyAtmosphere;
			ArrowComponent->SetupAttachment(SkyAtmosphereComponent);
			ArrowComponent->bLightAttachment = true;
			ArrowComponent->bIsScreenSizeScaled = true;
		}
	}
#endif // WITH_EDITORONLY_DATA

	PrimaryActorTick.bCanEverTick = true;
	bHidden = false;
}

#undef LOCTEXT_NAMESPACE


