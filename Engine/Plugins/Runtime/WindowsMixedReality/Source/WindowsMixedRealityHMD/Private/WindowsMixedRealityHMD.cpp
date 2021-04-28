// Copyright (c) Microsoft Corporation. All rights reserved.

#include "WindowsMixedRealityHMD.h"

#include "WindowsMixedRealityStatics.h"
#include "Misc/App.h"
#include "Modules/ModuleManager.h"
#include "EngineGlobals.h"
#include "Engine/Engine.h"
#include "Interfaces/IPluginManager.h"
#include "IWindowsMixedRealityHMDPlugin.h"
#include "RHI/Public/PipelineStateCache.h"
#include "GameFramework/InputSettings.h"
#include "GameFramework/PlayerController.h"
#include "Async/Async.h"

#include "HeadMountedDisplayFunctionLibrary.h"

#if WITH_EDITOR
#include "Editor/UnrealEd/Classes/Editor/EditorEngine.h"
#include "WindowsMixedRealityRuntimeSettings.h"
#endif

#include "Engine/GameEngine.h"
#include "HAL/PlatformMisc.h"
#include "Misc/MessageDialog.h"
#include "WindowsMixedRealityInteropLoader.h"

// Holographic Remoting is only supported in Windows 10 version 1809 or better
// Originally we were supporting 1803, but there were rendering issues specific to that version so for now we only support 1809
#define MIN_WIN_10_VERSION_FOR_WMR 1809

#include "WindowsMixedRealityAvailability.h"

#if SUPPORTS_WINDOWS_MIXED_REALITY_AR
	#include "HoloLensModule.h"
#endif

#if WITH_INPUT_SIMULATION
	#include "WindowsMixedRealityInputSimulationEngineSubsystem.h"
#endif

#include "GeneralProjectSettings.h"

// Control logging from here so we don't have to change the interop library to enable/disable logging
#ifndef WANTS_INTEROP_LOGGING
	#if UE_BUILD_DEBUG && PLATFORM_HOLOLENS
		#define WANTS_INTEROP_LOGGING 1
	#else
		#define WANTS_INTEROP_LOGGING 0
	#endif
#endif

//---------------------------------------------------
// Windows Mixed Reality HMD Plugin
//---------------------------------------------------

#if WITH_WINDOWS_MIXED_REALITY
class FDepthConversionPS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FDepthConversionPS, Global);

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) || IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::ES3_1);
	}

	FDepthConversionPS() { }

	FDepthConversionPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		// Bind shader inputs.
		InDepthTexture.Bind(Initializer.ParameterMap, TEXT("InDepthTexture"), SPF_Mandatory);
		InTextureSampler.Bind(Initializer.ParameterMap, TEXT("InTextureSampler"));
	}

	void SetParameters(FRHICommandList& RHICmdList, FRHITexture* DepthTexture)
	{
		FRHIPixelShader* PixelShaderRHI = RHICmdList.GetBoundPixelShader();

		FRHISamplerState* SamplerStateRHI = TStaticSamplerState<SF_Point>::GetRHI();
		SetTextureParameter(RHICmdList, PixelShaderRHI, InDepthTexture, InTextureSampler, SamplerStateRHI, DepthTexture);
	}

private:
	// Shader parameters.
	LAYOUT_FIELD(FShaderResourceParameter, InDepthTexture);
	LAYOUT_FIELD(FShaderResourceParameter, InTextureSampler);
};

IMPLEMENT_SHADER_TYPE(, FDepthConversionPS, TEXT("/Plugin/WindowsMixedReality/Private/DepthConversion.usf"), TEXT("MainPixelShader"), SF_Pixel)
#endif

void LogCallback(const wchar_t* LogMsg)
{
	UE_LOG(LogWmrHmd, Log, TEXT("%s"), LogMsg);
}

namespace WindowsMixedReality
{
	class FWindowsMixedRealityHMDPlugin : public IWindowsMixedRealityHMDPlugin
	{
		virtual TSharedPtr< class IXRTrackingSystem, ESPMode::ThreadSafe > CreateTrackingSystem() override;

		virtual WindowsMixedReality::MixedRealityInterop* GetMixedRealityInterop() override
		{
			return HMD;
		}

		bool IsHMDConnected()
		{
#if WITH_WINDOWS_MIXED_REALITY
			return HMD && HMD->IsAvailable();
#endif
			return false;
		}

		FString GetModuleKeyName() const override
		{
			return FString(TEXT("WindowsMixedRealityHMD"));
		}

		void StartupModule() override
		{
#if WITH_WINDOWS_MIXED_REALITY
			IHeadMountedDisplayModule::StartupModule();

			// Set the shader directory even if we won't be able to load the interop so shader compliation does not fail.
			const FString VirtualShaderDir = TEXT("/Plugin/WindowsMixedReality");
			if (!AllShaderSourceDirectoryMappings().Contains(VirtualShaderDir))  // Two modules try to map this, so we have to check if its already set.
			{
				FString PluginShaderDir = FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("WindowsMixedReality"))->GetBaseDir(), TEXT("Shaders"));
				AddShaderSourceDirectoryMapping(VirtualShaderDir, PluginShaderDir);
			}

			HMD = LoadInteropLibrary();
			if (!HMD)
			{
				return;
			}

#if WANTS_INTEROP_LOGGING
			if (HMD != nullptr)
			{
				HMD->SetLogCallback(&LogCallback);
			}
#endif // WANTS_INTEROP_LOGGING

#else // WITH_WINDOWS_MIXED_REALITY
			UE_LOG(LogWmrHmd, Error, TEXT("Windows Mixed Reality compiled with unsupported compiler.  Please recompile with Visual Studio 2017"));
#endif // WITH_WINDOWS_MIXED_REALITY
		}

		void ShutdownModule() override
		{
#if WITH_WINDOWS_MIXED_REALITY
			if (HMD)
			{
#if SUPPORTS_WINDOWS_MIXED_REALITY_AR
				FHoloLensModuleAR::SetInterop(nullptr);
#endif
				HMD->Dispose(true);
				delete HMD;
				HMD = nullptr;
			}
#endif
		}

		uint64 GetGraphicsAdapterLuid() override
		{
#if WITH_WINDOWS_MIXED_REALITY
			if (HMD)
			{
				return HMD->GraphicsAdapterLUID();
			}
#endif
			return 0;
		}

#if WITH_WINDOWS_MIXED_REALITY
		MixedRealityInterop* HMD = nullptr;
