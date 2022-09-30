// Copyright Epic Games, Inc. All Rights Reserved.

#include "VCamPixelStreamingSession.h"
#include "VCamOutputComposure.h"
#include "PixelStreamingServers.h"
#include "Editor/EditorPerformanceSettings.h"
#include "Containers/UnrealString.h"
#include "Async/Async.h"
#include "Modules/ModuleManager.h"
#include "Math/Vector4.h"
#include "Math/Matrix.h"
#include "Math/TransformNonVectorized.h"
#include "Serialization/MemoryReader.h"
#include "VCamPixelStreamingSubsystem.h"
#include "VCamPixelStreamingLiveLink.h"
#include "PixelStreamingVCamLog.h"
#include "IPixelStreamingModule.h"
#include "PixelStreamingProtocol.h"
#include "PixelStreamingEditorModule.h"
#include "VCamComponent.h"
#include "Input/HittestGrid.h"
#include "Widgets/SVirtualWindow.h"
#include "VPFullScreenUserWidget.h"
#include "InputCoreTypes.h"

namespace VCamPixelStreamingSession
{
	static const FName LevelEditorName(TEXT("LevelEditor"));
	static const FSoftClassPath EmptyUMGSoftClassPath(TEXT("/VCamCore/Assets/VCam_EmptyVisibleUMG.VCam_EmptyVisibleUMG_C"));
} // namespace VCamPixelStreamingSession

void UVCamPixelStreamingSession::Initialize()
{
	DisplayType = EVPWidgetDisplayType::PostProcess;
	Super::Initialize();
}

void UVCamPixelStreamingSession::Deinitialize()
{
	if (MediaOutput)
	{
		MediaOutput->ConditionalBeginDestroy();
	}
	MediaOutput = nullptr;
	Super::Deinitialize();
}

