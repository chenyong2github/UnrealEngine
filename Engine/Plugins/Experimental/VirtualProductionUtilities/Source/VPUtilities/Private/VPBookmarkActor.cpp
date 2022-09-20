// Copyright Epic Games, Inc. All Rights Reserved.


#include "VPBookmarkActor.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SplineMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "VPUtilitiesModule.h"
#include "VPBookmarkBlueprintLibrary.h"
#include "VPBlueprintLibrary.h"
#include "VPBookmark.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/Material.h"
#include "VPSettings.h"
#include "UObject/UObjectGlobals.h"


AVPBookmarkActor::AVPBookmarkActor(const FObjectInitializer& ObjectInitializer)
	:Super(ObjectInitializer)
	{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	//Root Component

	BookmarkMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BookmarkMesh"));
	SetRootComponent(BookmarkMesh);

	FSoftObjectPath BookmarkMeshAssetPath = GetDefault<UVPBookmarkSettings>()->BookmarkMeshPath;

	UStaticMesh* BookmarkStaticMesh = LoadObject<UStaticMesh>(nullptr, *BookmarkMeshAssetPath.ToString(), nullptr, LOAD_None, nullptr);
	
	BookmarkMesh->SetStaticMesh(BookmarkStaticMesh);

	FSoftObjectPath BookmarkMaterialAssetPath = GetDefault<UVPBookmarkSettings>()->BookmarkMaterialPath;

	UMaterialInterface* BookmarkMaterialInstance = LoadObject<UMaterialInterface>(nullptr, *BookmarkMaterialAssetPath.ToString(), nullptr, LOAD_None, nullptr);

	BookmarkMaterial = BookmarkMaterialInstance;
		
	BookmarkColor = FLinearColor(0.817708f, 0.107659f, 0.230336f);

	//SplineMesh
	SplineMesh = CreateDefaultSubobject<USplineMeshComponent>(TEXT("SplineMesh"));
	SplineMesh->SetMobility(EComponentMobility::Movable);
	SplineMesh->SetupAttachment(BookmarkMesh);
	SplineMesh->SetVisibility(false);

	FSoftObjectPath SplineMeshAssetPath = GetDefault<UVPBookmarkSettings>()->BookmarkSplineMeshPath;

	UStaticMesh* SplineStaticMesh = LoadObject<UStaticMesh>(nullptr, *SplineMeshAssetPath.ToString(), nullptr, LOAD_None, nullptr);

	SplineMesh->SetStaticMesh(SplineStaticMesh);

	FSoftObjectPath SplineMaterialPath = GetDefault<UVPBookmarkSettings>()->BookmarkSplineMeshMaterialPath;

	UMaterialInterface* SplineMaterialInstance = LoadObject<UMaterialInterface>(nullptr, *SplineMaterialPath.ToString(), nullptr, LOAD_None, nullptr);

	SplineMesh->SetMaterial(0, SplineMaterialInstance);
	
	//Text Render
	NameTextRender = CreateDefaultSubobject<UTextRenderComponent>(TEXT("NameTextRender"));
	NameTextRender->SetupAttachment(BookmarkMesh);
	NameTextRender->SetWorldSize(36);
	NameTextRender->AddRelativeLocation(FVector(0.f, 0.f, 70.f));
	NameTextRender->HorizontalAlignment = EHorizTextAligment(EHTA_Center);

	FSoftObjectPath LabelMaterialPath = GetDefault<UVPBookmarkSettings>()->BookmarkLabelMaterialPath;

	UMaterialInterface* LabelMaterialInstance = LoadObject<UMaterialInterface>(nullptr, *LabelMaterialPath.ToString(), nullptr, LOAD_None, nullptr);

	NameTextRender->SetMaterial(0, LabelMaterialInstance);

	CameraComponent = CreateDefaultSubobject<UCineCameraComponent>(TEXT("Camera"));
	CameraComponent->SetupAttachment(BookmarkMesh);

}

void AVPBookmarkActor::Tick(float DeltaSeconds)
	{
	Super::Tick(DeltaSeconds);


	NameTextRender->SetWorldRotation(FRotator(0.f, 0.f, 0.f));

	BookmarkRotation = GetActorRotation();
	BookmarkMesh->SetWorldRotation(FRotator(0.f,BookmarkRotation.Yaw,0.f));


#if WITH_EDITOR
	if (GIsEditor)
	{
	FEditorScriptExecutionGuard ScriptGuard;
	EditorTick(DeltaSeconds);
	}
#endif
}

