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
#include "IWindowsMixedRealityHandTrackingPlugin.h"
#include "GameFramework/InputSettings.h"
#include "GameFramework/PlayerController.h"
#include "Async/Async.h"

#if WITH_EDITOR
#include "Editor/UnrealEd/Classes/Editor/EditorEngine.h"
#endif

#include "Engine/GameEngine.h"
#include "HAL/PlatformMisc.h"
#include "Misc/MessageDialog.h"

// Holographic Remoting is only supported in Windows 10 version 1809 or better
// Originally we were supporting 1803, but there were rendering issues specific to that version so for now we only support 1809
#define MIN_WIN_10_VERSION_FOR_WMR 1809

#include "WindowsMixedRealityAvailability.h"

#if SUPPORTS_WINDOWS_MIXED_REALITY_AR
	#include "HoloLensModule.h"
#endif

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

		NearPlaneM.Bind(Initializer.ParameterMap, TEXT("NearPlaneM"));
		FarPlaneM.Bind(Initializer.ParameterMap, TEXT("FarPlaneM"));
		WorldToMeters.Bind(Initializer.ParameterMap, TEXT("WorldToMeters"));
	}

	void SetParameters(FRHICommandList& RHICmdList, float nearPlaneM, float farPlaneM, float worldToMetersValue, FRHITexture* DepthTexture)
	{
		FRHIPixelShader* PixelShaderRHI = GetPixelShader();

		SetShaderValue(RHICmdList, PixelShaderRHI, NearPlaneM, nearPlaneM);
		SetShaderValue(RHICmdList, PixelShaderRHI, FarPlaneM, farPlaneM);
		SetShaderValue(RHICmdList, PixelShaderRHI, WorldToMeters, worldToMetersValue);

		FRHISamplerState* SamplerStateRHI = TStaticSamplerState<SF_Point>::GetRHI();
		SetTextureParameter(RHICmdList, PixelShaderRHI, InDepthTexture, InTextureSampler, SamplerStateRHI, DepthTexture);
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);

		// Serialize shader inputs.
		Ar << InDepthTexture;
		Ar << InTextureSampler;
		
		Ar << NearPlaneM;
		Ar << FarPlaneM;
		Ar << WorldToMeters;

		return bShaderHasOutdatedParameters;
	}

private:
	// Shader parameters.
	FShaderResourceParameter InDepthTexture;
	FShaderResourceParameter InTextureSampler;
	FShaderParameter NearPlaneM;
	FShaderParameter FarPlaneM;
	FShaderParameter WorldToMeters;
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
		/** IHeadMountedDisplayModule implementation */
		virtual TSharedPtr< class IXRTrackingSystem, ESPMode::ThreadSafe > CreateTrackingSystem() override;

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

