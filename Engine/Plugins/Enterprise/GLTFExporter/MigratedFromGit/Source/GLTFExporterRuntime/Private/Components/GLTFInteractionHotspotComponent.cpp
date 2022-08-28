// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/GLTFInteractionHotspotComponent.h"
#include "Animation/SkeletalMeshActor.h"
#include "Animation/AnimSequence.h"
#include "Components/SphereComponent.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/ConstructorHelpers.h"
#include "Animation/AnimSingleNodeInstance.h"

DEFINE_LOG_CATEGORY_STATIC(LogEditorGLTFInteractionHotspot, Log, All);

UGLTFInteractionHotspotComponent::UGLTFInteractionHotspotComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer),
	Image(nullptr),
	HoveredImage(nullptr),
	ToggledImage(nullptr),
	ToggledHoveredImage(nullptr),
	SphereComponent(nullptr),
	DefaultMaterial(nullptr),
	ActiveImage(nullptr),
	ActiveImageSize(0.0f, 0.0f),
	bToggled(bToggled)
{
	bHiddenInGame = false;
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;

	struct FConstructorStatics
	{
		ConstructorHelpers::FObjectFinder<UMaterial> Material;
		FConstructorStatics()
			: Material(TEXT("/GLTFExporter/Materials/Hotspot"))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	DefaultMaterial = UMaterialInstanceDynamic::Create(ConstructorStatics.Material.Object, GetTransientPackage());

	CreateDefaultSpriteElement();

	SphereComponent = CreateDefaultSubobject<USphereComponent>(TEXT("Collider"), true);
	SphereComponent->InitSphereRadius(100.0f);
	SphereComponent->SetVisibility(false);
	SphereComponent->SetupAttachment(this);

	// Setup the most minimalistic collision profile for mouse input events
	SphereComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	SphereComponent->SetCollisionResponseToAllChannels(ECollisionResponse::ECR_Ignore);
	SphereComponent->SetCollisionResponseToChannel(ECollisionChannel::ECC_Visibility, ECollisionResponse::ECR_Block);
	SphereComponent->SetGenerateOverlapEvents(false);

	SphereComponent->OnBeginCursorOver.AddDynamic(this, &UGLTFInteractionHotspotComponent::BeginCursorOver);
	SphereComponent->OnEndCursorOver.AddDynamic(this, &UGLTFInteractionHotspotComponent::EndCursorOver);
	SphereComponent->OnClicked.AddDynamic(this, &UGLTFInteractionHotspotComponent::Clicked);
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
			SetActiveImage(Image);
		}
		else if (PropertyName == TEXT("SkeletalMeshActor"))
		{
			if (AnimationSequence != nullptr && AnimationSequence->GetSkeleton() != SkeletalMeshActor->GetSkeletalMeshComponent()->SkeletalMesh->Skeleton)
			{
				UE_LOG(LogEditorGLTFInteractionHotspot, Warning, TEXT("The skeleton of this actor is not compatible with the previously assigned animation sequence"));
			}
		}
		else if (PropertyName == TEXT("AnimationSequence"))
		{
			if (SkeletalMeshActor != nullptr && SkeletalMeshActor->GetSkeletalMeshComponent()->SkeletalMesh->Skeleton != AnimationSequence->GetSkeleton())
			{
				UE_LOG(LogEditorGLTFInteractionHotspot, Warning, TEXT("This animation sequence is not compatible with the skeleton of the previously assigned actor"));
			}
		}
	}
}
#endif // WITH_EDITOR

void UGLTFInteractionHotspotComponent::PostLoad()
{
	Super::PostLoad();

	CreateDefaultSpriteElement();	// NOTE: needed in order to overwrite any persisted element
	SetActiveImage(Image);
}

void UGLTFInteractionHotspotComponent::BeginPlay()
{
	Super::BeginPlay();

	SetActiveImage(Image);
}

void UGLTFInteractionHotspotComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	// Update scale of the sphere-component to match the screen-size of the active image
	if (APlayerController* PlayerController = World->GetFirstPlayerController())	// TODO: is is safe to assume that we want the first controller for projections?
	{
		const FVector ColliderLocation = SphereComponent->GetComponentLocation();
		FVector ColliderScreenLocation;

		if (PlayerController->ProjectWorldLocationToScreenWithDistance(ColliderLocation, ColliderScreenLocation))
		{
			const FVector2D CornerScreenLocation = FVector2D(ColliderScreenLocation) + ActiveImageSize * 0.5f;
			FVector RayLocation;
			FVector RayDirection;

			if (PlayerController->DeprojectScreenPositionToWorld(CornerScreenLocation.X, CornerScreenLocation.Y, RayLocation, RayDirection))
			{
				const FVector ExtentLocation = RayLocation + RayDirection * ColliderScreenLocation.Z;
				const float NewSphereRadius = (ExtentLocation - ColliderLocation).Size() / SphereComponent->GetShapeScale();
				const float OldSphereRadius = SphereComponent->GetUnscaledSphereRadius();

				if (FMath::Abs(NewSphereRadius - OldSphereRadius) > 0.1f)	// TODO: better epsilon?
				{
					SphereComponent->SetSphereRadius(NewSphereRadius);
				}
			}
		}
	}

	// Fade out / hide the component when it's hidden behind other objects
	if (World != nullptr)
	{
		// TODO: implement
	}
}

