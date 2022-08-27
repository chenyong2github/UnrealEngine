// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/GLTFInteractionHotspotComponent.h"
#include "Animation/SkeletalMeshActor.h"
#include "Animation/AnimSequence.h"
#include "Engine/CollisionProfile.h"
#include "UObject/ConstructorHelpers.h"
#include "PhysicsEngine/BodySetup.h"

namespace
{
	static const float DefaultCollisionVolumeRadius = 50.0f;
} // Anonymous namespace

UGLTFInteractionHotspotComponent::UGLTFInteractionHotspotComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer),
	DefaultSprite(nullptr),
	HighlightSprite(nullptr),
	ClickSprite(nullptr),
	ShapeBodySetup(nullptr)
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
	ShapeBodySetup->AggGeom.SphereElems.Add(DefaultCollisionVolumeRadius);
	
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
	for (TArray<FGLTFAnimation>::TConstIterator Animation = Animations.CreateConstIterator(); Animation; ++Animation)
	{
		if (Animation->SkeletalMeshActor != nullptr && Animation->AnimationSequence != nullptr)
		{
			USkeletalMeshComponent* SkeletalMeshComponent = Animation->SkeletalMeshActor->GetSkeletalMeshComponent();
			SkeletalMeshComponent->PlayAnimation(Animation->AnimationSequence, false);
		}
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

		if (!FMath::IsNearlyEqual(ShapeBodySetup->AggGeom.SphereElems[0].Radius, BillboardBoundingRadius) && DefaultCollisionVolumeRadius != 0.0f)
		{
			BodyInstance.UpdateBodyScale(FVector(BillboardBoundingRadius / DefaultCollisionVolumeRadius), true);
		}
	}
}

FTransform UGLTFInteractionHotspotComponent::GetWorldTransform() const
{
	const AActor* Owner = GetOwner();

	return (Owner != nullptr) ? (Owner->GetTransform() * GetComponentTransform()) : GetComponentTransform();
}

float UGLTFInteractionHotspotComponent::GetBillboardBoundingRadius() const
{
	const FTransform WorldTransform = GetWorldTransform();
	const FBoxSphereBounds WorldBounds = CalcBounds(WorldTransform);

	return WorldBounds.SphereRadius;
}