#endif
	};

	TSharedPtr< class IXRTrackingSystem, ESPMode::ThreadSafe > FWindowsMixedRealityHMDPlugin::CreateTrackingSystem()
	{
#if WITH_WINDOWS_MIXED_REALITY
		if (HMD)
		{
			IARSystemSupport* ARSystem = nullptr;
#if SUPPORTS_WINDOWS_MIXED_REALITY_AR
			ARSystem = FHoloLensModuleAR::CreateARSystem();
#endif
			auto WindowsMRHMD = FSceneViewExtensions::NewExtension<WindowsMixedReality::FWindowsMixedRealityHMD>(ARSystem, HMD);
			if (WindowsMRHMD->IsInitialized())
			{
#if SUPPORTS_WINDOWS_MIXED_REALITY_AR
				FHoloLensModuleAR::SetTrackingSystem(WindowsMRHMD);
				FHoloLensModuleAR::SetInterop(HMD);
				// Register the AR modular features
				WindowsMRHMD->GetARCompositionComponent()->InitializeARSystem();
#endif
				return WindowsMRHMD;
			}
		}
#endif
		return nullptr;
	}

	float FWindowsMixedRealityHMD::GetWorldToMetersScale() const
	{
		return CachedWorldToMetersScale;
	}

	//---------------------------------------------------
	// FWindowsMixedRealityHMD IHeadMountedDisplay Implementation
	//---------------------------------------------------

	bool FWindowsMixedRealityHMD::IsHeadTrackingAllowed() const
	{
		if (FHeadMountedDisplayBase::IsHeadTrackingAllowed())
		{
			return true;
		}

#if WITH_INPUT_SIMULATION
		if (auto* InputSim = UWindowsMixedRealityInputSimulationEngineSubsystem::GetInputSimulationIfEnabled())
		{
			if (InputSim->HasPositionalTracking())
			{
				return true;
			}
		}
#endif

		return false;
	}

	bool FWindowsMixedRealityHMD::IsHMDConnected()
	{
#if WITH_WINDOWS_MIXED_REALITY
		if (HMD->IsRemoting())
		{
			return true;
		}

		return HMD->IsAvailable();
#else
		return false;
#endif
	}

	bool FWindowsMixedRealityHMD::IsHMDEnabled() const
	{
		return true;
	}

	EHMDWornState::Type FWindowsMixedRealityHMD::GetHMDWornState()
	{
#if WITH_WINDOWS_MIXED_REALITY
		if (HMD->IsRemoting())
		{
			return EHMDWornState::Type::Unknown;
		}

		UserPresence currentPresence = HMD->GetCurrentUserPresence();

		EHMDWornState::Type wornState = EHMDWornState::Type::Unknown;

		switch (currentPresence)
		{
		case UserPresence::Worn:
			wornState = EHMDWornState::Type::Worn;
			break;
		case UserPresence::NotWorn:
			wornState = EHMDWornState::Type::NotWorn;
			break;
		};

		return wornState;
#else
		return EHMDWornState::Unknown;
#endif
	}

	void FWindowsMixedRealityHMD::OnBeginPlay(FWorldContext & InWorldContext)
	{
#if PLATFORM_HOLOLENS
		if (FParse::Param(FCommandLine::Get(), TEXT("vr")) || GetDefault<UGeneralProjectSettings>()->bStartInVR)
		{
			EnableStereo(true);
		}
		else
		{
			bIsStereoDesired = false;
		}
#endif

		//start speech recognition if there are any commands we care to listen for
		StartSpeechRecognition();

		FWindowsMixedRealityStatics::OnTogglePlayDelegate.Broadcast(true);
	}

	void FWindowsMixedRealityHMD::OnEndPlay(FWorldContext & InWorldContext)
	{
	#if PLATFORM_HOLOLENS
		EnableStereo(false);
	#endif

		StopSpeechRecognition();

		FWindowsMixedRealityStatics::OnTogglePlayDelegate.Broadcast(false);
	}

	TRefCountPtr<ID3D11Device> FWindowsMixedRealityHMD::InternalGetD3D11Device()
	{
		if (!D3D11Device.IsValid())
		{
			FWindowsMixedRealityHMD* Self = this;
			ENQUEUE_RENDER_COMMAND(InternalGetD3D11DeviceCmd)([Self](FRHICommandListImmediate& RHICmdList)
			{
				Self->D3D11Device = (ID3D11Device*)RHIGetNativeDevice();
			});

			FlushRenderingCommands();
		}

		return D3D11Device;
	}

	/** Helper function for acquiring the appropriate FSceneViewport */
	FSceneViewport* FindMRSceneViewport(bool& allowStereo)
	{
		allowStereo = true;

		if (!GIsEditor)
		{
			UGameEngine* GameEngine = Cast<UGameEngine>(GEngine);

			if (GameEngine->SceneViewport.Get() != nullptr)
			{
				allowStereo = GameEngine->SceneViewport.Get()->IsStereoRenderingAllowed();
			}

			return GameEngine->SceneViewport.Get();
		}
#if WITH_EDITOR
		else
		{
			UEditorEngine* EditorEngine = CastChecked<UEditorEngine>(GEngine);
			FSceneViewport* PIEViewport = (FSceneViewport*)EditorEngine->GetPIEViewport();
			if (PIEViewport != nullptr && PIEViewport->IsStereoRenderingAllowed())
			{
				// PIE is setup for stereo rendering
				allowStereo = PIEViewport->IsStereoRenderingAllowed();
				return PIEViewport;
			}
			else
			{
				// Check to see if the active editor viewport is drawing in stereo mode
				FSceneViewport* EditorViewport = (FSceneViewport*)EditorEngine->GetActiveViewport();
				if (EditorViewport != nullptr && EditorViewport->IsStereoRenderingAllowed())
				{
					allowStereo = EditorViewport->IsStereoRenderingAllowed();
					return EditorViewport;
				}
			}
		}
#endif

		allowStereo = false;
		return nullptr;
	}

	bool FWindowsMixedRealityHMD::HasValidTrackingPosition()
	{
#if WITH_INPUT_SIMULATION
		if (auto* InputSim = UWindowsMixedRealityInputSimulationEngineSubsystem::GetInputSimulationIfEnabled())
		{
			return InputSim->HasPositionalTracking();
		}
		else
#endif
		{
			const Frame& TheFrame = GetFrame();
			return TheFrame.bPositionalTrackingUsed;
		}
	}

	FString FWindowsMixedRealityHMD::GetVersionString() const
	{
#if WITH_WINDOWS_MIXED_REALITY
		return FString(HMD->GetDisplayName());
#else
		return FString();
#endif
	}

	void CenterMouse(RECT windowRect)
	{
#if !PLATFORM_HOLOLENS
		int width = windowRect.right - windowRect.left;
		int height = windowRect.bottom - windowRect.top;

		SetCursorPos(windowRect.left + width / 2, windowRect.top + height / 2);
#endif
	}

	bool FWindowsMixedRealityHMD::OnStartGameFrame(FWorldContext & WorldContext)
	{
		if (this->bRequestShutdown)
		{
			this->bRequestShutdown = false;
			ShutdownHolographic();

			return true;
		}
		else if (this->bRequestRestart)
		{
			this->bRequestRestart = false;

			ShutdownHolographic();

#if !PLATFORM_HOLOLENS
			if (bIsStereoDesired)
			{
				EnableStereo(true);
			}
#endif

			return true;
		}

		UpdateRemotingStatus();

#if WITH_WINDOWS_MIXED_REALITY
		if (bIsStereoDesired && !HMD->IsInitialized())
		{
			D3D11Device = InternalGetD3D11Device();
			HMD->Initialize(D3D11Device.GetReference(),
				GNearClippingPlane / GetWorldToMetersScale());
			return true;
		}
		else
		{
#if !PLATFORM_HOLOLENS
			if (!HMD->IsRemoting() && (bIsStereoDesired && !HMD->IsImmersiveWindowValid()))
			{
				// This can happen if the PC went to sleep.
				this->bRequestRestart = true;
				return true;
			}
#endif
		}

		if (HMD->IsRemoting() && !bIsStereoDesired)
		{
			EnableStereo(true);
		}

		if (!bIsStereoEnabled && bIsStereoDesired)
		{
			// Set up the HMD
			SetupHolographicCamera();
		}

		if (!HMD->IsRemoting() && HMD->HasUserPresenceChanged())
		{
			auto newWornState = GetHMDWornState();

			if (newWornState != currentWornState)
			{
				currentWornState = newWornState;
				// Broadcast HMD worn/ not worn delegates.
				if (currentWornState == EHMDWornState::Worn)
				{
					FCoreDelegates::VRHeadsetPutOnHead.Broadcast();
				}
				else if (currentWornState == EHMDWornState::NotWorn)
				{
					FCoreDelegates::VRHeadsetRemovedFromHead.Broadcast();
				}
			}
		}

#if !PLATFORM_HOLOLENS
		if (GEngine
			&& GEngine->GameViewport
			&& GEngine->GameViewport->GetWindow().IsValid())
		{
			HWND gameHWND = (HWND)GEngine->GameViewport->GetWindow()->GetNativeWindow()->GetOSWindowHandle();
			if (IsWindow(gameHWND))
			{
				RECT windowRect;
				GetWindowRect(gameHWND, &windowRect);

				gameWindowWidth = windowRect.right - windowRect.left;
				gameWindowHeight = windowRect.bottom - windowRect.top;
			}
		}

		// Restore windows focus to game window to preserve keyboard/mouse input.
		if ((currentWornState == EHMDWornState::Type::Worn) && GEngine && GEngine->GameViewport)
		{
			HWND gameHWND = (HWND)GEngine->GameViewport->GetWindow()->GetNativeWindow()->GetOSWindowHandle();

			// Set mouse focus to center of game window so any clicks interact with the game.
			if (mouseLockedToCenter)
			{
				RECT windowRect;
				GetWindowRect(gameHWND, &windowRect);

				CenterMouse(windowRect);
			}

			if (GetCapture() != gameHWND)
			{
				// Keyboard input
				SetForegroundWindow(gameHWND);

				// Mouse input
				SetCapture(gameHWND);
				SetFocus(gameHWND);

				FSlateApplication::Get().SetAllUserFocusToGameViewport();
			}
		}

#endif
#endif

#if WITH_WINDOWS_MIXED_REALITY
		InitTrackingFrame();
#endif
		CachedWorldToMetersScale = WorldContext.World()->GetWorldSettings()->WorldToMeters;

		// Only refresh this based on the game world.  When remoting there is also an editor world, which we do not want to have affect the transform.
		if (WorldContext.World()->IsGameWorld())
		{
			RefreshTrackingToWorldTransform(WorldContext);
		}

		return true;
	}

	void FWindowsMixedRealityHMD::UpdateRemotingStatus()
	{
#if WITH_WINDOWS_MIXED_REALITY
		if (HMD == nullptr)
		{
			return;
		}

		// Set Remoting status
		HMDRemotingConnectionState state = HMD->GetConnectionState();

		static bool bPreviousFrameDisconnect = false;
		//if there was a disconnect event and we haven't tried to do anything since, fire the event
		if (bPreviousFrameDisconnect && (state == HMDRemotingConnectionState::Disconnected))
		{
			FString Reason = HMD->GetFailureString();
			if (UHeadMountedDisplayFunctionLibrary::OnXRDeviceOnDisconnectDelegate.IsBound() && !Reason.IsEmpty())
			{
				UHeadMountedDisplayFunctionLibrary::OnXRDeviceOnDisconnectDelegate.Execute(Reason);
			}
			bPreviousFrameDisconnect = false;
		}

#if WITH_EDITOR
		if (state != prevState)
		{
			switch (state)
			{
			case HMDRemotingConnectionState::Unknown:
				UWindowsMixedRealityRuntimeSettings::Get()->OnRemotingStatusChanged.ExecuteIfBound(FString("Connection State Unknown - See log for any errors"), FLinearColor::Gray);
				break;
			case HMDRemotingConnectionState::Connecting:
				UWindowsMixedRealityRuntimeSettings::Get()->OnRemotingStatusChanged.ExecuteIfBound(FString("Connecting..."), FLinearColor::Yellow);
				break;
			case HMDRemotingConnectionState::Connected:
				UWindowsMixedRealityRuntimeSettings::Get()->OnRemotingStatusChanged.ExecuteIfBound(FString("Connected"), FLinearColor::Green);
				break;
			case HMDRemotingConnectionState::Disconnected:
				UWindowsMixedRealityRuntimeSettings::Get()->OnRemotingStatusChanged.ExecuteIfBound(FString("Disconnected"), FLinearColor::Red);

				bPreviousFrameDisconnect = true;

				break;
			}
		}
#else
		if (state != prevState)
		{
			switch (state)
			{
			case HMDRemotingConnectionState::Unknown:
				UE_LOG(LogWmrHmd, Log, TEXT("Connection State Unknown - See log for any errors"));
				break;
			case HMDRemotingConnectionState::Connecting:
				UE_LOG(LogWmrHmd, Log, TEXT("Connecting..."));
				break;
			case HMDRemotingConnectionState::Connected:
				UE_LOG(LogWmrHmd, Log, TEXT("Connected"));
				break;
			case HMDRemotingConnectionState::Disconnected:
				UE_LOG(LogWmrHmd, Log, TEXT("Disconnected"));
				break;
			}
		}
#endif

		prevState = state;
#endif
	}
	
#if WITH_WINDOWS_MIXED_REALITY
	void FWindowsMixedRealityHMD::OnConnectionEvent(WindowsMixedReality::MixedRealityInterop::ConnectionEvent evt)
	{
		if (evt == MixedRealityInterop::ConnectionEvent::DisconnectedFromPeer)
		{
			if (isRemotingReconnecting)
			{
				ConnectToRemoteHoloLens(*RemotingDesc.ip, RemotingDesc.bitrate, RemotingDesc.isHoloLens1, RemotingDesc.port, RemotingDesc.listen);
			}
			else
			{
				DisconnectFromRemoteHoloLens();
			}
		}
	}
