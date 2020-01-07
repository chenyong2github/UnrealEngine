// Copyright Epic Games, Inc. All Rights Reserved.

#include "MagicLeapHMD.h"
#include "MagicLeapHMDFunctionLibrary.h"
#include "DefaultStereoLayers.h"
#include "MagicLeapStereoLayers.h"
#include "RendererPrivate.h"
#include "PostProcess/PostProcessHMD.h"
#include "ScreenRendering.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/PlayerController.h"
#include "Engine/GameEngine.h"
#include "Widgets/SViewport.h"
#include "Slate/SceneViewport.h"
#include "AppFramework.h"
#include "PipelineStateCache.h"
#include "DeviceProfiles/DeviceProfileManager.h"
#include "DeviceProfiles/DeviceProfile.h"
#include "LuminRuntimeSettings.h"
#include "MotionControllerComponent.h"
#include "IMagicLeapTrackerEntity.h"
#include "MagicLeapCFUID.h"
#if PLATFORM_WINDOWS || PLATFORM_LINUX || PLATFORM_MAC
#include "Async/Async.h"
#endif // PLATFORM_WINDOWS || PLATFORM_LINUX || PLATFORM_MAC

#include "XRThreadUtils.h"

#if !PLATFORM_MAC
#include "MagicLeapHelperVulkan.h"
#endif //!PLATFORM_MAC

#include "MagicLeapUtils.h"
#include "MagicLeapPluginUtil.h"
#include "UnrealEngine.h"
#include "ClearQuad.h"
#include "MagicLeapXRCamera.h"
#include "GeneralProjectSettings.h"
#include "MagicLeapSettings.h"

#if !PLATFORM_MAC
#include "OpenGLDrv.h"
#include "VulkanRHIPrivate.h"
#include "VulkanRHIBridge.h"
#else
#include "MetalRHI.h"
#endif // !PLATFORM_MAC

#include "Lumin/CAPIShims/LuminAPIPerception.h"
#include "Lumin/CAPIShims/LuminAPIRemote.h"
#include "Lumin/CAPIShims/LuminAPIGraphics.h"
#include "Lumin/CAPIShims/LuminAPIPrivileges.h"

#if !PLATFORM_LUMIN
#include "Misc/MessageDialog.h"
#endif

#if WITH_EDITOR
#include "Editor.h"
#include "Editor/EditorPerformanceSettings.h"
#include "Misc/CoreDelegates.h"
#include "ISettingsModule.h"
#include "ISettingsSection.h"
#endif

#include "MagicLeapVulkanExtensions.h"

#define LOCTEXT_NAMESPACE "MagicLeap"

//---------------------------------------------------
// MagicLeapHMD Plugin Implementation
//---------------------------------------------------

class FMagicLeapPlugin : public IMagicLeapPlugin
{
public:
	FMagicLeapPlugin()
		: bIsVDZIEnabled(false)
		, bUseVulkanForZI(false)
		, bUseMLAudioForZI(false)
	{
	}

	/** IModuleInterface implementation */
	virtual void StartupModule() override
	{
		// Ideally, we should be able to query GetDefault<UMagicLeapSettings>()->bEnableZI directly.
		// Unfortunately, the UObject system hasn't finished initialization when this module has been loaded.
		GConfig->GetBool(TEXT("/Script/MagicLeap.MagicLeapSettings"), TEXT("bEnableZI"), bIsVDZIEnabled, GEngineIni);
		GConfig->GetBool(TEXT("/Script/MagicLeap.MagicLeapSettings"), TEXT("bUseVulkanForZI"), bUseVulkanForZI, GEngineIni);
		GConfig->GetBool(TEXT("/Script/MagicLeap.MagicLeapSettings"), TEXT("bUseMLAudioForZI"), bUseMLAudioForZI, GEngineIni);

#if (PLATFORM_WINDOWS || PLATFORM_MAC) && WITH_EDITOR
		if (bIsVDZIEnabled)
		{
#if WITH_MLSDK
			const bool bLoadedRemoteDLL = (MLSDK_API::LibraryLoader::Ref().LoadDLL(TEXT("ml_remote")) != nullptr);
#else
			const bool bLoadedRemoteDLL = false;
#endif // WITH_MLSDK
			if (!bLoadedRemoteDLL)
			{
				// Bail early, because we'll eventually die later.
				UE_LOG(LogMagicLeap, Warning, TEXT("VDZI enabled, but can't load the ml_remote DLL.  Is your MLSDK directory set up properly?"));
				bIsVDZIEnabled = false;
			}

			FString CommandLine = FCommandLine::Get();
#if !PLATFORM_MAC
			const FString GLFlag(" -opengl4 ");
			const FString VKFlag(" -vulkan ");
			const FString GLToken = GLFlag.TrimStartAndEnd();
			const FString VKToken = VKFlag.TrimStartAndEnd();

			CommandLine.ReplaceInline(*(GLToken.TrimEnd()), TEXT(""));
			CommandLine.ReplaceInline(*(VKToken.TrimEnd()), TEXT(""));
#endif // !PLATFORM_MAC

			const FString AudioMixerFlag(" -audiomixer MagicLeapAudio ");
			const FString AudioMixerToken = AudioMixerFlag.TrimStartAndEnd();
			CommandLine.ReplaceInline(*(AudioMixerToken.TrimEnd()), TEXT(""));

#if !PLATFORM_MAC
			if (bUseVulkanForZI)
			{
				UE_LOG(LogMagicLeap, Log, TEXT("ML VDZI mode enabled. Using Vulkan renderer."));
				ExtraCommandLineFlags = VKFlag;
			}
			else
			{
				// DirectX, which is currently not supported by MagicLeap, is default API on Windows.
				// OpenGL is forced by loading module in PostConfigInit phase and passing in command line.
				// -opengl will force editor to use OpenGL3/SM4 feature level. Fwd VR path requires SM5 feature level, thus passing -opengl here will break editor preview window with Fwd VR path
				// The cmd arg for OpenGL4/SM5 feature level is -opengl4 in Windows.
				ExtraCommandLineFlags = GLFlag;
			}
#endif // !PLATFORM_MAC

			if (bUseMLAudioForZI)
			{
				// Cache the audio mixer module already set for the project.
				GConfig->GetString(TEXT("Audio"), TEXT("AudioMixerModuleName"), PreviousAudioMixer, GEngineIni);

				GConfig->SetString(TEXT("Audio"), TEXT("AudioMixerModuleName"), TEXT("MagicLeapAudio"), GEngineIni);
				ExtraCommandLineFlags.Append(AudioMixerFlag);
			}

			CommandLine.Append(ExtraCommandLineFlags);
			FCommandLine::Set(*CommandLine);

			// This is the closest callback that happens before EditorExit. We need to undo our
			// command line changes before the EditorExit as that where the editor will relaunch
			// itself using the current command line. Hence to avoid the RHI option stick when
			// we change the VDZI option we need to remove before the EngineExit.
			FEditorDelegates::OnShutdownPostPackagesSaved.AddRaw(this, &FMagicLeapPlugin::UndoCommandLine);
		}
#endif // (PLATFORM_WINDOWS || PLATFORM_MAC) && WITH_EDITOR

#if WITH_EDITOR
		// We don't quite have control of when the "Settings" module is loaded, so we'll wait until PostEngineInit to register settings.
		FCoreDelegates::OnPostEngineInit.AddRaw(this, &FMagicLeapPlugin::AddEditorSettings);
#endif // WITH_EDITOR

		IMagicLeapPlugin::StartupModule();
	}

#if WITH_EDITOR
	void UndoCommandLine()
	{
		if (ExtraCommandLineFlags.Len() > 0)
		{
			FString CommandLine = FCommandLine::Get();
			CommandLine.ReplaceInline(*(ExtraCommandLineFlags.TrimStartAndEnd()), TEXT(""));
			FCommandLine::Set(*CommandLine);
		}

		if (bUseMLAudioForZI)
		{
			GConfig->SetString(TEXT("Audio"), TEXT("AudioMixerModuleName"), *PreviousAudioMixer, GEngineIni);
		}
	}
#endif

	virtual void ShutdownModule() override
	{
#if WITH_EDITOR
		RemoveEditorSettings();
#endif
		IMagicLeapPlugin::ShutdownModule();
	}

	/** IHeadMountedDisplayModule implementation */
	virtual TSharedPtr< class IXRTrackingSystem, ESPMode::ThreadSafe > CreateTrackingSystem() override
	{
#if !PLATFORM_LUMIN
		// Early out if VDZI is not enabled on non-Lumin platforms.  We don't want it reporting as available when no MLSDK is present
		if (!bIsVDZIEnabled)
		{
			return nullptr;
		}
#endif //PLATFORM_LUMIN
		
		TSharedPtr<FMagicLeapHMD, ESPMode::ThreadSafe> LocalHMD;

		if (HMD.IsValid())
		{
			LocalHMD = HMD.Pin();
		}
#if WITH_MLSDK
		else
		{
			//initialize AR system
			auto ARModule = FModuleManager::LoadModulePtr<ILuminARModule>("MagicLeapAR");
			check(ARModule);
			ARImplementation = ARModule->CreateARImplementation();
			LocalHMD = MakeShared<FMagicLeapHMD, ESPMode::ThreadSafe>(this, ARImplementation.Get(), bIsVDZIEnabled, bUseVulkanForZI);
			HMD = LocalHMD;
			ARModule->ConnectARImplementationToXRSystem(LocalHMD.Get());
			ARModule->InitializeARImplementation();
			LocalHMD->Startup();
		}
#endif // WITH_MLSDK

		return LocalHMD;
	}

	FString GetModuleKeyName() const
	{
		return FString(TEXT("MagicLeap"));
	}

	virtual bool IsMagicLeapHMDValid() const override
	{
#if PLATFORM_LUMIN
		return true;
#else
		if (IsValid(GEngine) && GEngine->XRSystem.IsValid() && (GEngine->XRSystem->GetSystemName().Compare(FMagicLeapHMD::SystemName) == 0))
		{
			TSharedPtr<FMagicLeapHMD, ESPMode::ThreadSafe> hmd = StaticCastSharedPtr<FMagicLeapHMD>(GEngine->XRSystem);
			// IsHMDConnected() is an expensive call when MLRemote is enabled, so we'll keep the onus of that check on the caller.
			return hmd.IsValid() && hmd->IsVDZIEnabled();
		}
		return false;
#endif // PLATFORM_LUMIN
	}

	virtual TSharedPtr<IHeadMountedDisplayVulkanExtensions, ESPMode::ThreadSafe> GetVulkanExtensions() override
	{
#if !PLATFORM_LUMIN
		// Check to see if VDZI is enabled, and abort if not.  We shouldn't modify the extensions if we're not active
		if (!bIsVDZIEnabled)
		{
			return nullptr;
		}
#endif //PLATFORM_LUMIN

#if PLATFORM_WINDOWS || PLATFORM_LUMIN
		if (!VulkanExtensions.IsValid())
		{
			VulkanExtensions = MakeShareable(new FMagicLeapVulkanExtensions);
		}
		return VulkanExtensions;
#endif
		return nullptr;
	}

	virtual void RegisterMagicLeapTrackerEntity(IMagicLeapTrackerEntity* TrackerEntity) override
	{
		check(TrackerEntity);
		TrackerEntities.Add(TrackerEntity);
	}

