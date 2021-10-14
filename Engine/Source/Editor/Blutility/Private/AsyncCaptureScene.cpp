// Copyright Epic Games, Inc. All Rights Reserved.

#include "AsyncCaptureScene.h"
#include "EditorUtilitySubsystem.h"
#include "Editor.h"
#include "Camera/CameraComponent.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Components/SceneCaptureComponent2D.h"
#include "ImageWriteQueue.h"
#include "Modules/ModuleManager.h"
#include "ImagePixelData.h"
#include "Engine/GameEngine.h"
#include "ContentStreaming.h"
#include "HAL/PlatformProperties.h"
#include "ShaderCompiler.h"
#include "IAutomationControllerManager.h"
#include "IAutomationControllerModule.h"

//----------------------------------------------------------------------//
// UAsyncCaptureScene
//----------------------------------------------------------------------//

UAsyncCaptureScene::UAsyncCaptureScene()
{
}

UAsyncCaptureScene* UAsyncCaptureScene::CaptureSceneAsync(UCameraComponent* ViewCamera, TSubclassOf<ASceneCapture2D> SceneCaptureClass, int ResX, int ResY)
{
	UAsyncCaptureScene* AsyncTask = NewObject<UAsyncCaptureScene>();
	AsyncTask->Start(ViewCamera, SceneCaptureClass, ResX, ResY);

	return AsyncTask;
}

void UAsyncCaptureScene::Start(UCameraComponent* ViewCamera, TSubclassOf<ASceneCapture2D> SceneCaptureClass, int ResX, int ResY)
{
	const FVector CaptureLocation = ViewCamera->GetComponentLocation();
	const FRotator CaptureRotation = ViewCamera->GetComponentRotation();

	UWorld* World = ViewCamera->GetWorld();
	SceneCapture = World->SpawnActor<ASceneCapture2D>(SceneCaptureClass, CaptureLocation, CaptureRotation);
	if (SceneCapture)
	{
		USceneCaptureComponent2D* CaptureComponent = SceneCapture->GetCaptureComponent2D();

		if (CaptureComponent->TextureTarget == nullptr)
		{
			SceneCaptureRT = NewObject<UTextureRenderTarget2D>(this, TEXT("AsyncCaptureScene_RT"), RF_Transient);
			SceneCaptureRT->RenderTargetFormat = RTF_RGBA8_SRGB;
			SceneCaptureRT->InitAutoFormat(ResX, ResY);
			SceneCaptureRT->UpdateResourceImmediate(true);

			CaptureComponent->TextureTarget = SceneCaptureRT;
		}
		else
		{
			SceneCaptureRT = CaptureComponent->TextureTarget;
		}

		FMinimalViewInfo CaptureView;
		ViewCamera->GetCameraView(0, CaptureView);
		CaptureComponent->SetCameraView(CaptureView);
	}
}

void UAsyncCaptureScene::Activate()
{
	if (!SceneCapture)
	{
		NotifyComplete(nullptr);
	}

	FinishLoadingBeforeScreenshot();

	USceneCaptureComponent2D* CaptureComponent = SceneCapture->GetCaptureComponent2D();
	CaptureComponent->CaptureScene();

	FinishLoadingBeforeScreenshot();

	CaptureComponent->CaptureScene();

	NotifyComplete(SceneCaptureRT);
}

void UAsyncCaptureScene::NotifyComplete(UTextureRenderTarget2D* InTexture)
{
	Complete.Broadcast(InTexture);
	SetReadyToDestroy();

	if (SceneCapture)
	{
		SceneCapture->Destroy();
	}
}

void UAsyncCaptureScene::FinishLoadingBeforeScreenshot()
{
	// Finish compiling the shaders if the platform doesn't require cooked data.
	if (!FPlatformProperties::RequiresCookedData())
	{
		GShaderCompilingManager->FinishAllCompilation();
		IAutomationControllerModule& AutomationController = IAutomationControllerModule::Get();
		AutomationController.GetAutomationController()->ResetAutomationTestTimeout(TEXT("shader compilation"));
	}

	FlushAsyncLoading();

	// Make sure we finish all level streaming
	if (UGameEngine* GameEngine = Cast<UGameEngine>(GEngine))
	{
		if (UWorld* GameWorld = GameEngine->GetGameWorld())
		{
			GameWorld->FlushLevelStreaming(EFlushLevelStreamingType::Full);
		}
	}

	// Force all mip maps to load before taking the screenshot.
	UTexture::ForceUpdateTextureStreaming();

	IStreamingManager::Get().StreamAllResources(0.0f);
}