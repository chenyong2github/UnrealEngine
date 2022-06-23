// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "MediaPlayer.h"
#include "MediaSource.h"
#include "MediaTexture.h"
#include "MediaOutput.h"

#include "Engine/TextureRenderTarget2D.h"

#include "DisplayClusterConfigurationTypes_Media.generated.h"


/*
 * Media input settings
 */
USTRUCT(Blueprintable)
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationMediaInput
{
	GENERATED_BODY()

public:
	/** Enable/disable media input */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Media Input", meta = (DisplayName = "Enable"))
	bool bEnabled = false;

	/** Media source to use */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Media Input", meta = (EditCondition = "bEnabled"))
	TObjectPtr<UMediaSource> MediaSource = nullptr;

	/** Media player to use */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Media Input", meta = (EditCondition = "bEnabled"))
	TObjectPtr<UMediaPlayer> MediaPlayer = nullptr;

	/** Media texture to use */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Media Input", meta = (EditCondition = "bEnabled"))
	TObjectPtr<UMediaTexture> MediaTexture = nullptr;
};


/*
 * Media output settings
 */
USTRUCT(Blueprintable)
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationMediaOutput
{
	GENERATED_BODY()

public:
	/** Enable/disable media capture */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Media Output", meta = (DisplayName = "Enable"))
	bool bEnabled = false;

	/** Media output to use */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Media Output", meta = (EditCondition = "bEnabled"))
	TObjectPtr<UMediaOutput> MediaOutput = nullptr;

	/** Render target to use */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Media Output", meta = (EditCondition = "bEnabled"))
	TObjectPtr<UTextureRenderTarget2D> RenderTarget = nullptr;
};


/*
 * Media settings
 */
USTRUCT(Blueprintable)
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationMedia
{
	GENERATED_BODY()

public:
	/** Enable/disable media features */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Media", meta = (DisplayName = "Enable"))
	bool bEnabled = false;

	/** Media input settings */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Media", meta = (EditCondition = "bEnabled"))
	FDisplayClusterConfigurationMediaInput MediaInput;

	/** Media capture settings */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Media", meta = (EditCondition = "bEnabled"))
	FDisplayClusterConfigurationMediaOutput MediaOutput;
};

/*
 * ICVFX media settings
 */
USTRUCT(Blueprintable)
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationICVFXMedia
	: public FDisplayClusterConfigurationMedia
{
	GENERATED_BODY()

public:
	/** When in-cluster media sharing us used, the cluster node specified here will be used as a source (Tx node) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Media", meta = (EditCondition = "bEnabled"))
	FString MediaOutputNode;

public:
	/** Returns true if media sharing is used */
	bool IsMediaSharingRequired() const
	{
		return bEnabled && MediaInput.bEnabled && MediaOutput.bEnabled;
	}
};