	virtual void UnregisterMagicLeapTrackerEntity(IMagicLeapTrackerEntity* TrackerEntity) override
	{
		check(TrackerEntity);
		TrackerEntities.Remove(TrackerEntity);
	}

	void EnableTrackerEntities()
	{
		for (auto& It : TrackerEntities)
		{
			It->CreateEntityTracker();
		}
	}

	void DisableTrackerEntities()
	{
		for (auto& It : TrackerEntities)
		{
			It->DestroyEntityTracker();
		}
	}

	virtual bool SetIgnoreInput(bool Ignore) override
	{
		if (HMD.IsValid())
		{
			return HMD.Pin()->SetIgnoreInput(Ignore);
		}
		return false;
	}

	void OnBeginRendering_GameThread()
	{
		for (auto& It : TrackerEntities)
		{
			It->OnBeginRendering_GameThread();
		}
	}

	virtual bool GetTransform(const FGuid& Id, FTransform& OutTransform, EMagicLeapTransformFailReason& OutReason) const override
	{
#if WITH_MLSDK
		if (HMD.IsValid())
		{
			MLCoordinateFrameUID CFUID;
			MagicLeap::FGuidToMLCFUID(Id, CFUID);
			return HMD.Pin()->GetAppFrameworkConst().GetTransform(CFUID, OutTransform, OutReason);
		}
#endif // WITH_MLSDK

		OutReason = EMagicLeapTransformFailReason::HMDNotInitialized;
		return false;
	}

#if WITH_MLSDK
	virtual bool GetTransform(const MLCoordinateFrameUID& Id, FTransform& OutTransform, EMagicLeapTransformFailReason& OutReason) const override
	{
		if (HMD.IsValid())
		{
			return HMD.Pin()->GetAppFrameworkConst().GetTransform(Id, OutTransform, OutReason);
		}

		OutReason = EMagicLeapTransformFailReason::HMDNotInitialized;
		return false;
	}
#endif // WITH_MLSDK

	virtual float GetWorldToMetersScale() const override
	{
		if (HMD.IsValid())
		{
			return HMD.Pin()->GetAppFrameworkConst().GetWorldToMetersScale();
		}

		return 100.0f;
	}

	virtual bool IsPerceptionEnabled() const override
	{
		if (HMD.IsValid())
		{
			return HMD.Pin()->IsPerceptionEnabled();
		}

		return false;
	}

	virtual bool UseMLAudioForZI() const override
	{
		// if VDZI is disabled, doesn't matter if MLAudio option is enabled.
		return bIsVDZIEnabled && bUseMLAudioForZI;
	}

private:

#if WITH_EDITOR
	void AddEditorSettings()
	{
		ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");

		// While this should usually be true, it's not guaranteed that the settings module will be loaded in the editor.
		// UBT allows setting bBuildDeveloperTools to false while bBuildEditor can be true.
		// The former option indirectly controls loading of the "Settings" module.
		if (SettingsModule)
		{
			SettingsModule->RegisterSettings("Project", "Plugins", "Magic Leap",
				LOCTEXT("MagicLeapSettingsName", "Magic Leap Plugin"),
				LOCTEXT("MagicLeapSettingsDescription", "Configure the Magic Leap plug-in."),
				GetMutableDefault<UMagicLeapSettings>()
			);
		}
	}

	void RemoveEditorSettings()
	{
		ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");
		if (SettingsModule)
		{
			SettingsModule->UnregisterSettings("Project", "Plugins", "Magic Leap");
		}
	}
#endif

	bool bIsVDZIEnabled;
	bool bUseVulkanForZI;
	bool bUseMLAudioForZI;
	TWeakPtr<FMagicLeapHMD, ESPMode::ThreadSafe> HMD;
	TSharedPtr<IARSystemSupport, ESPMode::ThreadSafe> ARImplementation;
	TSharedPtr<FMagicLeapVulkanExtensions, ESPMode::ThreadSafe> VulkanExtensions;
	TSet<IMagicLeapTrackerEntity*> TrackerEntities;
	FString ExtraCommandLineFlags;
	FString PreviousAudioMixer;
};

IMPLEMENT_MODULE(FMagicLeapPlugin, MagicLeap)

//////////////////////////////////////////////////////////////////////////

const FName FMagicLeapHMD::SystemName(TEXT("MagicLeap"));
const float FMagicLeapHMD::DefaultFocusDistance = 500.0f;

FName FMagicLeapHMD::GetSystemName() const
{
	return SystemName;
}

FString FMagicLeapHMD::GetVersionString() const
{
	FString s = FString::Printf(TEXT("LuminHMD - %s, built %s, %s"), *FEngineVersion::Current().ToString(),
		UTF8_TO_TCHAR(__DATE__), UTF8_TO_TCHAR(__TIME__));

	return s;
}

bool FMagicLeapHMD::OnStartGameFrame(FWorldContext& WorldContext)
{
#if WITH_MLSDK
	check(IsInGameThread());

	FTrackingFrame& TrackingFrame = GetCurrentFrameMutable();

	if (!WorldContext.World() || !WorldContext.World()->IsGameWorld())
	{
		// ignore all non-game worlds
		TrackingFrame.WorldContext = nullptr;
		return false;
	}

#if !PLATFORM_LUMIN
	// With VDZI, we need to enable on the start of game frame after stereo has been enabled.  On Lumin itself, it's enabled in EnableStereo() immediately
	if (bStereoEnabled != bStereoDesired)
	{
		bStereoEnabled = EnableStereo(bStereoDesired);
	}
#endif //!PLATFORM_LUMIN

	if (bStereoEnabled)
	{
		InitDevice();
	}

	AppFramework.BeginUpdate();

	// init tracking frame if first frame, otherwise we keep using last frame's data until it is refreshed in BeginRendering_GameThread
	if (TrackingFrame.Snapshot == nullptr)
	{
		RefreshTrackingFrame();
	}

	// override the default value that the frame constructor initialized and make sure it is non zero
	TrackingFrame.WorldToMetersScale = WorldContext.World()->GetWorldSettings()->WorldToMeters;
	TrackingFrame.WorldToMetersScale = TrackingFrame.WorldToMetersScale == 0.0f ? 100.0f : TrackingFrame.WorldToMetersScale;
	TrackingFrame.WorldContext = &WorldContext;

	RefreshTrackingToWorldTransform(WorldContext);

	//update AR system
	GetARCompositionComponent()->StartARGameFrame(WorldContext);

#endif //WITH_MLSDK

	return true;
}

bool FMagicLeapHMD::OnEndGameFrame(FWorldContext& WorldContext)
{
	check(IsInGameThread());

	if (!WorldContext.World() || !WorldContext.World()->IsGameWorld())
	{
		// ignore all non-game worlds
		return false;
	}

	return true;
}

bool FMagicLeapHMD::IsHMDConnected()
{
#if PLATFORM_LUMIN
	return true;
#elif WITH_MLSDK && (PLATFORM_WINDOWS || PLATFORM_LINUX || PLATFORM_MAC)
	const bool bLoadedRemoteDLL = (MLSDK_API::LibraryLoader::Ref().LoadDLL(TEXT("ml_remote")) != nullptr);
	if (!bIsVDZIEnabled || !bLoadedRemoteDLL)
	{
		return false;
	}

	EServerPingState ServerPingState = static_cast<EServerPingState>(FPlatformAtomics::AtomicRead(&ZIServerPingState));
	switch (ServerPingState)
	{
	case EServerPingState::NotInitialized:
	{
		FPlatformAtomics::InterlockedExchange(&ZIServerPingState, static_cast<int32>(EServerPingState::AttemptingFirstPing));
		AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [this]()
		{
			while (!IsEngineExitRequested())
			{
				bool bIsZIServerRunning;
				MLResult Result = MLRemoteIsServerConfigured(&bIsZIServerRunning);
				if (Result != MLResult_Ok && Result != MLResult_Timeout)
				{
					UE_LOG(LogMagicLeap, Error, TEXT("MLRemoteIsServerConfigured failed with error %s!"), UTF8_TO_TCHAR(MLGetResultString(Result)));
				}

				if (bIsZIServerRunning)
				{
					FPlatformAtomics::InterlockedExchange(&ZIServerPingState, static_cast<int32>(EServerPingState::ServerOnline));
				}
				else
				{
					FPlatformAtomics::InterlockedExchange(&ZIServerPingState, static_cast<int32>(EServerPingState::ServerOffline));
				}

				FPlatformProcess::Sleep(ServerPingInterval);
			}
		});
	}
	break;

	case EServerPingState::AttemptingFirstPing:
	{
		do 
		{
			ServerPingState = static_cast<EServerPingState>(FPlatformAtomics::AtomicRead(&ZIServerPingState));
		} while (ServerPingState == EServerPingState::AttemptingFirstPing);
	}
	break;

	case EServerPingState::ServerOnline:
	case EServerPingState::ServerOffline:
	{
	}
	break;
	}

	return ServerPingState == EServerPingState::ServerOnline;
#endif // PLATFORM_LUMIN
	return false;
}

bool FMagicLeapHMD::IsHMDEnabled() const
{
	return bHmdEnabled;
}

void FMagicLeapHMD::EnableHMD(bool Enable)
{
	bHmdEnabled = Enable;
	if (!bHmdEnabled)
	{
		EnableStereo(false);
	}
}

bool FMagicLeapHMD::GetHMDMonitorInfo(MonitorInfo& MonitorDesc)
{
	// Use ML device resolution only when HMD is enabled.
	// This ensures that we don't provide an invalid resolution when the device is not connected.
	// TODO: check if we can rely on the return bool value from AppFramework.GetDeviceResolution() instead.
	if (bHmdEnabled)
	{
		const FIntPoint& RTSize = GetIdealRenderTargetSize();
		MonitorDesc.MonitorName = "";
		MonitorDesc.MonitorId = 0;
		MonitorDesc.DesktopX = 0;
		MonitorDesc.DesktopY = 0;
		MonitorDesc.ResolutionX = RTSize.X;
		MonitorDesc.ResolutionY = RTSize.Y;
		return true;
	}
	else
	{
		MonitorDesc.MonitorName = "";
		MonitorDesc.MonitorId = 0;
		MonitorDesc.DesktopX = 0;
		MonitorDesc.DesktopY = 0;
		MonitorDesc.ResolutionX = 0;
		MonitorDesc.ResolutionY = 0;
		return false;
	}
}

void FMagicLeapHMD::GetFieldOfView(float& OutHFOVInDegrees, float& OutVFOVInDegrees) const
{
	FMagicLeapCustomPresent* const CP = GetActiveCustomPresent(true);
	if (CP)
	{
		CP->GetFieldOfView(OutHFOVInDegrees, OutVFOVInDegrees);
	}
}

void FMagicLeapHMD::SetPixelDensity(const float NewDensity)
{
	// Surface scale does not support > 1.0
	PixelDensity = FMath::Clamp(NewDensity, PixelDensityMin, 1.0f);
}

FIntPoint FMagicLeapHMD::GetIdealRenderTargetSize() const
{
	return DefaultRenderTargetSize;
}

bool FMagicLeapHMD::DoesSupportPositionalTracking() const
{
	return bHmdPosTracking;
}

bool FMagicLeapHMD::HasValidTrackingPosition()
{
	const FTrackingFrame& frame = GetCurrentFrame();
	return bHmdPosTracking ? frame.HasHeadTrackingPosition : false;
}

