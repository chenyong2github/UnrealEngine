// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/Viewport/DisplayClusterConfiguratorScreenActor.h"

#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"

#include "UObject/ConstructorHelpers.h"


ADisplayClusterConfiguratorScreenActor::ADisplayClusterConfiguratorScreenActor()
	: bSelected(false)
{
	static ConstructorHelpers::FObjectFinder<UMaterial> TestMat(TEXT("Material'/Engine/EngineMaterials/WorldGridMaterial.WorldGridMaterial'"));
	check(TestMat.Object);
	TempMaterial = TestMat.Object;
}

void ADisplayClusterConfiguratorScreenActor::AddComponents()
{
	UStaticMesh* PlaneMesh = LoadObject<UStaticMesh>(NULL, TEXT("/Engine/BasicShapes/Plane"), NULL, LOAD_None, NULL);

#if 0
	UMaterial* PreviewMaterial = CreateMateral("Material'/Engine/EngineMaterials/WorldGridMaterial.WorldGridMaterial'  /Game/Dennis/PreviewMaterial");
#else
	UMaterial* PreviewMaterial = TempMaterial;
#endif

	USceneComponent* NewSceneComp = NewObject<USceneComponent>(this, USceneComponent::StaticClass(), "NewSceneComp", RF_NoFlags);
	RootMeshComponent = NewObject<UStaticMeshComponent>(this, UStaticMeshComponent::StaticClass(), "RootMeshComponent", RF_NoFlags);
	MaterialInstance = NewObject<UMaterialInstanceConstant>(this, NAME_None, EObjectFlags::RF_Transient);

	MaterialInstance->Parent = PreviewMaterial;
	MaterialInstance->PostLoad();

	RootComponent = NewSceneComp;
	RootMeshComponent->SetupAttachment(NewSceneComp);
	RootMeshComponent->SetStaticMesh(PlaneMesh);
	RootMeshComponent->SetMaterial(0, MaterialInstance);
	RootMeshComponent->SetRelativeScale3D(FVector(1.f / 100.f));

	RegisterAllComponents();
}

void ADisplayClusterConfiguratorScreenActor::SetColor(const FColor& Color)
{
	MaterialInstance->SetVectorParameterValueEditorOnly(TEXT("Color"), Color);
}

void ADisplayClusterConfiguratorScreenActor::SetNodeSelection(bool bSelect)
{
	if (bSelect)
	{
		if (bSelected == false)
		{
			bSelected = true;
			OnSelection();
		}
	}
	else
	{
		bSelected = false;
	}

	RootMeshComponent->bDisplayVertexColors = bSelect;
	RootMeshComponent->PushSelectionToProxy();
}
