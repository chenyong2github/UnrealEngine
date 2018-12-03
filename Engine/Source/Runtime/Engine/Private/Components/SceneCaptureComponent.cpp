// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	
=============================================================================*/

#include "Components/SceneCaptureComponent.h"
#include "Misc/ScopeLock.h"
#include "UObject/RenderingObjectVersion.h"
#include "UObject/ConstructorHelpers.h"
#include "GameFramework/Actor.h"
#include "RenderingThread.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/Material.h"
#include "Components/BillboardComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Texture2D.h"
#include "SceneManagement.h"
#include "Engine/StaticMesh.h"
#include "Engine/SceneCapture.h"
#include "Engine/SceneCapture2D.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/SceneCaptureCube.h"
#include "Components/SceneCaptureComponentCube.h"
#include "Components/DrawFrustumComponent.h"
#include "Engine/PlanarReflection.h"
#include "Components/PlanarReflectionComponent.h"
#include "PlanarReflectionSceneProxy.h"
#include "Components/BoxComponent.h"
#include "Logging/MessageLog.h"

#define LOCTEXT_NAMESPACE "SceneCaptureComponent"

static TMultiMap<TWeakObjectPtr<UWorld>, TWeakObjectPtr<USceneCaptureComponent> > SceneCapturesToUpdateMap;

ASceneCapture::ASceneCapture(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SceneComponent = CreateDefaultSubobject<USceneComponent>(TEXT("SceneComponent"));
	RootComponent = SceneComponent;
}

void ASceneCapture::PostLoad()
{
	Super::PostLoad();

	if (GetLinkerCustomVersion(FEditorObjectVersion::GUID) < FEditorObjectVersion::ChangeSceneCaptureRootComponent)
	{
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		if (MeshComp_DEPRECATED)
		{
			MeshComp_DEPRECATED->SetStaticMesh(nullptr);
		}
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}
}

void ASceneCapture::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	Ar.UsingCustomVersion(FEditorObjectVersion::GUID);
}
// -----------------------------------------------

ASceneCapture2D::ASceneCapture2D(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	CaptureComponent2D = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("NewSceneCaptureComponent2D"));
	CaptureComponent2D->SetupAttachment(RootComponent);
}

void ASceneCapture2D::OnInterpToggle(bool bEnable)
{
	CaptureComponent2D->SetVisibility(bEnable);
}
// -----------------------------------------------

ASceneCaptureCube::ASceneCaptureCube(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	CaptureComponentCube = CreateDefaultSubobject<USceneCaptureComponentCube>(TEXT("NewSceneCaptureComponentCube"));
	CaptureComponentCube->SetupAttachment(RootComponent);
}

void ASceneCaptureCube::OnInterpToggle(bool bEnable)
{
	CaptureComponentCube->SetVisibility(bEnable);
}

// -----------------------------------------------
USceneCaptureComponent::USceneCaptureComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer), ShowFlags(FEngineShowFlags(ESFIM_Game))
{
	bCaptureEveryFrame = true;
	bCaptureOnMovement = true;
	bAlwaysPersistRenderingState = false;
	LODDistanceFactor = 1.0f;
	MaxViewDistanceOverride = -1;
	CaptureSortPriority = 0;

	// Disable features that are not desired when capturing the scene
	ShowFlags.SetMotionBlur(0); // motion blur doesn't work correctly with scene captures.
	ShowFlags.SetSeparateTranslucency(0);
	ShowFlags.SetHMDDistortion(0);

    CaptureStereoPass = EStereoscopicPass::eSSP_FULL;
}

void USceneCaptureComponent::OnRegister()
{
#if WITH_EDITORONLY_DATA
	if (AActor* MyOwner = GetOwner())
	{
		if (ProxyMeshComponent == nullptr)
		{
			ProxyMeshComponent = NewObject<UStaticMeshComponent>(MyOwner, NAME_None, RF_Transactional | RF_TextExportTransient);
			ProxyMeshComponent->SetupAttachment(this);
			ProxyMeshComponent->SetIsVisualizationComponent(true);
			ProxyMeshComponent->SetStaticMesh(CaptureMesh);
			ProxyMeshComponent->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
			ProxyMeshComponent->bHiddenInGame = true;
			ProxyMeshComponent->CastShadow = false;
			ProxyMeshComponent->PostPhysicsComponentTick.bCanEverTick = false;
			ProxyMeshComponent->CreationMethod = CreationMethod;
			ProxyMeshComponent->RegisterComponentWithWorld(GetWorld());
		}
	}
#endif

	Super::OnRegister();

	// Make sure any loaded saved flag settings are reflected in our FEngineShowFlags
	UpdateShowFlags();
}

void USceneCaptureComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	Super::OnComponentDestroyed(bDestroyingHierarchy);

#if WITH_EDITORONLY_DATA
	if (ProxyMeshComponent)
	{
		ProxyMeshComponent->DestroyComponent();
	}
#endif
}

void USceneCaptureComponent::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	USceneCaptureComponent* This = CastChecked<USceneCaptureComponent>(InThis);

	for (int32 ViewIndex = 0; ViewIndex < This->ViewStates.Num(); ViewIndex++)
	{
		FSceneViewStateInterface* Ref = This->ViewStates[ViewIndex].GetReference();
		if (Ref)
		{
			Ref->AddReferencedObjects(Collector);
		}
	}

#if WITH_EDITORONLY_DATA
	Collector.AddReferencedObject(This->ProxyMeshComponent);
#endif
	Super::AddReferencedObjects(This, Collector);
}

void USceneCaptureComponent::HideComponent(UPrimitiveComponent* InComponent)
{
	if (InComponent)
	{
		TWeakObjectPtr<UPrimitiveComponent> WeakComponent(InComponent);
		HiddenComponents.AddUnique(WeakComponent);
	}
}

void USceneCaptureComponent::HideActorComponents(AActor* InActor)
{
	if (InActor)
	{
		for (UActorComponent* Component : InActor->GetComponents())
		{
			if (UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(Component))
			{
				TWeakObjectPtr<UPrimitiveComponent> WeakComponent(PrimComp);
				HiddenComponents.AddUnique(WeakComponent);
			}
		}
	}
}

void USceneCaptureComponent::ShowOnlyComponent(UPrimitiveComponent* InComponent)
{
	if (InComponent)
	{
		// Backward compatibility - set PrimitiveRenderMode to PRM_UseShowOnlyList if BP / game code tries to add a ShowOnlyComponent
		PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
		ShowOnlyComponents.Add(InComponent);
	}
}

void USceneCaptureComponent::ShowOnlyActorComponents(AActor* InActor)
{
	if (InActor)
	{
		// Backward compatibility - set PrimitiveRenderMode to PRM_UseShowOnlyList if BP / game code tries to add a ShowOnlyComponent
		PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;

		for (UActorComponent* Component : InActor->GetComponents())
			{
			if (UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(Component))
			{
				ShowOnlyComponents.Add(PrimComp);
			}
		}
	}
}

void USceneCaptureComponent::RemoveShowOnlyComponent(UPrimitiveComponent* InComponent)
{
	TWeakObjectPtr<UPrimitiveComponent> WeakComponent(InComponent);
	ShowOnlyComponents.Remove(WeakComponent);
}

void USceneCaptureComponent::RemoveShowOnlyActorComponents(AActor* InActor)
{
	if (InActor)
	{
		for (UActorComponent* Component : InActor->GetComponents())
		{
			if (UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(Component))
			{
				TWeakObjectPtr<UPrimitiveComponent> WeakComponent(PrimComp);
				ShowOnlyComponents.Remove(WeakComponent);
			}
		}
	}
}

void USceneCaptureComponent::ClearShowOnlyComponents(UPrimitiveComponent* InComponent)
{
	ShowOnlyComponents.Reset();
}

void USceneCaptureComponent::ClearHiddenComponents()
{
	HiddenComponents.Reset();
}

void USceneCaptureComponent::SetCaptureSortPriority(int32 NewCaptureSortPriority)
{
	CaptureSortPriority = NewCaptureSortPriority;
}

FSceneViewStateInterface* USceneCaptureComponent::GetViewState(int32 ViewIndex)
{
	while (ViewIndex >= ViewStates.Num())
	{
		ViewStates.Add(new FSceneViewStateReference());
	}

	FSceneViewStateInterface* ViewStateInterface = ViewStates[ViewIndex].GetReference();
	if ((bCaptureEveryFrame || bAlwaysPersistRenderingState) && ViewStateInterface == NULL)
	{
		ViewStates[ViewIndex].Allocate();
		ViewStateInterface = ViewStates[ViewIndex].GetReference();
	}
	else if (!bCaptureEveryFrame && ViewStateInterface && !bAlwaysPersistRenderingState)
	{
		ViewStates[ViewIndex].Destroy();
		ViewStateInterface = NULL;
	}
	return ViewStateInterface;
}

