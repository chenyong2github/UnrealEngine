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

public:
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

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PICP")
	FString Id;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PICP")
	UTextureRenderTarget2D* SourceFrameCapture;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PICP")
	ECameraOverlayRenderMode OverlayBlendMode;

	FPicpOverlayFrameBlendingPair()
		: SourceFrameCapture(nullptr)
	{}
};


USTRUCT(BlueprintType, Category = "PICP")
struct FPicpCameraChromakey
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PICP")
	UTextureRenderTarget2D* ChromakeyOverlayFrame;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PICP")
	UTexture2D* ChromakeyMarkerTexture;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PICP")
	float ChromakeyMarkerScale = 1;

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

public:
	/** Maximum NumMips, generated for camera texture. To disable set to 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PICP")
	int NumMips = 32;

	/** Basic soft edges values */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PICP")
	FVector  SoftEdge;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PICP")
	UCineCameraComponent* CineCamera;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PICP")
	float FieldOfViewMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PICP")
	FString RTTViewportId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PICP")
	FPicpCameraChromakey  CameraChromakey;

	/** Assign any texture to override camera image*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PICP")
	UTexture2D* CustomCameraTexture;

	FPicpCameraBlendingParameters()
		: CineCamera(nullptr)
		, CustomCameraTexture(nullptr)
	{ }
};