#endif

	void FWindowsMixedRealityHMD::SetTrackingOrigin(EHMDTrackingOrigin::Type NewOrigin)
	{
		if (HMD != nullptr)
		{
			if (NewOrigin == EHMDTrackingOrigin::Eye)
			{
				HMD->SetTrackingOrigin(HMDTrackingOrigin::Eye);
			}
			else
			{
				HMD->SetTrackingOrigin(HMDTrackingOrigin::Floor);
			}
		}
	}

	EHMDTrackingOrigin::Type FWindowsMixedRealityHMD::GetTrackingOrigin() const
	{
		if (HMD != nullptr)
		{
			HMDTrackingOrigin origin = HMD->GetTrackingOrigin();
			if (origin == HMDTrackingOrigin::Floor)
			{
				return EHMDTrackingOrigin::Floor;
			}
		}

		return EHMDTrackingOrigin::Eye;
	}

	bool FWindowsMixedRealityHMD::EnumerateTrackedDevices(TArray<int32>& OutDevices, EXRTrackedDeviceType Type)
	{
		if (Type == EXRTrackedDeviceType::Any || Type == EXRTrackedDeviceType::HeadMountedDisplay)
		{
			OutDevices.Add(IXRTrackingSystem::HMDDeviceId);
			return true;
		}
		return false;
	}

	void FWindowsMixedRealityHMD::SetInterpupillaryDistance(float NewInterpupillaryDistance)
	{
		ipd = NewInterpupillaryDistance;
	}

	float FWindowsMixedRealityHMD::GetInterpupillaryDistance() const
	{
		if (ipd == 0)
		{
			return 0.064f;
		}

		return ipd;
	}

	void FWindowsMixedRealityHMD::OnBeginRendering_GameThread()
	{
#if WITH_WINDOWS_MIXED_REALITY
#endif
	}

	void FWindowsMixedRealityHMD::OnBeginRendering_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneViewFamily& ViewFamily)
	{
#if WITH_WINDOWS_MIXED_REALITY
		HMD->BlockUntilNextFrame();
		if (!HMD->UpdateRenderThreadFrame())
		{
			return;
		}
		InitTrackingFrame();

		if (SpectatorScreenController)
		{
			SpectatorScreenController->UpdateSpectatorScreenMode_RenderThread();
		}

		if (!HMD->CreateRenderingParameters())
		{
			// This will happen if an exception is thrown while creating the frame's rendering parameters.
			// Because Windows Mixed Reality can only have 2 rendering parameters in flight at any time, this is fatal.
			this->bRequestRestart = true;
		}

#if PLATFORM_HOLOLENS
		if (bIsMobileMultiViewEnabled && !HMD->IsThirdCameraActive())
		{
			ID3D11Texture2D* Texture = HMD->GetBackBufferTexture();
			FTexture2DArrayRHIRef BackBuffer = GD3D11RHI->RHICreateTexture2DArrayFromResource(PF_B8G8R8A8, TexCreate_RenderTargetable | TexCreate_ShaderResource, FClearValueBinding::None, Texture);
			GDynamicRHI->RHIAliasTextureResources((FTextureRHIRef&)CurrentBackBuffer, (FTextureRHIRef&)BackBuffer);
		}
#endif
#endif
	}

	FIntRect FWindowsMixedRealityHMD::GetFullFlatEyeRect_RenderThread(FTexture2DRHIRef EyeTexture) const
	{
		if (HMD->IsRemoting())
		{
			// Hololens has a relatively narrow FOV with very little distortion.
			static FVector2D SrcNormRectMin(0.0f, 0.0f);
			static FVector2D SrcNormRectMax(0.5f, 1.0f);
			return FIntRect(EyeTexture->GetSizeX() * SrcNormRectMin.X, EyeTexture->GetSizeY() * SrcNormRectMin.Y, EyeTexture->GetSizeX() * SrcNormRectMax.X, EyeTexture->GetSizeY() * SrcNormRectMax.Y);
		}
		else
		{
			static FVector2D SrcNormRectMin(0.05f, 0.2f);
			static FVector2D SrcNormRectMax(0.45f, 0.8f);

#if PLATFORM_HOLOLENS
			SrcNormRectMax.X = (bIsMobileMultiViewEnabled && !HMD->IsThirdCameraActive()) ? 0.95f : 0.45f;
#endif

			return FIntRect(EyeTexture->GetSizeX() * SrcNormRectMin.X, EyeTexture->GetSizeY() * SrcNormRectMin.Y, EyeTexture->GetSizeX() * SrcNormRectMax.X, EyeTexture->GetSizeY() * SrcNormRectMax.Y);
		}
	}

	void FWindowsMixedRealityHMD::CopyTexture_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture2D* SrcTexture, FIntRect SrcRect, FRHITexture2D* DstTexture, FIntRect DstRect, bool bClearBlack, bool bNoAlpha) const
	{
		check(IsInRenderingThread());

		const uint32 ViewportWidth = DstRect.Width();
		const uint32 ViewportHeight = DstRect.Height();
		const FIntPoint TargetSize(ViewportWidth, ViewportHeight);

		const float SrcTextureWidth = SrcTexture->GetSizeX();
		const float SrcTextureHeight = SrcTexture->GetSizeY();
		float U = 0.f, V = 0.f, USize = 1.f, VSize = 1.f;
		if (!SrcRect.IsEmpty())
		{
			U = SrcRect.Min.X / SrcTextureWidth;
			V = SrcRect.Min.Y / SrcTextureHeight;
			USize = SrcRect.Width() / SrcTextureWidth;
			VSize = SrcRect.Height() / SrcTextureHeight;
		}

		// #todo-renderpasses Possible optimization here - use DontLoad if we will immediately clear the entire target
		FRHIRenderPassInfo RPInfo(DstTexture, ERenderTargetActions::Load_Store);
		RHICmdList.BeginRenderPass(RPInfo, TEXT("WindowsMixedRealityHMD_CopyTexture"));
		{
			if (bClearBlack)
			{
				const FIntRect ClearRect(0, 0, DstTexture->GetSizeX(), DstTexture->GetSizeY());
				RHICmdList.SetViewport(ClearRect.Min.X, ClearRect.Min.Y, 0, ClearRect.Max.X, ClearRect.Max.Y, 1.0f);
				DrawClearQuad(RHICmdList, FLinearColor::Black);
			}

			RHICmdList.SetViewport(DstRect.Min.X, DstRect.Min.Y, 0, DstRect.Max.X, DstRect.Max.Y, 1.0f);

			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
			GraphicsPSOInit.BlendState = bNoAlpha ? TStaticBlendState<>::GetRHI() : TStaticBlendState<CW_RGBA, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_One, BF_InverseSourceAlpha>::GetRHI();
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
			GraphicsPSOInit.PrimitiveType = PT_TriangleList;

			const auto FeatureLevel = GMaxRHIFeatureLevel;
			auto ShaderMap = GetGlobalShaderMap(FeatureLevel);

			TShaderMapRef<FScreenVS> VertexShader(ShaderMap);
			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();

			const bool bSameSize = DstRect.Size() == SrcRect.Size();
			FRHISamplerState* PixelSampler = bSameSize ? TStaticSamplerState<SF_Point>::GetRHI() : TStaticSamplerState<SF_Bilinear>::GetRHI();

			if ((SrcTexture->GetFlags() & TexCreate_SRGB) != 0)
			{
				TShaderMapRef<FScreenPSsRGBSource> PixelShader(ShaderMap);
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();

				SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
				PixelShader->SetParameters(RHICmdList, PixelSampler, SrcTexture);
			}
			else
			{
				TShaderMapRef<FScreenPS> PixelShader(ShaderMap);
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();

				SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
				PixelShader->SetParameters(RHICmdList, PixelSampler, SrcTexture);
			}

			RendererModule->DrawRectangle(
				RHICmdList,
				0, 0,
				ViewportWidth, ViewportHeight,
				U, V,
				USize, VSize,
				TargetSize,
				FIntPoint(1, 1),
				VertexShader,
				EDRF_Default);

			RHICmdList.EndRenderPass();
		}
	}

	bool FWindowsMixedRealityHMD::GetCurrentPose(
		int32 DeviceId,
		FQuat& CurrentOrientation,
		FVector& CurrentPosition)
	{
		if (DeviceId != IXRTrackingSystem::HMDDeviceId)
		{
			return false;
		}

#if WITH_INPUT_SIMULATION
		if (auto* InputSim = UWindowsMixedRealityInputSimulationEngineSubsystem::GetInputSimulationIfEnabled())
		{
			CurrentOrientation = InputSim->GetHeadOrientation();
			CurrentPosition = InputSim->GetHeadPosition();
		}
		else
#endif
		{
			// Get most recently available tracking data.
			Frame& TheFrame = GetFrame();
			CurrentOrientation = TheFrame.HeadOrientation;
			CurrentPosition = TheFrame.HeadPosition;
		}

		return true;
	}

	bool FWindowsMixedRealityHMD::GetRelativeEyePose(int32 DeviceId, EStereoscopicPass Eye, FQuat& OutOrientation, FVector& OutPosition)
	{
		OutOrientation = FQuat::Identity;
		OutPosition = FVector::ZeroVector;
		if (Eye != eSSP_LEFT_EYE && Eye != eSSP_RIGHT_EYE && Eye != eSSP_THIRD_CAMERA_EYE)
		{
			return false;
		}
		Frame& TheFrame = GetFrame();
		FTransform relativeTransform = FTransform::Identity;

		switch (Eye)
		{
		case eSSP_LEFT_EYE:
			relativeTransform = TheFrame.LeftTransform * TheFrame.HeadTransform.Inverse();
			break;
		case eSSP_RIGHT_EYE:
			relativeTransform = TheFrame.RightTransform * TheFrame.HeadTransform.Inverse();
			break;
		case eSSP_THIRD_CAMERA_EYE:
			relativeTransform = TheFrame.ThirdCameraTransform * TheFrame.HeadTransform.Inverse();
			break;
		};

		OutPosition = relativeTransform.GetTranslation();
		OutOrientation = relativeTransform.GetRotation();

		return true;
	}

	
	void FWindowsMixedRealityHMD::GetMotionControllerData(UObject* WorldContext, const EControllerHand Hand, FXRMotionControllerData& MotionControllerData)
	{
		MotionControllerData.DeviceName = GetSystemName();
		MotionControllerData.ApplicationInstanceID = FApp::GetInstanceId();
		MotionControllerData.HandIndex = Hand;

		MotionControllerData.TrackingStatus = (ETrackingStatus)GetControllerTrackingStatus((WindowsMixedReality::HMDHand)Hand);

		//sword-grasping transform
		HMDHand WMRHand = (Hand == EControllerHand::Left ? HMDHand::Left : HMDHand::Right);
		FRotator GripTrackingRotation;
		FVector GripTrackingPosition;
		MotionControllerData.bValid = GetControllerOrientationAndPosition(WMRHand, GripTrackingRotation, GripTrackingPosition, GetWorldToMetersScale());
		if (MotionControllerData.bValid)
		{
			FTransform GripTrackingTransform(GripTrackingRotation.Quaternion(), GripTrackingPosition);
			FTransform GripWorldTransform = (GripTrackingTransform * CachedTrackingToWorld);

			MotionControllerData.GripRotation = GripWorldTransform.GetRotation();
			MotionControllerData.GripPosition = GripWorldTransform.GetLocation();
		}

		//far pointing from elbow transform
		FPointerPoseInfo PointerPoseInfo = UWindowsMixedRealityFunctionLibrary::GetPointerPoseInfo(Hand);
		MotionControllerData.AimPosition = PointerPoseInfo.Origin;
		MotionControllerData.AimRotation = PointerPoseInfo.Orientation;

		MotionControllerData.bIsGrasped = UWindowsMixedRealityFunctionLibrary::IsGrasped(Hand);

		if (!SupportsHandTracking())
		{
			MotionControllerData.DeviceVisualType = EXRVisualType::Controller;
			return;
		}
		MotionControllerData.DeviceVisualType = EXRVisualType::Hand;
		//assume valid
		FTransform Transform;
		float Radius = 0.0f;
		//if there are any valid hand transforms
		//MotionControllerData.bValid = IWindowsMixedRealityHandTrackingModule::Get().GetHandJointTransform(Hand, EWMRHandKeypoint::Palm, Transform, Radius);
		FWindowsMixedRealityStatics::OnGetHandJointTransformDelegate.Broadcast(Hand, EHandKeypoint::Palm, Transform, Radius, MotionControllerData.bValid);
		if (MotionControllerData.bValid)
		{
			MotionControllerData.HandKeyPositions.Reserve(EHandKeypointCount);
			MotionControllerData.HandKeyPositions.Empty();
			MotionControllerData.HandKeyPositions.Add(Transform.GetTranslation());

			MotionControllerData.HandKeyRotations.Reserve(EHandKeypointCount);
			MotionControllerData.HandKeyRotations.Empty();
			MotionControllerData.HandKeyRotations.Add(Transform.GetRotation());

			MotionControllerData.HandKeyRadii.Reserve(EHandKeypointCount);
			MotionControllerData.HandKeyRadii.Empty();
			//needed
			MotionControllerData.HandKeyRadii.Add(Radius);

			//get the remaining points
			for (int32 HandPointIndex = 1; HandPointIndex < EHandKeypointCount; ++HandPointIndex)
			{
				bool bHandPointSuccess = false;
				FWindowsMixedRealityStatics::OnGetHandJointTransformDelegate.Broadcast(Hand, (EHandKeypoint)HandPointIndex, Transform, Radius, bHandPointSuccess);
				MotionControllerData.bValid = MotionControllerData.bValid && bHandPointSuccess;
				MotionControllerData.HandKeyPositions.Add(Transform.GetTranslation());
				MotionControllerData.HandKeyRotations.Add(Transform.GetRotation());

				//needed
				MotionControllerData.HandKeyRadii.Add(Radius);
			}
		}
	}

	bool FWindowsMixedRealityHMD::ConfigureGestures(const FXRGestureConfig& GestureConfig)
	{
		bool bSuccess = true;

		FWindowsMixedRealityStatics::OnConfigureGesturesDelegate.Broadcast(GestureConfig, bSuccess);

		return bSuccess;
	}

	void FWindowsMixedRealityHMD::ResetOrientationAndPosition(float yaw)
	{
#if WITH_WINDOWS_MIXED_REALITY
		HMD->ResetOrientationAndPosition();
#endif
	}

	FMatrix FetchProjectionMatrix(EStereoscopicPass StereoPassType, WindowsMixedReality::MixedRealityInterop* HMD)
	{
		if (StereoPassType != eSSP_LEFT_EYE &&
			StereoPassType != eSSP_RIGHT_EYE &&
			StereoPassType != eSSP_THIRD_CAMERA_EYE)
		{
			return FMatrix::Identity;
		}

#if WITH_WINDOWS_MIXED_REALITY
		DirectX::XMFLOAT4X4 projection = DirectX::XMFLOAT4X4();

		switch (StereoPassType)
		{
		case eSSP_LEFT_EYE:
			projection = HMD->GetProjectionMatrix(HMDEye::Left);
			break;
		case eSSP_RIGHT_EYE:
			projection = HMD->GetProjectionMatrix(HMDEye::Right);
			break;
		case eSSP_THIRD_CAMERA_EYE:
			projection = HMD->GetProjectionMatrix(HMDEye::ThirdCamera);
			break;
		};

		auto result = WMRUtility::ToFMatrix(projection).GetTransposed();
		// Convert from RH to LH projection matrix
		// See PerspectiveOffCenterRH: https://msdn.microsoft.com/en-us/library/windows/desktop/ms918176.aspx
		result.M[2][0] *= -1;
		result.M[2][1] *= -1;
		result.M[2][2] *= -1;
		result.M[2][3] *= -1;

		return result;
#else
		return FMatrix::Identity;
#endif
	}

	void FWindowsMixedRealityHMD::InitTrackingFrame()
	{
#if WITH_WINDOWS_MIXED_REALITY
		DirectX::XMMATRIX leftPose;
		DirectX::XMMATRIX rightPose;
		DirectX::XMMATRIX thirdCameraPoseLeft;
		DirectX::XMMATRIX thirdCameraPoseRight;

		bool GotPose = false;
		if (IsInRenderingThread())
		{
			// This should be false if we fail to get a pose
			Frame_RenderThread.bPositionalTrackingUsed = false;

			// Get third camera view and projection
			if (HMD->IsThirdCameraActive())
			{
				GotPose = HMD->GetThirdCameraPoseRenderThread(thirdCameraPoseLeft, thirdCameraPoseRight);

				if (GotPose)
				{
					// Convert to unreal space
					FMatrix UPoseL = WMRUtility::ToFMatrix(thirdCameraPoseLeft);
					FMatrix UPoseR = WMRUtility::ToFMatrix(thirdCameraPoseRight);
					FQuat RotationL = FQuat(UPoseL);
					FQuat RotationR = FQuat(UPoseR);

					RotationL = FQuat(-1 * RotationL.Z, RotationL.X, RotationL.Y, -1 * RotationL.W);
					RotationR = FQuat(-1 * RotationR.Z, RotationR.X, RotationR.Y, -1 * RotationR.W);

					RotationL.Normalize();
					RotationR.Normalize();

					FQuat ThirdCamRotation = FMath::Lerp(RotationL, RotationR, 0.5f);
					ThirdCamRotation.Normalize();

					// Position = forward/ backwards, left/ right, up/ down.
					FVector PositionL = ((FVector(UPoseL.M[2][3], -1 * UPoseL.M[0][3], -1 * UPoseL.M[1][3]) * GetWorldToMetersScale()));
					FVector PositionR = ((FVector(UPoseR.M[2][3], -1 * UPoseR.M[0][3], -1 * UPoseR.M[1][3]) * GetWorldToMetersScale()));

					PositionL = RotationL.RotateVector(PositionL);
					PositionR = RotationR.RotateVector(PositionR);

					FVector ThirdCamPosition = FMath::Lerp(PositionL, PositionR, 0.5f);

					Frame_RenderThread.ThirdCameraTransform = FTransform(ThirdCamRotation, ThirdCamPosition, FVector::OneVector);
					Frame_RenderThread.ProjectionMatrixThirdCamera = FetchProjectionMatrix((EStereoscopicPass)eSSP_THIRD_CAMERA_EYE, HMD);

					// Rescale depth projection range from meters to world units
					Frame_RenderThread.ProjectionMatrixThirdCamera.M[3][2] *= GetWorldToMetersScale();
				}
			}

			// Get HMD view and projection
			GotPose = HMD->GetCurrentPoseRenderThread(leftPose, rightPose);

			if (GotPose)
			{
				// Convert to unreal space
				FMatrix UPoseL = WMRUtility::ToFMatrix(leftPose);
				FMatrix UPoseR = WMRUtility::ToFMatrix(rightPose);
				FQuat RotationL = FQuat(UPoseL);
				FQuat RotationR = FQuat(UPoseR);

				RotationL = FQuat(-1 * RotationL.Z, RotationL.X, RotationL.Y, -1 * RotationL.W);
				RotationR = FQuat(-1 * RotationR.Z, RotationR.X, RotationR.Y, -1 * RotationR.W);

				RotationL.Normalize();
				RotationR.Normalize();

				FQuat HeadRotation = FMath::Lerp(RotationL, RotationR, 0.5f);
				HeadRotation.Normalize();

				// Position = forward/ backwards, left/ right, up/ down.
				FVector PositionL = ((FVector(UPoseL.M[2][3], -1 * UPoseL.M[0][3], -1 * UPoseL.M[1][3]) * GetWorldToMetersScale()));
				FVector PositionR = ((FVector(UPoseR.M[2][3], -1 * UPoseR.M[0][3], -1 * UPoseR.M[1][3]) * GetWorldToMetersScale()));

				PositionL = RotationL.RotateVector(PositionL);
				PositionR = RotationR.RotateVector(PositionR);

				ipd = FVector::Dist(PositionL, PositionR) / GetWorldToMetersScale();

				FVector HeadPosition = FMath::Lerp(PositionL, PositionR, 0.5f);

				Frame_RenderThread.bPositionalTrackingUsed = HMD->GetTrackingState() == WindowsMixedReality::HMDSpatialLocatability::PositionalTrackingActive;
				Frame_RenderThread.RotationL = RotationL;
				Frame_RenderThread.RotationR = RotationR;
				Frame_RenderThread.PositionL = PositionL;
				Frame_RenderThread.PositionR = PositionR;
				Frame_RenderThread.HeadOrientation = HeadRotation;
				Frame_RenderThread.HeadPosition = HeadPosition;
				Frame_RenderThread.LeftTransform = FTransform(RotationL, PositionL, FVector::OneVector);
				Frame_RenderThread.RightTransform = FTransform(RotationR, PositionR, FVector::OneVector);
				Frame_RenderThread.HeadTransform = FTransform(HeadRotation, HeadPosition, FVector::OneVector);

				Frame_RenderThread.ProjectionMatrixL = FetchProjectionMatrix(eSSP_LEFT_EYE, HMD);
				Frame_RenderThread.ProjectionMatrixR = FetchProjectionMatrix(eSSP_RIGHT_EYE, HMD);

				// Rescale depth projection range from meters to world units
				Frame_RenderThread.ProjectionMatrixL.M[3][2] *= GetWorldToMetersScale();
				Frame_RenderThread.ProjectionMatrixR.M[3][2] *= GetWorldToMetersScale();

				{
					FScopeLock Lock(&Frame_NextGameThreadLock);
					Frame_NextGameThread = Frame_RenderThread;
				}
			}
		}
		else
		{
			// We are using the previous render thread frame for the game thread.
			FScopeLock Lock(&Frame_NextGameThreadLock);
			Frame_GameThread = Frame_NextGameThread;
		}
#endif
	}

