// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/GLTFInteractionHotspotComponent.h"
#include "Animation/SkeletalMeshActor.h"
#include "Animation/AnimSequence.h"
#include "Components/SphereComponent.h"
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
	struct FConstructorStatics
	{
		ConstructorHelpers::FObjectFinder<UMaterial> Material;
		FConstructorStatics()
			: Material(TEXT("/GLTFExporter/Materials/Hotspot"))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	bHiddenInGame = false;
	DefaultMaterial = UMaterialInstanceDynamic::Create(ConstructorStatics.Material.Object, GetTransientPackage());

	CreateDefaultSpriteElement();

	SphereComponent = CreateDefaultSubobject<USphereComponent>(TEXT("Collider"), true);
	SphereComponent->InitSphereRadius(100.0f);
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

void UGLTFInteractionHotspotComponent::OnRegister()
{
	Super::OnRegister();
}

void UGLTFInteractionHotspotComponent::SetActiveImage(class UTexture2D* NewImage)
{
	if (NewImage != ActiveImage)
	{
		ActiveImage = NewImage;
		GetSpriteMaterial()->SetTextureParameterValue("Sprite", ActiveImage);

		const FVector2D NewImageSize(
			ActiveImage != nullptr ? ActiveImage->GetSurfaceWidth() : 32.0f,
			ActiveImage != nullptr ? ActiveImage->GetSurfaceHeight() : 32.0f);

		if (NewImageSize != ActiveImageSize)
		{
			ActiveImageSize = NewImageSize;

			FMaterialSpriteElement& Element = GetSpriteElement();

			if (UGameViewportClient* GameViewportClient = GetWorld()->GetGameViewport())
			{
				FVector2D ViewportSize;
				GameViewportClient->GetViewportSize(ViewportSize);

				Element.BaseSizeX = ActiveImageSize.X / ViewportSize.X;
				Element.BaseSizeY = ActiveImageSize.X / ViewportSize.Y;
			}
			else
			{
				// TODO: if running in the editor, find and use the size of the viewport

				Element.BaseSizeX = 0.1f;
				Element.BaseSizeY = 0.1f;
			}

			MarkRenderStateDirty();
			UpdateCollisionVolume();
		}
	}
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

void UGLTFInteractionHotspotComponent::UpdateCollisionVolume()
{
	// TODO: update collider-size dynamically to always match screen-size
}

float UGLTFInteractionHotspotComponent::GetBillboardBoundingRadius() const
{
	const FTransform WorldTransform = GetComponentTransform();
	const FBoxSphereBounds WorldBounds = CalcBounds(WorldTransform);

	return WorldBounds.SphereRadius;
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
