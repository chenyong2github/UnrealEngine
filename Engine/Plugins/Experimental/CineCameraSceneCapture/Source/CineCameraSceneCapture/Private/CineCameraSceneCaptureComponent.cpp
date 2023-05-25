// Copyright Epic Games, Inc. All Rights Reserved.

#include "CineCameraSceneCaptureComponent.h"
#include "Components/StaticMeshComponent.h"
#include "SceneViewExtension.h"
#include "Runtime/CinematicCamera/Public/CineCameraComponent.h"
#include "Runtime/Engine/Public/SceneView.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "FCineCameraSceneCaptureModule"

DEFINE_LOG_CATEGORY(LogCineCapture);

#define CINE_CAMERA_INVALID_PARENT_WARNING "Cine Capture requires to be parented to Cine Camera Component. Cine Capture {0} on Actor \"{1}\" will be disabled until it is parented to Cine Camera Actor."

// This extension is only registered onto the scene capture 2d component, and therefore runs locally.
class FCineCameraCaptureSceneViewExtension : public ISceneViewExtension
{
public:
	//~ Begin FSceneViewExtensionBase Interface
	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override {};
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override
	{
		if (!CineCameraComponent.IsValid())
		{
			return;
		}

		FMinimalViewInfo DesiredView;

		// Immitate the behaviour of viewports.
		CineCameraComponent->GetCameraView(DeltaTime, DesiredView);
		FTransform Transform = CineCameraComponent->GetComponentToWorld();
		FVector ViewLocation = Transform.GetTranslation();
		InView.StartFinalPostprocessSettings(ViewLocation);

		InView.OverridePostProcessSettings(DesiredView.PostProcessSettings, 1.0);
		FSceneViewInitOptions ViewInitOptions;
		InView.EndFinalPostprocessSettings(ViewInitOptions);

		// Required for certain effects (lighting) to match that of Cine Camera.
		InView.bIsSceneCapture = bFollowSceneCaptureRenderPath;
	}
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override {};
	//~ End FSceneViewExtensionBase Interface

	/**
	* Prepare the scene view extension with relevant camera and render mode info.
	*/
	void PrepareRender(TSoftObjectPtr<UCineCameraComponent> InCineCameraComponent, bool bInFollowSceneCaptureRenderPath)
	{
		CineCameraComponent = InCineCameraComponent;
		bFollowSceneCaptureRenderPath = bInFollowSceneCaptureRenderPath;
	};

	/**
	* Delta time is needed for the purposes of camera smoothing.
	*/
	void SetFrameDeltaTime(float InDeltaTime)
	{
		DeltaTime = InDeltaTime;
	}
private:
	/**
	* Translates to View.bIsSceneCapture.
	*/
	bool bFollowSceneCaptureRenderPath = true;

	/**
	* A transient property that is used to deliver settings from CineCameraComponent to views that are related to cine capture.
	*/
	TSoftObjectPtr<UCineCameraComponent> CineCameraComponent = nullptr;

	/**
	* Delta time between frames. Used for camera smoothing.
	*/
	float DeltaTime = 0.0f;
};

UCineCaptureComponent2D::UCineCaptureComponent2D(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer), RenderTargetHighestDimension(1280), bFollowSceneCaptureRenderPath(true)
{
	CaptureSource = ESceneCaptureSource::SCS_FinalToneCurveHDR;
	bAlwaysPersistRenderingState = true;
}

void UCineCaptureComponent2D::UpdateSceneCaptureContents(FSceneInterface* Scene)
{
	if (!CineCameraComponent.IsValid() || !CineCaptureSVE.IsValid())
	{
		return;
	}

	SetRelativeTransform(FTransform::Identity);
	
	PostProcessSettings = CineCameraComponent->PostProcessSettings;
	FOVAngle = CineCameraComponent->FieldOfView;
	bOverride_CustomNearClippingPlane = CineCameraComponent->bOverride_CustomNearClippingPlane;
	CustomNearClippingPlane = CineCameraComponent->CustomNearClippingPlane;

	CineCaptureSVE->PrepareRender(CineCameraComponent, bFollowSceneCaptureRenderPath);

	Scene->UpdateSceneCaptureContents(this);
}

