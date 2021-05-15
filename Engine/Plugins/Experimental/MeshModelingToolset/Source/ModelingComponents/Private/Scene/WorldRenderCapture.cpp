// Copyright Epic Games, Inc. All Rights Reserved.

#include "Scene/WorldRenderCapture.h"

#include "Engine/Canvas.h"
#include "Engine/TextureRenderTarget2D.h"
#include "LegacyScreenPercentageDriver.h"
#include "EngineModule.h"
#include "Rendering/Texture2DResource.h"

#include "PreviewScene.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ChildActorComponent.h"

using namespace UE::Geometry;



FRenderCaptureTypeFlags FRenderCaptureTypeFlags::All()
{
	return FRenderCaptureTypeFlags{ true, true, true, true, true, true };
}

FRenderCaptureTypeFlags FRenderCaptureTypeFlags::None()
{
	return FRenderCaptureTypeFlags{ false, false, false, false, false, false };
}

FRenderCaptureTypeFlags FRenderCaptureTypeFlags::BaseColor()
{
	return FRenderCaptureTypeFlags{ true, false, false, false, false, false };
}

FRenderCaptureTypeFlags FRenderCaptureTypeFlags::WorldNormal()
{
	return FRenderCaptureTypeFlags{ false, false, false, false, false, true };
}

FRenderCaptureTypeFlags FRenderCaptureTypeFlags::Single(ERenderCaptureType CaptureType)
{
	FRenderCaptureTypeFlags Flags = None();
	Flags.SetEnabled(CaptureType, true);
	return Flags;
}

void FRenderCaptureTypeFlags::SetEnabled(ERenderCaptureType CaptureType, bool bEnabled)
{
	switch (CaptureType)
	{
	case ERenderCaptureType::BaseColor:
		bBaseColor = bEnabled;
		break;
	case ERenderCaptureType::WorldNormal:
		bWorldNormal = bEnabled;
		break;
	case ERenderCaptureType::Roughness:
		bRoughness = bEnabled;
		break;
	case ERenderCaptureType::Metallic:
		bMetallic = bEnabled;
		break;
	case ERenderCaptureType::Specular:
		bSpecular = bEnabled;
		break;
	case ERenderCaptureType::Emissive:
		bEmissive = bEnabled;
		break;
	default:
		check(false);
	}
}




FWorldRenderCapture::FWorldRenderCapture()
{
	Dimensions = FImageDimensions(128, 128);
}

FWorldRenderCapture::~FWorldRenderCapture()
{
	Shutdown();
}

void FWorldRenderCapture::Shutdown()
{
	if (LinearRenderTexture != nullptr)
	{
		LinearRenderTexture->RemoveFromRoot();
		LinearRenderTexture = nullptr;
	}
	if (GammaRenderTexture != nullptr)
	{
		GammaRenderTexture->RemoveFromRoot();
		GammaRenderTexture = nullptr;
	}
}



void FWorldRenderCapture::SetWorld(UWorld* WorldIn)
{
	this->World = WorldIn;
}


void FWorldRenderCapture::SetVisibleActors(const TArray<AActor*>& Actors)
{
	CaptureActors = Actors;

	VisiblePrimitives.Reset();

	VisibleBounds = FBoxSphereBounds();
	bool bFirst = true;

	// Find all components that need to be included in rendering.
	// This also descends into any ChildActorComponents
	TArray<UActorComponent*> ComponentQueue;
	for (AActor* Actor : Actors)
	{
		FVector ActorOrigin, ActorExtent;
		Actor->GetActorBounds(false, ActorOrigin, ActorExtent, true);
		FBoxSphereBounds ActorBounds(ActorOrigin, ActorExtent, ActorExtent.Size());
		VisibleBounds = (bFirst) ? ActorBounds : (VisibleBounds + ActorBounds);
		bFirst = false;

		ComponentQueue.Reset();
		for (UActorComponent* Component : Actor->GetComponents())
		{
			ComponentQueue.Add(Component);
		}
		while (ComponentQueue.Num() > 0)
		{
			UActorComponent* Component = ComponentQueue.Pop(false);
			if (Cast<UPrimitiveComponent>(Component) != nullptr)
			{
				UPrimitiveComponent* PrimitiveComponent = Cast<UPrimitiveComponent>(Component);
				VisiblePrimitives.Add(PrimitiveComponent->ComponentId);
			}
			else if (Cast<UChildActorComponent>(Component) != nullptr)
			{
				AActor* ChildActor = Cast<UChildActorComponent>(Component)->GetChildActor();
				for (UActorComponent* SubComponent : ChildActor->GetComponents())
				{
					ComponentQueue.Add(SubComponent);
				}
			}
		}
	}
}