void UVCamPixelStreamingSession::Activate()
{
	if (UVCamPixelStreamingSubsystem* PixelStreamingSubsystem = UVCamPixelStreamingSubsystem::Get())
	{
		PixelStreamingSubsystem->RegisterActiveOutputProvider(this);
		UVCamComponent* VCamComponent = GetTypedOuter<UVCamComponent>();
		if (bAutoSetLiveLinkSubject && IsValid(VCamComponent))
		{
			VCamComponent->LiveLinkSubject = GetFName();
		}
	}

	// If we don't have a UMG assigned, we still need to create an empty 'dummy' UMG in order to properly route the input back from the device
	if (UMGClass == nullptr)
	{
		bUsingDummyUMG = true;
		UMGClass = VCamPixelStreamingSession::EmptyUMGSoftClassPath.TryLoadClass<UUserWidget>();
	}

	if (!bInitialized)
	{
		UE_LOG(LogPixelStreamingVCam, Warning, TEXT("Trying to start Pixel Streaming, but has not been initialized yet"));
		SetActive(false);
		return;
	}

	if (MediaOutput == nullptr)
	{
		MediaOutput = NewObject<UPixelStreamingMediaOutput>(GetTransientPackage(), UPixelStreamingMediaOutput::StaticClass());
	}

	UEditorPerformanceSettings* Settings = GetMutableDefault<UEditorPerformanceSettings>();
	bOldThrottleCPUWhenNotForeground = Settings->bThrottleCPUWhenNotForeground;
	if (PreventEditorIdle)
	{
		Settings->bThrottleCPUWhenNotForeground = false;
		Settings->PostEditChange();
	}

	if (StartSignallingServer)
	{
		if(!FPixelStreamingEditorModule::GetModule()->bUseExternalSignallingServer)
		{
			if (UVCamPixelStreamingSubsystem* PixelStreamingSubsystem = UVCamPixelStreamingSubsystem::Get())
			{
				PixelStreamingSubsystem->LaunchSignallingServer();
			}
		}
		else
		{
			StartSignallingServer = false;
		}
		
	}

	FString SignallingDomain = FPixelStreamingEditorModule::GetModule()->GetSignallingDomain();
	int32 StreamerPort = FPixelStreamingEditorModule::GetModule()->GetStreamerPort();
	MediaOutput->SetSignallingServerURL(FString::Printf(TEXT("%s:%s"), *SignallingDomain, *FString::FromInt(StreamerPort)));
	UE_LOG(LogPixelStreamingVCam, Log, TEXT("Activating PixelStreaming VCam Session. Endpoint: %s:%s"), *SignallingDomain, *FString::FromInt(StreamerPort));

	MediaCapture = Cast<UPixelStreamingMediaCapture>(MediaOutput->CreateMediaCapture());

	FMediaCaptureOptions Options;
	Options.bResizeSourceBuffer = true;

	// If we are rendering from a ComposureOutputProvider, get the requested render target and use that instead of the viewport
	if (UVCamOutputComposure* ComposureProvider = Cast<UVCamOutputComposure>(GetOtherOutputProviderByIndex(FromComposureOutputProviderIndex)))
	{
		if (ComposureProvider->FinalOutputRenderTarget)
		{
			MediaCapture->CaptureTextureRenderTarget2D(ComposureProvider->FinalOutputRenderTarget, Options);
			UE_LOG(LogPixelStreamingVCam, Log, TEXT("PixelStreaming set with ComposureRenderTarget"));
		}
		else
		{
			UE_LOG(LogPixelStreamingVCam, Warning, TEXT("PixelStreaming Composure usage was requested, but the specified ComposureOutputProvider has no FinalOutputRenderTarget set"));
		}
	}
	else
	{
		TWeakPtr<FSceneViewport> SceneViewport = GetTargetSceneViewport();
		if (TSharedPtr<FSceneViewport> PinnedSceneViewport = SceneViewport.Pin())
		{
			MediaCapture->CaptureSceneViewport(PinnedSceneViewport, Options);
			UE_LOG(LogPixelStreamingVCam, Log, TEXT("PixelStreaming set with viewport"));
		}
	}

	
	Super::Activate();
	// Super::Activate() creates our UMG, so we must do our stuff after
	// If we have a UMG, then use it
	if (UMGWidget)
	{
		TSharedPtr<SVirtualWindow> InputWindow;

		// If we are rendering from a ComposureOutputProvider, we need to get the InputWindow from that UMG, not the one in the PixelStreamingOutputProvider
		if (UVCamOutputComposure* ComposureProvider = Cast<UVCamOutputComposure>(GetOtherOutputProviderByIndex(FromComposureOutputProviderIndex)))
		{
			if (UVPFullScreenUserWidget* ComposureUMGWidget = ComposureProvider->GetUMGWidget())
			{
				InputWindow = ComposureUMGWidget->PostProcessDisplayType.GetSlateWindow();
				UE_LOG(LogPixelStreamingVCam, Log, TEXT("InputChannel callback - Routing input to active viewport with Composure UMG"));
			}
			else
			{
				UE_LOG(LogPixelStreamingVCam, Warning, TEXT("InputChannel callback - Composure usage was requested, but the specified ComposureOutputProvider has no UMG set"));
			}
		}
		else
		{
			InputWindow = UMGWidget->PostProcessDisplayType.GetSlateWindow();
			UE_LOG(LogPixelStreamingVCam, Log, TEXT("InputChannel callback - Routing input to active viewport with UMG"));
		}

		MediaOutput->GetStreamer()->SetTargetWindow(InputWindow);
		bRouteTouchMessageToWidget = true;
	}
	else
	{
		MediaOutput->GetStreamer()->SetTargetWindow(GetTargetInputWindow());
		bRouteTouchMessageToWidget = true;
		UE_LOG(LogPixelStreamingVCam, Log, TEXT("InputChannel callback - Routing input to active viewport"));
	}

	if(MediaOutput->GetStreamer())
	{
		IPixelStreamingModule& PixelStreamingModule = IPixelStreamingModule::Get();
		typedef Protocol::EPixelStreamingMessageTypes EType;		
		Protocol::EPixelStreamingMessageDirection MessageDirection = Protocol::EPixelStreamingMessageDirection::ToStreamer;
		TMap<FString, Protocol::FPixelStreamingInputMessage> Protocol = PixelStreamingModule.GetProtocol().ToStreamerProtocol;

		/*
		* ====================
		* ARKit Transform
		* ====================
		*/
		Protocol::FPixelStreamingInputMessage ARKitMessage = Protocol::FPixelStreamingInputMessage(100, 72, {
			// 4x4 Transform
			EType::Float, EType::Float, EType::Float, EType::Float,
			EType::Float, EType::Float, EType::Float, EType::Float,
			EType::Float, EType::Float, EType::Float, EType::Float,
			EType::Float, EType::Float, EType::Float, EType::Float,
			// Timestamp
			EType::Double
		});

		const TFunction<void(FMemoryReader)>& ARKitHandler = [this](FMemoryReader Ar) { 
			// The buffer contains the transform matrix stored as 16 floats
			FMatrix ARKitMatrix;
			for (int32 Row = 0; Row < 4; ++Row)
			{
				float Col0, Col1, Col2, Col3;
				Ar << Col0 << Col1 << Col2 << Col3;
				ARKitMatrix.M[Row][0] = Col0;
				ARKitMatrix.M[Row][1] = Col1;
				ARKitMatrix.M[Row][2] = Col2;
				ARKitMatrix.M[Row][3] = Col3;
			}
			ARKitMatrix.DiagnosticCheckNaN();

			// Extract timestamp
			double Timestamp;
			Ar << Timestamp;

			if(TSharedPtr<FPixelStreamingLiveLinkSource> LiveLinkSource = UVCamPixelStreamingSubsystem::Get()->TryGetLiveLinkSource(this))
			{
				LiveLinkSource->PushTransformForSubject(GetFName(), FTransform(ARKitMatrix), Timestamp);
			}
		};
		PixelStreamingModule.RegisterMessage(MessageDirection, "ARKitTransform", ARKitMessage, ARKitHandler);

		if(!bRouteTouchMessageToWidget)
		{
			// The following code overrides touch input handling which is only necessary if we have a GUI and 
			// want to route touch events to it's widget
			return;
		}

		/*
		* ====================
		* TOUCH START
		* ====================
		*/
		const TFunction<void(FMemoryReader)>& TouchStartHandler = [this](FMemoryReader Ar) { 
			if(!MediaCapture->GetViewport().IsValid()) 
			{
				return;
			}

			uint8 NumTouches;
			Ar << NumTouches;
			for (uint8 TouchIdx = 0; TouchIdx < NumTouches; TouchIdx++)
			{
				uint16 TouchPosX, TouchPosY;
				uint8 TouchIndex, TouchForce, TouchValid;
				Ar << TouchPosX << TouchPosY << TouchIndex << TouchForce << TouchValid;

				// If Touch is valid
				if (TouchValid != 0)
				{
					//                                                                           convert range from 0,65536 -> 0,1
					FVector2D Location = ConvertFromNormalizedScreenLocation(FVector2D(TouchPosX / (float) UINT16_MAX, TouchPosY / (float) UINT16_MAX));
					Location = Location - MediaCapture->GetViewport()->GetCachedGeometry().GetAbsolutePosition();

					FWidgetPath WidgetPath = FindRoutingMessageWidget(Location);
					
					if (WidgetPath.IsValid())
					{
						FScopedSwitchWorldHack SwitchWorld(WidgetPath);
						FPointerEvent PointerEvent(0, TouchIndex, Location, Location, TouchForce / 255.0f, true);
						FSlateApplication::Get().RoutePointerDownEvent(WidgetPath, PointerEvent);
					}
				}
			}
		};
		PixelStreamingModule.RegisterMessage(MessageDirection, "TouchStart", *Protocol.Find("TouchStart"), TouchStartHandler);

		/*
		* ====================
		* TOUCH MOVE
		* ====================
		*/
		const TFunction<void(FMemoryReader)>& TouchMoveHandler = [this](FMemoryReader Ar) { 
			if(!MediaCapture->GetViewport().IsValid()) 
			{
				return;
			}
			uint8 NumTouches;
			Ar << NumTouches;
			for (uint8 TouchIdx = 0; TouchIdx < NumTouches; TouchIdx++)
			{
				uint16 TouchPosX, TouchPosY;
				uint8 TouchIndex, TouchForce, TouchValid;
				Ar << TouchPosX << TouchPosY << TouchIndex << TouchForce << TouchValid;

				// If Touch is valid
				if (TouchValid != 0)
				{
					//                                                                           convert range from 0,65536 -> 0,1
					FVector2D Location = ConvertFromNormalizedScreenLocation(FVector2D(TouchPosX / (float) UINT16_MAX, TouchPosY / (float) UINT16_MAX));
					Location = Location - MediaCapture->GetViewport()->GetCachedGeometry().GetAbsolutePosition();

					FWidgetPath WidgetPath = FindRoutingMessageWidget(Location);
					
					if (WidgetPath.IsValid())
					{
						FScopedSwitchWorldHack SwitchWorld(WidgetPath);
						FPointerEvent PointerEvent(0, TouchIndex, Location, LastTouchLocation, TouchForce / 255.0f, true);
						FSlateApplication::Get().RoutePointerMoveEvent(WidgetPath, PointerEvent, false);
					}

					LastTouchLocation = Location;
				}
			}
		};
		PixelStreamingModule.RegisterMessage(MessageDirection, "TouchMove", *Protocol.Find("TouchMove"), TouchMoveHandler);

		/*
		* ====================
		* TOUCH END
		* ====================
		*/
		const TFunction<void(FMemoryReader)>& TouchEndHandler = [this](FMemoryReader Ar) { 
			if(!MediaCapture->GetViewport().IsValid()) 
			{
				return;
			}

			uint8 NumTouches;
			Ar << NumTouches;
			for (uint8 TouchIdx = 0; TouchIdx < NumTouches; TouchIdx++)
			{
				uint16 TouchPosX, TouchPosY;
				uint8 TouchIndex, TouchForce, TouchValid;
				Ar << TouchPosX << TouchPosY << TouchIndex << TouchForce << TouchValid;

				// If Touch is valid
				if (TouchValid != 0)
				{
					//                                                                           convert range from 0,65536 -> 0,1
					FVector2D Location = ConvertFromNormalizedScreenLocation(FVector2D(TouchPosX / (float) UINT16_MAX, TouchPosY / (float) UINT16_MAX));
					Location = Location - MediaCapture->GetViewport()->GetCachedGeometry().GetAbsolutePosition();

					FWidgetPath WidgetPath = FindRoutingMessageWidget(Location);
					
					if (WidgetPath.IsValid())
					{
						FScopedSwitchWorldHack SwitchWorld(WidgetPath);
						FPointerEvent PointerEvent(0, TouchIndex, Location, Location, 0.0f, true);
						FSlateApplication::Get().RoutePointerUpEvent(WidgetPath, PointerEvent);
					}
				}
			}
		};
		PixelStreamingModule.RegisterMessage(MessageDirection, "TouchEnd", *Protocol.Find("TouchEnd"), TouchEndHandler);
	}
}