#if WITH_WINDOWS_MIXED_REALITY
	void SetupHiddenVisibleAreaMesh(TArray<FHMDViewMesh>& HiddenMeshes, TArray<FHMDViewMesh>& VisibleMeshes, MixedRealityInterop* HMD)
	{
		for (int i = (int)HMDEye::Left;
			i <= (int)HMDEye::Right; i++)
		{
			HMDEye eye = (HMDEye)i;

			DirectX::XMFLOAT2* vertices;
			int length;
			if (HMD->GetHiddenAreaMesh(eye, vertices, length))
			{
				FVector2D* const vertexPositions = new FVector2D[length];
				for (int v = 0; v < length; v++)
				{
					// Remap to space Unreal is expecting.
					float x = (vertices[v].x + 1) / 2.0f;
					float y = (vertices[v].y + 1) / 2.0f;

					vertexPositions[v].Set(x, y);
				}
				HiddenMeshes[i].BuildMesh(vertexPositions, length, FHMDViewMesh::MT_HiddenArea);

				delete[] vertexPositions;
			}

			if (HMD->GetVisibleAreaMesh(eye, vertices, length))
			{
				FVector2D* const vertexPositions = new FVector2D[length];
				for (int v = 0; v < length; v++)
				{
					// Remap from NDC space to [0..1] bottom-left origin.
					float x = (vertices[v].x + 1) / 2.0f;
					float y = (vertices[v].y + 1) / 2.0f;

					vertexPositions[v].Set(x, y);
				}
				VisibleMeshes[i].BuildMesh(vertexPositions, length, FHMDViewMesh::MT_VisibleArea);

				delete[] vertexPositions;
			}
		}
	}