void USceneCaptureComponent::UpdateShowFlags()
{
	USceneCaptureComponent* Archetype = Cast<USceneCaptureComponent>(GetArchetype());
	if (Archetype)
	{
		ShowFlags = Archetype->ShowFlags;
	}

	for (FEngineShowFlagsSetting ShowFlagSetting : ShowFlagSettings)
	{
		int32 SettingIndex = ShowFlags.FindIndexByName(*(ShowFlagSetting.ShowFlagName));
		if (SettingIndex != INDEX_NONE)
		{ 
			ShowFlags.SetSingleFlag(SettingIndex, ShowFlagSetting.Enabled);
		}
	}
}

#if WITH_EDITOR

bool USceneCaptureComponent::CanEditChange(const UProperty* InProperty) const
{
	if (InProperty)
	{
		FString PropertyName = InProperty->GetName();

		if (PropertyName == GET_MEMBER_NAME_STRING_CHECKED(USceneCaptureComponent, HiddenActors))
		{
			return PrimitiveRenderMode == ESceneCapturePrimitiveRenderMode::PRM_LegacySceneCapture ||
				PrimitiveRenderMode == ESceneCapturePrimitiveRenderMode::PRM_RenderScenePrimitives;
		}
		else if (PropertyName == GET_MEMBER_NAME_STRING_CHECKED(USceneCaptureComponent, ShowOnlyActors))
		{
			return PrimitiveRenderMode == ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
		}
	}

	return true;
}

void USceneCaptureComponent::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName Name = (PropertyChangedEvent.Property != nullptr) ? PropertyChangedEvent.Property->GetFName() : NAME_None;
	const FName MemberPropertyName = (PropertyChangedEvent.MemberProperty != NULL) ? PropertyChangedEvent.MemberProperty->GetFName() : NAME_None;

	// If our ShowFlagSetting UStruct changed, (or if PostEditChange was called without specifying a property) update the actual show flags
	if (MemberPropertyName.IsEqual("ShowFlagSettings") || MemberPropertyName.IsNone())
	{
		UpdateShowFlags();
	}
}
#endif

void USceneCaptureComponent::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	Ar.UsingCustomVersion(FRenderingObjectVersion::GUID);

	if (Ar.CustomVer(FRenderingObjectVersion::GUID) < FRenderingObjectVersion::AddedbUseShowOnlyList)
	{
		if (ShowOnlyActors.Num() > 0 || ShowOnlyComponents.Num() > 0)
		{
			PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
		}
	}
}

void USceneCaptureComponent::UpdateDeferredCaptures(FSceneInterface* Scene)
{
	UWorld* World = Scene->GetWorld();
	if (!World || SceneCapturesToUpdateMap.Num() == 0)
	{
		return;
	}

	// Only update the scene captures associated with the current scene.
	// Updating others not associated with the scene would cause invalid data to be rendered into the target
	TArray< TWeakObjectPtr<USceneCaptureComponent> > SceneCapturesToUpdate;
	SceneCapturesToUpdateMap.MultiFind(World, SceneCapturesToUpdate);
	SceneCapturesToUpdate.Sort([](const TWeakObjectPtr<USceneCaptureComponent>& A, const TWeakObjectPtr<USceneCaptureComponent>& B)
	{
		if (!A.IsValid())
		{
			return false;
		}
		else if (!B.IsValid())
		{
			return true;
		}
		return A->CaptureSortPriority > B->CaptureSortPriority;
	});

	for (TWeakObjectPtr<USceneCaptureComponent> Component : SceneCapturesToUpdate)
	{
		if (Component.IsValid())
		{
			Component->UpdateSceneCaptureContents(Scene);
		}
	}

	// All scene captures for this world have been updated
	SceneCapturesToUpdateMap.Remove(World);
}

void USceneCaptureComponent::OnUnregister()
{
	for (int32 ViewIndex = 0; ViewIndex < ViewStates.Num(); ViewIndex++)
	{
		ViewStates[ViewIndex].Destroy();
	}

	Super::OnUnregister();
}

// -----------------------------------------------