void UVCamPixelStreamingSession::StopSignallingServer()
{
	// Only stop the signalling server if we've been the ones to start it
	if (UVCamPixelStreamingSubsystem* PixelStreamingSubsystem = UVCamPixelStreamingSubsystem::Get(); StartSignallingServer)
	{
		PixelStreamingSubsystem->StopSignallingServer();
	}
}

void UVCamPixelStreamingSession::Deactivate()
{
	if (UVCamPixelStreamingSubsystem* PixelStreamingSubsystem = UVCamPixelStreamingSubsystem::Get())
	{
		PixelStreamingSubsystem->UnregisterActiveOutputProvider(this);
	}

	if (MediaCapture)
	{

		if (MediaOutput && MediaOutput->GetStreamer())
		{
			// Shutting streamer down before closing signalling server prevents an ugly websocket disconnect showing in the log
			MediaOutput->GetStreamer()->StopStreaming();
			StopSignallingServer();
		}

		MediaCapture->StopCapture(true);
		MediaCapture = nullptr;
	}
	else
	{
		// There is not media capture we defensively clean up the signalling server if it exists.
		StopSignallingServer();
	}

	Super::Deactivate();
	if (bUsingDummyUMG)
	{
		UMGClass = nullptr;
		bUsingDummyUMG = false;
	}

	UEditorPerformanceSettings* Settings = GetMutableDefault<UEditorPerformanceSettings>();
	Settings->bThrottleCPUWhenNotForeground = bOldThrottleCPUWhenNotForeground;
	Settings->PostEditChange();
}