#endif

	void FWindowsMixedRealityHMD::SetupHolographicCamera()
	{
#if WITH_WINDOWS_MIXED_REALITY
		// Set the viewport to match the HMD display
		FSceneViewport* SceneVP = FindMRSceneViewport(bIsStereoDesired);

		if (SceneVP)
		{
			TSharedPtr<SWindow> Window = SceneVP->FindWindow();
			//if (Window.IsValid() && SceneVP->GetViewportWidget().IsValid())
			{
#if !PLATFORM_HOLOLENS
				// Set MirrorWindow state on the Window
				Window->SetMirrorWindow(bIsStereoDesired);
#endif

				if (bIsStereoDesired)
				{
					int Width, Height;
					if (HMD->GetDisplayDimensions(Width, Height))
					{
						bIsStereoEnabled = HMD->IsStereoEnabled();

						SceneVP->SetViewportSize(
#if PLATFORM_HOLOLENS
							Width,
#else
							Width * 2,
#endif
							Height);

						Window->SetViewportSizeDrivenByWindow(false);

						if (bIsStereoEnabled)
						{
							HMD->CreateHiddenVisibleAreaMesh();

							FWindowsMixedRealityHMD* Self = this;
							ENQUEUE_RENDER_COMMAND(SetupHiddenVisibleAreaMeshCmd)([Self](FRHICommandListImmediate& RHICmdList)
							{
								SetupHiddenVisibleAreaMesh(Self->HiddenAreaMesh, Self->VisibleAreaMesh, Self->HMD);
							});
						}
					}
				}
				else
				{
					FVector2D size = SceneVP->FindWindow()->GetSizeInScreen();
					SceneVP->SetViewportSize(size.X, size.Y);
					Window->SetViewportSizeDrivenByWindow(true);
					bIsStereoEnabled = false;
				}
			}
		}
		else if (GIsEditor && HMD->IsInitialized() && bIsStereoDesired && !bIsStereoEnabled)
		{
			// This can happen if device is disconnected while running in VR Preview, then create a new VR preview window while device is still disconnected.
			// We can get a window that is not configured for stereo when we plug our device back in.
			this->bRequestRestart = true;
		}

		// Uncap fps to enable FPS higher than 62
		GEngine->bForceDisableFrameRateSmoothing = bIsStereoEnabled;
#endif
	}

	bool FWindowsMixedRealityHMD::IsStereoEnabled() const
	{
		return bIsStereoEnabled;
	}

	static bool ParseAddress(const FString& MatchesStr, FString& address, uint32& Port)
	{
		FString portStr;
		if (MatchesStr.Len() == 0)
		{
			return false;
		}

		if (MatchesStr.Split(TEXT(":"), &address, &portStr))
		{
			Port = FCString::Atoi(*portStr);
		}
		else
		{
			address = MatchesStr;
			Port = 8265;
		}
		return true;
	}

	bool FWindowsMixedRealityHMD::bEnableStereoReentranceGuard = false;

	bool FWindowsMixedRealityHMD::EnableStereo(bool stereo)
	{
		if (bEnableStereoReentranceGuard)
		{
			return false;
		}

		bEnableStereoReentranceGuard = true;
#if WITH_WINDOWS_MIXED_REALITY
		if (stereo)
		{
			if (FParse::Param(FCommandLine::Get(), TEXT("AutoConnectToRemoting")))
			{
				int32 RemotingBitRate = 4000;
				FString RemotingAppIp;
				if (FParse::Value(FCommandLine::Get(), TEXT("RemotingAppIp="), RemotingAppIp) &&
					FParse::Value(FCommandLine::Get(), TEXT("RemotingBitRate="), RemotingBitRate))
				{
					FWindowsMixedRealityStatics::ConnectToRemoteHoloLens(*RemotingAppIp, RemotingBitRate, false);
				}
			}

			if (bIsStereoDesired && HMD->IsInitialized())
			{
				bEnableStereoReentranceGuard = false;
				return false;
			}

			FindMRSceneViewport(bIsStereoDesired);
			if (!bIsStereoDesired)
			{
				bEnableStereoReentranceGuard = false;
				return false;
			}

#if PLATFORM_WINDOWS && !WITH_EDITOR
			FString MatchesStr;
			uint32 BitRate;
			FString IPAddress;
			uint32 Port;

			if (!FParse::Value(FCommandLine::Get(), TEXT("RemotingBitrate="), BitRate))
			{
				BitRate = 8000;
			}


			if (FParse::Value(FCommandLine::Get(), TEXT("HoloLens1Remoting="), MatchesStr))
			{
				if (ParseAddress(MatchesStr, IPAddress, Port))
				{
					ConnectToRemoteHoloLens(*IPAddress, BitRate, true, Port);
				}
			}
			else if (FParse::Value(FCommandLine::Get(), TEXT("HoloLensRemoting="), MatchesStr))
			{
				if (ParseAddress(MatchesStr, IPAddress, Port))
				{
					ConnectToRemoteHoloLens(*IPAddress, BitRate, false, Port);
				}
			}
			else if (FParse::Value(FCommandLine::Get(), TEXT("HoloLensRemotingListen="), MatchesStr))
			{
				if (ParseAddress(MatchesStr, IPAddress, Port))
				{
					ConnectToRemoteHoloLens(*IPAddress, BitRate, false, Port, true);
				}
			}
			else if (FParse::Value(FCommandLine::Get(), TEXT("HoloLensRemotingListenPort="), Port))
			{
				ConnectToRemoteHoloLens(TEXT("0.0.0.0"), BitRate, false, Port, true);
			}
#endif
			HMD->EnableStereo(stereo);
#if SUPPORTS_WINDOWS_MIXED_REALITY_GESTURES
			HMD->SetInteractionManagerForCurrentView();
#endif

			InitializeHolographic();

			currentWornState = GetHMDWornState();

			FApp::SetUseVRFocus(true);
			FApp::SetHasVRFocus(true);

			// Start speech recognition if there are any commands we care to listen for
			StartSpeechRecognition();
		}
		else
		{
#if PLATFORM_WINDOWS && !WITH_EDITOR
			if (HMD && HMD->IsRemoting())
			{
				DisconnectFromRemoteHoloLens();
			}
#endif
			
			ShutdownHolographic();

			FApp::SetUseVRFocus(false);
			FApp::SetHasVRFocus(false);

			StopSpeechRecognition();
		}
#endif
		bEnableStereoReentranceGuard = false;

		return bIsStereoDesired;
	}

	FMatrix FWindowsMixedRealityHMD::GetStereoProjectionMatrix(const enum EStereoscopicPass StereoPassType) const
	{
		if (IsInRenderingThread())
		{
			if (StereoPassType == eSSP_LEFT_EYE)
			{
				return Frame_RenderThread.ProjectionMatrixL;
			}
			else if (StereoPassType == eSSP_RIGHT_EYE)
			{
				return Frame_RenderThread.ProjectionMatrixR;
			}
			else if (StereoPassType == eSSP_THIRD_CAMERA_EYE)
			{
				return Frame_RenderThread.ProjectionMatrixThirdCamera;
			}
			else
			{
				return FMatrix::Identity;
			}
		}
		else
		{
			check(IsInGameThread());

			if (StereoPassType == eSSP_LEFT_EYE)
			{
				return Frame_GameThread.ProjectionMatrixL;
			}
			else if (StereoPassType == eSSP_RIGHT_EYE)
			{
				return Frame_GameThread.ProjectionMatrixR;
			}
			else if (StereoPassType == eSSP_THIRD_CAMERA_EYE)
			{
				return Frame_RenderThread.ProjectionMatrixThirdCamera;
			}
			else
			{
				return FMatrix::Identity;
			}
		}
	}

	struct RenderTargetDescription
	{
	public:
		int width = 0;
		int height = 0;
		int x = 0;
		int y = 0;
		float uvOffsetX = 0;
		float uvOffsetY = 0;
		float uvScaleX = 0;
		float uvScaleY = 0;

		RenderTargetDescription() { }
		RenderTargetDescription(int width, int height, int x, int y, float uvOffsetX, float uvOffsetY, float uvScaleX, float uvScaleY)
		{
			this->width = width;
			this->height = height;
			this->x = x;
			this->y = y;
			this->uvOffsetX = uvOffsetX;
			this->uvOffsetY = uvOffsetY;
			this->uvScaleX = uvScaleX;
			this->uvScaleY = uvScaleY;
		}
	};
	RenderTargetDescription wmrRenderTargets[3];

	void FWindowsMixedRealityHMD::GetEyeRenderParams_RenderThread(
		const FRenderingCompositePassContext& Context,
		FVector2D& EyeToSrcUVScaleValue,
		FVector2D& EyeToSrcUVOffsetValue) const
	{
		RenderTargetDescription desc = wmrRenderTargets[GetViewIndexForPass(Context.View.StereoPass)];

		EyeToSrcUVOffsetValue.X = desc.uvOffsetX;
		EyeToSrcUVOffsetValue.Y = desc.uvOffsetY;

		EyeToSrcUVScaleValue.X = desc.uvScaleX;
		EyeToSrcUVScaleValue.Y = desc.uvScaleY;
	}

	void FWindowsMixedRealityHMD::AdjustViewRect(
		EStereoscopicPass StereoPass,
		int32& X, int32& Y,
		uint32& SizeX, uint32& SizeY) const
	{
		RenderTargetDescription desc = wmrRenderTargets[GetViewIndexForPass(StereoPass)];
		X = desc.x;
		Y = desc.y;
		SizeX = desc.width;
		SizeY = desc.height;
	}

	FIntPoint FWindowsMixedRealityHMD::GetIdealRenderTargetSize() const
	{
		int Width, Height;
		int tcWidth, tcHeight;
#if WITH_WINDOWS_MIXED_REALITY
		HMD->GetDisplayDimensions(Width, Height);
		HMD->GetThirdCameraDimensions(tcWidth, tcHeight);
#else
		Width = 100;
		Height = 100;
		tcWidth = 0;
		tcHeight = 0;
#endif

#if PLATFORM_HOLOLENS
		int Offset = (bIsMobileMultiViewEnabled && !HMD->IsThirdCameraActive()) ? 0 : Width;
#else
		int Offset = Width;
#endif

		int RenderTargetWidth = Width + Offset + tcWidth;
		int RenderTargetHeight = FMath::Max(Height, tcHeight);

		// We always prefer the nearest multiple of 4 for our buffer sizes. Make sure we round up here,
		// so we're consistent with the rest of the engine in creating our buffers.
		FIntPoint Size = FIntPoint(RenderTargetWidth, RenderTargetHeight);
		QuantizeSceneBufferSize(Size, Size);

		float eyeUVX = (float)Width / (float)Size.X;
		float eyeUVY = (float)Height / (float)Size.Y;
#if PLATFORM_HOLOLENS
		float offUVX = (bIsMobileMultiViewEnabled && !HMD->IsThirdCameraActive()) ? 0.0f : eyeUVX;
#else
		float offUVX = eyeUVX;
#endif

		float camUVX = (float)tcWidth / (float)Size.X;
		float camUVY = (float)tcHeight / (float)Size.Y;

		//                                            width,    height,    x,           y,  uvOffsetX,   uvOffsetY, uvScaleX, uvScaleY
		wmrRenderTargets[0] = RenderTargetDescription(Width,    Height,    0,           0,  0.0f,        0.0f,      eyeUVX,   eyeUVY);  // Left Eye
		wmrRenderTargets[1] = RenderTargetDescription(Width,    Height,    Offset,      0,  offUVX,      0.0f,      eyeUVX,   eyeUVY);  // Right Eye
		wmrRenderTargets[2] = RenderTargetDescription(tcWidth,  tcHeight,  Offset * 2,  0,  offUVX * 2,  0.0f,      camUVX,   camUVY);  // Third Camera

		return Size;
	}

	//TODO: Spelling is intentional, overridden from IHeadMountedDisplay.h
	float FWindowsMixedRealityHMD::GetPixelDenity() const
	{
		check(IsInGameThread());
		return ScreenScalePercentage;
	}

	void FWindowsMixedRealityHMD::SetPixelDensity(const float NewDensity)
	{
		check(IsInGameThread());
		//TODO: Get actual minimum value from platform.
		ScreenScalePercentage = FMath::Clamp<float>(NewDensity, 0.4f, 1.0f);
	}

	// Called when screen size changes.
	void FWindowsMixedRealityHMD::UpdateViewportRHIBridge(bool bUseSeparateRenderTarget, const class FViewport& Viewport, FRHIViewport* const ViewportRHI)
	{
#if WITH_WINDOWS_MIXED_REALITY
		if (IsStereoEnabled() && mCustomPresent != nullptr)
		{
			HMD->SetScreenScaleFactor(ScreenScalePercentage);
			mCustomPresent->UpdateViewport(Viewport, ViewportRHI);
		}
#endif
	}

	void FWindowsMixedRealityHMD::RenderTexture_RenderThread(class FRHICommandListImmediate& RHICmdList, class FRHITexture2D* BackBuffer, class FRHITexture2D* SrcTexture, FVector2D WindowSize) const
	{
		if (SpectatorScreenController)
		{
			SpectatorScreenController->RenderSpectatorScreen_RenderThread(RHICmdList, BackBuffer, SrcTexture, WindowSize);
		}

#if PLATFORM_HOLOLENS
		if (bIsMobileMultiViewEnabled && !HMD->IsThirdCameraActive())
		{
			FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
			IPooledRenderTarget* depthRenderTarget = SceneContext.SceneDepthZ.GetReference();
			if (depthRenderTarget != nullptr)
			{
				FTextureRHIRef depthTargetableTexture = depthRenderTarget->GetRenderTargetItem().TargetableTexture;
				if (depthTargetableTexture != nullptr)
				{
					ID3D11Texture2D* Texture = static_cast<ID3D11Texture2D*>(depthTargetableTexture->GetNativeResource());
					if (Texture != nullptr)
					{
						if (mCustomPresent != nullptr)
						{
							mCustomPresent->SetDepthTexture(Texture);
						}
					}
				}
			}
		}
		else
#endif
		{
			// We keep refs to the depth texture in the hmd, so this function is non-const.
			// RenderTexture_RenderThread should perhaps be refactored or made non-const.
			// But to get this into a hotfix of 4.23 we shall simply cast.
			FWindowsMixedRealityHMD* nonconstthis = const_cast<FWindowsMixedRealityHMD*>(this);
			nonconstthis->CreateHMDDepthTexture(RHICmdList);
		}
	}

	void FWindowsMixedRealityHMD::PreRenderView_RenderThread(
		FRHICommandListImmediate& RHICmdList,
		FSceneView& InView)
	{
		if (bIsMobileMultiViewEnabled)
		{
			InView.bIsInstancedStereoEnabled = !HMD->IsThirdCameraActive();
			InView.bIsMobileMultiViewEnabled = !HMD->IsThirdCameraActive();
		}
	}
	
	// Create a BGRA backbuffer for rendering.
	bool FWindowsMixedRealityHMD::AllocateRenderTargetTexture(
		uint32 index,
		uint32 sizeX,
		uint32 sizeY,
		uint8 format,
		uint32 numMips,
		ETextureCreateFlags flags,
		ETextureCreateFlags targetableTextureFlags,
		FTexture2DRHIRef& outTargetableTexture,
		FTexture2DRHIRef& outShaderResourceTexture,
		uint32 numSamples)
	{
		if (!IsStereoEnabled())
		{
			return false;
		}

		// Since our textures must be BGRA, this plugin did require a change to WindowsD3D11Device.cpp
		// to add the D3D11_CREATE_DEVICE_BGRA_SUPPORT flag to the graphics device.
		FRHIResourceCreateInfo CreateInfo;

#if PLATFORM_HOLOLENS
		if (bIsMobileMultiViewEnabled && !HMD->IsThirdCameraActive())
		{
			FTexture2DArrayRHIRef texture, resource;
			RHICreateTargetableShaderResource2DArray(
				sizeX,
				sizeY,
				2,
				PF_B8G8R8A8, // must be BGRA
				numMips,
				flags,
				targetableTextureFlags,
				CreateInfo,
				texture,
				resource);
			outTargetableTexture = texture;
			outShaderResourceTexture = resource;

			CurrentBackBuffer = outTargetableTexture;
		}
		else
#endif
		{
			RHICreateTargetableShaderResource2D(
				sizeX,
				sizeY,
				PF_B8G8R8A8, // must be BGRA
				numMips,
				flags,
				targetableTextureFlags,
				false,
				CreateInfo,
				outTargetableTexture,
				outShaderResourceTexture);
		}

		bNeedReallocateDepthTexture = true;

		return true;
	}

	bool FWindowsMixedRealityHMD::NeedReAllocateDepthTexture(const TRefCountPtr<IPooledRenderTarget>& DepthTarget)
	{
		if (!IsStereoEnabled())
		{
			return false;
		}

		return bNeedReallocateDepthTexture;
	}

	bool FWindowsMixedRealityHMD::AllocateDepthTexture(uint32 Index, uint32 SizeX, uint32 SizeY, uint8 Format, uint32 NumMips, ETextureCreateFlags InTexFlags, ETextureCreateFlags TargetableTextureFlags, FTexture2DRHIRef & OutTargetableTexture, FTexture2DRHIRef & OutShaderResourceTexture, uint32 NumSamples)
	{
		// Depth textures are being allocated every frame, only explicitly reallocate if the rendertarget texture changes or the depth buffer is null.
		if (CurrentDepthBuffer != nullptr && 
			CurrentDepthBuffer.GetReference()->IsValid() &&
			!bNeedReallocateDepthTexture)
		{
			OutTargetableTexture = CurrentDepthBuffer;
			OutShaderResourceTexture = CurrentDepthBuffer;
			return true;
		}
		
		FRHIResourceCreateInfo CreateInfo;
		// This binding is necessary - without it there will be a runtime error.
		// Current shader assumes far depth since scene depth uses far depth.
		CreateInfo.ClearValueBinding = FClearValueBinding::DepthFar;

#if PLATFORM_HOLOLENS
		if (bIsMobileMultiViewEnabled && !HMD->IsThirdCameraActive())
		{
			FIntPoint size = GetIdealRenderTargetSize();

			FTexture2DArrayRHIRef texture, resource;
			RHICreateTargetableShaderResource2DArray(
				size.X,
				size.Y,
				2,
				// Do not use input format - this will resolve to X32_TYPELESS_G8X24_UINT which cannot be used for a depthstencil buffer.
				// DepthStencil will resolve to R32G8X24_TYPELESS which is usable for a depthstencil buffer!
				PF_DepthStencil,
				// Do not use input mips, this will resolve to 0 which will throw creating the texture.
				1,
				InTexFlags,
				TargetableTextureFlags,
				CreateInfo,
				texture,
				resource);
			OutTargetableTexture = texture;
			OutShaderResourceTexture = resource;
		}
		else
#endif
		{
			RHICreateTargetableShaderResource2D(
				SizeX,
				SizeY,
				// Do not use input format - this will resolve to X32_TYPELESS_G8X24_UINT which cannot be used for a depthstencil buffer.
				// DepthStencil will resolve to R32G8X24_TYPELESS which is usable for a depthstencil buffer!
				PF_DepthStencil,
				// Do not use input mips, this will resolve to 0 which will throw creating the texture.
				1,
				InTexFlags,
				TargetableTextureFlags,
				false,
				CreateInfo,
				OutTargetableTexture,
				OutShaderResourceTexture);
		}

		CurrentDepthBuffer = OutTargetableTexture;
		bNeedReallocateDepthTexture = false;

		return true;
	}

	bool FWindowsMixedRealityHMD::HasHiddenAreaMesh() const
	{
		return HiddenAreaMesh[0].IsValid() && HiddenAreaMesh[1].IsValid();
	}

	void FWindowsMixedRealityHMD::DrawHiddenAreaMesh_RenderThread(FRHICommandList& RHICmdList, EStereoscopicPass StereoPass) const
	{
		if (StereoPass == eSSP_FULL || StereoPass == eSSP_THIRD_CAMERA_EYE)
		{
			return;
		}

		int index = (StereoPass == eSSP_LEFT_EYE) ? 0 : 1;
		const FHMDViewMesh& Mesh = HiddenAreaMesh[index];
		check(Mesh.IsValid());

		RHICmdList.SetStreamSource(0, Mesh.VertexBufferRHI, 0);
		RHICmdList.DrawIndexedPrimitive(Mesh.IndexBufferRHI, 0, 0, Mesh.NumVertices, 0, Mesh.NumTriangles, 1);
	}

	bool FWindowsMixedRealityHMD::HasVisibleAreaMesh() const
	{
		//re-enable this when we're not running on the simulator once we can query for platform type
		return false;
		//return VisibleAreaMesh[0].IsValid() && VisibleAreaMesh[1].IsValid();
	}

	void FWindowsMixedRealityHMD::DrawVisibleAreaMesh_RenderThread(FRHICommandList& RHICmdList, EStereoscopicPass StereoPass) const
	{
		if (StereoPass == eSSP_FULL || StereoPass == eSSP_THIRD_CAMERA_EYE)
		{
			return;
		}

		int index = (StereoPass == eSSP_LEFT_EYE) ? 0 : 1;
		const FHMDViewMesh& Mesh = VisibleAreaMesh[index];
		check(Mesh.IsValid());

		RHICmdList.SetStreamSource(0, Mesh.VertexBufferRHI, 0);
		RHICmdList.DrawIndexedPrimitive(Mesh.IndexBufferRHI, 0, 0, Mesh.NumVertices, 0, Mesh.NumTriangles, 1);
	}

	void FWindowsMixedRealityHMD::SetupViewFamily(FSceneViewFamily& InViewFamily)
	{
		InViewFamily.EngineShowFlags.MotionBlur = 0;
		InViewFamily.EngineShowFlags.HMDDistortion = false;
		InViewFamily.EngineShowFlags.SetScreenPercentage(false);
		InViewFamily.EngineShowFlags.StereoRendering = IsStereoEnabled();
	}

	// Copy a double-wide src texture into a single-wide dst texture with 2 subresources.
	void _StereoCopy(
		ID3D11DeviceContext* D3D11Context,
		const float viewportScale,
		ID3D11Texture2D* src,
		ID3D11Texture2D* dst)
	{
		D3D11_TEXTURE2D_DESC desc{};
		dst->GetDesc(&desc);

		const uint32_t scaledWidth = (uint32_t)(desc.Width * viewportScale);
		const uint32_t scaledHeight = (uint32_t)(desc.Height * viewportScale);

		D3D11_BOX box = {};
		box.right = scaledWidth;
		box.bottom = scaledHeight;
		box.back = 1;
		for (int i = 0; i < 2; ++i) { // Copy each eye to HMD backbuffer
			const uint32_t offsetX = (desc.Width - scaledWidth) / 2;
			const uint32_t offsetY = (desc.Height - scaledHeight) / 2;
			D3D11Context->CopySubresourceRegion(dst, i, offsetX, offsetY, 0, src, 0, &box);
			box.left += scaledWidth;
			box.right += scaledWidth;
		}
	}

	void FWindowsMixedRealityHMD::CreateHMDDepthTexture(FRHICommandListImmediate& RHICmdList)
	{
		check(IsInRenderingThread());

#if WITH_WINDOWS_MIXED_REALITY
		if (CurrentDepthBuffer == nullptr
			|| !CurrentDepthBuffer.GetReference()->IsValid())
		{
			return;
		}

		// Update depth texture to match format Windows Mixed Reality platform is expecting.
		FRHITexture2D* depthFRHITexture = CurrentDepthBuffer.GetReference()->GetTexture2D();

		if (depthFRHITexture == nullptr || depthFRHITexture->GetNativeResource() == nullptr)
		{
			return;
		}

		const uint32 viewportWidth = depthFRHITexture->GetSizeX();
		const uint32 viewportHeight = depthFRHITexture->GetSizeY();

		bool recreateTextures = false;
		if (remappedDepthTexture != nullptr)
		{
			int width = remappedDepthTexture->GetSizeX();
			int height = remappedDepthTexture->GetSizeY();

			if (width != viewportWidth || height != viewportHeight)
			{
				recreateTextures = true;
			}
		}

		// Create a new texture for the remapped depth.
		if (remappedDepthTexture == nullptr || recreateTextures)
		{
			FRHIResourceCreateInfo CreateInfo;
			remappedDepthTexture = RHICreateTexture2D(depthFRHITexture->GetSizeX(), depthFRHITexture->GetSizeY(),
				PF_R32_FLOAT, 1, 1, ETextureCreateFlags::TexCreate_RenderTargetable, CreateInfo);
		}

		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		FRHIRenderPassInfo RPInfo(remappedDepthTexture, ERenderTargetActions::Load_Store);
		RHICmdList.BeginRenderPass(RPInfo, TEXT("RemapDepth"));
		{
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

			RHICmdList.SetViewport(0, 0, 0, viewportWidth, viewportHeight, 1.0f);

			GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

			const auto featureLevel = GMaxRHIFeatureLevel;
			auto shaderMap = GetGlobalShaderMap(featureLevel);

			TShaderMapRef<FScreenVS> vertexShader(shaderMap);
			TShaderMapRef<FDepthConversionPS> pixelShader(shaderMap);

			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = vertexShader.GetVertexShader();
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = pixelShader.GetPixelShader();
			GraphicsPSOInit.PrimitiveType = PT_TriangleList;

			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

			pixelShader->SetParameters(RHICmdList, depthFRHITexture);

			RendererModule->DrawRectangle(
				RHICmdList,
				0, 0, // X, Y
				viewportWidth, viewportHeight, // SizeX, SizeY
				0.0f, 0.0f, // U, V
				1.0f, 1.0f, // SizeU, SizeV
				FIntPoint(viewportWidth, viewportHeight), // TargetSize
				FIntPoint(1, 1), // TextureSize
				vertexShader,
				EDRF_Default);
		}
		RHICmdList.EndRenderPass();

		ID3D11Device* device = (ID3D11Device*)RHIGetNativeDevice();
		if (device == nullptr)
		{
			return;
		}

		// Create a new depth texture with 2 subresources for depth based reprojection.
		// Directly create an ID3D11Texture2D instead of an FTexture2D because we need an ArraySize of 2.
		if (stereoDepthTexture == nullptr || recreateTextures)
		{
			// Create stereo depth texture
			D3D11_TEXTURE2D_DESC tdesc;

			tdesc.Width = wmrRenderTargets[0].width;
			tdesc.Height = wmrRenderTargets[0].height;
			tdesc.MipLevels = 1;
			tdesc.ArraySize = 2;
			tdesc.SampleDesc.Count = 1;
			tdesc.SampleDesc.Quality = 0;
			tdesc.Usage = D3D11_USAGE_DEFAULT;
			tdesc.Format = DXGI_FORMAT_R32_FLOAT;
			tdesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
			tdesc.CPUAccessFlags = 0;
			tdesc.MiscFlags = 0;

			device->CreateTexture2D(&tdesc, NULL, &stereoDepthTexture);
		}

		if (HMD->IsThirdCameraActive() && (monoDepthTexture == nullptr || recreateTextures))
		{
			// Create third camera depth texture
			D3D11_TEXTURE2D_DESC tdesc;

			tdesc.Width = wmrRenderTargets[2].width;
			tdesc.Height = wmrRenderTargets[2].height;
			tdesc.MipLevels = 1;
			tdesc.ArraySize = 1;
			tdesc.SampleDesc.Count = 1;
			tdesc.SampleDesc.Quality = 0;
			tdesc.Usage = D3D11_USAGE_DEFAULT;
			tdesc.Format = DXGI_FORMAT_R32_FLOAT;
			tdesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
			tdesc.CPUAccessFlags = 0;
			tdesc.MiscFlags = 0;
			device->CreateTexture2D(&tdesc, NULL, &monoDepthTexture);
		}

		ID3D11DeviceContext* context;
		device->GetImmediateContext(&context);

		_StereoCopy(context, ScreenScalePercentage,
			(ID3D11Texture2D*)remappedDepthTexture->GetNativeResource(),
			stereoDepthTexture);

		if (mCustomPresent != nullptr)
		{
			mCustomPresent->SetDepthTexture(stereoDepthTexture);
		}
		// Third camera depth
		if (HMD->IsThirdCameraActive() && remappedDepthTexture != nullptr && monoDepthTexture != nullptr)
		{
			ID3D11Texture2D* sourceDepth = (ID3D11Texture2D*)remappedDepthTexture->GetNativeResource();
			int w, h;
			HMD->GetThirdCameraDimensions(w, h);

			D3D11_TEXTURE2D_DESC desc{};
			sourceDepth->GetDesc(&desc);

			if (w > 0 && h > 0
				&& desc.Width > (unsigned int)w
				&& desc.Height >= (unsigned int)h)
			{
				D3D11_BOX box = {};
				box.right = desc.Width;
				box.left = desc.Width - w;
				box.top = 0;
				box.bottom = h;
				box.back = 1;

				context->CopySubresourceRegion(
					monoDepthTexture,
					0, 0, 0, 0,
					sourceDepth,
					0,
					&box);

				HMD->CommitThirdCameraDepthBuffer(monoDepthTexture);
			}
		}
#endif
	}

	void FWindowsMixedRealityHMD::SetFocusPointForFrame(FVector Position)
	{
#if WITH_WINDOWS_MIXED_REALITY
		check(IsInGameThread());

		if (!HMD->IsInitialized() || !HMD->IsAvailable())
		{
			return;
		}

		// Convert Unreal world position to HoloLens tracking space.
		FVector TransformedPosition = CachedTrackingToWorld.Inverse().TransformPosition(Position) / GetWorldToMetersScale();

		// Enqueue command to send to render thread
		FWindowsMixedRealityHMD* WMRHMD = this;
		ENQUEUE_RENDER_COMMAND(SetFocustPointForFrame)(
			[WMRHMD, TransformedPosition](FRHICommandListImmediate& RHICmdList)
		{
			WMRHMD->SetFocustPointForFrame_RenderThread(TransformedPosition);
		}
		);
#endif
	}

	void FWindowsMixedRealityHMD::SetFocustPointForFrame_RenderThread(FVector TrackingSpacePosition)
	{
#if WITH_WINDOWS_MIXED_REALITY
		check(IsInRenderingThread());

		if (!HMD->IsInitialized() || !HMD->IsAvailable())
		{
			return;
		}

		HMD->SetFocusPointForFrame(WMRUtility::ToMixedRealityVector(TrackingSpacePosition));
#endif
	}

	bool FWindowsMixedRealityHMD::IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const
	{
		return GEngine && GEngine->IsStereoscopic3D(Context.Viewport);
	}

