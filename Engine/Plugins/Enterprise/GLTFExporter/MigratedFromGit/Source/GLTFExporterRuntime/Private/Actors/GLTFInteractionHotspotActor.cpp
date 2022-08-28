// Copyright Epic Games, Inc. All Rights Reserved.

#include "Actors/GLTFInteractionHotspotActor.h"
#include "Animation/SkeletalMeshActor.h"
#include "Animation/AnimSequence.h"
#include "Components/SphereComponent.h"
#include "Components/MaterialBillboardComponent.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/ConstructorHelpers.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Materials/MaterialInstanceDynamic.h"

#if WITH_EDITOR
#include "LevelEditor.h"
#include "Modules/ModuleManager.h"
#include "SLevelViewport.h"
#include "Slate/SceneViewport.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogEditorGLTFInteractionHotspot, Log, All);

namespace
{
	const FName NAME_InteractionHotspotTag = TEXT("InteractionHotspot");
	const FName NAME_LevelEditorModule = TEXT("LevelEditor");
	const FName NAME_SpriteParameter = TEXT("Sprite");
	const FName NAME_OpacityParameter = TEXT("OpacityMask");
}

AGLTFInteractionHotspotActor::AGLTFInteractionHotspotActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer),
	Image(nullptr),
	HoveredImage(nullptr),
	ToggledImage(nullptr),
	ToggledHoveredImage(nullptr),
	BillboardComponent(nullptr),
	SphereComponent(nullptr),
	DefaultMaterial(nullptr),
	DefaultImage(nullptr),
	DefaultHoveredImage(nullptr),
	DefaultToggledImage(nullptr),
	DefaultToggledHoveredImage(nullptr),
	ActiveImage(nullptr),
	ActiveImageSize(0.0f, 0.0f),
	bToggled(bToggled),
	bIsInteractable(true),
	RealtimeSecondsWhenLastInSight(0.0f),
	RealtimeSecondsWhenLastHidden(0.0f)
{
	USceneComponent* SceneRootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(SceneRootComponent);
	AddInstanceComponent(SceneRootComponent);
	SceneRootComponent->SetMobility(EComponentMobility::Movable);

	BillboardComponent = CreateDefaultSubobject<UMaterialBillboardComponent>(TEXT("MaterialBillboardComponent"));
	AddInstanceComponent(BillboardComponent);
	BillboardComponent->SetupAttachment(RootComponent);
	BillboardComponent->SetMobility(EComponentMobility::Movable);

	SphereComponent = CreateDefaultSubobject<USphereComponent>(TEXT("SphereComponent"));
	AddInstanceComponent(BillboardComponent);
	SphereComponent->SetupAttachment(RootComponent);
	SphereComponent->SetMobility(EComponentMobility::Movable);
	SphereComponent->ComponentTags.Add(NAME_InteractionHotspotTag);
	SphereComponent->InitSphereRadius(100.0f);
	SphereComponent->SetVisibility(false);

	// Setup the most minimalistic collision profile for mouse input events
	SphereComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	SphereComponent->SetCollisionResponseToAllChannels(ECR_Ignore);
	SphereComponent->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	SphereComponent->SetGenerateOverlapEvents(false);

	// Respond to interactions with the sphere-component
	SphereComponent->OnBeginCursorOver.AddDynamic(this, &AGLTFInteractionHotspotActor::BeginCursorOver);
	SphereComponent->OnEndCursorOver.AddDynamic(this, &AGLTFInteractionHotspotActor::EndCursorOver);
	SphereComponent->OnClicked.AddDynamic(this, &AGLTFInteractionHotspotActor::Clicked);

	struct FConstructorStatics
	{
		ConstructorHelpers::FObjectFinder<UMaterial> DefaultMaterial;
		ConstructorHelpers::FObjectFinder<UMaterial> DefaultIconMaterial;
		ConstructorHelpers::FObjectFinder<UTexture2D> DefaultImage;
		ConstructorHelpers::FObjectFinder<UTexture2D> DefaultHoveredImage;
		ConstructorHelpers::FObjectFinder<UTexture2D> DefaultToggledImage;
		ConstructorHelpers::FObjectFinder<UTexture2D> DefaultToggledHoveredImage;

		FConstructorStatics()
			: DefaultMaterial(TEXT("/GLTFExporter/Materials/Hotspot"))
			, DefaultIconMaterial(TEXT("/GLTFExporter/Materials/HotspotIcon"))
			, DefaultImage(TEXT("/GLTFExporter/Textures/Hotspots/Image"))
			, DefaultHoveredImage(TEXT("/GLTFExporter/Textures/Hotspots/HoveredImage"))
			, DefaultToggledImage(TEXT("/GLTFExporter/Textures/Hotspots/ToggledImage"))
			, DefaultToggledHoveredImage(TEXT("/GLTFExporter/Textures/Hotspots/ToggledHoveredImage"))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	DefaultMaterial = ConstructorStatics.DefaultMaterial.Object;
	DefaultIconMaterial = ConstructorStatics.DefaultIconMaterial.Object;
	DefaultImage = ConstructorStatics.DefaultImage.Object;
	DefaultHoveredImage = ConstructorStatics.DefaultHoveredImage.Object;
	DefaultToggledImage = ConstructorStatics.DefaultToggledImage.Object;
	DefaultToggledHoveredImage = ConstructorStatics.DefaultToggledHoveredImage.Object;

	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
}


#if WITH_EDITOR
void AGLTFInteractionHotspotActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FProperty* PropertyThatChanged = PropertyChangedEvent.Property;

	if (PropertyThatChanged)
	{
		const FName PropertyFName = PropertyThatChanged->GetFName();

		if (PropertyFName == GET_MEMBER_NAME_CHECKED(ThisClass, Image))
		{
			UpdateActiveImageFromState(EGLTFHotspotState::Default);
		}
		else if (PropertyFName == GET_MEMBER_NAME_CHECKED(ThisClass, SkeletalMeshActor))
		{
			if (AnimationSequence != nullptr && AnimationSequence->GetSkeleton() != SkeletalMeshActor->GetSkeletalMeshComponent()->SkeletalMesh->Skeleton)
			{
				UE_LOG(LogEditorGLTFInteractionHotspot, Warning, TEXT("The skeleton of this actor is not compatible with the previously assigned animation sequence"));
			}
		}
		else if (PropertyFName == GET_MEMBER_NAME_CHECKED(ThisClass, AnimationSequence))
		{
			if (SkeletalMeshActor != nullptr && SkeletalMeshActor->GetSkeletalMeshComponent()->SkeletalMesh->Skeleton != AnimationSequence->GetSkeleton())
			{
				UE_LOG(LogEditorGLTFInteractionHotspot, Warning, TEXT("This animation sequence is not compatible with the skeleton of the previously assigned actor"));
			}
		}
	}
}
#endif // WITH_EDITOR