USceneCaptureComponent2D::USceneCaptureComponent2D(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	FOVAngle = 90.0f;
	OrthoWidth = 512;
	bUseCustomProjectionMatrix = false;
	bAutoActivate = true;
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_DuringPhysics;
	PrimaryComponentTick.bAllowTickOnDedicatedServer = false;
	// Tick in the editor so that bCaptureEveryFrame preview works
	bTickInEditor = true;
	// previous behavior was to capture from raw scene color 
	CaptureSource = SCS_SceneColorHDR;
	// default to full blend weight..
	PostProcessBlendWeight = 1.0f;
	CaptureStereoPass = EStereoscopicPass::eSSP_FULL;
	CustomProjectionMatrix.SetIdentity();
	ClipPlaneNormal = FVector(0, 0, 1);
	bCameraCutThisFrame = false;
	
	// Legacy initialization.
	{
		// previous behavior was to capture 2d scene captures before cube scene captures.
		CaptureSortPriority = 1;

		// previous behavior was not exposing MotionBlur and Temporal AA in scene capture 2d.
		ShowFlags.TemporalAA = false;
		ShowFlags.MotionBlur = false;

#if WITH_EDITORONLY_DATA
		if (!IsRunningCommandlet())
		{
			static ConstructorHelpers::FObjectFinder<UStaticMesh> EditorMesh(TEXT("/Engine/EditorMeshes/MatineeCam_SM"));
			CaptureMesh = EditorMesh.Object;
		}
#endif
	}
}

void USceneCaptureComponent2D::OnRegister()
{
	Super::OnRegister();

#if WITH_EDITORONLY_DATA
	if (AActor* MyOwner = GetOwner())
	{
		if (DrawFrustum == nullptr)
		{
			DrawFrustum = NewObject<UDrawFrustumComponent>(MyOwner, NAME_None, RF_Transactional | RF_TextExportTransient);
			DrawFrustum->SetupAttachment(this);
			DrawFrustum->SetIsVisualizationComponent(true);
			DrawFrustum->CreationMethod = CreationMethod;
			DrawFrustum->RegisterComponentWithWorld(GetWorld());
			UpdateDrawFrustum();
		}
	}
#endif

#if WITH_EDITOR
	// Update content on register to have at least one frames worth of good data.
	// Without updating here this component would not work in a blueprint construction script which recreates the component after each move in the editor
	CaptureSceneDeferred();
#endif
}

void USceneCaptureComponent2D::SendRenderTransform_Concurrent()
{	
	if (bCaptureOnMovement && !bCaptureEveryFrame)
	{
		CaptureSceneDeferred();
	}

	Super::SendRenderTransform_Concurrent();
}

void USceneCaptureComponent2D::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (bCaptureEveryFrame)
	{
		CaptureSceneDeferred();
	}
}

void USceneCaptureComponent2D::CaptureSceneDeferred()
{
	UWorld* World = GetWorld();
	if (World && World->Scene && IsVisible())
	{
		// Defer until after updates finish
		// Needs some CS because of parallel updates.
		static FCriticalSection CriticalSection;
		FScopeLock ScopeLock(&CriticalSection);
		SceneCapturesToUpdateMap.AddUnique(World, this);
	}	
}

void USceneCaptureComponent2D::CaptureScene()
{
	UWorld* World = GetWorld();
	if (World && World->Scene && IsVisible())
	{
		// We must push any deferred render state recreations before causing any rendering to happen, to make sure that deleted resource references are updated
		World->SendAllEndOfFrameUpdates();
		UpdateSceneCaptureContents(World->Scene);
	}	

	if (bCaptureEveryFrame)
	{
		FMessageLog("Blueprint").Warning(LOCTEXT("CaptureScene", "CaptureScene: Scene capture with bCaptureEveryFrame enabled was told to update - major inefficiency."));
	}
}

void USceneCaptureComponent2D::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
#if WITH_EDITORONLY_DATA
	USceneCaptureComponent2D* This = CastChecked<USceneCaptureComponent2D>(InThis);
	Collector.AddReferencedObject(This->DrawFrustum);
#endif

	Super::AddReferencedObjects(InThis, Collector);
}

void USceneCaptureComponent2D::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	Super::OnComponentDestroyed(bDestroyingHierarchy);

#if WITH_EDITORONLY_DATA
	if (DrawFrustum)
	{
		DrawFrustum->DestroyComponent();
	}
#endif
}

