// Copyright Epic Games, Inc. All Rights Reserved.

#include "ImgMediaMipMapInfoManager.h"
#include "ImgMediaEngine.h"
#include "ImgMediaPrivate.h"
#include "MediaPlayer.h"
#include "MediaPlayerFacade.h"
#include "MediaTexture.h"
#include "MediaTextureTracker.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/GameEngine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "MovieSceneCommonHelpers.h"
#include "SceneView.h"

#if WITH_EDITOR
#include "EditorViewportClient.h"
#include "LevelEditorViewport.h"
#endif

static TAutoConsoleVariable<bool> CVarImgMediaMipMapDebugEnable(
	TEXT("ImgMedia.MipMapDebug"),
	0,
	TEXT("Display debug on mipmaps used by the ImgMedia plugin.\n")
	TEXT("   0: off (default)\n")
	TEXT("   1: on\n"),
	ECVF_Default);

FImgMediaMipMapInfoManager::FImgMediaMipMapInfoManager()
	: RefNearPlaneWidth(0.0f)
	, MipLevel0Distance(0.0f)
	, ViewportDistAdjust(0.0f)
{
	// Get size of the near plane in physical space.
	RefNearPlaneWidth = CalculateNearPlaneSize(RefCameraFOV);

	// Get size of the near plane in physical space.
	float NearPlaneWidth = RefNearPlaneWidth;

	// Calculate how big the object is at the near plane (i.e. the screen).
	float ObjectWidth = RefObjectWidth;
	float ObjectDistance = RefObjectDistance;
	float NearPlaneDistance = GetRefNearPlaneDistance();
	float ObjectWidthAtNearPlane = (ObjectWidth / ObjectDistance) * NearPlaneDistance;

	float FrameBufferWidth = RefFrameBufferWidth;
	float ObjectTextureWidth = RefObjectTextureWidth;

	// Calculate how big in frame buffer pixels the object is on the screen.
	float ObjectPixelWidth = (ObjectWidthAtNearPlane / NearPlaneWidth) * FrameBufferWidth;
	// Calculate how many texels are displayed per frame buffer pixel.
	float ObjectTexelsPerPixel = ObjectTextureWidth / ObjectPixelWidth;
	// How far the object should be so that one texel covers one frame buffer pixel.
	float ObjectDistanceForOneTexelPerPixel = ObjectDistance / ObjectTexelsPerPixel;

	MipLevel0Distance = ObjectDistanceForOneTexelPerPixel;

	CVarImgMediaMipMapDebugEnable.AsVariable()->SetOnChangedCallback(FConsoleVariableDelegate::CreateRaw(this, &FImgMediaMipMapInfoManager::OnCVarMipMapDebugEnable));
	
	// Make sure info is set up.
	Tick(0.0f);
}

FImgMediaMipMapInfoManager& FImgMediaMipMapInfoManager::Get()
{
	static FImgMediaMipMapInfoManager Instance;
	return Instance;
}

void FImgMediaMipMapInfoManager::AddCamera(AActor* InActor)
{
	if (InActor != nullptr)
	{
		UCameraComponent* CameraComponent = MovieSceneHelpers::CameraComponentFromRuntimeObject(InActor);
		if (CameraComponent != nullptr)
		{
			Cameras.Add(CameraComponent);
		}
		else
		{
			UE_LOG(LogImgMedia, Error, TEXT("FImgMediaMipMapInfoManager::AddCamera: %s is not a camera."), *InActor->GetName());
		}
	}
}

void FImgMediaMipMapInfoManager::RemoveCamera(AActor* InActor)
{
	if (InActor != nullptr)
	{
		UCameraComponent* CameraComponent = MovieSceneHelpers::CameraComponentFromRuntimeObject(InActor);
		if (CameraComponent != nullptr)
		{
			Cameras.RemoveSwap(CameraComponent);
		}
		else
		{
			UE_LOG(LogImgMedia, Error, TEXT("FImgMediaMipMapInfoManager::RemoveCamera: %s is not a camera."), *InActor->GetName());
		}
	}
}