bool FMagicLeapHMD::GetTrackingSensorProperties(int32 DeviceId, FQuat& OutOrientation, FVector& OutPosition, FXRSensorProperties& OutSensorProperties)
{
	// Assuming there is only one tracker (the device itself) on the system being optically tracked
	const FTrackingFrame& frame = GetCurrentFrame();
	const float HalfHFOV = frame.HFov / 2.f;
	const float HalfVFOV = frame.VFov / 2.f;

	OutSensorProperties.TopFOV = HalfVFOV;
	OutSensorProperties.BottomFOV = HalfVFOV;

	OutSensorProperties.LeftFOV = HalfHFOV;
	OutSensorProperties.RightFOV = HalfHFOV;

	// TODO: set correct values here.
	OutSensorProperties.CameraDistance = 0.f;
	OutSensorProperties.NearPlane = 8.f;
	OutSensorProperties.FarPlane = 400.f; // Assumption, should get real numbers on this!

	return true;
}

void FMagicLeapHMD::SetInterpupillaryDistance(float NewInterpupillaryDistance)
{
	IPD = NewInterpupillaryDistance;
}

float FMagicLeapHMD::GetInterpupillaryDistance() const
{
	return IPD;
}

bool FMagicLeapHMD::IsChromaAbCorrectionEnabled() const
{
	return true;
}

bool FMagicLeapHMD::IsHeadTrackingAllowed() const
{
	const FTrackingFrame& frame = GetCurrentFrame();
#if WITH_EDITOR
	if (GIsEditor)
	{
		UEditorEngine* EdEngine = Cast<UEditorEngine>(GEngine);
		return ((!EdEngine || EdEngine->bUseVRPreviewForPlayWorld || GetDefault<ULevelEditorPlaySettings>()->ViewportGetsHMDControl) && GEngine->IsStereoscopic3D());
	}
#endif // WITH_EDITOR
	return (GEngine->IsStereoscopic3D());
}

void FMagicLeapHMD::ResetOrientationAndPosition(float yaw)
{
	ResetOrientation(yaw);
	// TODO: we don't reset position? Tecnically none of this should be required. Everything related to this topic should be handled by XRPawn.
}

void FMagicLeapHMD::ResetOrientation(float Yaw)
{
	const FTrackingFrame& frame = GetCurrentFrame();

	FRotator ViewRotation = frame.RawPose.Rotator();
	ViewRotation.Pitch = 0;
	ViewRotation.Roll = 0;

	if (Yaw != 0.f)
	{
		// apply optional yaw offset
		ViewRotation.Yaw -= Yaw;
		ViewRotation.Normalize();
	}
}

bool FMagicLeapHMD::EnumerateTrackedDevices(TArray<int32>& OutDevices, EXRTrackedDeviceType Type /*= EXRTrackedDeviceType::Any*/)
{
	//@todo:  Add controller tracking here
	if (Type == EXRTrackedDeviceType::Any || Type == EXRTrackedDeviceType::HeadMountedDisplay)
	{
		static const int32 DeviceId = IXRTrackingSystem::HMDDeviceId;
		OutDevices.Add(DeviceId);
		return true;
	}

	return false;
}

void FMagicLeapHMD::RefreshTrackingFrame()
{
#if WITH_MLSDK
	check(IsInGameThread());

	GameTrackingFrame.PixelDensity = PixelDensity;

	// get the frame id for the frame
	GameTrackingFrame.FrameId = HeadTrackerData.coord_frame_head;
	GameTrackingFrame.FrameNumber = GFrameCounter;

	// set the horizontal and vertical fov for this frame
	FMagicLeapCustomPresent* const CP = GetActiveCustomPresent(true);
	if (CP)
	{
		CP->GetFieldOfView(GameTrackingFrame.HFov, GameTrackingFrame.VFov);
	}

	// Release the snapshot of the previous frame.
	// This is done here instead of on end frame because modules implemented as input devices (Gestures, controller)
	// are ticked and fire their events before the OnStartGameFrame().
	MLResult Result = MLPerceptionReleaseSnapshot(GameTrackingFrame.Snapshot);
#if PLATFORM_LUMIN
	UE_CLOG(Result != MLResult_Ok, LogMagicLeap, Error, TEXT("MLPerceptionReleaseSnapshot failed with error %s."), UTF8_TO_TCHAR(MLGetResultString(Result)));
#endif //PLATFORM_LUMIN

	// get the snapshot for the frame
	Result = MLPerceptionGetSnapshot(&GameTrackingFrame.Snapshot);
#if PLATFORM_LUMIN
	UE_CLOG(Result != MLResult_Ok, LogMagicLeap, Error, TEXT("MLPerceptionGetSnapshot failed with error %s."), UTF8_TO_TCHAR(MLGetResultString(Result)));
#endif //PLATFORM_LUMIN

	MLHeadTrackingState state;
	bHeadTrackingStateAvailable = MLHeadTrackingGetState(HeadTracker, &state) == MLResult_Ok;
	if (bHeadTrackingStateAvailable)
	{
		HeadTrackingState.Mode = MLToUnrealHeadTrackingMode(state.mode);
		HeadTrackingState.Error = MLToUnrealHeadTrackingError(state.error);
		HeadTrackingState.Confidence = state.confidence;
	}

	// MLHandle because Unreal's uint64 is 'unsigned long long' whereas uint64_t for the C-API is 'unsigned long'
	MLHandle MapEventsFlags = 0;
	Result = MLHeadTrackingGetMapEvents(HeadTracker, &MapEventsFlags);
#if PLATFORM_LUMIN
	UE_CLOG(Result != MLResult_Ok, LogMagicLeap, Error, TEXT("MLHeadTrackingGetMapEvents failed with error %s."), UTF8_TO_TCHAR(MLGetResultString(Result)));
#endif //PLATFORM_LUMIN

	bHeadposeMapEventsAvailable = (Result == MLResult_Ok);
	if (bHeadposeMapEventsAvailable)
	{
		static TArray<MLHeadTrackingMapEvent> AllMLFlags({
			MLHeadTrackingMapEvent_Lost,
			MLHeadTrackingMapEvent_Recovered,
			MLHeadTrackingMapEvent_RecoveryFailed,
			MLHeadTrackingMapEvent_NewSession
		});

		HeadposeMapEvents.Empty();

		for (MLHeadTrackingMapEvent flag : AllMLFlags)
		{
			if ((MapEventsFlags & static_cast<MLHeadTrackingMapEvent>(flag)) != 0)
			{
				HeadposeMapEvents.Add(MLToUnrealHeadTrackingMapEvent(flag));
			}
		}
	}

	EMagicLeapTransformFailReason FailReason = EMagicLeapTransformFailReason::None;
	// get the raw pose and tracking status for the frame
	FTransform head_transform;
	GameTrackingFrame.HasHeadTrackingPosition = AppFramework.GetTransform(GameTrackingFrame.FrameId, head_transform, FailReason);
	if (GameTrackingFrame.HasHeadTrackingPosition)
	{
		GameTrackingFrame.RawPose = head_transform;
	}
	else if (FailReason == EMagicLeapTransformFailReason::NaNsInTransform)
	{
		UE_LOG(LogMagicLeap, Error, TEXT("NaNs in head transform."));
		GameTrackingFrame.RawPose = OldTrackingFrame.RawPose;
	}
	else
	{
		UE_CLOG(bIsPerceptionEnabled, LogMagicLeap, Warning, TEXT("Failed to get head tracking position: Reason = %i."), static_cast<int>(FailReason));
		GameTrackingFrame.RawPose = OldTrackingFrame.RawPose;
	}

	FVector CurrentPosition;
	FQuat CurrentOrientation;
	if (!GetCurrentPose(IXRTrackingSystem::HMDDeviceId, CurrentOrientation, CurrentPosition))
	{
		UE_LOG(LogMagicLeap, Warning, TEXT("Failed to get current pose."));
	}

	if (!FocusActor.IsValid())
	{
		if (StabilizationDepthActor.IsValid())
		{
			UE_LOG(LogMagicLeap, Verbose, TEXT("Focus actor not set. Defaulting depth to stabilization actor. Call the SetFocusActor() function to set a valid actor."));
			FocusActor = StabilizationDepthActor;
		}
		else
		{
			UE_LOG(LogMagicLeap, Verbose, TEXT("Neither the Focus actor not the Stabilization actor are set. Defaulting focus distance to 500.0 cm. Call the SetFocusActor() function to set a valid focus actor."));
		}
	}

	if (!StabilizationDepthActor.IsValid())
	{
		if (FocusActor.IsValid())
		{
			UE_LOG(LogMagicLeap, Verbose, TEXT("Stabilization depth actor not set. Defaulting depth to focus actor. Call the SetStabilizationDepthActor() function to set a valid actor."));
			StabilizationDepthActor = FocusActor;
		}
		else
		{
			UE_LOG(LogMagicLeap, Verbose, TEXT("Neither the Focus actor not the Stabilization actor are set. Defaulting depth to %f. Call the SetStabilizationDepthActor() function to set a valid actor."), GameTrackingFrame.FarClippingPlane);
		}
	}

	// If GNearClipPlane is changed by the app at runtime,
	// ensure we clamp the near clip to the value provided by ml_graphics.
	UpdateNearClippingPlane();

	const FTransform WorldToTracking = GetTrackingToWorldTransform().Inverse();

	const FVector FocusPoint = (FocusActor.IsValid()) ? WorldToTracking.TransformPosition(FocusActor->GetActorLocation()) : ((CurrentOrientation.GetForwardVector() * FMagicLeapHMD::DefaultFocusDistance) + CurrentPosition);
	GameTrackingFrame.FocusDistance = (FocusPoint - CurrentPosition).Size();
	// focus distance is within the range of NearClippingPlane  <= FocusDistance <= FarClippingPlane
	GameTrackingFrame.FocusDistance = FMath::Clamp(GameTrackingFrame.FocusDistance, GNearClippingPlane, GameTrackingFrame.FarClippingPlane);

	// We default to the same value as far clipping plane when actor is not valid
	const float StabilizationDepthDistance = (StabilizationDepthActor.IsValid()
		? (WorldToTracking.TransformPosition(StabilizationDepthActor->GetActorLocation()) - CurrentPosition).Size()
		: GameTrackingFrame.FarClippingPlane);

	// If set focus distance is valid (greater than near clipping plane), then we must check to see if
	// stabilization depth is within the range of FocusDistance <= StabilizationDepthDistance <= FarClippingPlane
	GameTrackingFrame.StabilizationDepth = FMath::Clamp(StabilizationDepthDistance, GameTrackingFrame.FocusDistance, GameTrackingFrame.FarClippingPlane);

	// NearClippingPlane <= FocusDistance <= StabilizationDepthDistance <= FarClippingPlane
	check(GNearClippingPlane <= GameTrackingFrame.FocusDistance);
	check(GameTrackingFrame.FocusDistance <= GameTrackingFrame.StabilizationDepth);
	check(GameTrackingFrame.StabilizationDepth <= GameTrackingFrame.FarClippingPlane);
#endif //WITH_MLSDK
}

#if WITH_MLSDK

EMagicLeapHeadTrackingError FMagicLeapHMD::MLToUnrealHeadTrackingError(MLHeadTrackingError error) const
{
	#define ERRCASE(x) case MLHeadTrackingError_##x: { return EMagicLeapHeadTrackingError::x; }
	switch(error)
	{
		ERRCASE(None)
		ERRCASE(NotEnoughFeatures)
		ERRCASE(LowLight)
		ERRCASE(Unknown)
	}
	return EMagicLeapHeadTrackingError::Unknown;
}