void FWorldRenderCapture::SetDimensions(const FImageDimensions& DimensionsIn)
{
	this->Dimensions = DimensionsIn;
}


FSphere FWorldRenderCapture::ComputeContainingRenderSphere(float HorzFOVDegrees, float SafetyBoundsScale) const
{
	if (VisiblePrimitives.Num() == 0)
	{
		ensure(false);		// unclear what we should do here - get bounds of all actors?
		return FSphere(FVector(0,0,0), 1000.0f);
	}

	// todo: I think this maybe needs to be based on the box corners? 

	const float HalfFOVRadians = FMath::DegreesToRadians<float>(HorzFOVDegrees) * 0.5f;
	const float HalfMeshSize = VisibleBounds.SphereRadius * SafetyBoundsScale;
	const float TargetDistance = HalfMeshSize / FMath::Tan(HalfFOVRadians);
	return FSphere(VisibleBounds.Origin, TargetDistance);
}







UTextureRenderTarget2D* FWorldRenderCapture::GetRenderTexture(bool bLinear)
{
	if (RenderTextureDimensions != this->Dimensions)
	{
		if (LinearRenderTexture != nullptr)
		{
			LinearRenderTexture->RemoveFromRoot();
			LinearRenderTexture = nullptr;
		}
		if (GammaRenderTexture != nullptr)
		{
			GammaRenderTexture->RemoveFromRoot();
			GammaRenderTexture = nullptr;
		}
	}

	UTextureRenderTarget2D** WhichTexture = (bLinear) ? &LinearRenderTexture : &GammaRenderTexture;

	if ( *WhichTexture != nullptr )
	{
		return *WhichTexture;
	}

	RenderTextureDimensions = Dimensions;
	int32 Width = Dimensions.GetWidth();
	int32 Height = Dimensions.GetHeight();

	*WhichTexture = NewObject<UTextureRenderTarget2D>();
	if (ensure(*WhichTexture))
	{
		(*WhichTexture)->AddToRoot();		// keep alive for GC
		(*WhichTexture)->ClearColor = FLinearColor::Transparent;
		(*WhichTexture)->TargetGamma = (bLinear) ? 1.0f : 2.2f;
		(*WhichTexture)->InitCustomFormat(Width, Height, PF_FloatRGBA, false);
	}

	return *WhichTexture;
}



