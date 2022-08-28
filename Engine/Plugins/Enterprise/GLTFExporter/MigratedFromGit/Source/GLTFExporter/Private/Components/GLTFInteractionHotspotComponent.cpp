// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/GLTFInteractionHotspotComponent.h"
#include "Animation/SkeletalMeshActor.h"
#include "Animation/AnimSequence.h"
#include "Engine/CollisionProfile.h"
#include "UObject/ConstructorHelpers.h"
#include "PhysicsEngine/BodySetup.h"
#include "Animation/AnimSingleNodeInstance.h"

namespace
{
	const float UnitSphereRadius = 50.0f;
} // Anonymous namespace

UGLTFInteractionHotspotComponent::UGLTFInteractionHotspotComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer),
	Image(nullptr),
	HoveredImage(nullptr),
	ToggledImage(nullptr),
	ToggledHoveredImage(nullptr),
	ShapeBodySetup(nullptr),
	bToggled(bToggled)
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

#if WITH_EDITOR
void UGLTFInteractionHotspotComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FProperty* PropertyThatChanged = PropertyChangedEvent.Property;

	if (PropertyThatChanged)
	{
		const FString PropertyName = PropertyThatChanged->GetName();

		if (PropertyName == TEXT("Image"))
		{
			SetSprite(Image);
		}
	}
}
#endif // WITH_EDITOR

void UGLTFInteractionHotspotComponent::BeginPlay()
{
	Super::BeginPlay();

	SetSprite(Image);
}

void UGLTFInteractionHotspotComponent::OnRegister()
{
	ShapeBodySetup = NewObject<UBodySetup>(this, NAME_None, RF_Transient);

	if (GUObjectArray.IsDisregardForGC(this))
	{
		ShapeBodySetup->AddToRoot();
	}

	ShapeBodySetup->AddToCluster(this);

	if (ShapeBodySetup->HasAnyInternalFlags(EInternalObjectFlags::Async) && GUObjectClusters.GetObjectCluster(ShapeBodySetup))
	{
		ShapeBodySetup->ClearInternalFlags(EInternalObjectFlags::Async);
	}

	ShapeBodySetup->CollisionTraceFlag = CTF_UseSimpleAsComplex;
	ShapeBodySetup->AggGeom.SphereElems.Add(FKSphereElem(UnitSphereRadius));
	
	Super::OnRegister();
}

void UGLTFInteractionHotspotComponent::OnCreatePhysicsState()
{
	Super::OnCreatePhysicsState();

	if (BodyInstance.IsValidBodyInstance())
	{
#if WITH_PHYSX
		FPhysicsCommand::ExecuteWrite(BodyInstance.GetActorReferenceWithWelding(), [this](const FPhysicsActorHandle& Actor)
		{
			TArray<FPhysicsShapeHandle> Shapes;
			BodyInstance.GetAllShapes_AssumesLocked(Shapes);

			for (FPhysicsShapeHandle& Shape : Shapes)
			{
				if (BodyInstance.IsShapeBoundToBody(Shape))
				{
					FPhysicsInterface::SetUserData(Shape, (void*)ShapeBodySetup->AggGeom.SphereElems[0].GetUserData());
				}
			}
		});
#endif
	}

	UpdateCollisionVolume();
}

UBodySetup* UGLTFInteractionHotspotComponent::GetBodySetup()
{
	return ShapeBodySetup;
}

void UGLTFInteractionHotspotComponent::SetSprite(class UTexture2D* NewSprite)
{
	Super::SetSprite(NewSprite);

	UpdateCollisionVolume();
}

void UGLTFInteractionHotspotComponent::BeginCursorOver(UPrimitiveComponent* TouchedComponent)
{
	SetSprite(GetActiveImage(true));
}

void UGLTFInteractionHotspotComponent::EndCursorOver(UPrimitiveComponent* TouchedComponent)
{
	SetSprite(GetActiveImage(false));
}

void UGLTFInteractionHotspotComponent::Clicked(UPrimitiveComponent* TouchedComponent, FKey ButtonPressed)
{
	const bool bReverseAnimation = bToggled;

	for (TArray<FGLTFAnimation>::TConstIterator Animation = Animations.CreateConstIterator(); Animation; ++Animation)
	{
		if (Animation->SkeletalMeshActor != nullptr && Animation->AnimationSequence != nullptr)
		{
			USkeletalMeshComponent* SkeletalMeshComponent = Animation->SkeletalMeshActor->GetSkeletalMeshComponent();
			SkeletalMeshComponent->SetAnimationMode(EAnimationMode::Type::AnimationSingleNode);
			const float AbsolutePlayRate = FMath::Abs(SkeletalMeshComponent->GetPlayRate());

			if (SkeletalMeshComponent->IsPlaying())
			{
				UAnimSingleNodeInstance* SingleNodeInstance = SkeletalMeshComponent->GetSingleNodeInstance();
				
				if (SingleNodeInstance != nullptr && SingleNodeInstance->GetAnimationAsset() == Animation->AnimationSequence)
				{
					// If the same animation is already playing, just reverse the play rate for a smooth transition
					SkeletalMeshComponent->SetPlayRate(AbsolutePlayRate * -1.0f);
					
					continue;
				}
			}
			
			SkeletalMeshComponent->SetAnimation(Animation->AnimationSequence);
			SkeletalMeshComponent->SetPlayRate(AbsolutePlayRate * (bReverseAnimation ? -1.0f : 1.0f));
			SkeletalMeshComponent->SetPosition(bReverseAnimation ? Animation->AnimationSequence->GetPlayLength() : 0.0f);
			SkeletalMeshComponent->Play(false);
		}
	}

	bToggled = !bToggled;

	SetSprite(GetActiveImage(true));
}

void UGLTFInteractionHotspotComponent::UpdateCollisionVolume()
{
	if (ShapeBodySetup != nullptr)
	{
		// TODO: Figure out why the bounding radius doesn't match the size of the billboard
		const float Scaling = 0.15f;
		const float BillboardBoundingRadius = GetBillboardBoundingRadius() * Scaling;

		check(ShapeBodySetup->AggGeom.SphereElems.Num() == 1);

		if (!FMath::IsNearlyEqual(ShapeBodySetup->AggGeom.SphereElems[0].Radius, BillboardBoundingRadius) && UnitSphereRadius != 0.0f)
		{
			BodyInstance.UpdateBodyScale(FVector(BillboardBoundingRadius / UnitSphereRadius), true);
		}
	}
}

float UGLTFInteractionHotspotComponent::GetBillboardBoundingRadius() const
{
	const FTransform WorldTransform = GetComponentTransform();
	const FBoxSphereBounds WorldBounds = CalcBounds(WorldTransform);

	return WorldBounds.SphereRadius;
}

UTexture2D* UGLTFInteractionHotspotComponent::GetActiveImage(bool bCursorOver) const
{
	// A list of candidates ordered by descending priority
	TArray<UTexture2D*> ImageCandidates;

	if (bToggled)
	{
		if (bCursorOver)
		{
			ImageCandidates.Add(ToggledHoveredImage);
		}

		ImageCandidates.Add(ToggledImage);
	}
	else
	{
		if (bCursorOver)
		{
			ImageCandidates.Add(HoveredImage);
		}

		ImageCandidates.Add(Image);
	}

	ImageCandidates.Add(Image);

	for (UTexture2D* ImageCandidate : ImageCandidates)
	{
		if (ImageCandidate != nullptr)
		{
			return ImageCandidate;
		}
	}

	return nullptr;
}