EMagicLeapHeadTrackingMode FMagicLeapHMD::MLToUnrealHeadTrackingMode(MLHeadTrackingMode mode) const
{
	switch(mode)
	{
		case MLHeadTrackingMode_6DOF: { return EMagicLeapHeadTrackingMode::PositionAndOrientation; }
		case MLHeadTrackingMode_Unavailable: { return EMagicLeapHeadTrackingMode::Unavailable; }
	}
	return EMagicLeapHeadTrackingMode::Unknown;
}

EMagicLeapHeadTrackingMapEvent FMagicLeapHMD::MLToUnrealHeadTrackingMapEvent(MLHeadTrackingMapEvent mapevent) const
{
	#define MAPEVENTCASE(x) case MLHeadTrackingMapEvent_##x: { return EMagicLeapHeadTrackingMapEvent::x; }
	switch(mapevent)
	{
		MAPEVENTCASE(Lost)
		MAPEVENTCASE(Recovered)
		MAPEVENTCASE(RecoveryFailed)
		MAPEVENTCASE(NewSession)
	}

	{
		checkNoEntry()
		return EMagicLeapHeadTrackingMapEvent::Lost;
	}
}
#endif //WITH_MLSDK

#if !PLATFORM_LUMIN
void FMagicLeapHMD::DisplayWarningIfVDZINotEnabled()
{
	// If VDZI is disabled, IsHMDConnected() will false, and the editor won't attempt to run in VR mode.
	// However, the editor still stores LastExecutedPlayModeType as PlayMode_InVR, which gives us a hint that
	// the user was attempting to run with VR mode, but neglected to enabled VDZI.
	// For game mode on the host platform, we can just check command-line and .ini settings to see if VR is enabled.

	bool VREnabled = false;
#if WITH_EDITOR
	if (GIsEditor)
	{
		VREnabled = (GetDefault<ULevelEditorPlaySettings>()->LastExecutedPlayModeType == PlayMode_InVR);
	}
	else
#endif
	{
		VREnabled = FParse::Param(FCommandLine::Get(), TEXT("vr")) || GetDefault<UGeneralProjectSettings>()->bStartInVR;
	}

#if WITH_MLSDK
	if (!bIsVDZIEnabled && !bVDZIWarningDisplayed && VREnabled)
	{
		FString message =
			"Zero Iteration must be enabled to work with VR mode, which can be done as follows:\n"
			"1) Load the editor.\n"
			"2) Go to 'Edit -> Project Settings...'\n"
			"3) Toggle the 'Enable Zero Iteration' option under the 'Magic Leap Plugin' category.\n"
			"4) Restart the editor or game.";
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(message));
		bVDZIWarningDisplayed = true;
	}
#endif //WITH_MLSDK
}
#endif

void FMagicLeapHMD::GetClipExtents()
{
#if WITH_MLSDK
	if (GraphicsClient != ML_INVALID_HANDLE)
	{
		// get the clip extents for clipping content in update thread
		MLGraphicsClipExtentsParams ClipExtentsParams;
		MLGraphicsClipExtentsParamsInit(&ClipExtentsParams);
		MLResult Result = MLGraphicsGetClipExtentsEx(GraphicsClient, &ClipExtentsParams, &GameTrackingFrame.UpdateInfoArray);
		if (Result != MLResult_Ok)
		{
			FString ErrorMesg = FString::Printf(TEXT("MLGraphicsGetClipExtentsEx failed with error %s"), UTF8_TO_TCHAR(MLGetResultString(Result)));

			// In case we're running under VD/ZI, there's always the risk of disconnects.
			// In those cases, the graphics API can return an error, but the client handle might still be valid.
			// So we need to ensure that we always have valid data to prevent any Nan-related errors.
			// On Lumin, we'll just assert.
#if PLATFORM_WINDOWS || PLATFORM_LINUX || PLATFORM_MAC
			GameTrackingFrame.FrameInfo.handle = ML_INVALID_HANDLE;
			MagicLeap::ResetClipExtentsInfoArray(GameTrackingFrame.UpdateInfoArray);
			UE_LOG(LogMagicLeap, Error, TEXT("%s"), *ErrorMesg);
#else
			UE_LOG(LogMagicLeap, Fatal, TEXT("%s"), *ErrorMesg);
#endif // PLATFORM_WINDOWS || PLATFORM_LINUX || PLATFORM_MAC
		}
	}
#endif // WITH_MLSDK
}

void FMagicLeapHMD::SetFrameTimingHint_RenderThread()
{
	check(IsInRenderingThread() || IsInRHIThread())
#if WITH_MLSDK
	FString EnumVal;
	GConfig->GetString(TEXT("/Script/LuminRuntimeSettings.LuminRuntimeSettings"), TEXT("FrameTimingHint"), EnumVal, GEngineIni);

	if (EnumVal.Len() > 0 && MLHandleIsValid(GraphicsClient))
	{
		const static UEnum* FrameTimingEnum = FindObject<UEnum>(ANY_PACKAGE, TEXT("ELuminFrameTimingHint"));
		check(FrameTimingEnum);

		const ELuminFrameTimingHint InFrameTimingHint = static_cast<ELuminFrameTimingHint>(FrameTimingEnum->GetValueByNameString(EnumVal));

		if (InFrameTimingHint != CurrentFrameTimingHint)
		{
			if (GraphicsClient != ML_INVALID_HANDLE)
			{
				MLGraphicsFrameTimingHint FTH = MLGraphicsFrameTimingHint_Unspecified;
				EngineGameThreadFPS = 0.0f;

				switch (InFrameTimingHint)
				{
				case ELuminFrameTimingHint::Unspecified:
					FTH = MLGraphicsFrameTimingHint_Unspecified;
					EngineGameThreadFPS = 0.0f;
					break;
				case ELuminFrameTimingHint::Maximum:
					FTH = MLGraphicsFrameTimingHint_Maximum;
					EngineGameThreadFPS = 122.0f;
					break;
				case ELuminFrameTimingHint::FPS_60:
					FTH = MLGraphicsFrameTimingHint_60Hz;
					EngineGameThreadFPS = 62.0f;
					break;
				case ELuminFrameTimingHint::FPS_120:
					FTH = MLGraphicsFrameTimingHint_120Hz;
					EngineGameThreadFPS = 122.0f;
					break;
				default:
					FTH = MLGraphicsFrameTimingHint_Unspecified;
					UE_LOG(LogMagicLeap, Warning, TEXT("Tried to set invalid Frame Timing Hint!  Defaulting to unspecified."));
				}

				MLResult Result = MLGraphicsSetFrameTimingHint(GraphicsClient, FTH);
				if (Result == MLResult_Ok)
				{
					UE_LOG(LogMagicLeap, Display, TEXT("Set Lumin frame timing hint to %s."), *FrameTimingEnum->GetNameStringByValue((int64)InFrameTimingHint));
					CurrentFrameTimingHint = InFrameTimingHint;
				}
				else
				{
					UE_LOG(LogMagicLeap, Log, TEXT("Failed to set Lumin frame timing hint to %s with error %s!"), *FrameTimingEnum->GetNameStringByValue((int64)InFrameTimingHint), UTF8_TO_TCHAR(MLGetResultString(Result)));
				}
			}
			else
			{
				UE_LOG(LogMagicLeap, Warning, TEXT("Failed to set Lumin frame timing hint.  Invalid context."));
			}
		}
	}
#endif //WITH_MLSDK
}

float FMagicLeapHMD::GetWorldToMetersScale() const
{
	return GetCurrentFrame().WorldToMetersScale;
}

bool FMagicLeapHMD::EnableStereo(bool bStereo)
{
	const bool bShouldStereo = IsHMDEnabled() ? bStereo : false;

#if !PLATFORM_LUMIN
	bStereoDesired = bShouldStereo;
#endif //!PLATFORM_LUMIN

#if WITH_EDITOR
	// We disable input globally for editor play as all input must come from the
	// Virtual Device / Zero Iteration system.
	//
	// NOTE: We do this here in addition to OnBeginPlay because the game viewport client
	// is not defined yet when the HMD begin play is invoked while doing PIE.
	//
	SetIgnoreInput(true);
#endif
	bStereoEnabled = bShouldStereo;

	// Uncap fps to enable FPS higher than 62
	GEngine->bForceDisableFrameRateSmoothing = bStereoEnabled;

	return bStereoEnabled;
}

bool FMagicLeapHMD::SetIgnoreInput(bool Ignore)
{
#if WITH_EDITOR
	UGameViewportClient* ViewportClient = GetGameViewportClient();
	// Change input settings only if running in the editor.
	// Without the GIsEditor check input doesnt work in "Play in Standalone Mode" since that uses the editor dlls itself.
	if (ViewportClient && GIsEditor)
	{
		bool Result = ViewportClient->IgnoreInput();
		ViewportClient->SetIgnoreInput(Ignore);
		if (DisableInputForBeginPlay && !Ignore)
		{
			// First time around we call this to disable the input globally. Hence we
			// also set mouse options. On subsequent calls we only set the input ignore flags.
			DisableInputForBeginPlay = false;
			ViewportClient->SetCaptureMouseOnClick(EMouseCaptureMode::NoCapture);
			ViewportClient->SetMouseLockMode(EMouseLockMode::DoNotLock);
			ViewportClient->SetHideCursorDuringCapture(false);
		}
		return Result;
	}
#endif
	return false;
}

void FMagicLeapHMD::AdjustViewRect(EStereoscopicPass StereoPass, int32& X, int32& Y, uint32& SizeX, uint32& SizeY) const
{
	if (DebugViewportWidth > 0)
	{
		SizeX = DebugViewportWidth * PixelDensity;
		SizeY = DebugViewportHeight * PixelDensity;
	}
	else
	{
		const FIntPoint& IdealRenderTargetSize = GetIdealRenderTargetSize();
		SizeX = FMath::CeilToInt(IdealRenderTargetSize.X * PixelDensity);
		SizeY = FMath::CeilToInt(IdealRenderTargetSize.Y * PixelDensity);
	}

	X = 0;
	Y = 0;

	SizeX = SizeX / 2;
	if (StereoPass == eSSP_RIGHT_EYE)
	{
		X += SizeX;
	}
}

FMatrix FMagicLeapHMD::GetStereoProjectionMatrix(const enum EStereoscopicPass StereoPassType) const
{
#if WITH_MLSDK
	// This function should only be called in game thread
	check(IsInGameThread());
	check(IsStereoEnabled());
	const int viewport = (StereoPassType == eSSP_LEFT_EYE) ? 0 : 1;
	const FTrackingFrame& frame = GetCurrentFrame();
	return bDeviceInitialized ? MagicLeap::ToUEProjectionMatrix(frame.UpdateInfoArray.virtual_camera_extents[viewport].projection, GNearClippingPlane) : FMatrix::Identity;
#else
	return FMatrix();
#endif //WITH_MLSDK
}

void FMagicLeapHMD::InitCanvasFromView(FSceneView* InView, UCanvas* Canvas)
{
}