#if !PLATFORM_HOLOLENS
			FString OSVersionLabel;
			FString OSSubVersionLabel;
			FPlatformMisc::GetOSVersions(OSVersionLabel, OSSubVersionLabel);
			// GetOSVersion returns the Win10 release version in the OSVersion rather than the OSSubVersion, so parse it out ourselves
			OSSubVersionLabel = OSVersionLabel;
			bool bHasSupportedWindowsVersion = OSSubVersionLabel.RemoveFromStart("Windows 10 (Release ") && OSSubVersionLabel.RemoveFromEnd(")") && (FCString::Atoi(*OSSubVersionLabel) >= MIN_WIN_10_VERSION_FOR_WMR);

			if (bHasSupportedWindowsVersion)
			{
				// Get the base directory of this plugin
				FString BaseDir = IPluginManager::Get().FindPlugin("WindowsMixedReality")->GetBaseDir();

				FString EngineDir = FPaths::EngineDir();
				FString BinariesSubDir = FPlatformProcess::GetBinariesSubdirectory();
#if WINDOWS_MIXED_REALITY_DEBUG_DLL
				FString DLLName(TEXT("MixedRealityInteropDebug.dll"));
#else // WINDOWS_MIXED_REALITY_DEBUG_DLL
				FString DLLName(TEXT("MixedRealityInterop.dll"));
#endif // WINDOWS_MIXED_REALITY_DEBUG_DLL
				FString MRInteropLibraryPath = EngineDir / "Binaries/ThirdParty/MixedRealityInteropLibrary" / BinariesSubDir / DLLName;

#if PLATFORM_64BITS
				// Load these dependencies first or MixedRealityInteropLibraryHandle fails to load since it doesn't look in the correct path for its dependencies automatically
				FString HoloLensLibraryDir = EngineDir / "Binaries/ThirdParty/Windows/x64";
				FPlatformProcess::PushDllDirectory(*HoloLensLibraryDir);
				FPlatformProcess::GetDllHandle(_TEXT("PerceptionDevice.dll"));
				FPlatformProcess::GetDllHandle(_TEXT("Microsoft.Holographic.AppRemoting.dll"));
				FPlatformProcess::PopDllDirectory(*HoloLensLibraryDir);
#endif // PLATFORM_64BITS && WITH_EDITOR

				// Then finally try to load the WMR Interop Library
				void* MixedRealityInteropLibraryHandle = !MRInteropLibraryPath.IsEmpty() ? FPlatformProcess::GetDllHandle(*MRInteropLibraryPath) : nullptr;
				if (MixedRealityInteropLibraryHandle)
				{
					HMD = new MixedRealityInterop();
				}
				else
				{
					FText ErrorText = NSLOCTEXT("WindowsMixedRealityHMD", "MixedRealityInteropLibraryError",
						"Failed to load Windows Mixed Reality Interop Library!  Windows Mixed Reality cannot function.");
					FMessageDialog::Open(EAppMsgType::Ok, ErrorText);
					UE_LOG(LogWmrHmd, Error, TEXT("%s"), *ErrorText.ToString());
				}
			}
			else
			{
				FText ErrorText = FText::Format(FTextFormat(NSLOCTEXT("WindowsMixedRealityHMD", "MixedRealityInteropLibraryError",
					"Windows Mixed Reality is not supported on this Windows version. \nNote: UE4 only supports Windows Mixed Reality on Windows 10 Release {0} or higher. Current version: {1}")),
					FText::FromString(FString::FromInt(MIN_WIN_10_VERSION_FOR_WMR)), FText::FromString(OSVersionLabel));
				FMessageDialog::Open(EAppMsgType::Ok, ErrorText);
				if (IsRunningCommandlet())
				{
					UE_LOG(LogWmrHmd, Warning, TEXT("%s"), *ErrorText.ToString());
				}
				else
				{
					UE_LOG(LogWmrHmd, Error, TEXT("%s"), *ErrorText.ToString());
				}
			}

#else // !PLATFORM_HOLOLENS
			HMD = new MixedRealityInterop();
#endif // !PLATFORM_HOLOLENS

#if WANTS_INTEROP_LOGGING
			if (HMD != nullptr)
			{
				HMD->SetLogCallback(&LogCallback);
			}	
#endif // WANTS_INTEROP_LOGGING

			FString PluginShaderDir = FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("WindowsMixedReality"))->GetBaseDir(), TEXT("Shaders"));
			AddShaderSourceDirectoryMapping(TEXT("/Plugin/WindowsMixedReality"), PluginShaderDir);
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
		EnableStereo(true);
	#endif

		//start speech recognition if there are any commands we care to listen for
		StartSpeechRecognition();

		IWindowsMixedRealityHandTrackingModule::Get().AddLiveLinkSource();
	}

	void FWindowsMixedRealityHMD::OnEndPlay(FWorldContext & InWorldContext)
	{
	#if PLATFORM_HOLOLENS
		EnableStereo(false);
	#endif

		StopSpeechRecognition();

		IWindowsMixedRealityHandTrackingModule::Get().RemoveLiveLinkSource();
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
			EnableStereo(true);
#endif

			return true;
		}

