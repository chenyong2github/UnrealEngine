// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "StageAppResponse.generated.h"

/**
 * Event sent to a client that created a renderer.
 */
USTRUCT()
struct FRCPreviewRendererCreatedEvent
{
	GENERATED_BODY()

	FRCPreviewRendererCreatedEvent()
	: Type(TEXT("PreviewRendererCreated"))
	{
	}

	/**
	 * Type of the event.
	 */
	UPROPERTY()
	FString Type;

	/**
	 * The ID of the new renderer
	 */
	UPROPERTY()
	int32 RendererId = -1;
};


/**
 * Event sent to a client that created a renderer.
 */
USTRUCT()
struct FRCPreviewRenderCompletedEvent
{
	GENERATED_BODY()

	FRCPreviewRenderCompletedEvent()
	: Type(TEXT("PreviewRenderCompleted"))
	{
	}

	/**
	 * Type of the event.
	 */
	UPROPERTY()
	FString Type;

	/**
	 * The ID of the renderer that created the image.
	 */
	UPROPERTY()
	int32 RendererId = -1;

	/**
	 * Base64 encoded JPEG data of the preview render.
	 */
	UPROPERTY()
	FString ImageBase64 = "";
};