//VP Interaction InterfaceEvents

void AVPBookmarkActor::OnBookmarkActivation_Implementation(UVPBookmark* BookmarkOut, bool bIsActive)
{
	UE_LOG(LogVPUtilities, Display, TEXT("Bookmark Created"));
}


void AVPBookmarkActor::OnBookmarkChanged_Implementation(UVPBookmark* BookmarkOut)
{
	AActor* BookmarkActor = BookmarkOut->GetAssociatedBookmarkActor();
	IVPBookmarkProvider* BookmarkInterface = Cast<IVPBookmarkProvider>(BookmarkActor);
	if (BookmarkInterface)
	{
		BookmarkInterface->Execute_GenerateBookmarkName(BookmarkActor);
	}
	BookmarkObject = BookmarkOut;
	UE_LOG(LogVPUtilities, Display, TEXT("Bookmark Updated"));
}


void AVPBookmarkActor::UpdateBookmarkSplineMeshIndicator_Implementation()
{
	UVPBlueprintLibrary::VPBookmarkSplineMeshIndicatorSetStartAndEnd(SplineMesh);
}


void AVPBookmarkActor::HideBookmarkSplineMeshIndicator_Implementation()
{
	UVPBlueprintLibrary::VPBookmarkSplineMeshIndicatorDisable(SplineMesh);
}


void AVPBookmarkActor::GenerateBookmarkName_Implementation()
{
	FString GeneratedNumber;
	FString GeneratedLetter;
	UVPBookmarkBlueprintLibrary::CreateVPBookmarkName(this, FString("Bookmark %n"), GeneratedNumber, GeneratedLetter);
		
	NameTextRender->SetText(FText::AsCultureInvariant(GeneratedNumber));

}


//VP Interaction Interface Events

void AVPBookmarkActor::OnActorDroppedFromCarry_Implementation()
{
	UE_LOG(LogVPUtilities, Display, TEXT("Bookmark %s dropped from carry by VR Interactor"), *this->GetName());
}

void AVPBookmarkActor::OnActorSelectedForTransform_Implementation()
{
	UE_LOG(LogVPUtilities, Display, TEXT("Bookmark %s selected by VR Interactor"), *this->GetName());
}

void AVPBookmarkActor::OnActorDroppedFromTransform_Implementation()
{
	UE_LOG(LogVPUtilities, Display, TEXT("Bookmark %s dropped from transform dragging by VR Interactor"), *this->GetName());
}


void AVPBookmarkActor::UpdateBookmarkColor(FLinearColor Color)
{
	if (BookmarkMesh->GetStaticMesh() != nullptr)
	{
		UMaterialInterface* Material = BookmarkMesh->GetMaterial(0);

		if (BookmarkMaterial != nullptr)
		{
			if (Material && !Material->IsA<UMaterialInstanceDynamic>())
			{
				UMaterialInstanceDynamic* BookmarkMaterialInstance = UMaterialInstanceDynamic::Create(Material, BookmarkMaterial, TEXT("BookmarkMaterial"));

				BookmarkMaterialInstance->ClearParameterValues();
				BookmarkMaterialInstance->SetVectorParameterValue(TEXT("UserColor"), Color);

				for (int32 i = 0; i < BookmarkMesh->GetNumMaterials(); i++)
				{
					BookmarkMesh->SetMaterial(i, BookmarkMaterialInstance);
				}
			}
			else //If DMIs are already setup set the color value
			{
				DynamicMaterial = Cast<UMaterialInstanceDynamic>(Material);
				DynamicMaterial->SetVectorParameterValue(TEXT("UserColor"), Color);
			}
		}
	}
}
#if WITH_EDITOR
void AVPBookmarkActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(AVPBookmarkActor, BookmarkColor))
	{
		AVPBookmarkActor::UpdateBookmarkColor(BookmarkColor);
	}
}
#endif

void AVPBookmarkActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	AVPBookmarkActor::UpdateBookmarkColor(BookmarkColor);
}