#if WITH_EDITORONLY_DATA
void USceneCaptureComponent2D::UpdateDrawFrustum()
{
	if (DrawFrustum != nullptr)
	{
		const float FrustumDrawDistance = 1000.0f;
		if (ProjectionType == ECameraProjectionMode::Perspective)
		{
			DrawFrustum->FrustumAngle = FOVAngle;
		}
		else
		{
			DrawFrustum->FrustumAngle = -OrthoWidth;
		}

		DrawFrustum->FrustumStartDist = GNearClippingPlane;
		// 1000 is the default frustum distance, ideally this would be infinite but that might cause rendering issues
		DrawFrustum->FrustumEndDist = (MaxViewDistanceOverride > DrawFrustum->FrustumStartDist)
			? MaxViewDistanceOverride : 1000.0f;
		DrawFrustum->MarkRenderStateDirty();
	}
}
#endif

#if WITH_EDITOR

bool USceneCaptureComponent2D::CanEditChange(const UProperty* InProperty) const
{
	if (InProperty)
	{
		FString PropertyName = InProperty->GetName();

		if (bUseCustomProjectionMatrix 
			&& (PropertyName == GET_MEMBER_NAME_STRING_CHECKED(USceneCaptureComponent2D, ProjectionType)
				|| PropertyName == GET_MEMBER_NAME_STRING_CHECKED(USceneCaptureComponent2D, FOVAngle)
				|| PropertyName == GET_MEMBER_NAME_STRING_CHECKED(USceneCaptureComponent2D, OrthoWidth)))
		{
			return false;
		}

		if (PropertyName == GET_MEMBER_NAME_STRING_CHECKED(USceneCaptureComponent2D, FOVAngle))
		{
			return ProjectionType == ECameraProjectionMode::Perspective;
		}
		else if (PropertyName == GET_MEMBER_NAME_STRING_CHECKED(USceneCaptureComponent2D, OrthoWidth))
		{
			return ProjectionType == ECameraProjectionMode::Orthographic;
		}
		else if (PropertyName == GET_MEMBER_NAME_STRING_CHECKED(USceneCaptureComponent2D, CompositeMode))
		{
			return CaptureSource == SCS_SceneColorHDR;
		}

		static IConsoleVariable* ClipPlaneCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.AllowGlobalClipPlane"));

		if (PropertyName == GET_MEMBER_NAME_STRING_CHECKED(USceneCaptureComponent2D, bEnableClipPlane))
		{
			return ClipPlaneCVar->GetInt() != 0;
		}

		if (PropertyName == GET_MEMBER_NAME_STRING_CHECKED(USceneCaptureComponent2D, ClipPlaneBase)
			|| PropertyName == GET_MEMBER_NAME_STRING_CHECKED(USceneCaptureComponent2D, ClipPlaneNormal))
		{
			return bEnableClipPlane && ClipPlaneCVar->GetInt() != 0;
		}

		if (PropertyName == GET_MEMBER_NAME_STRING_CHECKED(USceneCaptureComponent2D, CustomProjectionMatrix))
		{
			return bUseCustomProjectionMatrix;
		}
	}

	return Super::CanEditChange(InProperty);
}

void USceneCaptureComponent2D::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	// AActor::PostEditChange will ForceUpdateComponents()
	Super::PostEditChangeProperty(PropertyChangedEvent);

	CaptureSceneDeferred();

	UpdateDrawFrustum();
}
#endif // WITH_EDITOR

void USceneCaptureComponent2D::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	if (Ar.IsLoading())
	{
#if WITH_EDITORONLY_DATA
		PostProcessSettings.OnAfterLoad();
#endif

		if (Ar.CustomVer(FRenderingObjectVersion::GUID) < FRenderingObjectVersion::MotionBlurAndTAASupportInSceneCapture2d)
		{
			ShowFlags.TemporalAA = false;
			ShowFlags.MotionBlur = false;
		}
	}
}

void USceneCaptureComponent2D::UpdateSceneCaptureContents(FSceneInterface* Scene)
{
	Scene->UpdateSceneCaptureContents(this);
}


// -----------------------------------------------

