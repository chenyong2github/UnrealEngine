// Copyright Epic Games, Inc. All Rights Reserved.

#include "ColorCorrectWindow.h"
#include "Components/StaticMeshComponent.h"
#include "CoreMinimal.h"
#include "Engine/Texture2D.h"
#include "UObject/ConstructorHelpers.h"

ENUM_RANGE_BY_COUNT(EColorCorrectWindowType, EColorCorrectWindowType::MAX)

AColorCorrectWindow::AColorCorrectWindow(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, WindowType(EColorCorrectWindowType::Plane)
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
	}
	SetMeshVisibilityForWindowType();
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