bool FWorldRenderCapture::CaptureEmissiveFromPosition(
	const FFrame3d& ViewFrame,
	double HorzFOVDegrees,
	double NearPlaneDist,
	FImageAdapter& ResultImageOut)
{
	// This function is a combination of CaptureFromPosition() and RenderSceneVisualizationToTexture() below,
	// that attempts to capture Emissive. The difficulty with capturing Emissive is there is no Visualization
	// Buffer Mode specifically for it, like there is for BaseColor, Specular, WorldNormal, etc. So, the
	// strategy we take here is basically to disable all scene lights but not disable lighting. This should
	// result in only Emissive being rendered, ideally without any tone mapping, gamma, bloom, etc, so that we 
	// directly capture the raw Emissive shader output.
	//
	// One known possible issue here is with baked lighting, which may also appear with the current setup.
	// Currently untested.

	check(this->World);
	FSceneInterface* Scene = this->World->Scene;

	UTextureRenderTarget2D* RenderTargetTexture = GetRenderTexture(true);
	if (!ensure(RenderTargetTexture))
	{
		return false;
	}
	FRenderTarget* RenderTargetResource = RenderTargetTexture->GameThread_GetRenderTargetResource();

	int32 Width = Dimensions.GetWidth();
	int32 Height = Dimensions.GetHeight();

	FQuat ViewOrientation = (FQuat)ViewFrame.Rotation;
	FMatrix ViewRotationMatrix = FInverseRotationMatrix(ViewOrientation.Rotator());
	FVector ViewOrigin = (FVector)ViewFrame.Origin;

	// convert to rendering coordinate system
	ViewRotationMatrix = ViewRotationMatrix * FMatrix(
		FPlane(0, 0, 1, 0),
		FPlane(1, 0, 0, 0),
		FPlane(0, 1, 0, 0),
		FPlane(0, 0, 0, 1));

	const float HalfFOVRadians = FMath::DegreesToRadians<float>(HorzFOVDegrees) * 0.5f;
	static_assert((int32)ERHIZBuffer::IsInverted != 0, "Check NearPlane and Projection Matrix");
	FMatrix ProjectionMatrix = FReversedZPerspectiveMatrix(HalfFOVRadians, 1.0f, 1.0f, (float)NearPlaneDist);

	EViewModeIndex ViewModeIndex = EViewModeIndex::VMI_Unlit;		// VMI_Lit, VMI_LightingOnly, VMI_VisualizeBuffer

	FEngineShowFlags ShowFlags = FEngineShowFlags(EShowFlagInitMode::ESFIM_Game);
	ApplyViewMode(ViewModeIndex, true, ShowFlags);

	// unclear if these flags need to be set before creating ViewFamily
	ShowFlags.SetAntiAliasing(false);
	ShowFlags.SetDepthOfField(false);
	ShowFlags.SetMotionBlur(false);
	ShowFlags.SetBloom(false);
	ShowFlags.SetSceneColorFringe(false);

	FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
		RenderTargetResource, Scene, ShowFlags)
		.SetWorldTimes(0, 0, 0)
		.SetRealtimeUpdate(false));

	// This set of flags currently seems to work for capturing Emissive. Possibly many are unnecessary or
	// ignored due to ther rendering config, but it's quite hard to know without extensive A/B testing

	ViewFamily.EngineShowFlags.DisableAdvancedFeatures();
	ViewFamily.EngineShowFlags.MotionBlur = 0;
	ViewFamily.EngineShowFlags.LOD = 0;

	ViewFamily.EngineShowFlags.SetTonemapper(false);
	ViewFamily.EngineShowFlags.SetColorGrading(false);
	ViewFamily.EngineShowFlags.SetToneCurve(false);

	ViewFamily.EngineShowFlags.SetPostProcessing(false);
	ViewFamily.EngineShowFlags.SetFog(false);
	ViewFamily.EngineShowFlags.SetGlobalIllumination(false);
	ViewFamily.EngineShowFlags.SetEyeAdaptation(false);
	ViewFamily.EngineShowFlags.SetDirectionalLights(false);
	ViewFamily.EngineShowFlags.SetPointLights(false);
	ViewFamily.EngineShowFlags.SetSpotLights(false);
	ViewFamily.EngineShowFlags.SetRectLights(false);

	ViewFamily.EngineShowFlags.SetDiffuse(false);
	ViewFamily.EngineShowFlags.SetSpecular(false);

	ViewFamily.EngineShowFlags.SetDynamicShadows(false);
	ViewFamily.EngineShowFlags.SetCapsuleShadows(false);
	ViewFamily.EngineShowFlags.SetContactShadows(false);

	// some of these settings may need to be configured in more complex cases, currently untested
	//ViewFamily.SceneCaptureSource = SCS_FinalColorHDR;
	//ViewFamily.bWorldIsPaused = true;
	//ViewFamily.ViewMode = ViewModeIndex;
	// since we are capturing actual render we are not using visualize-buffer mode (?)
	//ViewFamily.EngineShowFlags.SetVisualizeBuffer(false);

	//ViewFamily.EngineShowFlags.SetScreenPercentage(false);
	ViewFamily.SetScreenPercentageInterface(new FLegacyScreenPercentageDriver(ViewFamily, 1.f, false));

	// This is called in various other places, unclear if we should be doing this too
	//EngineShowFlagOverride(EShowFlagInitMode::ESFIM_Game, ViewModeIndex, ViewFamily.EngineShowFlags, true);

	FSceneViewInitOptions ViewInitOptions;
	ViewInitOptions.SetViewRectangle(FIntRect(0, 0, Width, Height));
	ViewInitOptions.ViewFamily = &ViewFamily;
	if (VisiblePrimitives.Num() > 0)
	{
		ViewInitOptions.ShowOnlyPrimitives = VisiblePrimitives;
	}
	ViewInitOptions.ViewOrigin = ViewOrigin;
	ViewInitOptions.ViewRotationMatrix = ViewRotationMatrix;
	ViewInitOptions.ProjectionMatrix = ProjectionMatrix;
	ViewInitOptions.FOV = HorzFOVDegrees;

	FSceneView* NewView = new FSceneView(ViewInitOptions);
	ViewFamily.Views.Add(NewView);

	NewView->StartFinalPostprocessSettings(ViewInitOptions.ViewOrigin);
	NewView->EndFinalPostprocessSettings(ViewInitOptions);

	// other FSceneView settings that may need configuring to properly capture Emissive - needs testing
	//NewView->SetupRayTracedRendering();
	//NewView->bIsOfflineRender = true;
	//NewView->bIsSceneCapture = false;
	//NewView->AntiAliasingMethod = IsAntiAliasingSupported() ? InOutSampleState.AntiAliasingMethod : EAntiAliasingMethod::AAM_None;
	//NewView->AntiAliasingMethod = EAntiAliasingMethod::AAM_None;

	// can we fully disable auto-exposure?
	NewView->FinalPostProcessSettings.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;

	// do we actually need for force SM5 here? the other FCanvas() constructor below does not pass these flags...
	FCanvas Canvas = FCanvas(RenderTargetResource, nullptr, this->World, ERHIFeatureLevel::SM5, FCanvas::CDM_DeferDrawing, 1.0f);
	Canvas.Clear(FLinearColor::Transparent);
	GetRendererModule().BeginRenderingViewFamily(&Canvas, &ViewFamily);

	ReadImageBuffer.SetNumUninitialized(Width * Height);
	FReadSurfaceDataFlags ReadSurfaceDataFlags;
	ReadSurfaceDataFlags.SetLinearToGamma(false);
	RenderTargetResource->ReadLinearColorPixels(ReadImageBuffer, ReadSurfaceDataFlags, FIntRect(0, 0, Width, Height));

	FlushRenderingCommands();

	ResultImageOut.SetDimensions(Dimensions);
	for (int32 yi = 0; yi < Height; ++yi)
	{
		for (int32 xi = 0; xi < Width; ++xi)
		{
			FLinearColor PixelColorf = ReadImageBuffer[yi * Width + xi];
			PixelColorf.A = 1.0f;		// ?
			ResultImageOut.SetPixel(FVector2i(xi, yi), FVector4f(PixelColorf));
		}
	}

	return true;
}