void AGLTFInteractionHotspotActor::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();

	SetupSpriteElement();
	UpdateActiveImageFromState(EGLTFHotspotState::Default);
}

void AGLTFInteractionHotspotActor::Tick(float DeltaTime)
{
	UWorld* World = GetWorld();

	// TODO: is is safe to assume that we want the first controller for projections?
	APlayerController* PlayerController = World != nullptr ? World->GetFirstPlayerController() : nullptr;
	if (PlayerController == nullptr)
	{
		return;
	}

	const FVector ColliderLocation = SphereComponent->GetComponentLocation();

	FVector ColliderScreenLocation;
	if (!PlayerController->ProjectWorldLocationToScreenWithDistance(ColliderLocation, ColliderScreenLocation))
	{
		return;
	}

	// Update scale of the sphere-component to match the screen-size of the active image
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

	// Update opacity and interactivity of the hotspot based on if it its occluded by other objects or not
	{
		FHitResult HitResult;
		bool bIsHotspotOccluded = false;

		if (PlayerController->GetHitResultAtScreenPosition(FVector2D(ColliderScreenLocation), ECC_Visibility, false, HitResult))
		{
			if (const UPrimitiveComponent* HitComponent = HitResult.GetComponent())
			{
				bIsHotspotOccluded = !HitComponent->ComponentTags.Contains(NAME_InteractionHotspotTag);
			}
		}

		const float CurrentRealtimeSeconds = UGameplayStatics::GetRealTimeSeconds(World);
		float Opacity;

		if (bIsHotspotOccluded)
		{
			RealtimeSecondsWhenLastHidden = CurrentRealtimeSeconds;

			const float HiddenDuration = FMath::Max(RealtimeSecondsWhenLastHidden - RealtimeSecondsWhenLastInSight, 0.0f);
			const float FadeOutDuration = 0.5f;

			Opacity = 1.0f - FMath::Clamp(HiddenDuration / FadeOutDuration, 0.0f, 1.0f);
		}
		else
		{
			RealtimeSecondsWhenLastInSight = CurrentRealtimeSeconds;

			const float VisibleDuration = FMath::Max(RealtimeSecondsWhenLastInSight - RealtimeSecondsWhenLastHidden, 0.0f);
			const float FadeInDuration = 0.25f;

			Opacity = FMath::Clamp(VisibleDuration / FadeInDuration, 0.0f, 1.0f);
		}

		SetSpriteOpacity(Opacity);
		bIsInteractable = Opacity >= 0.5f;
	}
}

