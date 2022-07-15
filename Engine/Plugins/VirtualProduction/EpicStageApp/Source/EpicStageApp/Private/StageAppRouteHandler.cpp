// Copyright Epic Games, Inc. All Rights Reserved.

#include "StageAppRouteHandler.h"

#include "CanvasTypes.h"
#include "Components/DisplayClusterCameraComponent.h"
#include "DisplayClusterLightCardActor.h"
#include "DisplayClusterRootActor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "IDisplayClusterScenePreview.h"
#include "IImageWrapperModule.h"
#include "Misc/Base64.h"
#include "Modules/ModuleManager.h"
#include "RemoteControlWebsocketRoute.h"
#include "StageAppRequest.h"
#include "StageAppResponse.h"
#include "WebRemoteControlUtils.h"

void FStageAppRouteHandler::RegisterRoutes(IWebRemoteControlModule& WebRemoteControl)
{
	RemoteControlModule = &WebRemoteControl;
	RemoteControlModule->OnWebSocketConnectionClosed().AddRaw(this, &FStageAppRouteHandler::HandleClientDisconnected);

	ImageWrapperModule = &FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));

	RegisterRoute(MakeUnique<FRemoteControlWebsocketRoute>(
		TEXT("Create a preview renderer for a specific nDisplay cluster"),
		TEXT("ndisplay.preview.renderer.create"),
		FWebSocketMessageDelegate::CreateRaw(this, &FStageAppRouteHandler::HandleWebSocketNDisplayPreviewRendererCreate)
	));

	RegisterRoute(MakeUnique<FRemoteControlWebsocketRoute>(
		TEXT("Set the root DisplayCluster actor for an nDisplay preview renderer"),
		TEXT("ndisplay.preview.renderer.setroot"),
		FWebSocketMessageDelegate::CreateRaw(this, &FStageAppRouteHandler::HandleWebSocketNDisplayPreviewRendererSetRoot)
	));

	RegisterRoute(MakeUnique<FRemoteControlWebsocketRoute>(
		TEXT("Destroy an nDisplay preview renderer"),
		TEXT("ndisplay.preview.renderer.destroy"),
		FWebSocketMessageDelegate::CreateRaw(this, &FStageAppRouteHandler::HandleWebSocketNDisplayPreviewRendererDestroy)
	));

	RegisterRoute(MakeUnique<FRemoteControlWebsocketRoute>(
		TEXT("Request a preview render from an nDisplay preview renderer"),
		TEXT("ndisplay.preview.render"),
		FWebSocketMessageDelegate::CreateRaw(this, &FStageAppRouteHandler::HandleWebSocketNDisplayPreviewRender)
	));
}

void FStageAppRouteHandler::UnregisterRoutes(IWebRemoteControlModule& WebRemoteControl)
{
	if (RemoteControlModule == &WebRemoteControl)
	{
		for (const TUniquePtr<FRemoteControlWebsocketRoute>& Route : Routes)
		{
			WebRemoteControl.UnregisterWebsocketRoute(*Route);
		}
	}
}

void FStageAppRouteHandler::RegisterRoute(TUniquePtr<FRemoteControlWebsocketRoute> Route)
{
	checkSlow(RemoteControlModule);

	RemoteControlModule->RegisterWebsocketRoute(*Route);
	Routes.Emplace(MoveTemp(Route));
}

void FStageAppRouteHandler::HandleWebSocketNDisplayPreviewRendererCreate(const FRemoteControlWebSocketMessage& WebSocketMessage)
{
	FRCWebSocketNDisplayPreviewRendererCreateBody Body;
	if (!WebRemoteControlUtils::DeserializeMessage(WebSocketMessage.RequestPayload, Body))
	{
		return;
	}

	const int32 RendererId = IDisplayClusterScenePreview::Get().CreateRenderer();
	NDisplayPreviewRendererIdsByClientId.FindOrAdd(WebSocketMessage.ClientId).Add(RendererId);

	if (!Body.RootActorPath.IsEmpty())
	{
		IDisplayClusterScenePreview::Get().SetRendererRootActorPath(RendererId, Body.RootActorPath, true);
	}

	FRCPreviewRendererCreatedEvent Event;
	Event.RendererId = RendererId;

	TArray<uint8> Payload;
	WebRemoteControlUtils::SerializeMessage(Event, Payload);

	RemoteControlModule->SendWebsocketMessage(WebSocketMessage.ClientId, Payload);
}