#if WITH_WINDOWS_MIXED_REALITY
	FWindowsMixedRealityHMD::FWindowsMixedRealityHMD(const FAutoRegister& AutoRegister, IARSystemSupport* InARSystem, MixedRealityInterop* InHMD)
		: FHeadMountedDisplayBase(InARSystem)
		, FSceneViewExtensionBase(AutoRegister)
		, HMD(InHMD)
		, ScreenScalePercentage(1.0f)
		, mCustomPresent(nullptr)
	{
		static const FName RendererModuleName("Renderer");
		RendererModule = FModuleManager::GetModulePtr<IRendererModule>(RendererModuleName);
		HandlerId = HMD->SubscribeConnectionEvent(std::bind(&FWindowsMixedRealityHMD::OnConnectionEvent, this, std::placeholders::_1));

#if PLATFORM_HOLOLENS
		static const auto CVarMobileMultiView = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("vr.MobileMultiView"));
		static const auto CVarMobileHDR = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.MobileHDR"));
		const bool bMobileHDR = (CVarMobileHDR && CVarMobileHDR->GetValueOnAnyThread() != 0);
		bIsMobileMultiViewEnabled = (GRHISupportsArrayIndexFromAnyShader && !bMobileHDR) && (CVarMobileMultiView && CVarMobileMultiView->GetValueOnAnyThread() != 0);
