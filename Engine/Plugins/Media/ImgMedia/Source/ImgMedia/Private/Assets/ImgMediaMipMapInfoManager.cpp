// Copyright Epic Games, Inc. All Rights Reserved.

#include "ImgMediaMipMapInfoManager.h"
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
{
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
	UpdateCameraInfo();
}

void FImgMediaMipMapInfoManager::UpdateCameraInfo()
{
	CameraInfos.Empty();

	// Loop through players.
	UGameViewportClient* GameViewportClient = GEngine->GameViewport;
	if (GameViewportClient != nullptr)
	{
		FViewport* Viewport = GameViewportClient->Viewport;
		if (Viewport != nullptr)
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
						// Get the scene view.
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
							// Add camera info for this scene view.
							AddCameraInfo(Viewport, SceneView);
						}
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
						// Get the scene view.
						FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
							Viewport,
							ViewportClient->GetScene(),
							ViewportClient->EngineShowFlags)
							.SetRealtimeUpdate(ViewportClient->IsRealtime()));
						FSceneView* SceneView = ViewportClient->CalcSceneView(&ViewFamily);
						
						// Add camera info for this scene view.
						AddCameraInfo(Viewport, SceneView);
					}
				}
			}
		}
	}
#endif
}

void FImgMediaMipMapInfoManager::AddCameraInfo(FViewport* Viewport, FSceneView* SceneView)
{
	FImgMediaMipMapCameraInfo Info;
	Info.Location = SceneView->ViewMatrices.GetViewOrigin();
	Info.ViewMatrix = SceneView->ViewMatrices.GetViewMatrix();
	Info.ViewProjectionMatrix = SceneView->ViewMatrices.GetViewProjectionMatrix();
	Info.ViewportRect = FIntRect(FIntPoint::ZeroValue, Viewport->GetSizeXY());
	Info.MaterialTextureMipBias = SceneView->MaterialTextureMipBias;

	CameraInfos.Emplace(MoveTemp(Info));
}

void FImgMediaMipMapInfoManager::OnCVarMipMapDebugEnable(IConsoleVariable* Var)
{
	bIsDebugEnabled = Var->GetBool();
}