void FStageAppRouteHandler::HandleWebSocketNDisplayPreviewRendererSetRoot(const FRemoteControlWebSocketMessage& WebSocketMessage)
{
	FRCWebSocketNDisplayPreviewRendererSetRootBody Body;
	if (!WebRemoteControlUtils::DeserializeMessage(WebSocketMessage.RequestPayload, Body))
	{
		return;
	}

	IDisplayClusterScenePreview::Get().SetRendererRootActorPath(Body.RendererId, Body.RootActorPath, true);
}

void FStageAppRouteHandler::HandleWebSocketNDisplayPreviewRendererDestroy(const FRemoteControlWebSocketMessage& WebSocketMessage)
{
	FRCWebSocketNDisplayPreviewRendererDestroyBody Body;
	if (!WebRemoteControlUtils::DeserializeMessage(WebSocketMessage.RequestPayload, Body))
	{
		return;
	}

	if (TArray<int32>* ClientRendererIds = NDisplayPreviewRendererIdsByClientId.Find(WebSocketMessage.ClientId))
	{
		// Check that this client created the renderer
		if (ClientRendererIds->Remove(Body.RendererId) > 0)
		{
			IDisplayClusterScenePreview::Get().DestroyRenderer(Body.RendererId);
		}
	}
}

void FStageAppRouteHandler::HandleWebSocketNDisplayPreviewRender(const FRemoteControlWebSocketMessage& WebSocketMessage)
{
	if (!ImageWrapperModule)
	{
		return;
	}

	FRCWebSocketNDisplayPreviewRenderBody Body;
	if (!WebRemoteControlUtils::DeserializeMessage(WebSocketMessage.RequestPayload, Body))
	{
		return;
	}

	IDisplayClusterScenePreview& PreviewModule = IDisplayClusterScenePreview::Get();
	ADisplayClusterRootActor* RootActor = PreviewModule.GetRendererRootActor(Body.RendererId);
	if (RootActor == nullptr)
	{
		return;
	}

	UWorld* World = RootActor->GetWorld();
	if (!World)
	{
		return;
	}

	USceneComponent* ViewOriginComponent = RootActor->GetCommonViewPoint();
	if (!ViewOriginComponent)
	{
		return;
	}

	// Set up the render
	FDisplayClusterMeshProjectionRenderSettings RenderSettings;

	switch (Body.RenderType)
	{
	case ERCWebSocketNDisplayPreviewRenderType::Color:
		RenderSettings.RenderType = EDisplayClusterMeshProjectionOutput::Color;
		break;

	case ERCWebSocketNDisplayPreviewRenderType::Normals:
		RenderSettings.RenderType = EDisplayClusterMeshProjectionOutput::Normals;
		break;
	}

	switch (Body.ProjectionType)
	{
	case ERCWebSocketNDisplayPreviewRenderProjectionType::Azimuthal:
		RenderSettings.ProjectionType = EDisplayClusterMeshProjectionType::Azimuthal;
		break;

	case ERCWebSocketNDisplayPreviewRenderProjectionType::Perspective:
		RenderSettings.ProjectionType = EDisplayClusterMeshProjectionType::Linear;
		break;
	}

	RenderSettings.EngineShowFlags.SetSelectionOutline(false);	// Determine FOV multipliers to match render target's aspect ratio
	float XAxisMultiplier;
	float YAxisMultiplier;

	const FIntPoint TargetSize = Body.Resolution;
	if (TargetSize.X > TargetSize.Y)
	{
		XAxisMultiplier = 1.0f;
		YAxisMultiplier = TargetSize.X / (float)TargetSize.Y;
	}
	else
	{
		XAxisMultiplier = TargetSize.Y / (float)TargetSize.X;
		YAxisMultiplier = 1.0f;
	}

	const float FOV = Body.FOV * (float)PI / 360.0f;

	FSceneViewInitOptions& ViewInitOptions = RenderSettings.ViewInitOptions;
	ViewInitOptions.BackgroundColor = FLinearColor::Black;
	ViewInitOptions.SetViewRectangle(FIntRect(0, 0, TargetSize.X, TargetSize.Y));
	ViewInitOptions.ViewOrigin = ViewOriginComponent->GetComponentLocation();
	ViewInitOptions.ViewLocation = ViewInitOptions.ViewOrigin;
	ViewInitOptions.ViewRotation = ViewOriginComponent->GetComponentRotation() + Body.Rotation;

	// Rotate view 90 degrees to match FDisplayClusterLightCardEditorViewportClient
	ViewInitOptions.ViewRotationMatrix = FInverseRotationMatrix(ViewInitOptions.ViewRotation) * FMatrix(
		FPlane(0, 0, 1, 0),
		FPlane(1, 0, 0, 0),
		FPlane(0, 1, 0, 0),
		FPlane(0, 0, 0, 1)
	);

	if ((bool)ERHIZBuffer::IsInverted)
	{
		ViewInitOptions.ProjectionMatrix = FReversedZPerspectiveMatrix(
			FOV,
			FOV,
			XAxisMultiplier,
			YAxisMultiplier,
			GNearClippingPlane,
			GNearClippingPlane
		);
	}
	else
	{
		ViewInitOptions.ProjectionMatrix = FPerspectiveMatrix(
			FOV,
			FOV,
			XAxisMultiplier,
			YAxisMultiplier,
			GNearClippingPlane,
			GNearClippingPlane
		);
	}

	const FIntPoint Resolution = Body.Resolution;
	const FGuid ClientId = WebSocketMessage.ClientId;
	const int32 RendererId = Body.RendererId;
	const int32 JpegQuality = Body.JpegQuality;

	// Render the image
	PreviewModule.RenderQueued(Body.RendererId, RenderSettings, Resolution, FRenderResultDelegate::CreateLambda(
		[this, ClientId, RendererId, Resolution, JpegQuality](FRenderTarget& RenderTarget)
		{
			// Save the image
			TArray<FColor> PixelData;
			RenderTarget.ReadPixels(PixelData);
			FImageView ImageView(PixelData.GetData(), Resolution.X, Resolution.Y, ERawImageFormat::BGRA8);

			// Convert to JPEG
			TArray64<uint8> JpegData;
			ImageWrapperModule->CompressImage(JpegData, EImageFormat::JPEG, ImageView, JpegQuality);

			// Encode to Base64
			FRCPreviewRenderCompletedEvent Event;
			Event.RendererId = RendererId;
			Event.ImageBase64 = FBase64::Encode(JpegData.GetData(), JpegData.Num());

			// Send it back to the client
			TArray<uint8> Payload;
			WebRemoteControlUtils::SerializeMessage(Event, Payload);
			RemoteControlModule->SendWebsocketMessage(ClientId, Payload);
		}
	));
}

void FStageAppRouteHandler::HandleClientDisconnected(FGuid ClientId)
{
	/** Destroy the client's renderers. */
	if (const TArray<int32>* RendererIds = NDisplayPreviewRendererIdsByClientId.Find(ClientId))
	{
		IDisplayClusterScenePreview& PreviewModule = IDisplayClusterScenePreview::Get();
		for (const int32 RendererId : *RendererIds)
		{
			PreviewModule.DestroyRenderer(RendererId);
		}
		NDisplayPreviewRendererIdsByClientId.Remove(ClientId);
	}

}