#if WITH_WINDOWS_MIXED_REALITY
		if (!HMD->IsInitialized())
		{
			D3D11Device = InternalGetD3D11Device();
			HMD->Initialize(D3D11Device.GetReference(),
				GNearClippingPlane / GetWorldToMetersScale(), farPlaneDistance);
			return true;
		}
		else
		{
#if !PLATFORM_HOLOLENS
			if (!HMD->IsRemoting() && !HMD->IsImmersiveWindowValid())
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

		CachedWorldToMetersScale = WorldContext.World()->GetWorldSettings()->WorldToMeters;

		// Only refresh this based on the game world.  When remoting there is also an editor world, which we do not want to have affect the transform.
		if (WorldContext.World()->IsGameWorld())
		{
			RefreshTrackingToWorldTransform(WorldContext);
		}

		return true;
	}

	void FWindowsMixedRealityHMD::SetTrackingOrigin(EHMDTrackingOrigin::Type NewOrigin)
	{
		HMDTrackingOrigin = NewOrigin;
	}

	EHMDTrackingOrigin::Type FWindowsMixedRealityHMD::GetTrackingOrigin() const
	{
		return HMDTrackingOrigin;
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

	bool FWindowsMixedRealityHMD::GetCurrentPose(
		int32 DeviceId,
		FQuat& CurrentOrientation,
		FVector& CurrentPosition)
	{
		if (DeviceId != IXRTrackingSystem::HMDDeviceId)
		{
			return false;
		}

		// Get most recently available tracking data.
		InitTrackingFrame();

		CurrentOrientation = CurrOrientation;
		CurrentPosition = CurrPosition;

		return true;
	}

	bool FWindowsMixedRealityHMD::GetRelativeEyePose(int32 DeviceId, EStereoscopicPass Eye, FQuat& OutOrientation, FVector& OutPosition)
	{
		OutOrientation = FQuat::Identity;
		OutPosition = FVector::ZeroVector;
		if (DeviceId == IXRTrackingSystem::HMDDeviceId && (Eye == eSSP_LEFT_EYE || Eye == eSSP_RIGHT_EYE))
		{
			OutPosition = FVector(0, (Eye == EStereoscopicPass::eSSP_LEFT_EYE ? -0.5f : 0.5f) * GetInterpupillaryDistance() * GetWorldToMetersScale(), 0);
			return true;
		}
		else
		{
			return false;
		}
	}

	void FWindowsMixedRealityHMD::ResetOrientationAndPosition(float yaw)
	{
#if WITH_WINDOWS_MIXED_REALITY
		HMD->ResetOrientationAndPosition();
#endif
	}

	void FWindowsMixedRealityHMD::InitTrackingFrame()
	{
#if WITH_WINDOWS_MIXED_REALITY
		DirectX::XMMATRIX leftPose;
		DirectX::XMMATRIX rightPose;
		WindowsMixedReality::HMDTrackingOrigin trackingOrigin;
		if (HMD->GetCurrentPose(leftPose, rightPose, trackingOrigin))
		{
			trackingOrigin == WindowsMixedReality::HMDTrackingOrigin::Eye ?
				SetTrackingOrigin(EHMDTrackingOrigin::Eye) :
				SetTrackingOrigin(EHMDTrackingOrigin::Floor);

			// Convert to unreal space
			FMatrix UPoseL = WMRUtility::ToFMatrix(leftPose);
			FMatrix UPoseR = WMRUtility::ToFMatrix(rightPose);
			RotationL = FQuat(UPoseL);
			RotationR = FQuat(UPoseR);

			RotationL = FQuat(-1 * RotationL.Z, RotationL.X, RotationL.Y, -1 * RotationL.W);
			RotationR = FQuat(-1 * RotationR.Z, RotationR.X, RotationR.Y, -1 * RotationR.W);

			RotationL.Normalize();
			RotationR.Normalize();

			FQuat HeadRotation = FMath::Lerp(RotationL, RotationR, 0.5f);
			HeadRotation.Normalize();

			// Position = forward/ backwards, left/ right, up/ down.
			PositionL = ((FVector(UPoseL.M[2][3], -1 * UPoseL.M[0][3], -1 * UPoseL.M[1][3]) * GetWorldToMetersScale()));
			PositionR = ((FVector(UPoseR.M[2][3], -1 * UPoseR.M[0][3], -1 * UPoseR.M[1][3]) * GetWorldToMetersScale()));

			PositionL = RotationL.RotateVector(PositionL);
			PositionR = RotationR.RotateVector(PositionR);

			if (ipd == 0)
			{
				ipd = FVector::Dist(PositionL, PositionR) / GetWorldToMetersScale();
			}

			FVector HeadPosition = FMath::Lerp(PositionL, PositionR, 0.5f);

			CurrOrientation = HeadRotation;
			CurrPosition = HeadPosition;
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
				if (bIsStereoDesired)
				{
					int Width, Height;
					if (HMD->GetDisplayDimensions(Width, Height))
					{
						SceneVP->SetViewportSize(
							Width * 2,
							Height);

						Window->SetViewportSizeDrivenByWindow(false);

						bIsStereoEnabled = HMD->IsStereoEnabled();
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

	bool FWindowsMixedRealityHMD::EnableStereo(bool stereo)
	{
#if WITH_WINDOWS_MIXED_REALITY
		if (stereo)
		{
			if (bIsStereoDesired && HMD->IsInitialized())
			{
				return false;
			}

			FindMRSceneViewport(bIsStereoDesired);
			if (!bIsStereoDesired)
			{
				return false;
			}

			HMD->EnableStereo(stereo);
#if SUPPORTS_WINDOWS_MIXED_REALITY_GESTURES
			HMD->SetInteractionManagerForCurrentView();
#endif

			InitializeHolographic();

			currentWornState = GetHMDWornState();

			FApp::SetUseVRFocus(true);
			FApp::SetHasVRFocus(true);
		}
		else
		{
			ShutdownHolographic();

			FApp::SetUseVRFocus(false);
			FApp::SetHasVRFocus(false);
		}
#endif
		return bIsStereoDesired;
	}

	void FWindowsMixedRealityHMD::AdjustViewRect(
		EStereoscopicPass StereoPass,
		int32& X, int32& Y,
		uint32& SizeX, uint32& SizeY) const
	{
		SizeX *= ScreenScalePercentage;
		SizeY *= ScreenScalePercentage;

		SizeX = SizeX / 2;
		if (StereoPass == eSSP_RIGHT_EYE)
		{
			X += SizeX;
		}
	}

	void FWindowsMixedRealityHMD::CalculateStereoViewOffset(const enum EStereoscopicPass StereoPassType, FRotator& ViewRotation, const float WorldToMeters, FVector& ViewLocation)
	{
		if (StereoPassType != eSSP_LEFT_EYE &&
			StereoPassType != eSSP_RIGHT_EYE)
		{
			return;
		}

		FVector HmdToEyeOffset = FVector::ZeroVector;

		FQuat CurEyeOrient;
		if (StereoPassType == eSSP_LEFT_EYE)
		{
			HmdToEyeOffset = PositionL - CurrPosition;
			CurEyeOrient = RotationL;
		}
		else if (StereoPassType == eSSP_RIGHT_EYE)
		{
			HmdToEyeOffset = PositionR - CurrPosition;
			CurEyeOrient = RotationR;
		}

		const FQuat ViewOrient = ViewRotation.Quaternion();
		const FQuat deltaControlOrientation = ViewOrient * CurEyeOrient.Inverse();
		const FVector vEyePosition = deltaControlOrientation.RotateVector(HmdToEyeOffset);
		ViewLocation += vEyePosition;
	}

	FMatrix FWindowsMixedRealityHMD::GetStereoProjectionMatrix(const enum EStereoscopicPass StereoPassType) const
	{
		if (StereoPassType != eSSP_LEFT_EYE &&
			StereoPassType != eSSP_RIGHT_EYE)
		{
			return FMatrix::Identity;
		}

#if WITH_WINDOWS_MIXED_REALITY
		DirectX::XMFLOAT4X4 projection = (StereoPassType == eSSP_LEFT_EYE)
			? HMD->GetProjectionMatrix(HMDEye::Left)
			: HMD->GetProjectionMatrix(HMDEye::Right);

		auto result = WMRUtility::ToFMatrix(projection).GetTransposed();
		// Convert from RH to LH projection matrix
		// See PerspectiveOffCenterRH: https://msdn.microsoft.com/en-us/library/windows/desktop/ms918176.aspx
		result.M[2][0] *= -1;
		result.M[2][1] *= -1;
		result.M[2][2] *= -1;
		result.M[2][3] *= -1;

		// Switch to reverse-z, replace near and far distance
		result.M[2][2] = 0.0f;  // Must be 0 for reverse-z.
		result.M[3][2] = GNearClippingPlane;

		return result;
#else
		return FMatrix::Identity;
#endif
	}

	void FWindowsMixedRealityHMD::GetEyeRenderParams_RenderThread(
		const FRenderingCompositePassContext& Context,
		FVector2D& EyeToSrcUVScaleValue,
		FVector2D& EyeToSrcUVOffsetValue) const
	{
		if (Context.View.StereoPass == eSSP_LEFT_EYE)
		{
			EyeToSrcUVOffsetValue.X = 0.0f;
			EyeToSrcUVOffsetValue.Y = 0.0f;

			EyeToSrcUVScaleValue.X = 0.5f;
			EyeToSrcUVScaleValue.Y = 1.0f;
		}
		else
		{
			EyeToSrcUVOffsetValue.X = 0.5f;
			EyeToSrcUVOffsetValue.Y = 0.0f;

			EyeToSrcUVScaleValue.X = 0.5f;
			EyeToSrcUVScaleValue.Y = 1.0f;
		}
	}

	FIntPoint FWindowsMixedRealityHMD::GetIdealRenderTargetSize() const
	{
		int Width, Height;
#if WITH_WINDOWS_MIXED_REALITY
		HMD->GetDisplayDimensions(Width, Height);
#else
		Width = 100;
		Height = 100;
#endif

		return FIntPoint(Width * 2, Height);
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
#if PLATFORM_HOLOLENS
		return;
#endif
		const uint32 WindowWidth = gameWindowWidth;
		const uint32 WindowHeight = gameWindowHeight;

		const uint32 ViewportWidth = BackBuffer->GetSizeX();
		const uint32 ViewportHeight = BackBuffer->GetSizeY();

		const uint32 TextureWidth = SrcTexture->GetSizeX();
		const uint32 TextureHeight = SrcTexture->GetSizeY();

		const uint32 SourceWidth = TextureWidth / 2;
		const uint32 SourceHeight = TextureHeight;

		const float r = (float)SourceWidth / (float)SourceHeight;

		float width = (float)WindowWidth;
		float height = (float)WindowHeight;

		if ((float)WindowWidth / r < WindowHeight)
		{
			width = ViewportWidth;

			float displayHeight = (float)WindowWidth / r;
			height = (float)ViewportHeight * (displayHeight / (float)WindowHeight);
		}
		else // width > height
		{
			height = ViewportHeight;

			float displayWidth = (float)WindowHeight * r;
			width = (float)ViewportWidth * (displayWidth / (float)WindowWidth);
		}

		width = FMath::Clamp<int>(width, 10, ViewportWidth);
		height = FMath::Clamp<int>(height, 10, ViewportHeight);

		const uint32 x = (ViewportWidth - width) * 0.5f;
		const uint32 y = (ViewportHeight - height) * 0.5f;
		FRHIRenderPassInfo RPInfo(BackBuffer, ERenderTargetActions::Load_Store);
		RHICmdList.BeginRenderPass(RPInfo, TEXT("WindowsMixedRealityHMD"));
		DrawClearQuad(RHICmdList, FLinearColor(0.0f, 0.0f, 0.0f, 1.0f));
		RHICmdList.SetViewport(x, y, 0, width + x, height + y, 1.0f);

		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

		const auto FeatureLevel = GMaxRHIFeatureLevel;
		auto ShaderMap = GetGlobalShaderMap(FeatureLevel);
		TShaderMapRef<FScreenVS> VertexShader(ShaderMap);
		TShaderMapRef<FScreenPS> PixelShader(ShaderMap);

		GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;

		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

		PixelShader->SetParameters(RHICmdList, TStaticSamplerState<SF_Bilinear>::GetRHI(), SrcTexture);

		RendererModule->DrawRectangle(
			RHICmdList,
			0, 0,
			ViewportWidth, ViewportHeight,
			0.0f, 0.0f,
			0.5f, 1.0f,
			FIntPoint(ViewportWidth, ViewportHeight),
			FIntPoint(1, 1),
			*VertexShader,
			EDRF_Default);

		RHICmdList.EndRenderPass();
	}

	// Create a BGRA backbuffer for rendering.
	bool FWindowsMixedRealityHMD::AllocateRenderTargetTexture(
		uint32 index,
		uint32 sizeX,
		uint32 sizeY,
		uint8 format,
		uint32 numMips,
		uint32 flags,
		uint32 targetableTextureFlags,
		FTexture2DRHIRef& outTargetableTexture,
		FTexture2DRHIRef& outShaderResourceTexture,
		uint32 numSamples)
	{
		if (!IsStereoEnabled())
		{
			return false;
		}

		FRHIResourceCreateInfo CreateInfo;

		// Since our textures must be BGRA, this plugin did require a change to WindowsD3D11Device.cpp
		// to add the D3D11_CREATE_DEVICE_BGRA_SUPPORT flag to the graphics device.
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

		CurrentBackBuffer = outTargetableTexture;

		return true;
	}

	bool FWindowsMixedRealityHMD::HasHiddenAreaMesh() const
	{
		return HiddenAreaMesh[0].IsValid() && HiddenAreaMesh[1].IsValid();
	}

	void FWindowsMixedRealityHMD::DrawHiddenAreaMesh_RenderThread(FRHICommandList& RHICmdList, EStereoscopicPass StereoPass) const
	{
		if (StereoPass == eSSP_FULL)
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
		if (StereoPass == eSSP_FULL)
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
		// Update depth texture to match format Windows Mixed Reality platform is expecting.
		FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
		FRHITexture2D* depthFRHITexture = SceneContext.GetSceneDepthTexture().GetReference()->GetTexture2D();

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
			remappedDepthTexture = RHICmdList.CreateTexture2D(depthFRHITexture->GetSizeX(), depthFRHITexture->GetSizeY(),
				PF_R32_UINT, 1, 1, ETextureCreateFlags::TexCreate_RenderTargetable | ETextureCreateFlags::TexCreate_UAV, CreateInfo);
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
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*vertexShader);
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*pixelShader);
			GraphicsPSOInit.PrimitiveType = PT_TriangleList;

			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

			pixelShader->SetParameters(RHICmdList, GNearClippingPlane / GetWorldToMetersScale(), farPlaneDistance, GetWorldToMetersScale(), depthFRHITexture);

			RendererModule->DrawRectangle(
				RHICmdList,
				0, 0, // X, Y
				viewportWidth, viewportHeight, // SizeX, SizeY
				0.0f, 0.0f, // U, V
				1.0f, 1.0f, // SizeU, SizeV
				FIntPoint(viewportWidth, viewportHeight), // TargetSize
				FIntPoint(1, 1), // TextureSize
				*vertexShader,
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
			D3D11_TEXTURE2D_DESC tdesc;

			tdesc.Width = viewportWidth / 2;
			tdesc.Height = viewportHeight;
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

		ID3D11DeviceContext* context;
		device->GetImmediateContext(&context);

		_StereoCopy(context, ScreenScalePercentage,
			(ID3D11Texture2D*)remappedDepthTexture->GetNativeResource(),
			stereoDepthTexture);
#endif
	}

	void FWindowsMixedRealityHMD::PreRenderViewFamily_RenderThread(
		FRHICommandListImmediate& RHICmdList,
		FSceneViewFamily& InViewFamily)
	{
#if WITH_WINDOWS_MIXED_REALITY
		if (!mCustomPresent || !HMD->IsInitialized() || !HMD->IsAvailable())
		{
			return;
		}

		// Update currentFrame in the interop
		HMD->UpdateCurrentFrame();

		CreateHMDDepthTexture(RHICmdList);
		if (!HMD->CreateRenderingParameters(stereoDepthTexture))
		{
			// This will happen if an exception is thrown while creating the frame's rendering parameters.
			// Because Windows Mixed Reality can only have 2 rendering parameters in flight at any time, this is fatal.
			this->bRequestRestart = true;
		}
#endif
	}

	bool FWindowsMixedRealityHMD::IsActiveThisFrame(class FViewport* InViewport) const
	{
		return GEngine && GEngine->IsStereoscopic3D(InViewport);
	}

#if WITH_WINDOWS_MIXED_REALITY
	FWindowsMixedRealityHMD::FWindowsMixedRealityHMD(const FAutoRegister& AutoRegister, IARSystemSupport* InARSystem, MixedRealityInterop* InHMD)
		: FHeadMountedDisplayBase(InARSystem)
		, FSceneViewExtensionBase(AutoRegister)
		, HMD(InHMD)
		, ScreenScalePercentage(1.0f)
		, mCustomPresent(nullptr)
		, HMDTrackingOrigin(EHMDTrackingOrigin::Floor)
		, CurrOrientation(FQuat::Identity)
		, CurrPosition(FVector::ZeroVector)
	{
		static const FName RendererModuleName("Renderer");
		RendererModule = FModuleManager::GetModulePtr<IRendererModule>(RendererModuleName);

		HiddenAreaMesh.SetNum(2);
		VisibleAreaMesh.SetNum(2);
	}
#endif

	FWindowsMixedRealityHMD::~FWindowsMixedRealityHMD()
	{
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

		bIsStereoDesired = false;
		bIsStereoEnabled = false;

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
		return HMD->IsDisplayOpaque();
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
#endif
	}

	// Prevent crashes if computer goes to sleep.
	void FWindowsMixedRealityHMD::AppServicePause()
	{
		this->bRequestRestart = true;
	}

	void CallSpeechCallback(FKey InKey, FName InActionName)
	{
		AsyncTask(ENamedThreads::GameThread, [InKey, InActionName]()
		{
			APlayerController* PlayerController = GEngine->GetFirstLocalPlayerController(GWorld);
			if (PlayerController)
			{
				PlayerController->InputKey(InKey, IE_Pressed, 1.0f, false);
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
			mCustomPresent = new FWindowsMixedRealityCustomPresent(HMD, D3D11Device);
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
		return HMD->GetControllerTrackingStatus(hand);
	}

	// GetControllerOrientationAndPosition() is pre-scaled to UE4 world scale
	bool FWindowsMixedRealityHMD::GetControllerOrientationAndPosition(HMDHand hand, FRotator & OutOrientation, FVector & OutPosition)
	{
		if (!bIsStereoEnabled)
		{
			return false;
		}

		DirectX::XMFLOAT4 rot;
		DirectX::XMFLOAT3 pos;
		if (HMD->GetControllerOrientationAndPosition(hand, rot, pos))
		{
			FTransform TrackingSpaceTransform(WMRUtility::FromMixedRealityQuaternion(rot), WMRUtility::FromMixedRealityVector(pos));

			OutOrientation = FRotator(TrackingSpaceTransform.GetRotation());
			OutPosition = TrackingSpaceTransform.GetLocation();

			// If the device does not support full hand tracking, set the orientation manually
			if (!FWindowsMixedRealityStatics::SupportsHandedness())
			{
				OutOrientation = FRotator(CurrOrientation);
				OutOrientation.Roll = 0;
				OutOrientation.Pitch = 0;
			}

			return true;
		}

		return false;
	}

	// GetHandJointOrientationAndPosition() is NOT scaled to UE4 world scale, so we must do it here
	bool FWindowsMixedRealityHMD::GetHandJointOrientationAndPosition(HMDHand hand, HMDHandJoint joint, FRotator& OutOrientation, FVector& OutPosition)
	{
		if (!bIsStereoEnabled)
		{
			return false;
		}

		DirectX::XMFLOAT4 rot;
		DirectX::XMFLOAT3 pos;
		if (HMD->GetHandJointOrientationAndPosition(hand, joint, rot, pos))
		{
			FTransform TrackingSpaceTransform(WMRUtility::FromMixedRealityQuaternion(rot), WMRUtility::FromMixedRealityVector(pos) * GetWorldToMetersScale());

			OutOrientation = FRotator(TrackingSpaceTransform.GetRotation());
			OutPosition = TrackingSpaceTransform.GetLocation();

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

	HMDInputPressState WindowsMixedReality::FWindowsMixedRealityHMD::GetPressState(HMDHand hand, HMDInputControllerButtons button)
	{
		return HMD->GetPressState(hand, button);
	}

	float FWindowsMixedRealityHMD::GetAxisPosition(HMDHand hand, HMDInputControllerAxes axis)
	{
		return HMD->GetAxisPosition(hand, axis);
	}

	void FWindowsMixedRealityHMD::SubmitHapticValue(HMDHand hand, float value)
	{
		HMD->SubmitHapticValue(hand, FMath::Clamp(value, 0.f, 1.f));
	}
	
	bool FWindowsMixedRealityHMD::QueryCoordinateSystem(ABI::Windows::Perception::Spatial::ISpatialCoordinateSystem *& pCoordinateSystem, WindowsMixedReality::HMDTrackingOrigin& trackingOrigin)
	{
		return HMD->QueryCoordinateSystem(pCoordinateSystem, trackingOrigin);
	}

	bool FWindowsMixedRealityHMD::IsTrackingAvailable()
	{
		return HMD->IsTrackingAvailable();
	}

	void FWindowsMixedRealityHMD::GetPointerPose(EControllerHand hand, PointerPoseInfo& pi)
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

#endif

	namespace WindowsMixedRealityHMD
	{
		void LogForInterop(const wchar_t* text)
		{
			UE_LOG(LogWmrHmd, Log, TEXT("WMRInterop: %s"), text);
		}
	}

	// Remoting
	void FWindowsMixedRealityHMD::ConnectToRemoteHoloLens(const wchar_t* ip, unsigned int bitrate, bool isHoloLens1)
	{
#if WITH_EDITOR
		D3D11Device = InternalGetD3D11Device();

#if WITH_WINDOWS_MIXED_REALITY
		HMD->SetLogCallback(WindowsMixedRealityHMD::LogForInterop);
		HMD->ConnectToRemoteHoloLens(D3D11Device.GetReference(), ip, bitrate, isHoloLens1);
#endif
#endif
	}

	void FWindowsMixedRealityHMD::DisconnectFromRemoteHoloLens()
	{
#if WITH_EDITOR
#if WITH_WINDOWS_MIXED_REALITY
		HMD->DisconnectFromDevice();
		HMD->SetLogCallback(nullptr);
#endif
#endif
	}
}

IMPLEMENT_MODULE(WindowsMixedReality::FWindowsMixedRealityHMDPlugin, WindowsMixedRealityHMD)

DEFINE_LOG_CATEGORY(LogWmrHmd);
