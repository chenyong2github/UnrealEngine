// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/GLTFInteractionHotspotComponent.h"
#include "Animation/SkeletalMeshActor.h"
#include "Animation/AnimSequence.h"
#include "Engine/CollisionProfile.h"
#include "UObject/ConstructorHelpers.h"
#include "PhysicsEngine/BodySetup.h"

UGLTFInteractionHotspotComponent::UGLTFInteractionHotspotComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer),
	SkeletalMeshActor(nullptr),
	AnimationSequence(nullptr),
	DefaultSprite(nullptr),
	HighlightSprite(nullptr),
	ClickSprite(nullptr),
	Radius(50.0f)
{
	// Setup the most minimalistic collision profile for mouse input events
	SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	SetCollisionResponseToAllChannels(ECollisionResponse::ECR_Ignore);
	SetCollisionResponseToChannel(ECollisionChannel::ECC_Visibility, ECollisionResponse::ECR_Block);
	SetGenerateOverlapEvents(false);

	bHiddenInGame = false;

	OnBeginCursorOver.AddDynamic(this, &UGLTFInteractionHotspotComponent::BeginCursorOver);
	OnEndCursorOver.AddDynamic(this, &UGLTFInteractionHotspotComponent::EndCursorOver);
	OnClicked.AddDynamic(this, &UGLTFInteractionHotspotComponent::Clicked);
}

void UGLTFInteractionHotspotComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	const FProperty* PropertyThatChanged = PropertyChangedEvent.Property;

	if (PropertyThatChanged)
	{
		const FString PropertyName = PropertyThatChanged->GetName();

		if (PropertyName == TEXT("DefaultSprite"))
		{
			SetSprite(DefaultSprite);
		}
		else if (PropertyName == TEXT("Radius"))
		{
			SetRadius(Radius);
		}
	}
}

void UGLTFInteractionHotspotComponent::BeginPlay()
{
	Super::BeginPlay();

	SetSprite(DefaultSprite);
}

void UGLTFInteractionHotspotComponent::OnRegister()
{
	ShapeBodySetup = NewObject<UBodySetup>(this);
	ShapeBodySetup->CollisionTraceFlag = CTF_UseSimpleAsComplex;
	ShapeBodySetup->AggGeom.SphereElems.Add(Radius);
	
	BodyInstance.InitBody(ShapeBodySetup, GetWorldTransform(), this, GetWorld()->GetPhysicsScene());

	RecreatePhysicsState();
	UpdateCollisionVolume();

	Super::OnRegister();
}

void UGLTFInteractionHotspotComponent::SetSprite(class UTexture2D* NewSprite)
{
	Super::SetSprite(NewSprite);

	UpdateCollisionVolume();
}

void UGLTFInteractionHotspotComponent::SetRadius(float NewRadius)
{
	// TODO: Implement
}

void UGLTFInteractionHotspotComponent::BeginCursorOver(UPrimitiveComponent* TouchedComponent)
{
	if (HighlightSprite != nullptr)
	{
		SetSprite(HighlightSprite);
	}
}

void UGLTFInteractionHotspotComponent::EndCursorOver(UPrimitiveComponent* TouchedComponent)
{
	SetSprite(DefaultSprite);
}

void UGLTFInteractionHotspotComponent::Clicked(UPrimitiveComponent* TouchedComponent, FKey ButtonPressed)
{
	if (SkeletalMeshActor != nullptr && AnimationSequence != nullptr)
	{
		USkeletalMeshComponent* SkeletalMeshComponent = SkeletalMeshActor->GetSkeletalMeshComponent();
		SkeletalMeshComponent->PlayAnimation(AnimationSequence, false);
	}
}

void UGLTFInteractionHotspotComponent::UpdateCollisionVolume()
{
	if (ShapeBodySetup != nullptr)
	{
		// TODO: Figure out where this difference stems from
		const float Scaling = 0.15;
		const float BillboardBoundingRadius = GetBillboardBoundingRadius() * Scaling;

		check(ShapeBodySetup->AggGeom.SphereElems.Num() == 1);

		if (!FMath::IsNearlyEqual(ShapeBodySetup->AggGeom.SphereElems[0].Radius, BillboardBoundingRadius) && Radius != 0.0f) {
			BodyInstance.UpdateBodyScale(FVector(BillboardBoundingRadius / Radius), true);
		}
	}
}

FTransform UGLTFInteractionHotspotComponent::GetWorldTransform() const
{
	const AActor* Owner = GetOwner();

	return Owner ? (Owner->GetTransform() * GetComponentTransform()) : GetComponentTransform();
}

float UGLTFInteractionHotspotComponent::GetBillboardBoundingRadius() const
{
	const FTransform WorldTransform = GetWorldTransform();
	const FBoxSphereBounds WorldBounds = CalcBounds(WorldTransform);

	return WorldBounds.SphereRadius;
}
