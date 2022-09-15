// Copyright Epic Games, Inc. All Rights Reserved.

#include "ColorCorrectWindow.h"
#include "Components/BillboardComponent.h"
#include "CoreMinimal.h"
#include "Materials/Material.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "UObject/ConstructorHelpers.h"

ENUM_RANGE_BY_COUNT(EColorCorrectWindowType, EColorCorrectWindowType::MAX)

AColorCorrectWindow::AColorCorrectWindow(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, WindowType(EColorCorrectWindowType::Square)
{
	UMaterial* Material = LoadObject<UMaterial>(NULL, TEXT("/ColorCorrectRegions/Materials/M_ColorCorrectRegionTransparentPreview.M_ColorCorrectRegionTransparentPreview"), NULL, LOAD_None, NULL);
	const TArray<UStaticMesh*> StaticMeshes =
	{
		Cast<UStaticMesh>(StaticLoadObject(UStaticMesh::StaticClass(), NULL, TEXT("/Engine/BasicShapes/Plane"))),
		Cast<UStaticMesh>(StaticLoadObject(UStaticMesh::StaticClass(), NULL, TEXT("/Engine/BasicShapes/Cylinder")))
	};
	for (EColorCorrectWindowType CCWType : TEnumRange<EColorCorrectWindowType>())
	{
		UStaticMeshComponent* MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(*UEnum::GetValueAsString(CCWType));
		MeshComponents.Add(MeshComponent);
		MeshComponent->SetupAttachment(RootComponent);
		MeshComponent->SetStaticMesh(StaticMeshes[static_cast<uint8>(CCWType)]);
		MeshComponent->SetWorldScale3D(FVector(1., 1., 0.001));
		MeshComponent->SetMaterial(0, Material);
		MeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		MeshComponent->SetCollisionProfileName(TEXT("OverlapAll"));
	}
	SetMeshVisibilityForWindowType();

#if WITH_METADATA
	CreateIcon();
#endif
}

#if WITH_EDITOR
void AColorCorrectWindow::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	const FName PropertyName = PropertyChangedEvent.GetPropertyName();

	if (PropertyName == GET_MEMBER_NAME_CHECKED(AColorCorrectWindow, WindowType))
	{
		SetMeshVisibilityForWindowType();
	}
}
#endif //WITH_EDITOR


#if WITH_METADATA
void AColorCorrectWindow::CreateIcon()
{
	// Create billboard component
	if (GIsEditor && !IsRunningCommandlet())
	{
		// Structure to hold one-time initialization

		struct FConstructorStatics
		{
			ConstructorHelpers::FObjectFinderOptional<UTexture2D> SpriteTextureObject;
			FName ID_ColorCorrectRegion;
			FText NAME_ColorCorrectRegion;

			FConstructorStatics()
				: SpriteTextureObject(TEXT("/ColorCorrectRegions/Icons/S_ColorCorrectWindowIcon"))
				, ID_ColorCorrectRegion(TEXT("Color Correct Window"))
				, NAME_ColorCorrectRegion(NSLOCTEXT("SpriteCategory", "ColorCorrectWindow", "Color Correct Window"))
			{
			}
		};

		static FConstructorStatics ConstructorStatics;

		SpriteComponent = CreateEditorOnlyDefaultSubobject<UBillboardComponent>(TEXT("Color Correct Window Icon"));

		if (SpriteComponent)
		{
			SpriteComponent->Sprite = ConstructorStatics.SpriteTextureObject.Get();
			SpriteComponent->SpriteInfo.Category = ConstructorStatics.ID_ColorCorrectRegion;
			SpriteComponent->SpriteInfo.DisplayName = ConstructorStatics.NAME_ColorCorrectRegion;
			SpriteComponent->SetIsVisualizationComponent(true);
			SpriteComponent->SetRelativeLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);
			SpriteComponent->SetMobility(EComponentMobility::Movable);
			SpriteComponent->bHiddenInGame = true;
			SpriteComponent->bIsScreenSizeScaled = true;

			SpriteComponent->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepRelativeTransform);
		}
	}

}
#endif 

void AColorCorrectWindow::SetMeshVisibilityForWindowType()
{
	for (EColorCorrectWindowType CCWType : TEnumRange<EColorCorrectWindowType>())
	{
		uint8 TypeIndex = static_cast<uint8>(CCWType);

		if (CCWType == WindowType)
		{
			MeshComponents[TypeIndex]->SetVisibility(true, true);
		}
		else
		{
			MeshComponents[TypeIndex]->SetVisibility(false, true);
		}
	}
}