void AGLTFInteractionHotspotActor::BeginCursorOver(UPrimitiveComponent* TouchedComponent)
{
	if (bIsInteractable)
	{
		UpdateActiveImageFromState(bToggled ? EGLTFHotspotState::ToggledHovered :  EGLTFHotspotState::Hovered);
	}
}

void AGLTFInteractionHotspotActor::EndCursorOver(UPrimitiveComponent* TouchedComponent)
{
	UpdateActiveImageFromState(bToggled ? EGLTFHotspotState::Toggled :  EGLTFHotspotState::Default);
}

void AGLTFInteractionHotspotActor::Clicked(UPrimitiveComponent* TouchedComponent, FKey ButtonPressed)
{
	if (!bIsInteractable)
	{
		return;
	}

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

	UpdateActiveImageFromState(bToggled ? EGLTFHotspotState::ToggledHovered :  EGLTFHotspotState::Hovered);
}

void AGLTFInteractionHotspotActor::UpdateActiveImageFromState(EGLTFHotspotState State)
{
	UTexture2D* NewImage = const_cast<UTexture2D*>(GetImageForState(State));

	UTexture* DefaultTexture;
	GetSpriteMaterial()->GetTextureParameterDefaultValue(NAME_SpriteParameter, DefaultTexture);

	UTexture* SpriteTexture = NewImage != nullptr ? NewImage : DefaultTexture;
	GetSpriteMaterial()->SetTextureParameterValue(NAME_SpriteParameter, SpriteTexture);

	ActiveImage = NewImage;
	ActiveImageSize = { SpriteTexture->GetSurfaceWidth(), SpriteTexture->GetSurfaceHeight() };

	// NOTE: we do this even if size is unchanged since the last update may have failed
	UpdateSpriteSize();
}

void AGLTFInteractionHotspotActor::SetupSpriteElement() const
{
	UMaterialInstanceDynamic* MaterialInstance;

#if WITH_EDITORONLY_DATA
	if (GetWorld() != nullptr && GetWorld()->WorldType == EWorldType::Editor)
	{
		MaterialInstance = UMaterialInstanceDynamic::Create(DefaultIconMaterial, GetTransientPackage());
	}
	else
#endif // WITH_EDITORONLY_DATA
	{
		MaterialInstance = UMaterialInstanceDynamic::Create(DefaultMaterial, GetTransientPackage());
	}

	FMaterialSpriteElement Element;
	Element.Material = MaterialInstance;
	Element.bSizeIsInScreenSpace = true;
	Element.BaseSizeX = 0.1f;
	Element.BaseSizeY = 0.1f;

	BillboardComponent->SetElements({ Element });
}

UMaterialInstanceDynamic* AGLTFInteractionHotspotActor::GetSpriteMaterial() const
{
	return static_cast<UMaterialInstanceDynamic*>(BillboardComponent->GetMaterial(0));
}

