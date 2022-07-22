// Copyright Epic Games, Inc. All Rights Reserved.

#include "StageAppRouteHandler.h"

#include "CanvasTypes.h"
#include "Components/DisplayClusterCameraComponent.h"
#include "DisplayClusterLightCardActor.h"
#include "DisplayClusterLightCardEditorHelper.h"
#include "DisplayClusterRootActor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "IDisplayClusterScenePreview.h"
#include "IImageWrapperModule.h"
#include "Misc/Base64.h"
#include "Modules/ModuleManager.h"
#include "RemoteControlWebsocketRoute.h"
#include "StageAppResponse.h"
#include "WebRemoteControlUtils.h"

#if WITH_EDITOR
#include "Editor.h"
#endif

#define DEBUG_STAGEAPP_NORMALS false

#define LOCTEXT_NAMESPACE "StageAppRouteHandler"

static TAutoConsoleVariable<float> CVarStageAppDragTimeoutCheckInterval(
	TEXT("StageApp.DragTimeoutCheckInterval"),
	2.f,
	TEXT("The interval at which to check whether a drag operation is still active, and if not, time it out.")
);

namespace StageAppRouteHandlerUtils
{
	/**
	 * Convert from a projection type provided over WebSocket to the values needed internally.
	 */
	void GetProjectionSettingsFromWebSocketType(
		ERCWebSocketNDisplayPreviewRenderProjectionType InProjectionType,
		EDisplayClusterMeshProjectionType& OutProjectionType,
		bool& bOutIsOrthographic)
	{
		switch (InProjectionType)
		{
		case ERCWebSocketNDisplayPreviewRenderProjectionType::Azimuthal:
			OutProjectionType = EDisplayClusterMeshProjectionType::Azimuthal;
			bOutIsOrthographic = false;
			break;

		case ERCWebSocketNDisplayPreviewRenderProjectionType::Perspective:
			OutProjectionType = EDisplayClusterMeshProjectionType::Linear;
			bOutIsOrthographic = false;
			break;

		case ERCWebSocketNDisplayPreviewRenderProjectionType::Orthographic:
			OutProjectionType = EDisplayClusterMeshProjectionType::Linear;
			bOutIsOrthographic = true;
			break;

		default:
			checkf(false, TEXT("Unknown projection type %d"), InProjectionType);
		}
	}

	/**
	 * Convert from a preview render type provided over WebSocket to the internal value used for the preview renderer.
	 */
	EDisplayClusterMeshProjectionOutput GetInternalPreviewRenderType(ERCWebSocketNDisplayPreviewRenderType InRenderType)
	{
		switch (InRenderType)
		{
		case ERCWebSocketNDisplayPreviewRenderType::Color:
			return EDisplayClusterMeshProjectionOutput::Color;

		case ERCWebSocketNDisplayPreviewRenderType::Normals:
			return EDisplayClusterMeshProjectionOutput::Normals;

		default:
			checkf(false, TEXT("Unknown projection type %d"), InRenderType);
			return EDisplayClusterMeshProjectionOutput::Color;
		}
	}
}


//////////////////////////////////////////////////////////////////////////
// FPerRendererData

FStageAppRouteHandler::FPerRendererData::FPerRendererData(int32 RendererId)
	: RendererId(RendererId)
{
}

FDisplayClusterLightCardEditorHelper& FStageAppRouteHandler::FPerRendererData::GetLightCardHelper()
{
	if (!LightCardHelper.IsValid())
	{
		LightCardHelper = MakeShared<FDisplayClusterLightCardEditorHelper>(RendererId);
		UpdateLightCardHelperSettings();
	}

	return *LightCardHelper;
}

const FRCWebSocketNDisplayPreviewRendererSettings& FStageAppRouteHandler::FPerRendererData::GetPreviewSettings() const
{
	return PreviewSettings;
}

void FStageAppRouteHandler::FPerRendererData::SetPreviewSettings(const FRCWebSocketNDisplayPreviewRendererSettings& NewSettings)
{
	PreviewSettings = NewSettings;
	UpdateLightCardHelperSettings();
}

bool FStageAppRouteHandler::FPerRendererData::GetSceneViewInitOptions(FSceneViewInitOptions& ViewInitOptions)
{
	ADisplayClusterRootActor* RootActor = GetRootActor();
	if (!RootActor)
	{
		return false;
	}

	USceneComponent* ViewOriginComponent = RootActor->GetCommonViewPoint();
	if (!ViewOriginComponent)
	{
		return false;
	}

	GetLightCardHelper().GetSceneViewInitOptions(
		ViewInitOptions,
		PreviewSettings.FOV,
		PreviewSettings.Resolution,
		ViewOriginComponent->GetComponentLocation(),
		FRotator(ViewOriginComponent->GetComponentRotation().Quaternion() * PreviewSettings.Rotation.Quaternion())
	);

	return true;
}

