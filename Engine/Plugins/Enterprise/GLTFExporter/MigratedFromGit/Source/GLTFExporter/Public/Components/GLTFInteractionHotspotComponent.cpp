// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/GLTFInteractionHotspotComponent.h"
#include "Animation/SkeletalMeshActor.h"
#include "Animation/AnimSequence.h"
#include "Engine/CollisionProfile.h"
#include "UObject/ConstructorHelpers.h"
#include "PhysicsEngine/BodySetup.h"

UGLTFInteractionHotspotComponent::UGLTFInteractionHotspotComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer),
	ShapeBodySetup(nullptr)
{
	SetSprite(DefaultSprite);

	// Setup the most minimalistic collision profile for mouse input events
	SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	SetCollisionResponseToAllChannels(ECollisionResponse::ECR_Ignore);
	SetCollisionResponseToChannel(ECollisionChannel::ECC_Visibility, ECollisionResponse::ECR_Block);

	bHiddenInGame = false;
	SetGenerateOverlapEvents(false);

	OnBeginCursorOver.AddDynamic(this, &UGLTFInteractionHotspotComponent::BeginCursorOver);
	OnEndCursorOver.AddDynamic(this, &UGLTFInteractionHotspotComponent::EndCursorOver);
	OnClicked.AddDynamic(this, &UGLTFInteractionHotspotComponent::Clicked);
}

void UGLTFInteractionHotspotComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	FProperty* PropertyThatChanged = PropertyChangedEvent.Property;

	if (PropertyThatChanged)
	{
		if (PropertyThatChanged->GetName() == TEXT("DefaultSprite"))
		{
			SetSprite(DefaultSprite);
		}

	}
}

void UGLTFInteractionHotspotComponent::BeginPlay()
{
	Super::BeginPlay();
}

void UGLTFInteractionHotspotComponent::OnRegister()
{
#if 1
	const UStaticMesh* SphereMesh = LoadObject<UStaticMesh>(NULL, TEXT("/Engine/BasicShapes/Sphere.Sphere"), NULL, LOAD_None, NULL);

	if (SphereMesh != nullptr)
	{
		ShapeBodySetup = NewObject<UBodySetup>(this, NAME_None, RF_Transient);
		FTransform BodyTransform = FTransform::Identity;// = GetComponentTransform();
		BodyInstance.InitBody(SphereMesh->BodySetup, BodyTransform, this, GetWorld()->GetPhysicsScene());

		RecreatePhysicsState();
	}
#else
	// TODO: Figure out why this isn't working
	ShapeBodySetup = NewObject<UBodySetup>(this, UBodySetup::StaticClass());
	ShapeBodySetup->AggGeom.SphereElems.Add(FKSphereElem(50.0f));
	BodyInstance.InitBody(ShapeBodySetup, FTransform::Identity, this, GetWorld()->GetPhysicsScene());

	RecreatePhysicsState();
#endif

	Super::OnRegister();
}

void UGLTFInteractionHotspotComponent::BeginCursorOver(UPrimitiveComponent* TouchedComponent)
{
	UE_LOG(LogTemp, Warning, TEXT("%s BeginCursorOver"), *GetOwner()->GetName());

	if (HighlightSprite != nullptr)
	{
		SetSprite(HighlightSprite);
	}
}

void UGLTFInteractionHotspotComponent::EndCursorOver(UPrimitiveComponent* TouchedComponent)
{
	UE_LOG(LogTemp, Warning, TEXT("%s EndCursorOver"), *GetOwner()->GetName());

	SetSprite(DefaultSprite);
}

void UGLTFInteractionHotspotComponent::Clicked(UPrimitiveComponent* TouchedComponent, FKey ButtonPressed)
{
	UE_LOG(LogTemp, Warning, TEXT("UGLTFInteractionHotspotComponent::Clicked()"));

	if (SkeletalMeshActor != nullptr && AnimationSequence != nullptr)
	{
		SkeletalMeshActor->GetSkeletalMeshComponent()->PlayAnimation(AnimationSequence, false);
	}
}