void FMagicLeapHMD::UpdateViewportRHIBridge(bool /* bUseSeparateRenderTarget */, const class FViewport& InViewport, FRHIViewport* const ViewportRHI)
{
	// Since device initialization finishes on the render thread, we must assume here that the device will be initialized by the time the frame is presented.
	const bool bRequireDeviceIsInitialized = false;
	FMagicLeapCustomPresent* const CP = GetActiveCustomPresent(bRequireDeviceIsInitialized);
	if (CP)
	{
		CP->UpdateViewport(InViewport, ViewportRHI);
	}
}

bool FMagicLeapHMD::GetHeadTrackingState(FMagicLeapHeadTrackingState& State) const
{
	if (bHeadTrackingStateAvailable)
	{
		State = HeadTrackingState;
	}
	return bHeadTrackingStateAvailable;
}

bool FMagicLeapHMD::GetHeadTrackingMapEvents(TSet<EMagicLeapHeadTrackingMapEvent>& MapEvents) const
{
	if (bHeadposeMapEventsAvailable)
	{
		MapEvents = HeadposeMapEvents;
	}
	return bHeadposeMapEventsAvailable;
}

void FMagicLeapHMD::UpdateNearClippingPlane()
{
	GNearClippingPlane = (GameTrackingFrame.NearClippingPlane > GNearClippingPlane) ? GameTrackingFrame.NearClippingPlane : GNearClippingPlane;
}

FMagicLeapCustomPresent* FMagicLeapHMD::GetActiveCustomPresent(const bool bRequireDeviceIsInitialized) const
{
	if (bRequireDeviceIsInitialized && !bDeviceInitialized)
	{
		return nullptr;
	}

#if PLATFORM_WINDOWS || PLATFORM_LINUX || PLATFORM_LUMIN
	if (CustomPresentOpenGL)
	{
		return CustomPresentOpenGL;
	}
#endif // PLATFORM_WINDOWS || PLATFORM_LINUX || PLATFORM_LUMIN

#if PLATFORM_WINDOWS || PLATFORM_LUMIN
	if (CustomPresentVulkan)
	{
		return CustomPresentVulkan;
	}
#endif //PLATFORM_WINDOWS ||  PLATFORM_LUMIN

#if PLATFORM_MAC
	if (CustomPresentMetal)
	{
		return CustomPresentMetal;
	}
#endif // PLATFORM_MAC

	return nullptr;
}

void FMagicLeapHMD::CalculateRenderTargetSize(const class FViewport& Viewport, uint32& InOutSizeX, uint32& InOutSizeY)
{
	check(IsInGameThread());

	const FIntPoint& IdealRenderTargetSize = GetIdealRenderTargetSize();
	InOutSizeX = FMath::CeilToInt(IdealRenderTargetSize.X * PixelDensity);
	InOutSizeY = FMath::CeilToInt(IdealRenderTargetSize.Y * PixelDensity);
}

bool FMagicLeapHMD::NeedReAllocateViewportRenderTarget(const FViewport& Viewport)
{
	check(IsInGameThread());

	if (IsStereoEnabled())
	{
		const uint32 InSizeX = Viewport.GetSizeXY().X;
		const uint32 InSizeY = Viewport.GetSizeXY().Y;
		FIntPoint RenderTargetSize;
		RenderTargetSize.X = Viewport.GetRenderTargetTexture()->GetSizeX();
		RenderTargetSize.Y = Viewport.GetRenderTargetTexture()->GetSizeY();

		uint32 NewSizeX = InSizeX, NewSizeY = InSizeY;
		CalculateRenderTargetSize(Viewport, NewSizeX, NewSizeY);
		if (NewSizeX != RenderTargetSize.X || NewSizeY != RenderTargetSize.Y)
		{
			return true;
		}
	}
	return false;
}

bool FMagicLeapHMD::AllocateRenderTargetTexture(uint32 Index, uint32 SizeX, uint32 SizeY, uint8 Format, uint32 NumMips, uint32 Flags, uint32 TargetableTextureFlags, FTexture2DRHIRef& OutTargetableTexture, FTexture2DRHIRef& OutShaderResourceTexture, uint32 NumSamples)
{
	if (!IsStereoEnabled())
	{
		return false;
	}

	FRHIResourceCreateInfo CreateInfo;
	RHICreateTargetableShaderResource2D(SizeX, SizeY, PF_R8G8B8A8, 1, TexCreate_None, TexCreate_RenderTargetable, false, CreateInfo, OutTargetableTexture, OutShaderResourceTexture);

	return true;
}

FMagicLeapHMD::FMagicLeapHMD(IMagicLeapPlugin* InMagicLeapPlugin, IARSystemSupport* InARImplementation, bool bEnableVDZI, bool bUseVulkan) :
	// We don't do any mirroring on Lumin as we render direct to the device only.
	FHeadMountedDisplayBase(InARImplementation),
#if PLATFORM_LUMIN
	WindowMirrorMode(0),
#else
	WindowMirrorMode(1),
#endif
	DebugViewportWidth(0),
	DebugViewportHeight(0),
#if WITH_MLSDK
	GraphicsClient(ML_INVALID_HANDLE),
#endif //WITH_MLSDK
	bDeviceInitialized(0),
	bDeviceWasJustInitialized(0),
	bHmdEnabled(true),
#if PLATFORM_LUMIN
	bStereoEnabled(true),
#else
	bStereoEnabled(false),
	bStereoDesired(false),
#endif
	bIsRenderingPaused(false),
	bHmdPosTracking(true),
	bHaveVisionTracking(false),
	IPD(0.064f),
	DeltaControlRotation(FRotator::ZeroRotator),
#if WITH_MLSDK
	HeadTracker(ML_INVALID_HANDLE),
	HeadTrackerData{},
#endif //WITH_MLSDK
	RendererModule(nullptr),
	MagicLeapPlugin(InMagicLeapPlugin),
	PixelDensity(1.0f),
	bIsPlaying(false),
	bIsPerceptionEnabled(false),
	bIsVDZIEnabled(bEnableVDZI),
	bUseVulkanForZI(bUseVulkan),
	bVDZIWarningDisplayed(false),
#if PLATFORM_WINDOWS || PLATFORM_LINUX || PLATFORM_MAC
	ZIServerPingState(static_cast<int32>(EServerPingState::NotInitialized)),
#endif // PLATFORM_WINDOWS || PLATFORM_LINUX || PLATFORM_MAC
	bPrivilegesEnabled(false),
	CurrentFrameTimingHint(ELuminFrameTimingHint::Unspecified),
	EngineGameThreadFPS(0.0f),
	bQueuedGraphicsCreateCall(false),
	bHeadTrackingStateAvailable(false),
	bHeadposeMapEventsAvailable(false),
	SavedMaxFPS(0.0f),
	DefaultRenderTargetSize(1280 * 2, 960)
{
#if WITH_EDITOR
	World = nullptr;
	DisableInputForBeginPlay = false;
#endif
}

FMagicLeapHMD::~FMagicLeapHMD()
{
	Shutdown();
}

void FMagicLeapHMD::Startup()
{
	// grab a pointer to the renderer module for displaying our mirror window
	const FName RendererModuleName("Renderer");
	RendererModule = FModuleManager::GetModulePtr<IRendererModule>(RendererModuleName);

	// ALWAYS SET r.FinishCurrentFrame to false! Otherwise the perf might be poor.
	static const auto CFinishFrameVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.FinishCurrentFrame"));
	CFinishFrameVar->Set(false);

	// Uncap fps to enable FPS higher than 62
	GEngine->bForceDisableFrameRateSmoothing = bStereoEnabled;

	// Context must be created before the bridge so that the bridge can set the render api.
	AppFramework.Startup();

	// Set initial pixel density
	static const auto PixelDensityCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("vr.PixelDensity"));
	if (PixelDensityCVar)
	{
		PixelDensity = FMath::Clamp(PixelDensityCVar->GetFloat(), PixelDensityMin, PixelDensityMax);
	}

#if PLATFORM_WINDOWS || PLATFORM_LINUX || PLATFORM_LUMIN
	if (IsOpenGLPlatform(GMaxRHIShaderPlatform))
	{
		UE_LOG(LogMagicLeap, Display, TEXT("Creating FMagicLeapCustomPresentOpenGL"));
		CustomPresentOpenGL = new FMagicLeapCustomPresentOpenGL(this);
	}
#endif // PLATFORM_WINDOWS || PLATFORM_LINUX || PLATFORM_LUMIN

#if PLATFORM_WINDOWS || PLATFORM_LUMIN
	if (IsVulkanPlatform(GMaxRHIShaderPlatform))
	{
		UE_LOG(LogMagicLeap, Display, TEXT("Creating FMagicLeapCustomPresentVulkan"));
		CustomPresentVulkan = new FMagicLeapCustomPresentVulkan(this);
	}
#endif // PLATFORM_WINDOWS || PLATFORM_LUMIN

#if PLATFORM_MAC
	if (IsMetalPlatform(GMaxRHIShaderPlatform))
	{
		UE_LOG(LogMagicLeap, Display, TEXT("Creating FMagicLeapCustomPresentMetal"));
		CustomPresentMetal = new FMagicLeapCustomPresentMetal(this);
	}
#endif // PLATFORM_MAC

	UE_LOG(LogMagicLeap, Log, TEXT("MagicLeap initialized."));
}

void FMagicLeapHMD::Shutdown()
{
	FMagicLeapHMD* Plugin = this;
	ENQUEUE_RENDER_COMMAND(ShutdownRendering)(
		[Plugin](FRHICommandListImmediate& RHICmdList)
		{
			Plugin->ShutdownRendering();
		});
	FlushRenderingCommands();

	ReleaseDevice();

	// IXRTrackingSystem::OnEndPlay() gets called only in the Editor.
	// This was causing the input trackers, header tracker and perception client to not be shutdown on the device resulting in the app not exiting cleanly.
	// Thus, we make an explicit call to function here.
	DisableDeviceFeatures();

	AppFramework.Shutdown();
}

void FMagicLeapHMD::LoadFromIni()
{
	const TCHAR* MagicLeapSettings = TEXT("MagicLeapSettings");
	// We don't do any mirroring on Lumin as we render direct to the device only.
#if !PLATFORM_LUMIN
	int32 WindowMirrorModeValue;

	if (GConfig->GetInt(MagicLeapSettings, TEXT("WindowMirrorMode"), WindowMirrorModeValue, GEngineIni))
	{
		WindowMirrorMode = WindowMirrorModeValue;
	}
#endif
}

void FMagicLeapHMD::SaveToIni()
{
	const TCHAR* MagicLeapSettings = TEXT("MagicLeapSettings");
	// We don't do any mirroring on Lumin as we render direct to the device only.
#if !PLATFORM_LUMIN
	GConfig->SetInt(MagicLeapSettings, TEXT("WindowMirrorMode"), WindowMirrorMode, GEngineIni);
#endif
}

class FSceneViewport* FMagicLeapHMD::FindSceneViewport()
{
	if (!GIsEditor)
	{
		UGameEngine* GameEngine = Cast<UGameEngine>(GEngine);
		return GameEngine->SceneViewport.Get();
	}
#if WITH_EDITOR
	else
	{
		UEditorEngine* EditorEngine = Cast<UEditorEngine>(GEngine);
		return (FSceneViewport*)(EditorEngine->GetPIEViewport());
	}
#endif
	return nullptr;
}