void FStageAppRouteHandler::FPerRendererData::UpdateDragSequenceNumber(int64 ReceivedSequenceNumber)
{
	if (ReceivedSequenceNumber > SequenceNumber)
	{
		SequenceNumber = ReceivedSequenceNumber;
	}
}

int64 FStageAppRouteHandler::FPerRendererData::GetDragSequenceNumber() const
{
	return SequenceNumber;
}

ADisplayClusterRootActor* FStageAppRouteHandler::FPerRendererData::GetRootActor() const
{
	return IDisplayClusterScenePreview::Get().GetRendererRootActor(RendererId);
}

void FStageAppRouteHandler::FPerRendererData::UpdateLightCardHelperSettings()
{
	if (FDisplayClusterLightCardEditorHelper* LightCardHelperPtr = LightCardHelper.Get())
	{
		EDisplayClusterMeshProjectionType ProjectionType;
		bool bIsOrthographic;

		StageAppRouteHandlerUtils::GetProjectionSettingsFromWebSocketType(PreviewSettings.ProjectionType, ProjectionType, bIsOrthographic);

		LightCardHelperPtr->SetProjectionMode(ProjectionType);
		LightCardHelperPtr->SetIsOrthographic(bIsOrthographic);
	}
}

//////////////////////////////////////////////////////////////////////////
// FStageAppRouteHandler

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

	RegisterRoute(MakeUnique<FRemoteControlWebsocketRoute>(
		TEXT("Start dragging lightcards relative to a projected preview"),
		TEXT("ndisplay.preview.lightcard.drag.begin"),
		FWebSocketMessageDelegate::CreateRaw(this, &FStageAppRouteHandler::HandleWebSocketNDisplayPreviewLightCardDragBegin)
	));

	RegisterRoute(MakeUnique<FRemoteControlWebsocketRoute>(
		TEXT("Move the light cards that are currently being dragged"),
		TEXT("ndisplay.preview.lightcard.drag.move"),
		FWebSocketMessageDelegate::CreateRaw(this, &FStageAppRouteHandler::HandleWebSocketNDisplayPreviewLightCardDragMove)
	));

	RegisterRoute(MakeUnique<FRemoteControlWebsocketRoute>(
		TEXT("Finish dragging the light cards that are currently being dragged"),
		TEXT("ndisplay.preview.lightcard.drag.end"),
		FWebSocketMessageDelegate::CreateRaw(this, &FStageAppRouteHandler::HandleWebSocketNDisplayPreviewLightCardDragEnd)
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

	if (DragTimeoutTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(DragTimeoutTickerHandle);
		DragTimeoutTickerHandle.Reset();
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

	IDisplayClusterScenePreview& PreviewModule = IDisplayClusterScenePreview::Get();

	const int32 RendererId = PreviewModule.CreateRenderer();
	PerRendererDataMapsByClientId.FindOrAdd(WebSocketMessage.ClientId).Emplace(RendererId, RendererId);

	PreviewModule.SetRendererUsePostProcessTexture(RendererId, true);

	if (!Body.RootActorPath.IsEmpty())
	{
		PreviewModule.SetRendererRootActorPath(RendererId, Body.RootActorPath, true);
	}

	ChangePreviewRendererSettings(WebSocketMessage.ClientId, RendererId, Body.Settings);

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

void FStageAppRouteHandler::HandleWebSocketNDisplayPreviewRendererConfigure(const FRemoteControlWebSocketMessage& WebSocketMessage)
{
	FRCWebSocketNDisplayPreviewRendererConfigureBody Body;
	if (!WebRemoteControlUtils::DeserializeMessage(WebSocketMessage.RequestPayload, Body))
	{
		return;
	}

	ChangePreviewRendererSettings(WebSocketMessage.ClientId, Body.RendererId, Body.Settings);
}

void FStageAppRouteHandler::HandleWebSocketNDisplayPreviewRendererDestroy(const FRemoteControlWebSocketMessage& WebSocketMessage)
{
	FRCWebSocketNDisplayPreviewRendererDestroyBody Body;
	if (!WebRemoteControlUtils::DeserializeMessage(WebSocketMessage.RequestPayload, Body))
	{
		return;
	}

	if (TClientIdToPerRendererDataMap* PerRendererDataMap = PerRendererDataMapsByClientId.Find(WebSocketMessage.ClientId))
	{
		// Check that this client created the renderer
		if (PerRendererDataMap->Remove(Body.RendererId) > 0)
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

	FPerRendererData* PerRendererData = GetClientPerRendererData(WebSocketMessage.ClientId, Body.RendererId);
	if (!PerRendererData)
	{
		return;
	}

	// Set up the render settings
	FDisplayClusterMeshProjectionRenderSettings RenderSettings;
	if (!PerRendererData->GetSceneViewInitOptions(RenderSettings.ViewInitOptions))
	{
		return;
	}

	const FRCWebSocketNDisplayPreviewRendererSettings& PreviewSettings = PerRendererData->GetPreviewSettings();
	RenderSettings.RenderType = StageAppRouteHandlerUtils::GetInternalPreviewRenderType(PreviewSettings.RenderType);
	RenderSettings.EngineShowFlags.SetSelectionOutline(false);

	EDisplayClusterMeshProjectionType ProjectionType;
	bool bIsOrthographic;

	StageAppRouteHandlerUtils::GetProjectionSettingsFromWebSocketType(PreviewSettings.ProjectionType, ProjectionType, bIsOrthographic);
	EngineShowFlagOrthographicOverride(!bIsOrthographic, RenderSettings.EngineShowFlags);

	FSceneViewInitOptions ViewInitOptions = RenderSettings.ViewInitOptions;
	const FIntPoint Resolution = PreviewSettings.Resolution;
	const FGuid ClientId = WebSocketMessage.ClientId;
	const int32 RendererId = Body.RendererId;
	const int32 JpegQuality = PreviewSettings.JpegQuality;
	const bool bIncludeActorPositions = PreviewSettings.IncludeActorPositions;

	// Render the image
	IDisplayClusterScenePreview::Get().RenderQueued(Body.RendererId, RenderSettings, Resolution, FRenderResultDelegate::CreateLambda(
		[this, ViewInitOptions, ClientId, RendererId, Resolution, JpegQuality, bIncludeActorPositions](FRenderTarget& RenderTarget)
		{
			FPerRendererData* PerRendererData = GetClientPerRendererData(ClientId, RendererId);

#if DEBUG_STAGEAPP_NORMALS
			// Draw the normal texture in the top-left corner if available
			if (PerRendererData)
			{
				FDisplayClusterLightCardEditorHelper& Helper = PerRendererData->GetLightCardHelper();
				const FGameTime Time = FGameTime::GetTimeSinceAppStart();
				const ERHIFeatureLevel::Type FeatureLevel = PerRendererData->GetRootActor()->GetWorld()->Scene->GetFeatureLevel();
				const FIntPoint HalfResolution = Resolution / 2;

				auto DrawNormalMap = [&](const FIntPoint& Position, bool bShowNorthMap) {
					if (const UTexture2D* NorthMapTexture = Helper.GetNormalMapTexture(bShowNorthMap))
					{
						FCanvas Canvas(&RenderTarget, nullptr, Time, FeatureLevel);
						Canvas.DrawTile(Position.X, Position.Y, HalfResolution.X, HalfResolution.Y, 0, 0, 1, 1, FColor::White, NorthMapTexture->GetResource());
						Canvas.Flush_GameThread();
					}
				};

				DrawNormalMap(FIntPoint(0, 0), true);
				DrawNormalMap(FIntPoint(HalfResolution.X, 0), false);
			}
#endif

			// Read image data
			TArray<FColor> PixelData;
			RenderTarget.ReadPixels(PixelData);
			FImageView ImageView(PixelData.GetData(), Resolution.X, Resolution.Y, 1, ERawImageFormat::BGRA8, EGammaSpace::Linear);

			// Convert to JPEG
			TArray64<uint8> JpegData;
			ImageWrapperModule->CompressImage(JpegData, EImageFormat::JPEG, ImageView, JpegQuality);

			// Encode to Base64
			FRCPreviewRenderCompletedEvent Event;
			Event.RendererId = RendererId;
			Event.ImageBase64 = FBase64::Encode(JpegData.GetData(), JpegData.Num());

			if (PerRendererData)
			{
				Event.SequenceNumber = PerRendererData->GetDragSequenceNumber();

				// Add the list of actors, if requested
				if (bIncludeActorPositions)
				{
					TArray<AActor*> Actors;
					if (IDisplayClusterScenePreview::Get().GetActorsInRendererScene(RendererId, false, Actors))
					{
						const FSceneView View(ViewInitOptions);

						for (const AActor* Actor : Actors)
						{
							if (!Actor)
							{
								continue;
							}

							FVector WorldPosition;
							if (const ADisplayClusterLightCardActor* LightCard = Cast<ADisplayClusterLightCardActor>(Actor))
							{
								if (LightCard->bIsUVLightCard)
								{
									// TODO: add support for UV light cards and projection mode
									continue;
								}

								WorldPosition = LightCard->GetLightCardTransform().GetTranslation();
							}
							else
							{
								WorldPosition = Actor->GetActorLocation();
							}

							FRCPreviewRenderCompletedEventActorPosition ActorInfo;
							ActorInfo.Path = Actor->GetPathName();

							PerRendererData->GetLightCardHelper().WorldToPixel(
								View,
								WorldPosition,
								ActorInfo.Position
							);

							// Normalize to the size of the texture
							ActorInfo.Position /= Resolution;

							Event.ActorPositions.Add(ActorInfo);
						}
					}
				}
			}

			// Send data back to the client
			TArray<uint8> Payload;
			WebRemoteControlUtils::SerializeMessage(Event, Payload);
			RemoteControlModule->SendWebsocketMessage(ClientId, Payload);
		}
	));
}

void FStageAppRouteHandler::HandleWebSocketNDisplayPreviewLightCardDragBegin(const FRemoteControlWebSocketMessage& WebSocketMessage)
{
	FRCWebSocketNDisplayPreviewLightCardDragBeginBody Body;
	if (!WebRemoteControlUtils::DeserializeMessage(WebSocketMessage.RequestPayload, Body))
	{
		return;
	}

	FPerRendererData* PerRendererData = GetClientPerRendererData(WebSocketMessage.ClientId, Body.RendererId);
	if (!PerRendererData)
	{
		return;
	}

	if (!PerRendererData->DraggedLightCards.IsEmpty())
	{
		// A drag is already in progress
		return;
	}

#if WITH_EDITOR
	GEditor->BeginTransaction(LOCTEXT("DragLightcardsTransaction", "Drag Light Cards with Stage App"));
#endif

	PerRendererData->UpdateDragSequenceNumber(Body.SequenceNumber);
	PerRendererData->bHasDragMovedRecently = true;

	for (const FString& LightCardPath : Body.LightCards)
	{
		if (ADisplayClusterLightCardActor* Actor = FindObject<ADisplayClusterLightCardActor>(nullptr, *LightCardPath))
		{
			PerRendererData->DraggedLightCards.Emplace(Actor);
		}
	}

	if (!Body.PrimaryLightCard.IsEmpty())
	{
		PerRendererData->PrimaryLightCard = FindObject<ADisplayClusterLightCardActor>(nullptr, *Body.PrimaryLightCard);
	}

	if (!DragTimeoutTickerHandle.IsValid())
	{
		DragTimeoutTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateRaw(this, &FStageAppRouteHandler::TimeOutDrags),
			CVarStageAppDragTimeoutCheckInterval.GetValueOnGameThread()
		);
	}
}

void FStageAppRouteHandler::HandleWebSocketNDisplayPreviewLightCardDragMove(const FRemoteControlWebSocketMessage& WebSocketMessage)
{
	FRCWebSocketNDisplayPreviewLightCardDragMoveBody Body;
	if (!WebRemoteControlUtils::DeserializeMessage(WebSocketMessage.RequestPayload, Body))
	{
		return;
	}

	FPerRendererData* PerRendererData = GetClientPerRendererData(WebSocketMessage.ClientId, Body.RendererId);
	if (!PerRendererData)
	{
		return;
	}

	PerRendererData->UpdateDragSequenceNumber(Body.SequenceNumber);
	PerRendererData->bHasDragMovedRecently = true;

	DragLightCards(*PerRendererData, Body.DragPosition);
}

void FStageAppRouteHandler::HandleWebSocketNDisplayPreviewLightCardDragEnd(const FRemoteControlWebSocketMessage& WebSocketMessage)
{
	FRCWebSocketNDisplayPreviewLightCardDragEndBody Body;
	if (!WebRemoteControlUtils::DeserializeMessage(WebSocketMessage.RequestPayload, Body))
	{
		return;
	}

	FPerRendererData* PerRendererData = GetClientPerRendererData(WebSocketMessage.ClientId, Body.RendererId);
	if (!PerRendererData)
	{
		return;
	}

	PerRendererData->UpdateDragSequenceNumber(Body.SequenceNumber);
	DragLightCards(*PerRendererData, Body.DragPosition);
	EndLightCardDrag(*PerRendererData, WebSocketMessage.ClientId, Body.RendererId, true);
}

void FStageAppRouteHandler::HandleClientDisconnected(FGuid ClientId)
{
	// Destroy the client's renderers
	if (const TClientIdToPerRendererDataMap* PerRendererDataMap = PerRendererDataMapsByClientId.Find(ClientId))
	{
		IDisplayClusterScenePreview& PreviewModule = IDisplayClusterScenePreview::Get();
		for (const TClientIdToPerRendererDataMap::ElementType& PerRendererDataPair : *PerRendererDataMap)
		{
			PreviewModule.DestroyRenderer(PerRendererDataPair.Key);
		}

		PerRendererDataMapsByClientId.Remove(ClientId);
	}
}

void FStageAppRouteHandler::ChangePreviewRendererSettings(const FGuid& ClientId, int32 RendererId, const FRCWebSocketNDisplayPreviewRendererSettings& NewSettings)
{
	FPerRendererData* PerRendererData = GetClientPerRendererData(ClientId, RendererId);
	if (!PerRendererData)
	{
		return;
	}

	PerRendererData->SetPreviewSettings(NewSettings);
}

FStageAppRouteHandler::FPerRendererData* FStageAppRouteHandler::GetClientPerRendererData(const FGuid& ClientId, int32 RendererId)
{
	TClientIdToPerRendererDataMap* PerRendererDataMap = PerRendererDataMapsByClientId.Find(ClientId);
	if (!PerRendererDataMap)
	{
		return nullptr;
	}

	return PerRendererDataMap->Find(RendererId);
}

void FStageAppRouteHandler::DragLightCards(FPerRendererData& PerRendererData, FVector2D DragPosition)
{
	// Set up the scene view relative to which we're dragging
	FSceneViewInitOptions ViewInitOptions;
	if (!PerRendererData.GetSceneViewInitOptions(ViewInitOptions))
	{
		return;
	}

	PerRendererData.LastDragPosition = DragPosition;

	const FSceneView View(ViewInitOptions);

	// Determine the screen pixel position to which we're dragging
	const FRCWebSocketNDisplayPreviewRendererSettings& PreviewSettings = PerRendererData.GetPreviewSettings();
	const FIntPoint& Resolution = PreviewSettings.Resolution;

	const FIntPoint PixelPos(
		FMath::RoundToInt(DragPosition.X * Resolution.X),
		FMath::RoundToInt(DragPosition.Y * Resolution.Y)
	);
	const FVector DragWidgetOffset = FVector::ZeroVector;
	
	FDisplayClusterLightCardEditorHelper& LightCardHelper = PerRendererData.GetLightCardHelper();
	LightCardHelper.DragLightCards(
		PerRendererData.DraggedLightCards,
		PixelPos,
		View,
		DragWidgetOffset,
		EAxisList::Type::XYZ,
		PerRendererData.PrimaryLightCard.Get()
	);
}

bool FStageAppRouteHandler::TimeOutDrags(float DeltaTime)
{
	bool bAnyDragsInProgress = false;
	for (TPair<FGuid, TClientIdToPerRendererDataMap>& ClientPair : PerRendererDataMapsByClientId)
	{
		for (TClientIdToPerRendererDataMap::ElementType& PerRendererDataPair : ClientPair.Value)
		{
			FPerRendererData& PerRendererData = PerRendererDataPair.Value;
			if (PerRendererData.DraggedLightCards.IsEmpty())
			{
				continue;
			}
			
			if (!PerRendererData.bHasDragMovedRecently)
			{
				EndLightCardDrag(PerRendererData, ClientPair.Key, PerRendererDataPair.Key, false);
				continue;
			}

			PerRendererData.bHasDragMovedRecently = false;
			bAnyDragsInProgress = true;
		}
	}

	if (!bAnyDragsInProgress)
	{
		DragTimeoutTickerHandle.Reset();
		return false;
	}

	return true;
}

void FStageAppRouteHandler::EndLightCardDrag(FPerRendererData& PerRendererData, const FGuid& ClientId, int32 RendererId, bool bEndedByClient)
{
	PerRendererData.DraggedLightCards.Empty();
	PerRendererData.bHasDragMovedRecently = false;

#if WITH_EDITOR
	GEditor->EndTransaction();
#endif

	if (!bEndedByClient)
	{
		// Notify the client that its drag in progress was cancelled
		FRCLightCardDragCancelled Event;
		Event.RendererId = RendererId;

		TArray<uint8> Payload;
		WebRemoteControlUtils::SerializeMessage(Event, Payload);

		RemoteControlModule->SendWebsocketMessage(ClientId, Payload);
	}
}

#undef LOCTEXT_NAMESPACE