void AGLTFInteractionHotspotActor::UpdateSpriteSize()
{
	const FIntPoint ViewportSize = GetCurrentViewportSize();

	float BaseSizeX = 0.1f;
	float BaseSizeY = 0.1f;

	if (ViewportSize.X > 0 && ViewportSize.Y > 0)
	{
		BaseSizeX = ActiveImageSize.X / static_cast<float>(ViewportSize.X);
		BaseSizeY = ActiveImageSize.Y / static_cast<float>(ViewportSize.Y);
	}

	FMaterialSpriteElement& Element = BillboardComponent->Elements[0];

	if (BaseSizeX != Element.BaseSizeX || BaseSizeY != Element.BaseSizeY)	// TODO: use epsilon for comparison?
	{
		Element.BaseSizeX = BaseSizeX;
		Element.BaseSizeY = BaseSizeY;

		BillboardComponent->MarkRenderStateDirty();
	}
}

void AGLTFInteractionHotspotActor::SetSpriteOpacity(const float Opacity) const
{
	GetSpriteMaterial()->SetScalarParameterValue(NAME_OpacityParameter, Opacity);
}

FIntPoint AGLTFInteractionHotspotActor::GetCurrentViewportSize()
{
	// TODO: verify that correct size is calculated in the various play-modes and in the editor

	const FViewport* Viewport = nullptr;

	if (UWorld* World = GetWorld())
	{
		if (World->IsGameWorld())
		{
			if (UGameViewportClient* GameViewportClient = World->GetGameViewport())
			{
				Viewport = GameViewportClient->Viewport;
			}
		}
#if WITH_EDITOR
		else
		{
			if (FModuleManager::Get().IsModuleLoaded(NAME_LevelEditorModule))
			{
				FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>(NAME_LevelEditorModule);

				if (const TSharedPtr<SLevelViewport> ActiveLevelViewport = LevelEditorModule.GetFirstActiveLevelViewport())
				{
					Viewport = ActiveLevelViewport->GetActiveViewport();
				}
			}
		}
#endif
	}

	if (Viewport != nullptr)
	{
		if (!Viewport->ViewportResizedEvent.IsBoundToObject(this))
		{
			Viewport->ViewportResizedEvent.AddUObject(this, &AGLTFInteractionHotspotActor::ViewportResized);
		}

		return Viewport->GetSizeXY();
	}
	else
	{
		return FIntPoint(0, 0);
	}
}

void AGLTFInteractionHotspotActor::ViewportResized(FViewport*, uint32)
{
	UpdateSpriteSize();
}

const UTexture2D* AGLTFInteractionHotspotActor::GetImageForState(EGLTFHotspotState State) const
{
	const UTexture2D* CurrentImage = DefaultImage;
	const UTexture2D* CurrentHoveredImage = DefaultHoveredImage;
	const UTexture2D* CurrentToggledImage = DefaultToggledImage;
	const UTexture2D* CurrentToggledHoveredImage = DefaultToggledHoveredImage;

	if (Image != nullptr)
	{
		CurrentImage = Image;
		CurrentHoveredImage = HoveredImage != nullptr ? HoveredImage : CurrentImage;
		CurrentToggledImage = ToggledImage != nullptr ? ToggledImage : CurrentImage;

		if (ToggledHoveredImage != nullptr)
		{
			CurrentToggledHoveredImage = ToggledHoveredImage;
		}
		else if (ToggledImage != nullptr)
		{
			CurrentToggledHoveredImage = ToggledImage;
		}
		else if (HoveredImage != nullptr)
		{
			CurrentToggledHoveredImage = HoveredImage;
		}
		else
		{
			CurrentToggledHoveredImage = Image;
		}
	}

	switch (State)
	{
		case EGLTFHotspotState::Default:        return CurrentImage;
		case EGLTFHotspotState::Hovered:        return CurrentHoveredImage;
		case EGLTFHotspotState::Toggled:        return CurrentToggledImage;
		case EGLTFHotspotState::ToggledHovered: return CurrentToggledHoveredImage;
		default:                                checkNoEntry();
	}

	return nullptr;
}
