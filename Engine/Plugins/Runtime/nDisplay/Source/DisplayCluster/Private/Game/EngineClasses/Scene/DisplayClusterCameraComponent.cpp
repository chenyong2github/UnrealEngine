// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/DisplayClusterCameraComponent.h"

#include "Components/BillboardComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/Material.h"
#include "UObject/ConstructorHelpers.h"

#include "DisplayClusterConfigurationTypes.h"


UDisplayClusterCameraComponent::UDisplayClusterCameraComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, InterpupillaryDistance(0.064f)
	, bSwapEyes(false)
	, StereoOffset(EDisplayClusterEyeStereoOffset::None)
{
#if WITH_EDITOR
	if (GIsEditor)
	{
		SpriteComponent = CreateEditorOnlyDefaultSubobject<UBillboardComponent>(TEXT("Sprite"));
		if (SpriteComponent)
		{
			ConstructorHelpers::FObjectFinderOptional<UTexture2D> RootTextureObject = TEXT("/nDisplay/Icons/S_nDisplayViewOrigin");

			SpriteComponent->Sprite = RootTextureObject.Get();
			SpriteComponent->SetRelativeScale3D_Direct(FVector(0.5f));
			SpriteComponent->bHiddenInGame = false;
			SpriteComponent->SpriteInfo.Category = TEXT("NDisplayViewOrigin");
			SpriteComponent->SpriteInfo.DisplayName = NSLOCTEXT("DisplayClusterCameraComponent", "NDisplayViewOriginSpriteInfo", "nDisplay View Origin");
			SpriteComponent->bIsScreenSizeScaled = true;
			SpriteComponent->SetIsVisualizationComponent(true);
			SpriteComponent->AttachToComponent(this, FAttachmentTransformRules(EAttachmentRule::KeepRelative, false));
		}
	}
#endif
}

void UDisplayClusterCameraComponent::ApplyConfigurationData()
{
	Super::ApplyConfigurationData();

	const UDisplayClusterConfigurationSceneComponentCamera* CfgCamera = Cast<UDisplayClusterConfigurationSceneComponentCamera>(GetConfigParameters());
	if (CfgCamera)
	{
		InterpupillaryDistance = CfgCamera->InterpupillaryDistance;
		bSwapEyes = CfgCamera->bSwapEyes;

		switch (CfgCamera->StereoOffset)
		{
		case EDisplayClusterConfigurationEyeStereoOffset::Left:
			StereoOffset = EDisplayClusterEyeStereoOffset::Left;
			break;

		case EDisplayClusterConfigurationEyeStereoOffset::None:
			StereoOffset = EDisplayClusterEyeStereoOffset::None;
			break;

		case EDisplayClusterConfigurationEyeStereoOffset::Right:
			StereoOffset = EDisplayClusterEyeStereoOffset::Right;
			break;

		default:
			StereoOffset = EDisplayClusterEyeStereoOffset::None;
			break;
		}
	}
}