void FMagicLeapHMD::OnBeginPlay(FWorldContext& InWorldContext)
{
#if WITH_EDITOR
	InWorldContext.AddRef(World);
	DisableInputForBeginPlay = true;
	// We disable input globally for editor play as all input must come from the
	// Virtual Device / Zero Iteration system.
	SetIgnoreInput(true);
#endif

	// Reset CurrentFrameTimingHint so that proper value from config can be updated after device is initialized.
	CurrentFrameTimingHint = ELuminFrameTimingHint::Unspecified;

	EnableDeviceFeatures();
}

void FMagicLeapHMD::OnEndPlay(FWorldContext& InWorldContext)
{
#if WITH_EDITOR
	InWorldContext.RemoveRef(World);
#endif
	DisableDeviceFeatures();
}

void FMagicLeapHMD::EnableDeviceFeatures()
{
	bIsPlaying = true;
	if (GIsEditor)
	{
		InitDevice();
	}

#if !PLATFORM_LUMIN
	DisplayWarningIfVDZINotEnabled();
#endif

	// When run on a non-target platform, the VDZI may not necessarily be initialized.
	// In this case, just skip these steps since their timeouts may cause the game to appear to hang.
	if (IsHMDConnected())
	{
		EnablePrivileges();
		EnablePerception();
		EnableHeadTracking();
		EnableTrackerEntities();

		// We also avoid enabling the custom profile when there's no HMD, as otherwise
		// we get the profile effects on non-vr-preview rendering.
		EnableLuminProfile();
	}
}

void FMagicLeapHMD::DisableDeviceFeatures()
{
	AppFramework.OnApplicationShutdown();
	RestoreBaseProfile();
	DisableTrackerEntities();
	DisableHeadTracking();
	DisablePerception();
	DisablePrivileges();
	if (GIsEditor)
	{
		ReleaseDevice();
	}
	bIsPlaying = false;
	bVDZIWarningDisplayed = false;
}


void FMagicLeapHMD::InitDevice_RenderThread()
{
#if WITH_MLSDK
	if (bQueuedGraphicsCreateCall)
	{
		UE_LOG(LogMagicLeap, Warning, TEXT("Graphics client create call already queued."));
		return;
	}

	if (!bDeviceInitialized)
	{
		bool bDeviceSuccessfullyInitialized = false;
		// Unreal supports sRGB which is the default we are requesting from graphics as well now.
		MLGraphicsOptions gfx_opts;
		gfx_opts.graphics_flags = 0; //MLGraphicsFlags_DebugMode;
		gfx_opts.color_format = MLSurfaceFormat_RGBA8UNormSRGB;
		gfx_opts.depth_format = MLSurfaceFormat_D32Float;

		gfx_opts.graphics_flags = MLGraphicsFlags_Default;

#if PLATFORM_WINDOWS
		if (IsPCPlatform(GMaxRHIShaderPlatform) && !IsOpenGLPlatform(GMaxRHIShaderPlatform))
		{
			bDeviceSuccessfullyInitialized = true;
			//UE_LOG(LogMagicLeap, Error, TEXT("FMagicLeapCustomPresentD3D11 is not supported."));
		}
#endif // PLATFORM_WINDOWS

#if PLATFORM_MAC
		if (IsMetalPlatform(GMaxRHIShaderPlatform) && !IsOpenGLPlatform(GMaxRHIShaderPlatform))
		{
			bQueuedGraphicsCreateCall = true;

			ExecuteOnRHIThread_DoNotWait([this, gfx_opts]()
			{
				UE_LOG(LogMagicLeap, Display, TEXT("FMagicLeapCustomPresentMetal is supported."));

				FMetalDynamicRHI* MetalDynamicRHI = static_cast<FMetalDynamicRHI*>(GDynamicRHI);
                check(MetalDynamicRHI != nullptr);

				id<MTLDevice> MetalDevice = (id<MTLDevice>)MetalDynamicRHI->RHIGetNativeDevice();

				MLGraphicsOptions Options = gfx_opts;

				MLRemoteGraphicsCreateClientMTLParams MTLParams;
				MLRemoteGraphicsCreateClientMTLParamsInit(&MTLParams);
				MTLParams.options = &Options;
				MTLParams.metal_device = &MetalDevice;

				GraphicsClient = ML_INVALID_HANDLE;
				MLResult Result = MLResult_Ok;

				Result = MLRemoteGraphicsCreateClientMTL(&MTLParams, &GraphicsClient);
				if (Result == MLResult_Ok)
				{
					InitializeClipExtents_RenderThread();
					SetFrameTimingHint_RenderThread();
				}
				else
				{
					GraphicsClient = ML_INVALID_HANDLE;
					UE_LOG(LogMagicLeap, Error, TEXT("MLGraphicsCreateClientVk failed with status %s"), UTF8_TO_TCHAR(MLGetResultString(Result)));
				}

				FPlatformAtomics::InterlockedExchange(&bDeviceInitialized, Result == MLResult_Ok);
				FPlatformAtomics::InterlockedExchange(&bDeviceWasJustInitialized, Result == MLResult_Ok);
				bQueuedGraphicsCreateCall = false;
			});
		}
#endif // PLATFORM_MAC

#if PLATFORM_WINDOWS || PLATFORM_LINUX || PLATFORM_LUMIN
		if (IsOpenGLPlatform(GMaxRHIShaderPlatform))
		{
			UE_LOG(LogMagicLeap, Display, TEXT("FMagicLeapCustomPresentOpenGL is supported."));
			MLHandle ContextHandle;
			auto OpenGLRHI = static_cast<FOpenGLDynamicRHI*>(GDynamicRHI);
			ContextHandle = reinterpret_cast<MLHandle>(OpenGLRHI->GetOpenGLCurrentContextHandle());
			MLResult Result = MLGraphicsCreateClientGL(&gfx_opts, ContextHandle, &GraphicsClient);
			if (Result == MLResult_Ok)
			{
				bDeviceSuccessfullyInitialized = true;
				InitializeClipExtents_RenderThread();
				SetFrameTimingHint_RenderThread();
			}
			else
			{
				bDeviceSuccessfullyInitialized = false;
				GraphicsClient = ML_INVALID_HANDLE;
				UE_LOG(LogMagicLeap, Error, TEXT("MLGraphicsCreateClientGL failed with status %s"), UTF8_TO_TCHAR(MLGetResultString(Result)));
			}
		}
		if (IsVulkanPlatform(GMaxRHIShaderPlatform))
		{
#if PLATFORM_WINDOWS || PLATFORM_LUMIN
			static const auto* VulkanRHIThread = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Vulkan.RHIThread"));
			UE_LOG(LogMagicLeap, Log, TEXT("RHI Thread Usage (r.Vulkan.RHIThread)=%d"), VulkanRHIThread->GetValueOnAnyThread());

			bQueuedGraphicsCreateCall = true;

			ExecuteOnRHIThread_DoNotWait([this, gfx_opts]()
			{
				UE_LOG(LogMagicLeap, Display, TEXT("FMagicLeapCustomPresentVulkan is supported."));
				FVulkanDynamicRHI* VulkanDynamicRHI = (FVulkanDynamicRHI*)GDynamicRHI;
				uint64 Instance = VulkanRHIBridge::GetInstance(VulkanDynamicRHI);
				FVulkanDevice* VulkanDevice = VulkanRHIBridge::GetDevice(VulkanDynamicRHI);
				uint64 PhysicalDevice = VulkanRHIBridge::GetPhysicalDevice(VulkanDevice);
				uint64 LogicalDevice = VulkanRHIBridge::GetLogicalDevice(VulkanDevice);
				GraphicsClient = ML_INVALID_HANDLE;
				MLResult Result = MLResult_Ok;

				Result = MLGraphicsCreateClientVk(&gfx_opts, (VkInstance)Instance, (VkPhysicalDevice)PhysicalDevice, (VkDevice)LogicalDevice, &GraphicsClient);
				if (Result == MLResult_Ok)
				{
					InitializeClipExtents_RenderThread();
					SetFrameTimingHint_RenderThread();
				}
				else
				{
					GraphicsClient = ML_INVALID_HANDLE;
					UE_LOG(LogMagicLeap, Error, TEXT("MLGraphicsCreateClientVk failed with status %s"), UTF8_TO_TCHAR(MLGetResultString(Result)));
				}

				FPlatformAtomics::InterlockedExchange(&bDeviceInitialized, Result == MLResult_Ok);
				FPlatformAtomics::InterlockedExchange(&bDeviceWasJustInitialized, Result == MLResult_Ok);
				bQueuedGraphicsCreateCall = false;
			});
#endif // PLATFORM_WINDOWS || PLATFORM_LUMIN
		}
		else
		{
			FPlatformAtomics::InterlockedExchange(&bDeviceInitialized, bDeviceSuccessfullyInitialized);
			FPlatformAtomics::InterlockedExchange(&bDeviceWasJustInitialized, bDeviceSuccessfullyInitialized);
		}
#endif // PLATFORM_WINDOWS || PLATFORM_LINUX || PLATFORM_LUMIN
	}
#endif //WITH_MLSDK
}

void FMagicLeapHMD::InitDevice()
{
	if (!bDeviceInitialized)
	{
		// If the HMD is not connected don't bother initializing the render device since the VDZI graphics calls freeze the editor if the VDZI server is not running.
		if (IsHMDConnected())
		{
			// Enable HMD as it could have been disabled on a previous VRPreview failure
			EnableHMD(true);

			// Save the default MaxFPS value to be restored when we shutdown VRPreview.
			SavedMaxFPS = GEngine->GetMaxFPS();

			FMagicLeapHMD* This = this;
			ENQUEUE_RENDER_COMMAND(InitDevice)(
				[This](FRHICommandList& RHICmdList)
			{
				This->InitDevice_RenderThread();
			}
			);
		}
		else
		{
			bDeviceInitialized = 1;
			bDeviceWasJustInitialized = 1;
			// Disable HMD and Stereo rendering if the device is not connected.
			// This fixes the render target size and view rect for standalone desktop builds.
			EnableHMD(false);
		}
	}

	if (bDeviceWasJustInitialized)
	{
		FSceneViewport* SceneVP = FindSceneViewport();
		if (SceneVP && SceneVP->IsStereoRenderingAllowed())
		{
			// This init must happen on the main thread for VR preview, otherwise it crashes on a non-Lumin RHI.

			// Save any runtime configuration changes from the .ini.
			LoadFromIni();

			// Moved here from SetFrameTimingHint() because it crashes on non-game threads.
			if (GEngine != nullptr)
			{
				// Don't rely on MLGraphicsBeginFrame to manage app's frame rate cadence because when the device goes into standby mode
				// we skip calls to it which then causes the render thread to run at it's max pace.
				GEngine->SetMaxFPS(EngineGameThreadFPS);
			}

			// VD/ZI works best in windowed mode since it can sometimes be used in conjunction with the mock ml1 device's window.
#if PLATFORM_LUMIN
			EWindowMode::Type WindowMode = EWindowMode::Fullscreen;
#else
			EWindowMode::Type WindowMode = EWindowMode::Windowed;
#endif

			if (bHmdEnabled)
			{
				const FIntPoint& RTSize = GetIdealRenderTargetSize();
				DebugViewportWidth = RTSize.X;
				DebugViewportHeight = RTSize.Y;
				FSystemResolution::RequestResolutionChange(RTSize.X, RTSize.Y, WindowMode);
				FPlatformAtomics::InterlockedExchange(&bDeviceWasJustInitialized, 0);
			}
			else
			{
				// If HMD is not enabled, set bDeviceWasJustInitialized to false so the device resolution code is not run every frame.
				FPlatformAtomics::InterlockedExchange(&bDeviceWasJustInitialized, 0);				
			}
		}
	}
}

