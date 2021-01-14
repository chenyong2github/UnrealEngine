// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "OverlayRenderingParameters.h"

#include "Components/SceneCaptureComponent2D.h"
#include "CineCameraComponent.h"
#include "TextureResource.h"

#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Scene.h"

#include "ReprojectionBlendingData.generated.h"


//////////////////////////////////////////////////////////////////////////////////////////////
// Blend inputs
//////////////////////////////////////////////////////////////////////////////////////////////

USTRUCT(BlueprintType, Category = "PICP")
struct FPicpFrameBlendingParameters
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PICP")
	TArray<USceneCaptureComponent2D*> SourceFrames;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PICP")
	UTextureRenderTarget2D* DestinationFrame;

	FPicpFrameBlendingParameters()
		: DestinationFrame(nullptr)
	{}
};

USTRUCT(BlueprintType, Category = "PICP")
struct FPicpOverlayFrameBlendingPair
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PICP")
	FString Id;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PICP")
	UTextureRenderTarget2D* SourceFrameCapture;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PICP")
	ECameraOverlayRenderMode OverlayBlendMode = ECameraOverlayRenderMode::Over;

	FPicpOverlayFrameBlendingPair()
		: SourceFrameCapture(nullptr)
	{}
};

USTRUCT(BlueprintType, Category = "PICP")
struct FPicpCameraChromakey
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PICP")
	UTextureRenderTarget2D* ChromakeyOverlayFrame;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PICP")
	UTexture2D* ChromakeyMarkerTexture;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PICP")
	float ChromakeyMarkerScale = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PICP")
	EChromakeyMarkerUVSource ChromakeyMarkerUVSource = EChromakeyMarkerUVSource::ScreenSpace;

	FPicpCameraChromakey()
		: ChromakeyOverlayFrame(nullptr)
		, ChromakeyMarkerTexture(nullptr)
	{}
};

USTRUCT(BlueprintType, Category = "PICP")
struct FPicpCameraBlendingParameters
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PICP")
	UTextureRenderTarget2D* CameraOverlayFrame;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PICP")
	FVector  SoftEdge;    // Basic soft edges values

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PICP")
	UCineCameraComponent* CineCamera;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PICP")
	float FieldOfViewMultiplier = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PICP")
	FString RTTViewportId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PICP")
	FPicpCameraChromakey  CameraChromakey;

	FPicpCameraBlendingParameters()
		: CameraOverlayFrame(nullptr)
		, SoftEdge(ForceInitToZero)
		, CineCamera(nullptr)
	{}
};