void UCineCaptureComponent2D::CheckResizeRenderTarget()
{
	// Making sure that censor's aspect ratio is reflected render target and that RenderTarget Dimension specified by the user is taken into account.
	if (TextureTarget)
	{
		float Ratio = TextureTarget->GetSurfaceWidth() / TextureTarget->GetSurfaceHeight();
		const float SmallNum = 1e-3;
		if ((FMath::Abs(Ratio - CineCameraComponent->AspectRatio) > SmallNum)
			|| (FMath::Max(TextureTarget->GetSurfaceWidth(), TextureTarget->GetSurfaceHeight()) != RenderTargetHighestDimension))
		{
			if (CineCameraComponent->AspectRatio >= 1.)
			{
				int32 LowestDim = FMath::Max(RenderTargetHighestDimension / CineCameraComponent->AspectRatio, 1);
				TextureTarget->ResizeTarget(RenderTargetHighestDimension, LowestDim);
			}
			else
			{
				int32 LowestDim = FMath::Max(RenderTargetHighestDimension * CineCameraComponent->AspectRatio, 1);
				TextureTarget->ResizeTarget(LowestDim, RenderTargetHighestDimension);
			}
		}
	}
}

void UCineCaptureComponent2D::OnRegister()
{
	Super::OnRegister();

#if WITH_EDITORONLY_DATA
	// Remove mesh created by Scene Capture Component.
	ProxyMeshComponent->DestroyComponent();
	ProxyMeshComponent = nullptr;
#endif

	if (!CineCaptureSVE.IsValid())
	{
		// This will create a new scene extension per each capture.
		CineCaptureSVE = MakeShared<FCineCameraCaptureSceneViewExtension>();
		SceneViewExtensions.Add(CineCaptureSVE);
	}

	ValidateCineCameraComponent();
}

void UCineCaptureComponent2D::OnUnregister()
{
	Super::OnUnregister();

	if (CineCaptureSVE.IsValid())
	{
		SceneViewExtensions.Remove(CineCaptureSVE);
		CineCaptureSVE.Reset();
	}

	CineCameraComponent = nullptr;
}

void UCineCaptureComponent2D::OnAttachmentChanged()
{
	// If no parent is present, then this component is in a transient state.
	if (USceneComponent* ParentComponent = GetAttachParent())
	{
		ValidateCineCameraComponent();
	}
}

void UCineCaptureComponent2D::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (CineCaptureSVE.IsValid())
	{
		CineCaptureSVE->SetFrameDeltaTime(DeltaTime);
	}

	// Since CineCameraComponent properties can change any time, render target needs to be validated every frame.
	// The rest of the properties are copied right before the view is setup.
	if (CineCameraComponent.IsValid())
	{
		CheckResizeRenderTarget();
	}
}

void UCineCaptureComponent2D::ValidateCineCameraComponent()
{
	if (!IsValid(this))
	{
		return;
	}

	CineCameraComponent = Cast<UCineCameraComponent>(GetAttachParent());

	if (!CineCameraComponent)
	{
#if WITH_EDITOR
		FNotificationInfo Info(FText::Format(LOCTEXT("AddCineCameraNotification", CINE_CAMERA_INVALID_PARENT_WARNING), FText::FromString(GetName()), FText::FromString(GetOuter()->GetName())));
		Info.ExpireDuration = 5.0f;

		FSlateNotificationManager::Get().AddNotification(Info);
#endif
		UE_LOG(LogCineCapture, Warning, TEXT(CINE_CAMERA_INVALID_PARENT_WARNING), *GetName(), *GetOuter()->GetName());
	}
}


#undef LOCTEXT_NAMESPACE