APlanarReflection::APlanarReflection(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	bShowPreviewPlane_DEPRECATED = true;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	PlanarReflectionComponent = CreateDefaultSubobject<UPlanarReflectionComponent>(TEXT("NewPlanarReflectionComponent"));
	RootComponent = PlanarReflectionComponent;

	UBoxComponent* DrawInfluenceBox = CreateDefaultSubobject<UBoxComponent>(TEXT("DrawBox0"));
	DrawInfluenceBox->SetupAttachment(PlanarReflectionComponent);
	DrawInfluenceBox->bUseEditorCompositing = true;
	DrawInfluenceBox->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	PlanarReflectionComponent->PreviewBox = DrawInfluenceBox;

#if WITH_EDITORONLY_DATA
	SpriteComponent = CreateEditorOnlyDefaultSubobject<UBillboardComponent>(TEXT("Sprite"));
	if (!IsRunningCommandlet() && (SpriteComponent != nullptr))
	{
		// Structure to hold one-time initialization
		struct FConstructorStatics
		{
			FName NAME_ReflectionCapture;
			ConstructorHelpers::FObjectFinderOptional<UTexture2D> DecalTexture;
			FConstructorStatics()
				: NAME_ReflectionCapture(TEXT("ReflectionCapture"))
				, DecalTexture(TEXT("/Engine/EditorResources/S_ReflActorIcon"))
			{
			}
		};
		static FConstructorStatics ConstructorStatics;

		SpriteComponent->Sprite = ConstructorStatics.DecalTexture.Get();
		SpriteComponent->RelativeScale3D = FVector(0.5f, 0.5f, 0.5f);
		SpriteComponent->bHiddenInGame = true;
		SpriteComponent->bAbsoluteScale = true;
		SpriteComponent->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		SpriteComponent->bIsScreenSizeScaled = true;
	}
#endif
}

void APlanarReflection::PostLoad()
{
	Super::PostLoad();

	if (GetLinkerCustomVersion(FEditorObjectVersion::GUID) < FEditorObjectVersion::ChangeSceneCaptureRootComponent)
	{
		if (PlanarReflectionComponent)
		{
			PRAGMA_DISABLE_DEPRECATION_WARNINGS
			PlanarReflectionComponent->bShowPreviewPlane = bShowPreviewPlane_DEPRECATED;
			PRAGMA_ENABLE_DEPRECATION_WARNINGS
		}
	}
}

void APlanarReflection::OnInterpToggle(bool bEnable)
{
	PlanarReflectionComponent->SetVisibility(bEnable);
}

#if WITH_EDITOR
void APlanarReflection::EditorApplyScale(const FVector& DeltaScale, const FVector* PivotLocation, bool bAltDown, bool bShiftDown, bool bCtrlDown)
{
	Super::EditorApplyScale(FVector(DeltaScale.X, DeltaScale.Y, 0), PivotLocation, bAltDown, bShiftDown, bCtrlDown);

	UPlanarReflectionComponent* ReflectionComponent = GetPlanarReflectionComponent();
	check(ReflectionComponent);
	const FVector ModifiedScale = FVector(0, 0, DeltaScale.Z) * ( AActor::bUsePercentageBasedScaling ? 500.0f : 50.0f );
	FMath::ApplyScaleToFloat(ReflectionComponent->DistanceFromPlaneFadeoutStart, ModifiedScale);
	FMath::ApplyScaleToFloat(ReflectionComponent->DistanceFromPlaneFadeoutEnd, ModifiedScale);
	PostEditChange();
}

#endif

// -----------------------------------------------

// 0 is reserved to mean invalid
int32 NextPlanarReflectionId = 0;

UPlanarReflectionComponent::UPlanarReflectionComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{

	bShowPreviewPlane = true;
	bCaptureEveryFrame = true;
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_DuringPhysics;
	PrimaryComponentTick.bAllowTickOnDedicatedServer = false;
	// Tick in the editor so that bCaptureEveryFrame preview works
	bTickInEditor = true;
	RenderTarget = NULL;
	PrefilterRoughness = .01f;
	PrefilterRoughnessDistance = 10000;
	ScreenPercentage = 50;
	NormalDistortionStrength = 500;
	DistanceFromPlaneFadeStart_DEPRECATED = 400;
	DistanceFromPlaneFadeEnd_DEPRECATED = 600;
	DistanceFromPlaneFadeoutStart = 60;
	DistanceFromPlaneFadeoutEnd = 100;
	AngleFromPlaneFadeStart = 20;
	AngleFromPlaneFadeEnd = 30;
	ProjectionWithExtraFOV[0] = FMatrix::Identity;
	ProjectionWithExtraFOV[1] = FMatrix::Identity;

	// Disable screen space effects that don't work properly with the clip plane
	ShowFlags.SetLightShafts(0);
	ShowFlags.SetContactShadows(0);
	ShowFlags.SetScreenSpaceReflections(0);

	NextPlanarReflectionId++;
	PlanarReflectionId = NextPlanarReflectionId;

#if WITH_EDITORONLY_DATA
	if (!IsRunningCommandlet())
	{
		static ConstructorHelpers::FObjectFinder<UStaticMesh> EditorMesh(TEXT("/Engine/EditorMeshes/PlanarReflectionPlane.PlanarReflectionPlane"));
		CaptureMesh = EditorMesh.Object;
		static ConstructorHelpers::FObjectFinder<UMaterial> EditorMaterial(TEXT("/Engine/EditorMeshes/ColorCalibrator/M_ChromeBall.M_ChromeBall"));
		CaptureMaterial = EditorMaterial.Object;
	}
#endif
}