void FMagicLeapHMD::ReleaseDevice()
{
	check(IsInGameThread());

	//Disable the HMD, reverting flags for potential followup in PIE 
	EnableHMD(false);

	// save any runtime configuration changes to the .ini
	SaveToIni();

	// Restore original max fps after shutting down VRPreview.
	if (GEngine != nullptr)
	{
		GEngine->SetMaxFPS(SavedMaxFPS);
	}

	FMagicLeapHMD* Plugin = this;
	ENQUEUE_RENDER_COMMAND(ReleaseDevice_RT)(
		[Plugin](FRHICommandListImmediate& RHICmdList)
		{
			Plugin->ReleaseDevice_RenderThread();
		}
	);

	// Wait for all resources to be released
	FlushRenderingCommands();
}

void FMagicLeapHMD::ReleaseDevice_RenderThread()
{
	check(IsInRenderingThread());

	// Do not check for SceneViewport here because it does not work for all platforms.
	// This is because of slightly different order of operations. Just check the flag.
	if (bDeviceInitialized)
	{
		FPlatformAtomics::InterlockedExchange(&bDeviceInitialized, 0);

#if PLATFORM_WINDOWS || PLATFORM_LUMIN
		if (CustomPresentOpenGL)
		{
			CustomPresentOpenGL->Reset();
		}
		if (CustomPresentVulkan)
		{
			CustomPresentVulkan->Reset();
		}
#elif PLATFORM_LINUX
		if (CustomPresentOpenGL)
		{
			CustomPresentOpenGL->Reset();
		}
#elif PLATFORM_MAC
		if (CustomPresentMetal)
		{
			CustomPresentMetal->Reset();
		}
#endif

		ExecuteOnRHIThread([this]()
		{
#if WITH_MLSDK
			if (MLHandleIsValid(GraphicsClient))
			{
				MLResult Result = MLGraphicsDestroyClient(&GraphicsClient);
				UE_CLOG(Result != MLResult_Ok, LogMagicLeap, Error, TEXT("MLGraphicsDestroyClient failed with status %s"), UTF8_TO_TCHAR(MLGetResultString(Result)));
				UE_CLOG(Result == MLResult_Ok, LogMagicLeap, Display, TEXT("Graphics client destroyed successfully."));
			}

			GraphicsClient = ML_INVALID_HANDLE;
#endif //WITH_MLSDK
		});
	}
}

bool FMagicLeapHMD::GetCurrentPose(int32 DeviceId, FQuat& OutOrientation, FVector& OutPosition)
{
	const FTrackingFrame& Frame = GetCurrentFrame();
	OutOrientation = Frame.RawPose.GetRotation();
	OutPosition = Frame.RawPose.GetLocation();

	return true;
}

bool FMagicLeapHMD::GetRelativeEyePose(int32 DeviceId, EStereoscopicPass Eye, FQuat& OutOrientation, FVector& OutPosition)
{
#if WITH_MLSDK
	OutOrientation = FQuat::Identity;
	OutPosition = FVector::ZeroVector;
	if (DeviceId == IXRTrackingSystem::HMDDeviceId && (Eye == eSSP_LEFT_EYE || Eye == eSSP_RIGHT_EYE))
	{
		const FTrackingFrame& Frame = GetCurrentFrame();
		const int EyeIdx = (Eye == eSSP_LEFT_EYE) ? 0 : 1;

		const FTransform EyeToWorld = MagicLeap::ToFTransform(Frame.FrameInfo.virtual_camera_info_array.virtual_cameras[EyeIdx].transform, Frame.WorldToMetersScale);		// "world" here means the HMDs tracking space
		const FTransform EyeToHMD = EyeToWorld * Frame.RawPose.Inverse();		// RawPose is HMDToWorld
		OutPosition = EyeToHMD.GetTranslation();
		OutOrientation = EyeToHMD.GetRotation();

		return true;
	}

#endif //WITH_MLSDK
	return false;
}

void FMagicLeapHMD::GetEyeRenderParams_RenderThread(const FRenderingCompositePassContext& Context, FVector2D& EyeToSrcUVScaleValue, FVector2D& EyeToSrcUVOffsetValue) const
{
	check(bDeviceInitialized);
	check(IsInRenderingThread());

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

void FMagicLeapHMD::OnBeginRendering_GameThread()
{
	check(IsInGameThread());

	RefreshTrackingFrame();
	FTrackingFrame& TrackingFrame = GetCurrentFrameMutable();
	if (TrackingFrame.WorldContext != nullptr)
	{
		RefreshTrackingToWorldTransform(*(TrackingFrame.WorldContext));
	}

#if WITH_MLSDK
	// Copy the game tracking frame to the render frame.
	// Since we don't flush the render commands here, we copy the game frame thrice:
	// 1st copy when enqueuing the command
	// 2nd copy on the render thread during the command execution
	ExecuteOnRenderThread_DoNotWait([this, TrackingFrameCopy = GameTrackingFrame]() 
	{
		MLSnapshot* OldSnapshot = RenderTrackingFrame.Snapshot;
		// Don't update RenderTrackingFrame here. It is updated by the RHITrackingFrame in FMagicLeapCustomPreset::BeginRendering()
		// RenderTrackingFrame = TrackingFrameCopy;
		ExecuteOnRHIThread_DoNotWait([this, TrackingFrameCopy]()
		{
			RHITrackingFrame = TrackingFrameCopy;
		});
	});
#endif //WITH_MLSDK

	// Update the trackers
	static_cast<FMagicLeapPlugin*>(&IMagicLeapPlugin::Get())->OnBeginRendering_GameThread();
}

TSharedPtr<class IXRCamera, ESPMode::ThreadSafe> FMagicLeapHMD::GetXRCamera(int32 DeviceId /*= HMDDeviceId*/)
{
	check(DeviceId == HMDDeviceId);
	if (!XRCamera.IsValid())
	{
		XRCamera = FSceneViewExtensions::NewExtension<FMagicLeapXRCamera>(*this, DeviceId);
	}
	return XRCamera;
}

void FMagicLeapHMD::OnBeginRendering_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneViewFamily& ViewFamily)
{
	check(IsInRenderingThread());

	GetClipExtents();

	if (GetActiveCustomPresent())
	{
		GetActiveCustomPresent()->BeginRendering();
	}

}

void FMagicLeapHMD::RenderTexture_RenderThread(class FRHICommandListImmediate& RHICmdList, class FRHITexture2D* BackBuffer, class FRHITexture2D* SrcTexture, FVector2D WindowSize) const
{
#if WITH_MLSDK
	check(IsInRenderingThread());

	if (GetActiveCustomPresent() != nullptr)
	{
		// If custom present wants to render using Unreal's pipeline instead of direct blit or texture copy functions
		GetActiveCustomPresent()->RenderToMLSurfaces_RenderThread(RHICmdList, SrcTexture);
	}

	// TODO: Refactor the code below. FMagicLeapCustomPresent::RenderToTextureSlice_RenderThread() is very similar.

	// If we aren't mirroring there's nothing to do as the actual render on device
	// happens in the custom presenter.
	if (WindowMirrorMode > 0)
	{
		SCOPED_DRAW_EVENT(RHICmdList, HMD_RenderTexture);

		// The debug viewport is the mirror window (if any).
		const uint32 ViewportWidth = BackBuffer->GetSizeX();
		const uint32 ViewportHeight = BackBuffer->GetSizeY();
		// The source texture is the two eye side-by-side render.
		const uint32 TextureWidth = SrcTexture->GetSizeX();
		const uint32 TextureHeight = SrcTexture->GetSizeY();

		// The BackBuffer is the debug view for mirror modes, i.e. vr-preview. In which case
		// it can be an arbitrary size different than the render size. Which means
		// we scale to that BackBuffer size, with either a letter-box or pill-box to
		// maintain aspect ratio.
		const uint32 SourceWidth = WindowMirrorMode == 1 ? TextureWidth / 2 : TextureWidth;
		const uint32 SourceHeight = TextureHeight;
		const float LetterboxScale = static_cast<float>(ViewportWidth) / static_cast<float>(SourceWidth);
		const float PillarboxScale = static_cast<float>(ViewportHeight) / static_cast<float>(SourceHeight);
		const float BlitScale = FMath::Min(LetterboxScale, PillarboxScale);
		const uint32 BlitWidth = static_cast<float>(SourceWidth) * BlitScale;
		const uint32 BlitHeight = static_cast<float>(SourceHeight) * BlitScale;
		const uint32 QuadX = (ViewportWidth - BlitWidth) * 0.5f;
		const uint32 QuadY = (ViewportHeight - BlitHeight) * 0.5f;

		FRHIRenderPassInfo RPInfo(BackBuffer, ERenderTargetActions::Load_Store);
		RHICmdList.BeginRenderPass(RPInfo, TEXT("MagicLeap_RenderTexture"));
		{

			DrawClearQuad(RHICmdList, FLinearColor(0.0f, 0.0f, 0.0f, 1.0f));
			RHICmdList.SetViewport(QuadX, QuadY, 0, BlitWidth + QuadX, BlitHeight + QuadY, 1.0f);

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

			if (WindowMirrorMode == 1)
			{
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
			}
			else if (WindowMirrorMode == 2)
			{
				RendererModule->DrawRectangle(
					RHICmdList,
					0, 0,
					ViewportWidth, ViewportHeight,
					0.0f, 0.0f,
					1.0f, 1.0f,
					FIntPoint(ViewportWidth, ViewportHeight),
					FIntPoint(1, 1),
					*VertexShader,
					EDRF_Default);
			}
		}
		RHICmdList.EndRenderPass();
	}
#endif //WITH_MLSDK
}

void FMagicLeapHMD::SetClippingPlanes(float NCP, float FCP)
{
	check(IsInGameThread());
	FTrackingFrame& frame = GetCurrentFrameMutable();
	frame.FarClippingPlane = (frame.RecommendedFarClippingPlane < FCP) ? frame.RecommendedFarClippingPlane : FCP;
	GNearClippingPlane = NCP;
	UpdateNearClippingPlane();
}

void FMagicLeapHMD::ShutdownRendering()
{
	check(IsInRenderingThread());
#if PLATFORM_WINDOWS || PLATFORM_LINUX || PLATFORM_LUMIN
	if (CustomPresentOpenGL.GetReference())
	{
		CustomPresentOpenGL->Reset();
		CustomPresentOpenGL->Shutdown();
		CustomPresentOpenGL = nullptr;
	}
#endif // PLATFORM_WINDOWS || PLATFORM_LINUX || PLATFORM_LUMIN
#if PLATFORM_WINDOWS || PLATFORM_LUMIN
	if (CustomPresentVulkan.GetReference())
	{
		CustomPresentVulkan->Reset();
		CustomPresentVulkan->Shutdown();
		CustomPresentVulkan = nullptr;
	}
#endif // PLATFORM_WINDOWS || PLATFORM_LUMIN
#if PLATFORM_MAC
	if (CustomPresentMetal.GetReference())
	{
		CustomPresentMetal->Reset();
		CustomPresentMetal->Shutdown();
		CustomPresentMetal = nullptr;
	}
#endif // PLATFORM_MAC
}