#endif

		HiddenAreaMesh.SetNum(2);
		VisibleAreaMesh.SetNum(2);

#if !PLATFORM_HOLOLENS
		CreateSpectatorScreenController();
#endif
	}
#endif

	FWindowsMixedRealityHMD::~FWindowsMixedRealityHMD()
	{
#if WITH_WINDOWS_MIXED_REALITY
		if (HMD && HMD->IsRemoting())
		{
			DisconnectFromRemoteHoloLens();
		}
#endif

		ShutdownHolographic();
	}

	bool FWindowsMixedRealityHMD::IsInitialized() const
	{
		// Return true here because hmd needs an hwnd to initialize itself, but VR preview will not create a window if this is not true.
		return true;
	}

	// Cleanup resources needed for Windows Holographic view and tracking space.
	void FWindowsMixedRealityHMD::ShutdownHolographic()
	{
		if (!IsInGameThread())
		{
			this->bRequestShutdown = true;
			return;
		}

#if WITH_WINDOWS_MIXED_REALITY
		HMD->EnableStereo(false);
#endif
		// Ensure that we aren't currently trying to render a frame before destroying our custom present.
		FlushRenderingCommands();
		StopCustomPresent();

		if (PauseHandle.IsValid())
		{
			FCoreDelegates::ApplicationWillEnterBackgroundDelegate.Remove(PauseHandle);
			PauseHandle.Reset();
		}
		if (ResumeHandle.IsValid())
		{
			FCoreDelegates::ApplicationHasEnteredForegroundDelegate.Remove(ResumeHandle);
			ResumeHandle.Reset();
		}

		bIsStereoDesired = false;
		bIsStereoEnabled = false;

#if WITH_WINDOWS_MIXED_REALITY
		// Reset the viewport to no longer match the HMD display
		FSceneViewport* SceneVP = FindMRSceneViewport(bIsStereoDesired);

		if (SceneVP)
		{
			TSharedPtr<SWindow> Window = SceneVP->FindWindow();
			if (Window.IsValid())
			{
#if !PLATFORM_HOLOLENS
				// Set MirrorWindow state on the Window
				Window->SetMirrorWindow(bIsStereoDesired);
				Window->SetViewportSizeDrivenByWindow(true);
#endif
			}
		}
#endif

		bNeedReallocateDepthTexture = false;

		GetFrame().bPositionalTrackingUsed = false;

		for (int i = 0; i < 2; i++)
		{
			HiddenAreaMesh[i].NumVertices = 0;
			HiddenAreaMesh[i].NumIndices = 0;
			HiddenAreaMesh[i].NumTriangles = 0;

			HiddenAreaMesh[i].IndexBufferRHI = nullptr;
			HiddenAreaMesh[i].VertexBufferRHI = nullptr;

			VisibleAreaMesh[i].NumVertices = 0;
			VisibleAreaMesh[i].NumIndices = 0;
			VisibleAreaMesh[i].NumTriangles = 0;

			VisibleAreaMesh[i].IndexBufferRHI = nullptr;
			VisibleAreaMesh[i].VertexBufferRHI = nullptr;
		}

#if WITH_EDITOR
#if WITH_WINDOWS_MIXED_REALITY
		HMD->UnsubscribeConnectionEvent(HandlerId);
		HandlerId = 0;
		prevState = HMDRemotingConnectionState::Undefined;
#endif
#endif
	}

	bool FWindowsMixedRealityHMD::IsCurrentlyImmersive()
	{
#if WITH_WINDOWS_MIXED_REALITY
		return HMD->IsCurrentlyImmersive();
#else
		return false;
#endif
	}

	bool FWindowsMixedRealityHMD::IsDisplayOpaque()
	{
#if WITH_WINDOWS_MIXED_REALITY
		if (HMD->IsActiveAndValid())
		{
			return HMD->IsDisplayOpaque();
		}
		return false;
#else
		return true;
#endif
	}

	// Setup Windows Holographic view and tracking space.
	void FWindowsMixedRealityHMD::InitializeHolographic()
	{
#if WITH_WINDOWS_MIXED_REALITY
		if (!HMD->IsInitialized())
		{
			D3D11Device = InternalGetD3D11Device();
			if (D3D11Device != nullptr)
			{
				SetupHolographicCamera();
			}
		}

		static const auto screenPercentVar = IConsoleManager::Get().FindTConsoleVariableDataFloat(TEXT("vr.PixelDensity"));
		SetPixelDensity(screenPercentVar->GetValueOnGameThread());

		StartCustomPresent();

		// Hook into suspend/resume events
		if (!PauseHandle.IsValid())
		{
			PauseHandle = FCoreDelegates::ApplicationWillEnterBackgroundDelegate.AddRaw(this, &FWindowsMixedRealityHMD::AppServicePause);
		}

		if (!ResumeHandle.IsValid())
		{
			ResumeHandle = FCoreDelegates::ApplicationHasEnteredForegroundDelegate.AddRaw(this, &FWindowsMixedRealityHMD::AppServiceResume);
		}
#endif
	}

	// Prevent crashes if computer goes to sleep.
	void FWindowsMixedRealityHMD::AppServicePause()
	{
		// We support sleep on hololens.
#if !PLATFORM_HOLOLENS
		this->bRequestRestart = true;
#endif
	}

	void FWindowsMixedRealityHMD::AppServiceResume()
	{
		StartSpeechRecognition();
	}

	void CallSpeechCallback(FKey InKey, FName InActionName)
	{
		AsyncTask(ENamedThreads::GameThread, [InKey, InActionName]()
		{
			// Have to find the game world, not the editor world, if we are in vr preview
			UWorld* World = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE)
				{
					World = Context.World();
				}
			}

			if (World && World->GetGameInstance())
			{
				APlayerController* PlayerController = World->GetGameInstance()->GetFirstLocalPlayerController();
				if (PlayerController)
				{
					PlayerController->InputKey(InKey, IE_Pressed, 1.0f, false);
				}
			}
		});
	}

	void FWindowsMixedRealityHMD::StartSpeechRecognition()
	{
#if WITH_WINDOWS_MIXED_REALITY && SUPPORTS_WINDOWS_MIXED_REALITY_SPEECH_RECOGNITION
		StopSpeechRecognition();

		//get all speech keywords
		const TArray <FInputActionSpeechMapping>& SpeechMappings = GetDefault<UInputSettings>()->GetSpeechMappings();

		if (SpeechMappings.Num() > 0)
		{
			//this is destroyed when the Interp layer is removed.
			WindowsMixedReality::SpeechRecognizerInterop* speechInterop = CreateSpeechRecognizer();

			for (const FInputActionSpeechMapping& SpeechMapping : SpeechMappings)
			{
				FName ActionName = SpeechMapping.GetActionName();
				FKey Key(SpeechMapping.GetKeyName());
				EKeys::AddKey(FKeyDetails(Key, FText(), FKeyDetails::NotBlueprintBindableKey, FInputActionSpeechMapping::GetKeyCategory()));

				// Bind Unreal delegate to function pointer we can pass to the interop lib.
				std::function<void()> callSpeechDelegate = std::bind(CallSpeechCallback, Key, ActionName);

				FName Keyword = SpeechMapping.GetSpeechKeyword();
				speechInterop->AddKeyword(*Keyword.ToString(), callSpeechDelegate);
			}

			speechInterop->StartSpeechRecognition();
		} //-V773
#endif
	}

	void FWindowsMixedRealityHMD::StopSpeechRecognition()
	{
#if WITH_WINDOWS_MIXED_REALITY && SUPPORTS_WINDOWS_MIXED_REALITY_SPEECH_RECOGNITION
		//remove keys from "speech" namespace
		EKeys::RemoveKeysWithCategory(FInputActionSpeechMapping::GetKeyCategory());
#endif
	}

	bool WindowsMixedReality::FWindowsMixedRealityHMD::IsAvailable()
	{
#if WITH_WINDOWS_MIXED_REALITY
		return HMD->IsAvailable();
#else
		return false;
#endif
	}

	// Initialize Windows Holographic present.
	void FWindowsMixedRealityHMD::StartCustomPresent()
	{
#if WITH_WINDOWS_MIXED_REALITY
		if (mCustomPresent == nullptr)
		{
			mCustomPresent = new FWindowsMixedRealityCustomPresent(HMD, D3D11Device, bIsMobileMultiViewEnabled);
		}
#endif
	}

	// Cleanup resources for holographic present.
	void FWindowsMixedRealityHMD::StopCustomPresent()
	{
		mCustomPresent = nullptr;
	}

	// Spatial Input
	bool FWindowsMixedRealityHMD::SupportsSpatialInput()
	{
#if WITH_WINDOWS_MIXED_REALITY
		return HMD->SupportsSpatialInput();
#else
		return false;
#endif
	}

