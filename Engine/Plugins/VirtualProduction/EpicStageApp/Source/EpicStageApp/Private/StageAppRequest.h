// Copyright Epic Games, Inc. All Rights Reserved.
#pragma  once

#include "CoreMinimal.h"
#include "IDisplayClusterScenePreview.h"
#include "RemoteControlWebsocketRoute.h"

#include "StageAppRequest.generated.h"

/** Type of preview render to perform, exposed as a UENUM. Corresponds to EDisplayClusterMeshProjectionOutput. */
UENUM()
enum class ERCWebSocketNDisplayPreviewRenderType : uint8
{
	Color,
	Normals
};

/** Type of projection to use for a preview render, exposed as a UENUM. Corresponds to EDisplayClusterMeshProjectionType. */
UENUM()
enum class ERCWebSocketNDisplayPreviewRenderProjectionType : uint8
{
	Perspective,
	Azimuthal
};

/**
 * Holds a request made via websocket to create an nDisplay preview renderer.
 */
USTRUCT()
struct FRCWebSocketNDisplayPreviewRendererCreateBody : public FRCRequest
{
	GENERATED_BODY()

	FRCWebSocketNDisplayPreviewRendererCreateBody()
	{
		AddStructParameter(ParametersFieldLabel());
	}

	/**
	 * Get the label for the property value struct.
	 */
	static FString ParametersFieldLabel() { return TEXT("Parameters"); }

	/**
	 * The path of the root DisplayCluster actor to preview. This may be empty, in which case you can set the root actor later.
	 */
	UPROPERTY()
	FString RootActorPath = "";
};

/**
 * Holds a request made via websocket to change the root DisplayCluster actor of an nDisplay preview renderer.
 */
USTRUCT()
struct FRCWebSocketNDisplayPreviewRendererSetRootBody : public FRCRequest
{
	GENERATED_BODY()

	FRCWebSocketNDisplayPreviewRendererSetRootBody()
	{
		AddStructParameter(ParametersFieldLabel());
	}

	/**
	 * Get the label for the property value struct.
	 */
	static FString ParametersFieldLabel() { return TEXT("Parameters"); }

	/**
	 * The ID of the renderer returned when it was created.
	 */
	UPROPERTY()
	int32 RendererId = -1;

	/**
	 * The path of the root DisplayCluster actor to preview.
	 */
	UPROPERTY()
	FString RootActorPath = "";
};

/**
 * Holds a request made via websocket to destroy an nDisplay preview renderer.
 */
USTRUCT()
struct FRCWebSocketNDisplayPreviewRendererDestroyBody : public FRCRequest
{
	GENERATED_BODY()

	FRCWebSocketNDisplayPreviewRendererDestroyBody()
	{
		AddStructParameter(ParametersFieldLabel());
	}

	/**
	 * Get the label for the property value struct.
	 */
	static FString ParametersFieldLabel() { return TEXT("Parameters"); }

	/**
	 * The ID of the renderer returned when it was created.
	 */
	UPROPERTY()
	int32 RendererId = -1;
};

/**
 * Holds a request made via websocket to render a preview of a display cluster.
 */
USTRUCT()
struct FRCWebSocketNDisplayPreviewRenderBody : public FRCRequest
{
	GENERATED_BODY()

	FRCWebSocketNDisplayPreviewRenderBody()
	{
		AddStructParameter(ParametersFieldLabel());
	}

	/**
	 * Get the label for the property value struct.
	 */
	static FString ParametersFieldLabel() { return TEXT("Parameters"); }

	/**
	 * The ID of the renderer returned when it was created.
	 */
	UPROPERTY()
	int32 RendererId = -1;

	/**
	 * The type of render to perform.
	 */
	UPROPERTY()
	ERCWebSocketNDisplayPreviewRenderType RenderType = ERCWebSocketNDisplayPreviewRenderType::Color;

	/**
	 * The type of projection to use.
	 */
	UPROPERTY()
	ERCWebSocketNDisplayPreviewRenderProjectionType ProjectionType = ERCWebSocketNDisplayPreviewRenderProjectionType::Azimuthal;

	/**
	 * The resolution of the image to render.
	 */
	UPROPERTY()
	FIntPoint Resolution = FIntPoint(1024, 1024);

	/**
	 * The preview camera's field of view (both horizontal and vertical) in degrees.
	 */
	UPROPERTY()
	float FOV = 130.0f;

	/**
	 * The preview camera's Euler rotation relative to the camera's actual rotation in the scene.
	 */
	UPROPERTY()
	FRotator Rotation = FRotator(90, 0, 0);

	/**
	 * The quality of the JPEG to send back to the requesting client, in range 50-100.
	 */
	UPROPERTY()
	int32 JpegQuality = 50;
};