void UPlanarReflectionComponent::OnRegister()
{
	Super::OnRegister();

#if WITH_EDITORONLY_DATA
	if (ProxyMeshComponent)
	{
		ProxyMeshComponent->SetMaterial(0, CaptureMaterial);
		ProxyMeshComponent->bVisible = bShowPreviewPlane;
		ProxyMeshComponent->SetRelativeScale3D(FVector(4, 4, 1));
	}
#endif
}

void UPlanarReflectionComponent::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	Ar.UsingCustomVersion(FRenderingObjectVersion::GUID);

	if (Ar.IsLoading() && Ar.CustomVer(FRenderingObjectVersion::GUID) < FRenderingObjectVersion::ChangedPlanarReflectionFadeDefaults)
	{
		DistanceFromPlaneFadeoutEnd = DistanceFromPlaneFadeEnd_DEPRECATED;
		DistanceFromPlaneFadeoutStart = DistanceFromPlaneFadeStart_DEPRECATED;
	}
}

void UPlanarReflectionComponent::CreateRenderState_Concurrent()
{
	UpdatePreviewShape();

	Super::CreateRenderState_Concurrent();

	if (ShouldComponentAddToScene() && ShouldRender())
	{
		SceneProxy = new FPlanarReflectionSceneProxy(this);
		GetWorld()->Scene->AddPlanarReflection(this);
	}
}

void UPlanarReflectionComponent::SendRenderTransform_Concurrent()
{	
	UpdatePreviewShape();

	if (SceneProxy)
	{
		GetWorld()->Scene->UpdatePlanarReflectionTransform(this);
	}

	Super::SendRenderTransform_Concurrent();
}

void UPlanarReflectionComponent::DestroyRenderState_Concurrent()
{
	Super::DestroyRenderState_Concurrent();

	if (SceneProxy)
	{
		GetWorld()->Scene->RemovePlanarReflection(this);

		ENQUEUE_UNIQUE_RENDER_COMMAND_ONEPARAMETER(
			FDestroyPlanarReflectionCommand,
			FPlanarReflectionSceneProxy*,SceneProxy,SceneProxy,
		{
			delete SceneProxy;
		});

		SceneProxy = NULL;
	}
}

#if WITH_EDITOR

void UPlanarReflectionComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	for (int32 ViewIndex = 0; ViewIndex < ViewStates.Num(); ViewIndex++)
	{
		// Recreate the view state to reset temporal history so that property changes can be seen immediately
		ViewStates[ViewIndex].Destroy();
		ViewStates[ViewIndex].Allocate();
	}

	ProxyMeshComponent->bVisible = bShowPreviewPlane;
	ProxyMeshComponent->MarkRenderStateDirty();
}

#endif

void UPlanarReflectionComponent::BeginDestroy()
{
	if (RenderTarget)
	{
		BeginReleaseResource(RenderTarget);
	}
	
	// Begin a fence to track the progress of the BeginReleaseResource being processed by the RT
	ReleaseResourcesFence.BeginFence();

	Super::BeginDestroy();
}

bool UPlanarReflectionComponent::IsReadyForFinishDestroy()
{
	// Wait until the fence is complete before allowing destruction
	return Super::IsReadyForFinishDestroy() && ReleaseResourcesFence.IsFenceComplete();
}

void UPlanarReflectionComponent::FinishDestroy()
{
	Super::FinishDestroy();

	delete RenderTarget;
	RenderTarget = NULL;
}

void UPlanarReflectionComponent::UpdatePreviewShape()
{
	if (PreviewBox)
	{
		PreviewBox->InitBoxExtent(FVector(500 * 4, 500 * 4, DistanceFromPlaneFadeoutEnd));
	}
}

// -----------------------------------------------


USceneCaptureComponentCube::USceneCaptureComponentCube(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bAutoActivate = true;
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_DuringPhysics;
	PrimaryComponentTick.bAllowTickOnDedicatedServer = false;
	bTickInEditor = true;
	IPD = 6.2f;

#if WITH_EDITORONLY_DATA
	if (!IsRunningCommandlet())
	{
		static ConstructorHelpers::FObjectFinder<UStaticMesh> EditorMesh(TEXT("/Engine/EditorMeshes/MatineeCam_SM"));
		CaptureMesh = EditorMesh.Object;
	}
#endif
}