void UVCamPixelStreamingSession::Tick(const float DeltaTime)
{
	Super::Tick(DeltaTime);
}

#if WITH_EDITOR
void UVCamPixelStreamingSession::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	FProperty* Property = PropertyChangedEvent.MemberProperty;
	if (Property && PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive)
	{
		static FName NAME_FromComposureOutputProviderIndex = GET_MEMBER_NAME_CHECKED(UVCamPixelStreamingSession, FromComposureOutputProviderIndex);
		static FName NAME_StartSignallingServer = GET_MEMBER_NAME_CHECKED(UVCamPixelStreamingSession, StartSignallingServer);
		
		if (Property->GetFName() == NAME_FromComposureOutputProviderIndex || Property->GetFName() == NAME_StartSignallingServer)
		{
			SetActive(false);
		}
	}
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

FWidgetPath UVCamPixelStreamingSession::FindRoutingMessageWidget(const FVector2D& Location) const
{
	if (TSharedPtr<SWindow> PlaybackWindowPinned = MediaOutput->GetStreamer()->GetTargetWindow().Pin())
	{
		if (PlaybackWindowPinned->AcceptsInput())
		{
			bool bIgnoreEnabledStatus = false;
			TArray<FWidgetAndPointer> WidgetsAndCursors = PlaybackWindowPinned->GetHittestGrid().GetBubblePath(Location, FSlateApplication::Get().GetCursorRadius(), bIgnoreEnabledStatus);
			return FWidgetPath(MoveTemp(WidgetsAndCursors));
		}
	}
	return FWidgetPath();
}