void FImgMediaMipMapInfoManager::GetMediaTexturesFromPlayer(TArray<UMediaTexture*>& OutMediaTextures, IMediaPlayer* InPlayer)
{
	FMediaTextureTracker& TextureTracker = FMediaTextureTracker::Get();

	// Look through all the media textures we know about.
	const TArray<TWeakObjectPtr<UMediaTexture>>& MediaTextures = TextureTracker.GetTextures();
	for (TWeakObjectPtr<UMediaTexture> TexturePtr : MediaTextures)
	{
		UMediaTexture* Texture = TexturePtr.Get();
		if (Texture != nullptr)
		{
			// Does this match the player?
			UMediaPlayer* UPlayer = Texture->GetMediaPlayer();
			if (UPlayer != nullptr)
			{
				TSharedPtr<IMediaPlayer, ESPMode::ThreadSafe> Player = UPlayer->GetPlayerFacade()->GetPlayer();
				if (Player.IsValid())
				{
					if (Player.Get() == InPlayer)
					{
						OutMediaTextures.Add(Texture);
					}
				}
			}
		}
	}
}

void FImgMediaMipMapInfoManager::Tick(float DeltaTime)
{
	UpdateViewportInfo();
	UpdateCameraInfo();
}

void FImgMediaMipMapInfoManager::UpdateViewportInfo()
{
	// Get main viewport.
	UEngine* Engine = GEngine;
	if (Engine != nullptr)
	{
		UGameViewportClient* Viewport = Engine->GameViewport;
		if (Viewport != nullptr)
		{
			FVector2D ViewportSize;
			Viewport->GetViewportSize(ViewportSize);
			ViewportDistAdjust = RefFrameBufferWidth / ViewportSize.X;
		}
	}
}

void FImgMediaMipMapInfoManager::UpdateCameraInfo()
{
	CameraInfos.Empty();

	const FString Name = "Unknown";

	// Loop through players.
	UGameViewportClient* GameViewportClient = GEngine->GameViewport;
	if (GameViewportClient != nullptr)
	{
		UWorld* World = GameViewportClient->GetWorld();
		if (World != nullptr)
		{
			for (FConstPlayerControllerIterator Iterator = World->GetPlayerControllerIterator(); Iterator; ++Iterator)
			{
				APlayerController* PlayerController = Iterator->Get();
				ULocalPlayer* LocalPlayer = PlayerController->GetLocalPlayer();
				if (LocalPlayer != nullptr)
				{
					// Get view info.
					FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
						LocalPlayer->ViewportClient->Viewport,
						World->Scene,
						LocalPlayer->ViewportClient->EngineShowFlags)
						.SetRealtimeUpdate(true));
					FVector ViewLocation;
					FRotator ViewRotation;
					FSceneView* SceneView = LocalPlayer->CalcSceneView(&ViewFamily, ViewLocation, ViewRotation, LocalPlayer->ViewportClient->Viewport);

					// Did we get a view?
					if (SceneView != nullptr)
					{
						// Add camera info.
						float NearPlaneSize = CalculateNearPlaneSize(SceneView->FOV);
						float DistAdjust = NearPlaneSize / RefNearPlaneWidth;
						CameraInfos.Emplace(Name, ViewLocation, 0.0f, DistAdjust);
					}
				}
			}
		}
	}

#if WITH_EDITOR
	// If we are have no cameras then we could be in the editor and not PIE so get the editor view.
	if ((CameraInfos.IsEmpty()) && (GEditor != nullptr))
	{
		// Loop through all viewport clients.
		for (FEditorViewportClient* ViewportClient : GEditor->GetAllViewportClients())
		{
			if (ViewportClient != nullptr) 
			{
				FViewport* Viewport = ViewportClient->Viewport;
				if (Viewport != nullptr)
				{
					// The editor has a few viewports,
					// and some that we do not care about have no size,
					// so filter on this.
					FIntPoint ViewportSize = Viewport->GetSizeXY();
					if ((ViewportSize.X > 0) && (ViewportSize.Y > 0))
					{
						// Get the view.
						const FViewportCameraTransform& ViewTransform = ViewportClient->GetViewTransform();
						
						// Add camera info.
						float NearPlaneSize = CalculateNearPlaneSize(ViewportClient->ViewFOV);
						float DistAdjust = NearPlaneSize / RefNearPlaneWidth;
						DistAdjust *= RefFrameBufferWidth / ViewportSize.X;
						CameraInfos.Emplace(Name, ViewTransform.GetLocation(), ViewportSize.X, DistAdjust);
					}
				}
			}
		}
	}
#endif
}

float FImgMediaMipMapInfoManager::CalculateNearPlaneSize(float InFOV) const
{
	float NearPlaneDistance = RefNearPlaneDistance;
	float NearPlaneWidth = FMath::Tan(FMath::DegreesToRadians(InFOV) * 0.5f) * NearPlaneDistance * 2.0f;

	return NearPlaneWidth;
}

void FImgMediaMipMapInfoManager::OnCVarMipMapDebugEnable(IConsoleVariable* Var)
{
	bIsDebugEnabled = Var->GetBool();
}