void USceneCaptureComponentCube::OnRegister()
{
	Super::OnRegister();

#if WITH_EDITORONLY_DATA
	if (AActor* MyOwner = GetOwner())
	{
		if (DrawFrustum == nullptr)
		{
			DrawFrustum = NewObject<UDrawFrustumComponent>(MyOwner, NAME_None, RF_Transactional | RF_TextExportTransient);
			DrawFrustum->SetupAttachment(this);
			DrawFrustum->SetIsVisualizationComponent(true);
			DrawFrustum->CreationMethod = CreationMethod;
			DrawFrustum->RegisterComponentWithWorld(GetWorld());
			UpdateDrawFrustum();
		}
	}
#endif

#if WITH_EDITOR
	// Update content on register to have at least one frames worth of good data.
	// Without updating here this component would not work in a blueprint construction script which recreates the component after each move in the editor
	CaptureSceneDeferred();
#endif
}

void USceneCaptureComponentCube::SendRenderTransform_Concurrent()
{	
	if (bCaptureOnMovement && !bCaptureEveryFrame)
	{
		CaptureSceneDeferred();
	}

	Super::SendRenderTransform_Concurrent();
}

void USceneCaptureComponentCube::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (bCaptureEveryFrame)
	{
		CaptureSceneDeferred();
	}
}

void USceneCaptureComponentCube::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{

#if WITH_EDITORONLY_DATA
	USceneCaptureComponentCube* This = CastChecked<USceneCaptureComponentCube>(InThis);
	Collector.AddReferencedObject(This->DrawFrustum);
#endif

	Super::AddReferencedObjects(InThis, Collector);
}

void USceneCaptureComponentCube::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	Super::OnComponentDestroyed(bDestroyingHierarchy);

#if WITH_EDITORONLY_DATA
	if (DrawFrustum)
	{
		DrawFrustum->DestroyComponent();
	}
#endif
}

#if WITH_EDITORONLY_DATA
void USceneCaptureComponentCube::UpdateDrawFrustum()
{
	if (DrawFrustum != nullptr)
	{
		DrawFrustum->FrustumStartDist = GNearClippingPlane;

		// 1000 is the default frustum distance, ideally this would be infinite but that might cause rendering issues
		const float OldEndDist = DrawFrustum->FrustumEndDist;
		DrawFrustum->FrustumEndDist = (MaxViewDistanceOverride > DrawFrustum->FrustumStartDist)
			? MaxViewDistanceOverride : 1000.0f;

		DrawFrustum->FrustumAngle = 90;

		if (OldEndDist != DrawFrustum->FrustumEndDist)
		{
			DrawFrustum->MarkRenderStateDirty();
		}
	}
}
#endif

void USceneCaptureComponentCube::CaptureSceneDeferred()
{
	UWorld* World = GetWorld();
	if (World && World->Scene && IsVisible())
	{
		// Defer until after updates finish
		// Needs some CS because of parallel updates.
		static FCriticalSection CriticalSection;
		FScopeLock ScopeLock(&CriticalSection);
		SceneCapturesToUpdateMap.AddUnique( World, this );
	}	
}

void USceneCaptureComponentCube::CaptureScene()
{
	UWorld* World = GetWorld();
	if (World && World->Scene && IsVisible())
	{
		// We must push any deferred render state recreations before causing any rendering to happen, to make sure that deleted resource references are updated
		World->SendAllEndOfFrameUpdates();
		UpdateSceneCaptureContents(World->Scene);
	}	

	if (bCaptureEveryFrame)
	{
		FMessageLog("Blueprint").Warning(LOCTEXT("CaptureScene", "CaptureScene: Scene capture with bCaptureEveryFrame enabled was told to update - major inefficiency."));
	}
}

void USceneCaptureComponentCube::UpdateSceneCaptureContents(FSceneInterface* Scene)
{
	Scene->UpdateSceneCaptureContents(this);
}

#if WITH_EDITOR
void USceneCaptureComponentCube::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	// AActor::PostEditChange will ForceUpdateComponents()
	Super::PostEditChangeProperty(PropertyChangedEvent);

	CaptureSceneDeferred();

	UpdateDrawFrustum();
}
#endif // WITH_EDITOR

#undef LOCTEXT_NAMESPACE