void UGLTFInteractionHotspotComponent::OnRegister()
{
	Super::OnRegister();
}

void UGLTFInteractionHotspotComponent::SetActiveImage(class UTexture2D* NewImage)
{
	if (NewImage != ActiveImage)
	{
		GetSpriteMaterial()->SetTextureParameterValue("Sprite", NewImage);
		ActiveImage = NewImage;
	}

	const FVector2D NewImageSize(
		NewImage != nullptr ? NewImage->GetSurfaceWidth() : 32.0f,
		NewImage != nullptr ? NewImage->GetSurfaceHeight() : 32.0f);

	if (NewImageSize != ActiveImageSize)
	{
		ActiveImageSize = NewImageSize;
	}

	// NOTE: we do this even if size is unchanged since the last update may have failed
	UpdateSpriteSize();
}

void UGLTFInteractionHotspotComponent::BeginCursorOver(UPrimitiveComponent* TouchedComponent)
{
	SetActiveImage(CalculateActiveImage(true));
}

void UGLTFInteractionHotspotComponent::EndCursorOver(UPrimitiveComponent* TouchedComponent)
{
	SetActiveImage(CalculateActiveImage(false));
}

void UGLTFInteractionHotspotComponent::Clicked(UPrimitiveComponent* TouchedComponent, FKey ButtonPressed)
{
	const bool bReverseAnimation = bToggled;

	if (SkeletalMeshActor != nullptr && AnimationSequence != nullptr)
	{
		USkeletalMeshComponent* SkeletalMeshComponent = SkeletalMeshActor->GetSkeletalMeshComponent();
		SkeletalMeshComponent->SetAnimationMode(EAnimationMode::Type::AnimationSingleNode);
		const float AbsolutePlayRate = FMath::Abs(SkeletalMeshComponent->GetPlayRate());
		const UAnimSingleNodeInstance* SingleNodeInstance = SkeletalMeshComponent->GetSingleNodeInstance();

		if (SkeletalMeshComponent->IsPlaying() && SingleNodeInstance != nullptr && SingleNodeInstance->GetAnimationAsset() == AnimationSequence)
		{
			// If the same animation is already playing, just reverse the play rate for a smooth transition
			SkeletalMeshComponent->SetPlayRate(AbsolutePlayRate * -1.0f);
		}
		else
		{
			SkeletalMeshComponent->SetAnimation(AnimationSequence);
			SkeletalMeshComponent->SetPlayRate(AbsolutePlayRate * (bReverseAnimation ? -1.0f : 1.0f));
			SkeletalMeshComponent->SetPosition(bReverseAnimation ? AnimationSequence->GetPlayLength() : 0.0f);
			SkeletalMeshComponent->Play(false);
		}
	}

	bToggled = !bToggled;

	SetActiveImage(CalculateActiveImage(true));
}

UTexture2D* UGLTFInteractionHotspotComponent::CalculateActiveImage(bool bCursorOver) const
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

void UGLTFInteractionHotspotComponent::CreateDefaultSpriteElement()
{
	FMaterialSpriteElement Element;
	Element.Material = DefaultMaterial;
	Element.bSizeIsInScreenSpace = true;
	Element.BaseSizeX = 0.1f;
	Element.BaseSizeY = 0.1f;

	SetElements({ Element });
}

FMaterialSpriteElement& UGLTFInteractionHotspotComponent::GetSpriteElement()
{
	return Elements[0];
}

UMaterialInstanceDynamic* UGLTFInteractionHotspotComponent::GetSpriteMaterial() const
{
	return static_cast<UMaterialInstanceDynamic*>(GetMaterial(0));
}

void UGLTFInteractionHotspotComponent::UpdateSpriteSize()
{
	if (UGameViewportClient* GameViewportClient = GetWorld()->GetGameViewport())
	{
		FVector2D ViewportSize;
		GameViewportClient->GetViewportSize(ViewportSize);

		const float BaseSizeX = ActiveImageSize.X / ViewportSize.X;
		const float BaseSizeY = ActiveImageSize.Y / ViewportSize.Y;

		FMaterialSpriteElement& Element = GetSpriteElement();

		if (BaseSizeX != Element.BaseSizeX || BaseSizeY != Element.BaseSizeY)	// TODO: use epsilon for comparison?
		{
			Element.BaseSizeX = BaseSizeX;
			Element.BaseSizeY = BaseSizeY;

			MarkRenderStateDirty();
		}
	}
}