FIntPoint UVCamPixelStreamingSession::ConvertFromNormalizedScreenLocation(const FVector2D& ScreenLocation)
{
	FIntPoint OutVector((int32)ScreenLocation.X, (int32)ScreenLocation.Y);

	TSharedPtr<SWindow> ApplicationWindow = MediaCapture->GetViewport()->FindWindow();
	if (ApplicationWindow.IsValid())
	{
		FVector2D WindowOrigin = ApplicationWindow->GetPositionInScreen();
		TWeakPtr<SViewport> TargetViewport = MediaCapture->GetViewport()->GetViewportWidget();
		if (TargetViewport.IsValid())
			{
			TSharedPtr<SViewport> ViewportWidget = TargetViewport.Pin();

			if (ViewportWidget.IsValid())
			{
				FGeometry InnerWindowGeometry = ApplicationWindow->GetWindowGeometryInWindow();

				// Find the widget path relative to the window
				FArrangedChildren JustWindow(EVisibility::Visible);
				JustWindow.AddWidget(FArrangedWidget(ApplicationWindow.ToSharedRef(), InnerWindowGeometry));

				FWidgetPath PathToWidget(ApplicationWindow.ToSharedRef(), JustWindow);
				if (PathToWidget.ExtendPathTo(FWidgetMatcher(ViewportWidget.ToSharedRef()), EVisibility::Visible))
				{
					FArrangedWidget ArrangedWidget = PathToWidget.FindArrangedWidget(ViewportWidget.ToSharedRef()).Get(FArrangedWidget::GetNullWidget());

					FVector2D WindowClientOffset = ArrangedWidget.Geometry.GetAbsolutePosition();
					FVector2D WindowClientSize = ArrangedWidget.Geometry.GetAbsoluteSize();

					FVector2D OutTemp = WindowOrigin + WindowClientOffset + (ScreenLocation * WindowClientSize);
					OutVector = FIntPoint((int32)OutTemp.X, (int32)OutTemp.Y);
				}
			}
		}
		else
		{
			FVector2D SizeInScreen = ApplicationWindow->GetSizeInScreen();
			FVector2D OutTemp = SizeInScreen * ScreenLocation;
			OutVector = FIntPoint((int32)OutTemp.X, (int32)OutTemp.Y);
		}
	}

	return OutVector;
}


TWeakPtr<SWindow> UVCamPixelStreamingSession::GetTargetInputWindow() const
{
	TWeakPtr<SWindow> InputWindow;

	if (UVCamComponent* OuterComponent = GetTypedOuter<UVCamComponent>())
	{
		InputWindow = OuterComponent->GetTargetInputWindow();
	}

	return InputWindow;
}

IMPLEMENT_MODULE(FDefaultModuleImpl, PixelStreamingVCam)