FTrackingFrame& FMagicLeapHMD::GetCurrentFrameMutable()
{
	if (IsInRHIThread())
	{
		return RHITrackingFrame;
	}
	if (IsInRenderingThread())
	{
		return RenderTrackingFrame;
	}
	else
	{
		return GameTrackingFrame;
	}
}

const FTrackingFrame& FMagicLeapHMD::GetCurrentFrame() const
{
	if (IsInRHIThread())
	{
		return RHITrackingFrame;
	}
	if (IsInRenderingThread())
	{
		return RenderTrackingFrame;
	}
	else
	{
		return GameTrackingFrame;
	}
}

const FTrackingFrame& FMagicLeapHMD::GetOldFrame() const
{
	check(IsInGameThread());
	return OldTrackingFrame;
}

void FMagicLeapHMD::InitializeOldFrameFromRenderFrame()
{
	if (IsInRHIThread())
	{
		OldTrackingFrame = RHITrackingFrame;
	}
	else if (IsInRenderingThread())
	{
		OldTrackingFrame = RenderTrackingFrame;
	}
}

void FMagicLeapHMD::InitializeRenderFrameFromRHIFrame()
{
	RenderTrackingFrame = RHITrackingFrame;
}

const FAppFramework& FMagicLeapHMD::GetAppFrameworkConst() const
{
	return AppFramework;
}

FAppFramework& FMagicLeapHMD::GetAppFramework()
{
	return AppFramework;
}

void FMagicLeapHMD::SetFocusActor(const AActor* InFocusActor, bool bSetStabilizationActor)
{
	FocusActor = InFocusActor;
	if (bSetStabilizationActor)
	{
		StabilizationDepthActor = InFocusActor;
	}
}

void FMagicLeapHMD::SetStabilizationDepthActor(const AActor* InStabilizationDepthActor, bool bSetFocusActor)
{
	StabilizationDepthActor = InStabilizationDepthActor;
	if (bSetFocusActor)
	{
		FocusActor = InStabilizationDepthActor;
	}
}

bool FMagicLeapHMD::IsPerceptionEnabled() const
{
	return bIsPerceptionEnabled;
}

void FMagicLeapHMD::EnableLuminProfile()
{
	if (!GIsEditor)
	{
		// We only need to enable, and hence disable, the profile while doing vr-preview. Which
		// only is relevant while we are in the editor.
		return;
	}

	UDeviceProfileManager& ProfileManager = UDeviceProfileManager::Get();
	UDeviceProfile* Profile = ProfileManager.FindProfile("Lumin");
	UDeviceProfile* ActiveProfile = ProfileManager.GetActiveProfile();
	bool ShouldEnable =
		Profile &&
		(Profile != ActiveProfile) &&
		!BaseProfileState.bSaved;

	if (ShouldEnable)
	{
		for (const FString& CVarEntry : Profile->CVars)
		{
			FString CVarKey;
			FString CVarValue;
			TMap<FString, FString> ValidCVars;
			if (CVarEntry.Split(TEXT("="), &CVarKey, &CVarValue))
			{
				ValidCVars.Add(CVarKey, CVarValue);

				IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(*CVarKey);
				if (CVar)
				{
					FString PreviousValue = CVar->GetString();
					BaseProfileState.CVarState.Add(CVarKey, PreviousValue);
					CVar->Set(*CVarValue);
				}
			}
		}

#if WITH_EDITOR
		UEditorPerformanceSettings* Settings = GetMutableDefault<UEditorPerformanceSettings>();
		check(Settings);
		BaseProfileState.bCPUThrottleEnabled = Settings->bThrottleCPUWhenNotForeground;
		Settings->bThrottleCPUWhenNotForeground = false;
		Settings->PostEditChange();
		Settings->SaveConfig();
#endif

		BaseProfileState.bSaved = true;
	}
}

void FMagicLeapHMD::RestoreBaseProfile()
{
	//if we're quitting, we shouldn't be restoring a profile
	if (!GIsRunning)
	{
		return;
	}

	if (!GIsEditor)
	{
		// We only need to enable, and hence disable, the profile while doing vr-preview. Which
		// only is relevant while we are in the editor.
		return;
	}

	if (BaseProfileState.bSaved)
	{
#if WITH_EDITOR
		UEditorPerformanceSettings* Settings = GetMutableDefault<UEditorPerformanceSettings>();
		check(Settings);
		Settings->bThrottleCPUWhenNotForeground = BaseProfileState.bCPUThrottleEnabled;
		Settings->PostEditChange();
		Settings->SaveConfig();
#endif

		for (auto& It : BaseProfileState.CVarState)
		{
			IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(*It.Key);
			if (CVar)
			{
				CVar->Set(*It.Value);
			}
		}

		BaseProfileState.bSaved = false;
		BaseProfileState.CVarState.Empty();
	}
}

void FMagicLeapHMD::EnablePrivileges()
{
#if WITH_MLSDK
	MLResult Result = MLPrivilegesStartup();
	bPrivilegesEnabled = (Result == MLResult_Ok);
	UE_CLOG(!bPrivilegesEnabled, LogMagicLeap, Error, TEXT("MLPrivilegesStartup() "
		"failed with error %s"), UTF8_TO_TCHAR(MLPrivilegesGetResultString(Result)));
#endif // WITH_MLSDK
}

void FMagicLeapHMD::DisablePrivileges()
{
#if WITH_MLSDK
	if (bPrivilegesEnabled)
	{
		MLResult Result = MLPrivilegesShutdown();
		UE_CLOG(Result != MLResult_Ok, LogMagicLeap, Error, TEXT("MLPrivilegesShutdown() "
			"failed with error %s"), UTF8_TO_TCHAR(MLPrivilegesGetResultString(Result)));
	}
#endif // WITH_MLSDK
}

void FMagicLeapHMD::EnableTrackerEntities()
{
#if WITH_MLSDK
	if (GameTrackingFrame.Snapshot == nullptr)
	{
		RefreshTrackingFrame();
	}
#endif // WITH_MLSDK
	static_cast<FMagicLeapPlugin*>(&IMagicLeapPlugin::Get())->EnableTrackerEntities();
}

void FMagicLeapHMD::DisableTrackerEntities()
{
	static_cast<FMagicLeapPlugin*>(&IMagicLeapPlugin::Get())->DisableTrackerEntities();
}

void FMagicLeapHMD::EnablePerception()
{
#if WITH_MLSDK
	if (!bIsPerceptionEnabled)
	{
		MLPerceptionSettings perception_settings;
		MLResult Result = MLPerceptionInitSettings(&perception_settings);
		if (Result == MLResult_Ok)
		{
			Result = MLPerceptionStartup(&perception_settings);
			if (Result == MLResult_Ok)
			{
				bIsPerceptionEnabled = true;
			}
			else
			{
				UE_LOG(LogMagicLeap, Error, TEXT("MLPerceptionStartup failed with error %s."), UTF8_TO_TCHAR(MLGetResultString(Result)));
			}
		}
		else
		{
			UE_LOG(LogMagicLeap, Error, TEXT("MLPerceptionInitSettings failed with error %s."), UTF8_TO_TCHAR(MLGetResultString(Result)));
		}
	}
#endif //WITH_MLSDK
}

void FMagicLeapHMD::DisablePerception()
{
#if WITH_MLSDK
	if (bIsPerceptionEnabled)
	{
		MLResult Result = MLPerceptionShutdown();
		if (Result == MLResult_Ok)
		{
			bIsPerceptionEnabled = false;
			UE_LOG(LogMagicLeap, Display, TEXT("Perception client shutdown successfully."));
		}
		else
		{
			UE_LOG(LogMagicLeap, Error, TEXT("MLPerceptionShutdown failed with error %s"), UTF8_TO_TCHAR(MLGetResultString(Result)));
		}
	}
#endif //WITH_MLSDK
}

void FMagicLeapHMD::EnableHeadTracking()
{
#if WITH_MLSDK
	if (HeadTracker == ML_INVALID_HANDLE)
	{
		MLResult Result = MLHeadTrackingCreate(&HeadTracker);
		if (Result == MLResult_Ok && HeadTracker != ML_INVALID_HANDLE)
		{
			if (MLResult_Ok != MLHeadTrackingGetStaticData(HeadTracker, &HeadTrackerData))
			{
				UE_LOG(LogMagicLeap, Error, TEXT("MLHeadTrackingGetStaticData failed with error %s."), UTF8_TO_TCHAR(MLGetResultString(Result)));
			}
		}
		else
		{
			UE_LOG(LogMagicLeap, Error, TEXT("MLHeadTrackingCreate failed with error %s."), UTF8_TO_TCHAR(MLGetResultString(Result)));
		}
	}
#endif //WITH_MLSDK
}

void FMagicLeapHMD::DisableHeadTracking()
{
#if WITH_MLSDK
	if (HeadTracker != ML_INVALID_HANDLE)
	{
		MLResult Result = MLHeadTrackingDestroy(HeadTracker);
		UE_CLOG(Result != MLResult_Ok, LogMagicLeap, Error, TEXT("MLHeadTrackingDestroy failed with error %s."), UTF8_TO_TCHAR(MLGetResultString(Result)));
		HeadTracker = ML_INVALID_HANDLE;

		// Reset HeadTracking flags in case game calls GetHeadTrackingState()/GetHeadTrackingMapEvents() after head tracking is disabled. Also helps to reset the values for next VRPreview run.
		bHeadTrackingStateAvailable = false;
		bHeadposeMapEventsAvailable = false;
	}
#endif //WITH_MLSDK
}

void FMagicLeapHMD::InitializeClipExtents_RenderThread()
{
#if WITH_MLSDK
	MLGraphicsRenderTargetsInfo RenderTargetInfo;
	MLResult Result = MLGraphicsGetRenderTargets(GraphicsClient, &RenderTargetInfo);
	if (Result == MLResult_Ok)
	{
		GameTrackingFrame.NearClippingPlane = RenderTargetInfo.min_clip * GameTrackingFrame.WorldToMetersScale;
		GameTrackingFrame.RecommendedFarClippingPlane = RenderTargetInfo.max_clip * GameTrackingFrame.WorldToMetersScale;
		UpdateNearClippingPlane();

		DefaultRenderTargetSize.X = 0;
		for (uint32 i = 0; i < 2 && i < RenderTargetInfo.num_virtual_cameras; ++i)
		{
			DefaultRenderTargetSize.X += RenderTargetInfo.buffers[i].color.width;
			DefaultRenderTargetSize.Y = RenderTargetInfo.buffers[i].color.height;
		}
	}
	else
	{
		UE_LOG(LogMagicLeap, Error, TEXT("MLGraphicsGetRenderTargets failed with error %s"), UTF8_TO_TCHAR(MLGetResultString(Result)));
	}

	GetClipExtents();

	// TODO Apply snapshot head pose to all the update transforms because graphics does not apply pose
	// But we currently use the last frame render transforms so this does not need to be done just yet
#endif //WITH_MLSDK
}

#if WITH_EDITOR
UGameViewportClient * FMagicLeapHMD::GetGameViewportClient()
{
	return World ? World->GetGameViewport() : nullptr;
}

FMagicLeapHMD* FMagicLeapHMD::GetHMD()
{
	return static_cast<FMagicLeapHMD*>(GEngine->XRSystem->GetHMDDevice());
}
#endif

#undef LOCTEXT_NAMESPACE