#if WITH_WINDOWS_MIXED_REALITY

	bool FWindowsMixedRealityHMD::SupportsHandTracking()
	{
		return HMD->SupportsHandTracking();
	}

	bool FWindowsMixedRealityHMD::SupportsHandedness()
	{
		return HMD->SupportsHandedness();
	}

	HMDTrackingStatus FWindowsMixedRealityHMD::GetControllerTrackingStatus(HMDHand hand)
	{
#if WITH_INPUT_SIMULATION
		if (auto* InputSim = UWindowsMixedRealityInputSimulationEngineSubsystem::GetInputSimulationIfEnabled())
		{
			return (HMDTrackingStatus)InputSim->GetControllerTrackingStatus((EControllerHand)hand);
		}
		else
#endif
		{
			return HMD->GetControllerTrackingStatus(hand);
		}
	}

	// GetControllerOrientationAndPosition() is pre-scaled to UE4 world scale by default
	bool FWindowsMixedRealityHMD::GetControllerOrientationAndPosition(HMDHand hand, FRotator & OutOrientation, FVector & OutPosition, float WorldScale)
	{
		if (!bIsStereoEnabled)
		{
			return false;
		}

		DirectX::XMFLOAT4 rot;
		DirectX::XMFLOAT3 pos;
		if (HMD->GetControllerOrientationAndPosition(hand, rot, pos))
		{
			FTransform TrackingSpaceTransform(WMRUtility::FromMixedRealityQuaternion(rot), WMRUtility::FromMixedRealityVector(pos) * WorldScale);

			OutOrientation = FRotator(TrackingSpaceTransform.GetRotation());
			OutPosition = TrackingSpaceTransform.GetLocation();

			// If the device does not support full hand tracking, set the orientation manually
			if (!FWindowsMixedRealityStatics::SupportsHandedness())
			{
				OutOrientation = FRotator(GetFrame().HeadOrientation);
				OutOrientation.Roll = 0;
				OutOrientation.Pitch = 0;
			}

			return true;
		}

		return false;
	}

	// GetHandJointOrientationAndPosition() is NOT scaled to UE4 world scale, so we must do it here
	bool FWindowsMixedRealityHMD::GetHandJointOrientationAndPosition(HMDHand hand, HMDHandJoint joint, FRotator& OutOrientation, FVector& OutPosition, float& OutRadius)
	{
		if (!bIsStereoEnabled)
		{
			return false;
		}

		DirectX::XMFLOAT4 rot;
		DirectX::XMFLOAT3 pos;
		float radius;
		if (HMD->GetHandJointOrientationAndPosition(hand, joint, rot, pos, radius))
		{
			FTransform TrackingSpaceTransform(WMRUtility::FromMixedRealityQuaternion(rot), WMRUtility::FromMixedRealityVector(pos) * GetWorldToMetersScale());

			OutOrientation = FRotator(TrackingSpaceTransform.GetRotation());
			OutPosition = TrackingSpaceTransform.GetLocation();
			OutRadius = radius * GetWorldToMetersScale();

			return true;
		}
		return false;
	}


	bool FWindowsMixedRealityHMD::PollInput()
	{
		if (!bIsStereoEnabled)
		{
			return false;
		}

		HMD->PollInput();
		return true;
	}

	bool FWindowsMixedRealityHMD::PollHandTracking()
	{
		if (!bIsStereoEnabled)
		{
			return false;
		}

		HMD->PollHandTracking();
		return true;
	}

	HMDInputPressState WindowsMixedReality::FWindowsMixedRealityHMD::GetPressState(HMDHand hand, HMDInputControllerButtons button, bool onlyRegisterClicks)
	{
#if WITH_INPUT_SIMULATION
		if (auto* InputSim = UWindowsMixedRealityInputSimulationEngineSubsystem::GetInputSimulationIfEnabled())
		{
			bool IsPressed;
			if (InputSim->GetPressState((EControllerHand)hand, (EHMDInputControllerButtons)button, onlyRegisterClicks, IsPressed))
			{
				return IsPressed ? HMDInputPressState::Pressed : HMDInputPressState::Released;
			}
			return HMDInputPressState::NotApplicable;
		}
		else
#endif
		{
			return HMD->GetPressState(hand, button, onlyRegisterClicks);
		}
	}

	float FWindowsMixedRealityHMD::GetAxisPosition(HMDHand hand, HMDInputControllerAxes axis)
	{
		return HMD->GetAxisPosition(hand, axis);
	}

	void FWindowsMixedRealityHMD::SubmitHapticValue(HMDHand hand, float value)
	{
		HMD->SubmitHapticValue(hand, FMath::Clamp(value, 0.f, 1.f));
	}

	bool FWindowsMixedRealityHMD::QueryCoordinateSystem(ABI::Windows::Perception::Spatial::ISpatialCoordinateSystem *& pCoordinateSystem)
	{
		return HMD->QueryCoordinateSystem(pCoordinateSystem);
	}

	bool FWindowsMixedRealityHMD::IsTrackingAvailable()
	{
		return HMD->IsTrackingAvailable();
	}

	bool FWindowsMixedRealityHMD::IsTracking(int32 DeviceId)
	{
		if (DeviceId == IXRTrackingSystem::HMDDeviceId)
		{
			return IsTrackingAvailable();
		}
		return FXRTrackingSystemBase::IsTracking(DeviceId);
	}


	void FWindowsMixedRealityHMD::GetPointerPose(EControllerHand hand, PointerPoseInfo& pi)
	{
#if WITH_INPUT_SIMULATION
		if (auto* InputSim = UWindowsMixedRealityInputSimulationEngineSubsystem::GetInputSimulationIfEnabled())
		{
			FWindowsMixedRealityInputSimulationPointerPose InputSimPointerPose;
			if (InputSim->GetHandPointerPose(hand, InputSimPointerPose))
			{
				FVector pos = InputSimPointerPose.Origin;
				FVector dir = InputSimPointerPose.Direction;
				FVector up = InputSimPointerPose.Up;
				FQuat rot = InputSimPointerPose.Orientation;
				pi.origin = DirectX::XMFLOAT3(pos.X, pos.Y, pos.Z);
				pi.orientation = DirectX::XMFLOAT4(rot.X, rot.Y, rot.Z, rot.W);
				pi.up = DirectX::XMFLOAT3(up.X, up.Y, up.Z);
				pi.direction = DirectX::XMFLOAT3(dir.X, dir.Y, dir.Z);
			}
		}
		else
#endif
		{
			HMD->GetPointerPose((HMDHand)hand, pi);

			FTransform TrackingSpaceTransformOrigin(WMRUtility::FromMixedRealityQuaternion(pi.orientation), WMRUtility::FromMixedRealityVector(pi.origin) * GetWorldToMetersScale());

			FTransform tOrigin = (TrackingSpaceTransformOrigin * CachedTrackingToWorld);
			FVector pos = tOrigin.GetLocation();
			FQuat rot = tOrigin.GetRotation();

			FVector up = CachedTrackingToWorld.GetRotation() * WMRUtility::FromMixedRealityVector(pi.up);
			FVector dir = CachedTrackingToWorld.GetRotation() * WMRUtility::FromMixedRealityVector(pi.direction);

			pi.origin = DirectX::XMFLOAT3(pos.X, pos.Y, pos.Z);
			pi.orientation = DirectX::XMFLOAT4(rot.X, rot.Y, rot.Z, rot.W);
			pi.up = DirectX::XMFLOAT3(up.X, up.Y, up.Z);
			pi.direction = DirectX::XMFLOAT3(dir.X, dir.Y, dir.Z);
		}
	}

#endif

	namespace WindowsMixedRealityHMD
	{
		void LogForInterop(const wchar_t* text)
		{
			UE_LOG(LogWmrHmd, Log, TEXT("WMRInterop: %s"), text);
		}
	}

	EXRDeviceConnectionResult::Type FWindowsMixedRealityHMD::ConnectRemoteXRDevice(const FString& IpAddress, const int32 BitRate)
	{
		//call back out into the function used by the editor for IP parsing
		UE_LOG(LogWmrHmd, Log, TEXT("Connecting to remote HoloLens: %s"), *IpAddress);

		return WindowsMixedReality::FWindowsMixedRealityStatics::ConnectToRemoteHoloLens(IpAddress, BitRate, false);
	}
	
	void FWindowsMixedRealityHMD::DisconnectRemoteXRDevice()
	{
		UE_LOG(LogWmrHmd, Log, TEXT("Disconnecting from remote HoloLens"));
		WindowsMixedReality::FWindowsMixedRealityStatics::DisconnectFromRemoteHoloLens();
	}


	// Remoting
	EXRDeviceConnectionResult::Type FWindowsMixedRealityHMD::ConnectToRemoteHoloLens(const wchar_t* ip, unsigned int bitrate, bool isHoloLens1, unsigned int Port, bool listen)
	{
		EXRDeviceConnectionResult::Type Result = EXRDeviceConnectionResult::Success;

		D3D11Device = InternalGetD3D11Device();

#  if WITH_WINDOWS_MIXED_REALITY
		RemotingDesc.ip = ip;
		RemotingDesc.bitrate = bitrate;
		RemotingDesc.isHoloLens1 = isHoloLens1;
		RemotingDesc.port = Port;
		RemotingDesc.listen = listen;
		isRemotingReconnecting = !WITH_EDITOR && listen != 0 && !isHoloLens1;
		HMD->SetLogCallback(WindowsMixedRealityHMD::LogForInterop);
		HMD->ConnectToRemoteHoloLens(D3D11Device.GetReference(), ip, bitrate, isHoloLens1, Port, listen);
#  else
		UE_LOG(LogWmrHmd, Log, TEXT("FWindowsMixedRealityHMD::ConnectToRemoteHoloLens() is doing nothing because !WITH_WINDOWS_MIXED_REALITY."));
#  endif

		TWeakPtr<SViewport> ActiveViewport;
#if WITH_EDITOR
		UEditorEngine* EditorEngine = Cast<UEditorEngine>(GEngine);
		if (EditorEngine)
		{
			FSceneViewport* PIEViewport = (FSceneViewport*)EditorEngine->GetPIEViewport();
			if (PIEViewport)
			{
				TWeakPtr<SViewport> PIEViewportWidget = PIEViewport->GetViewportWidget();
				if (PIEViewportWidget.IsValid() && PIEViewportWidget.Pin()->IsStereoRenderingAllowed())
				{
					ActiveViewport = PIEViewport->GetViewportWidget();
				}
			}
		}
		else
#endif
		{
			ActiveViewport = GEngine->GetGameViewportWidget();
		}

		if (ActiveViewport.IsValid())
		{
			ActiveViewport.Pin()->EnableStereoRendering(true);
			EnableStereo(true);
		}
		else
		{
			Result = EXRDeviceConnectionResult::NoValidViewport;
		}

		return Result;
	}

	void FWindowsMixedRealityHMD::DisconnectFromRemoteHoloLens()
	{
#if WITH_WINDOWS_MIXED_REALITY
		isRemotingReconnecting = false;
		HMD->DisconnectFromDevice();
		HMD->SetLogCallback(nullptr);

		EnableStereo(false);
#endif
	}

	void FWindowsMixedRealityHMD::CreateSpectatorScreenController()
	{
		SpectatorScreenController = MakeUnique<FDefaultSpectatorScreenController>(this);
	}
}

IMPLEMENT_MODULE(WindowsMixedReality::FWindowsMixedRealityHMDPlugin, WindowsMixedRealityHMD)

DEFINE_LOG_CATEGORY(LogWmrHmd);
