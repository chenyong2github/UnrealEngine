// Copyright Epic Games, Inc. All Rights Reserved.

#include "ImgMediaMipMapInfoManager.h"
#include "ImgMediaEngine.h"
#include "ImgMediaPrivate.h"
#include "MediaPlayer.h"
#include "MediaPlayerFacade.h"
#include "MediaTexture.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/GameEngine.h"
#include "Engine/GameViewportClient.h"
#include "MovieSceneCommonHelpers.h"

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
	FImgMediaEngine& ImgMediaEngine = FImgMediaEngine::Get();

	// Look through all the media textures we know about.
	const TArray<TWeakObjectPtr<UMediaTexture>>& MediaTextures = ImgMediaEngine.GetTextures();
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

	// Get camera info from the engine.
	FImgMediaEngine::Get().GetCameraInfo(CameraInfos);
	// Take screen size into account if set.
	for (FImgMediaMipMapCameraInfo& CameraInfo : CameraInfos)
	{
		if (CameraInfo.ScreenSize != 0.0f)
		{
			float ScreenAdjust = RefFrameBufferWidth / CameraInfo.ScreenSize;
			CameraInfo.DistAdjust *= ScreenAdjust;
		}
	}

	// Loop over all cameras.
	for (TWeakObjectPtr<UCameraComponent> CameraPtr : Cameras)
	{
		UCameraComponent* CameraComponent = CameraPtr.Get();
		if (CameraComponent != nullptr)
		{
			// Get info.
			FString Name;
			AActor* Owner = CameraComponent->GetOwner();
			if (Owner != nullptr)
			{
				Name = Owner->GetName();
			}
			else
			{
				Name = "Unknown";
			}
			FMinimalViewInfo CameraView;
			CameraComponent->GetCameraView(FApp::GetDeltaTime(), CameraView);
			float NearPlaneSize = CalculateNearPlaneSize(CameraView.FOV);
			float DistAdjust = NearPlaneSize / RefNearPlaneWidth;
			CameraInfos.Emplace(Name, CameraView.Location, 0.0f, DistAdjust);
		}
	}
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