/**
 * internal Utility function to render the given Scene to a render target and capture
 * one of the render buffers, defined by VisualizationMode. Not clear where
 * the valid VisualizationMode FNames are defined, possibly this list: "BaseColor,Specular,SubsurfaceColor,WorldNormal,SeparateTranslucencyRGB,,,WorldTangent,SeparateTranslucencyA,,,Opacity,SceneDepth,Roughness,Metallic,ShadingModel,,SceneDepthWorldUnits,SceneColor,PreTonemapHDRColor,PostTonemapHDRColor"
 */
namespace UE
{
namespace Internal
{ 
	static void RenderSceneVisualizationToTexture(
		UTextureRenderTarget2D* RenderTargetTexture,
		FImageDimensions Dimensions,
		FSceneInterface* Scene,
		const FName& VisualizationMode,
		const FVector& ViewOrigin,
		const FMatrix& ViewRotationMatrix,
		const FMatrix& ProjectionMatrix,
		const TSet<FPrimitiveComponentId>& HiddenPrimitives,		// these primitives will be hidden
		const TSet<FPrimitiveComponentId>& VisiblePrimitives,		// if non-empty, only these primitives are shown
		TArray<FLinearColor>& OutSamples
	)
	{
		int32 Width = Dimensions.GetWidth();
		int32 Height = Dimensions.GetHeight();
		FTextureRenderTargetResource* RenderTargetResource = RenderTargetTexture->GameThread_GetRenderTargetResource();

		FSceneViewFamilyContext ViewFamily(
			FSceneViewFamily::ConstructionValues(RenderTargetResource, Scene, FEngineShowFlags(ESFIM_Game))
			.SetWorldTimes(FApp::GetCurrentTime() - GStartTime, FApp::GetDeltaTime(), FApp::GetCurrentTime() - GStartTime)
		);

		// To enable visualization mode
		ViewFamily.EngineShowFlags.SetPostProcessing(true);
		ViewFamily.EngineShowFlags.SetVisualizeBuffer(true);
		ViewFamily.EngineShowFlags.SetTonemapper(false);
		ViewFamily.EngineShowFlags.SetScreenPercentage(false);

		FSceneViewInitOptions ViewInitOptions;
		ViewInitOptions.SetViewRectangle(FIntRect(0, 0, Width, Height));
		ViewInitOptions.ViewFamily = &ViewFamily;
		ViewInitOptions.HiddenPrimitives = HiddenPrimitives;
		if (VisiblePrimitives.Num() > 0)
		{
			ViewInitOptions.ShowOnlyPrimitives = VisiblePrimitives;
		}
		ViewInitOptions.ViewOrigin = ViewOrigin;
		ViewInitOptions.ViewRotationMatrix = ViewRotationMatrix;
		ViewInitOptions.ProjectionMatrix = ProjectionMatrix;

		FSceneView* NewView = new FSceneView(ViewInitOptions);
		NewView->CurrentBufferVisualizationMode = VisualizationMode;
		ViewFamily.Views.Add(NewView);

		ViewFamily.SetScreenPercentageInterface(new FLegacyScreenPercentageDriver(ViewFamily, 1.0f, false));

		// should we cache the FCanvas?
		FCanvas Canvas(RenderTargetResource, NULL, FApp::GetCurrentTime() - GStartTime, FApp::GetDeltaTime(), FApp::GetCurrentTime() - GStartTime, Scene->GetFeatureLevel());
		Canvas.Clear(FLinearColor::Transparent);

		GetRendererModule().BeginRenderingViewFamily(&Canvas, &ViewFamily);

		// Copy the contents of the remote texture to system memory
		OutSamples.SetNumUninitialized(Width * Height);
		FReadSurfaceDataFlags ReadSurfaceDataFlags;
		ReadSurfaceDataFlags.SetLinearToGamma(false);
		RenderTargetResource->ReadLinearColorPixels(OutSamples, ReadSurfaceDataFlags, FIntRect(0, 0, Width, Height));

		FlushRenderingCommands();
	}
}
}



bool FWorldRenderCapture::CaptureFromPosition(
	ERenderCaptureType CaptureType,
	const FFrame3d& ViewFrame,
	double HorzFOVDegrees,
	double NearPlaneDist,
	FImageAdapter& ResultImageOut)
{
	if (CaptureType == ERenderCaptureType::Emissive)
	{
		return CaptureEmissiveFromPosition(ViewFrame, HorzFOVDegrees, NearPlaneDist, ResultImageOut);
	}

	// Roughness visualization is rendered with gamma correction (unclear why)
	bool bLinear = (CaptureType != ERenderCaptureType::Roughness);
	UTextureRenderTarget2D* RenderTargetTexture = GetRenderTexture(bLinear);
	if (ensure(RenderTargetTexture) == false)
	{
		return false;
	}

	int32 Width = Dimensions.GetWidth();
	int32 Height = Dimensions.GetHeight();

	const FRotator RotationOffsetToViewCenter(0.f, 90.f, 0.f);
	FQuat ViewOrientation = (FQuat)ViewFrame.Rotation;
	FMatrix ViewRotationMatrix = FInverseRotationMatrix(ViewOrientation.Rotator());
	FVector ViewOrigin = (FVector)ViewFrame.Origin;

	// convert to rendering coordinate system
	ViewRotationMatrix = ViewRotationMatrix * FMatrix(
		FPlane(0, 0, 1, 0),
		FPlane(1, 0, 0, 0),
		FPlane(0, 1, 0, 0),
		FPlane(0, 0, 0, 1));

	const float HalfFOVRadians = FMath::DegreesToRadians<float>(HorzFOVDegrees) * 0.5f;
	static_assert((int32)ERHIZBuffer::IsInverted != 0, "Check NearPlane and Projection Matrix");
	FMatrix ProjectionMatrix = FReversedZPerspectiveMatrix( HalfFOVRadians, 1.0f, 1.0f, (float)NearPlaneDist );

	FName CaptureTypeName = FName("BaseColor");
	switch (CaptureType)
	{
	case ERenderCaptureType::WorldNormal:	CaptureTypeName = FName("WorldNormal");	break;
	case ERenderCaptureType::Roughness:		CaptureTypeName = FName("Roughness");	break;
	case ERenderCaptureType::Metallic:		CaptureTypeName = FName("Metallic");	break;
	case ERenderCaptureType::Specular:		CaptureTypeName = FName("Specular");	break;
	case ERenderCaptureType::BaseColor:		CaptureTypeName = FName("BaseColor");	break;
	}

	ReadImageBuffer.Reset();
	TSet<FPrimitiveComponentId> HiddenPrimitives;
	UE::Internal::RenderSceneVisualizationToTexture( 
		RenderTargetTexture, this->Dimensions,
		World->Scene, CaptureTypeName,
		ViewOrigin, ViewRotationMatrix, ProjectionMatrix,
		HiddenPrimitives,
		VisiblePrimitives,
		ReadImageBuffer);

	ResultImageOut.SetDimensions(Dimensions);
	for (int32 yi = 0; yi < Height; ++yi)
	{
		for (int32 xi = 0; xi < Width; ++xi)
		{
			//FColor PixelColor = OutputImage[yi * Width + xi];
			//FLinearColor PixelColorf = PixelColor.ReinterpretAsLinear();
			FLinearColor PixelColorf = ReadImageBuffer[yi * Width + xi];
			PixelColorf.A = 1.0f;		// ?
			ResultImageOut.SetPixel(FVector2i(xi, yi), FVector4f(PixelColorf));
		}
	}

	return true;
}
