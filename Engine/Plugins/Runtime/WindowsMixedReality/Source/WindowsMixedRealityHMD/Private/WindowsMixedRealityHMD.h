// Copyright (c) Microsoft Corporation. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "HeadMountedDisplayBase.h"
#include "XRTrackingSystemBase.h"
#include "SceneViewExtension.h"
#include "WindowsMixedRealityFunctionLibrary.h"
#include "DefaultXRCamera.h"

#if !PLATFORM_HOLOLENS
#include "Windows/WindowsApplication.h"
#endif
#if PLATFORM_HOLOLENS
#include "HoloLens/HoloLensApplication.h"
#endif

#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerInput.h"

#include "WindowsMixedRealityStatics.h"
#include "WindowsMixedRealityCustomPresent.h"

#include "XRRenderTargetManager.h"
#include "RendererInterface.h"

#if WITH_WINDOWS_MIXED_REALITY
#include "MixedRealityInterop.h"
#endif

DECLARE_LOG_CATEGORY_EXTERN(LogWmrHmd, Log, All);

#define eSSP_THIRD_CAMERA_EYE 3

namespace WindowsMixedReality
{
	// Plugin for stereo rendering on Windows Mixed Reality devices.
	class WINDOWSMIXEDREALITYHMD_API FWindowsMixedRealityHMD
		: public FHeadMountedDisplayBase
		, public FXRRenderTargetManager
		, public FSceneViewExtensionBase
	{
	public:

		/** IXRTrackingSystem interface */
		virtual FName GetSystemName() const override
		{
			static FName DefaultName(TEXT("WindowsMixedRealityHMD"));
			return DefaultName;
		}
		virtual int32 GetXRSystemFlags() const override
		{
			int32 XRFlags = EXRSystemFlags::IsHeadMounted;
			if (!UWindowsMixedRealityFunctionLibrary::IsDisplayOpaque())
			{
				XRFlags |= EXRSystemFlags::IsAR;
			}

			if (FWindowsMixedRealityStatics::OnGetXRSystemFlagsDelegate.IsBound())
			{
				FWindowsMixedRealityStatics::OnGetXRSystemFlagsDelegate.Broadcast(XRFlags);
			}

			return XRFlags;
		}

		virtual FString GetVersionString() const override;

		virtual void OnBeginPlay(FWorldContext& InWorldContext) override;
		virtual void OnEndPlay(FWorldContext& InWorldContext) override;
		virtual bool OnStartGameFrame(FWorldContext& WorldContext) override;
		virtual void SetTrackingOrigin(EHMDTrackingOrigin::Type NewOrigin) override;
		virtual EHMDTrackingOrigin::Type GetTrackingOrigin() const override;

		virtual bool EnumerateTrackedDevices(
			TArray<int32>& OutDevices,
			EXRTrackedDeviceType Type = EXRTrackedDeviceType::Any) override;

		virtual void SetInterpupillaryDistance(float NewInterpupillaryDistance) override;
		virtual float GetInterpupillaryDistance() const override;

		virtual void ResetOrientationAndPosition(float yaw = 0.f) override;
		virtual void ResetOrientation(float yaw = 0.f) override { }
		virtual void ResetPosition() override { }

		virtual void OnBeginRendering_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneViewFamily& ViewFamily) override;
		virtual void OnBeginRendering_GameThread() override;

		virtual bool GetCurrentPose(
			int32 DeviceId,
			FQuat& CurrentOrientation,
			FVector& CurrentPosition) override;
		virtual bool GetRelativeEyePose(int32 DeviceId, EStereoscopicPass Eye, FQuat& OutOrientation, FVector& OutPosition) override;

		virtual class IHeadMountedDisplay* GetHMDDevice() override
		{
			return this;
		}

		virtual class TSharedPtr< class IStereoRendering, ESPMode::ThreadSafe > GetStereoRenderingDevice() override
		{
			return SharedThis(this);
		}

		// Tracking status
		virtual bool DoesSupportPositionalTracking() const override { return true; }
		virtual bool HasValidTrackingPosition() override;

		virtual void GetMotionControllerData(UObject* WorldContext, const EControllerHand Hand, FXRMotionControllerData& MotionControllerData) override;

		virtual bool ConfigureGestures(const FXRGestureConfig& GestureConfig) override;

	protected:
		/** FXRTrackingSystemBase protected interface */
		virtual float GetWorldToMetersScale() const override;

	private:
		int gameWindowWidth = 1920;
		int gameWindowHeight = 1080;

	public:
		/** IHeadMountedDisplay interface */
		virtual bool IsHeadTrackingAllowed() const override;
		virtual bool IsHMDConnected() override;
		virtual bool IsHMDEnabled() const override;
		virtual EHMDWornState::Type GetHMDWornState() override;
		virtual void EnableHMD(bool allow = true) override { }
		virtual bool GetHMDMonitorInfo(MonitorInfo&) override { return true; }
		virtual void GetFieldOfView(
			float& OutHFOVInDegrees,
			float& OutVFOVInDegrees) const override { }
		virtual bool IsChromaAbCorrectionEnabled() const override { return false; }

		/** IStereoRendering interface */
		virtual bool IsStereoEnabled() const override;
		virtual bool EnableStereo(bool stereo = true) override;
		virtual void AdjustViewRect(
			EStereoscopicPass StereoPass,
			int32& X, int32& Y,
			uint32& SizeX, uint32& SizeY) const override;
		virtual FMatrix GetStereoProjectionMatrix(const enum EStereoscopicPass StereoPassType) const override;
		virtual IStereoRenderTargetManager* GetRenderTargetManager() override { return this; }
		virtual class IStereoLayers* GetStereoLayers() override;

		virtual int32 GetDesiredNumberOfViews(bool bStereoRequested) const override
		{
			return (bStereoRequested) ?
#if PLATFORM_HOLOLENS && PLATFORM_CPU_ARM_FAMILY //third cam possible for Hololens device only
				3
#else
				2
#endif
				: 1;
		}

		virtual EStereoscopicPass GetViewPassForIndex(bool bStereoRequested, uint32 ViewIndex) const override
		{
			if (!bStereoRequested)
				return EStereoscopicPass::eSSP_FULL;

			return static_cast<EStereoscopicPass>(eSSP_LEFT_EYE + ViewIndex);
		}

		virtual uint32 GetViewIndexForPass(EStereoscopicPass StereoPassType) const override
		{
			switch (StereoPassType)
			{
			case eSSP_LEFT_EYE:
			case eSSP_FULL:
				return 0;

			case eSSP_RIGHT_EYE:
				return 1;

			default:
				return StereoPassType - eSSP_LEFT_EYE;
			}
		}

		virtual bool HasHiddenAreaMesh() const override;
		virtual void DrawHiddenAreaMesh_RenderThread(FRHICommandList& RHICmdList, EStereoscopicPass StereoPass) const override;

		virtual bool HasVisibleAreaMesh() const override;
		virtual void DrawVisibleAreaMesh_RenderThread(FRHICommandList& RHICmdList, EStereoscopicPass StereoPass) const override;

		// Spectator screen Hooks.
		virtual FIntRect GetFullFlatEyeRect_RenderThread(FTexture2DRHIRef EyeTexture) const override;
		// Helper to copy one render target into another for spectator screen display
		virtual void CopyTexture_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture2D* SrcTexture, FIntRect SrcRect, FRHITexture2D* DstTexture, FIntRect DstRect, bool bClearBlack, bool bNoAlpha) const override;

		/** ISceneViewExtension interface */
		virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override;
		virtual void SetupView(
			FSceneViewFamily& InViewFamily,
			FSceneView& InView) override { }
		virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override { }
		virtual void PreRenderView_RenderThread(
			FRHICommandListImmediate& RHICmdList,
			FSceneView& InView) override;
		virtual void PreRenderViewFamily_RenderThread(
			FRHICommandListImmediate& RHICmdList,
			FSceneViewFamily& InViewFamily) override { }
		virtual bool IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const;

		void CreateHMDDepthTexture(FRHICommandListImmediate& RHICmdList);

		void SetFocusPointForFrame(FVector Position);
		void SetFocustPointForFrame_RenderThread(FVector TrackingSpacePosition);
	public:
#if WITH_WINDOWS_MIXED_REALITY
		FWindowsMixedRealityHMD(const FAutoRegister&, IARSystemSupport* InARSystem, MixedRealityInterop* InHMD);
#endif
		virtual ~FWindowsMixedRealityHMD();
		bool IsInitialized() const;

		void InitializeHolographic();
		void ShutdownHolographic();

		bool IsCurrentlyImmersive();
		bool IsDisplayOpaque();

	private:
		void StartCustomPresent();

		TRefCountPtr<ID3D11Device> InternalGetD3D11Device();
#if WITH_WINDOWS_MIXED_REALITY
		MixedRealityInterop* HMD = nullptr;
#endif

		bool bIsStereoEnabled = false;
		bool bIsStereoDesired = true;

		bool bRequestRestart = false;
		bool bRequestShutdown = false;

		bool bIsMobileMultiViewEnabled = false;

		float ScreenScalePercentage = 1.0f;
		float CachedWorldToMetersScale = 100.0f;

		TRefCountPtr<ID3D11Device> D3D11Device = nullptr;

		FTexture2DRHIRef remappedDepthTexture = nullptr;
		ID3D11Texture2D* stereoDepthTexture = nullptr;
		// For third camera
		ID3D11Texture2D* monoDepthTexture = nullptr;

		bool bNeedReallocateDepthTexture = false;
		FTexture2DRHIRef CurrentBackBuffer;
		FTexture2DRHIRef CurrentDepthBuffer;
		void InitTrackingFrame();
		TRefCountPtr<FWindowsMixedRealityCustomPresent> mCustomPresent = nullptr;

		FIntRect EyeRenderViewport;

		struct Frame
		{
			FQuat HeadOrientation = FQuat::Identity;
			FVector HeadPosition = FVector::ZeroVector;
			FQuat RotationL = FQuat::Identity;
			FQuat RotationR = FQuat::Identity;
			FVector PositionL = FVector::ZeroVector;
			FVector PositionR = FVector::ZeroVector;
			FTransform LeftTransform = FTransform::Identity;
			FTransform RightTransform = FTransform::Identity;
			FTransform HeadTransform = FTransform::Identity;
			FTransform ThirdCameraTransform = FTransform::Identity;
			FMatrix ProjectionMatrixR = FMatrix::Identity;
			FMatrix ProjectionMatrixL = FMatrix::Identity;
			FMatrix ProjectionMatrixThirdCamera = FMatrix::Identity;
			bool bPositionalTrackingUsed = false;

		};
		Frame Frame_NextGameThread;
		FCriticalSection Frame_NextGameThreadLock;
		Frame Frame_GameThread;
		Frame Frame_RenderThread;
		Frame& GetFrame() { return IsInRenderingThread() ? Frame_RenderThread : Frame_GameThread; }
		const Frame& GetFrame() const { return IsInRenderingThread() ? Frame_RenderThread : Frame_GameThread; }

		float ipd = 0;

		TArray<FHMDViewMesh> HiddenAreaMesh;
		TArray<FHMDViewMesh> VisibleAreaMesh;

		void StopCustomPresent();

		void SetupHolographicCamera();

		// Inherited via FXRRenderTargetManager
		virtual void GetEyeRenderParams_RenderThread(
			const struct FRenderingCompositePassContext& Context,
			FVector2D& EyeToSrcUVScaleValue,
			FVector2D& EyeToSrcUVOffsetValue) const override;
		virtual FIntPoint GetIdealRenderTargetSize() const override;
		virtual float GetPixelDenity() const override;
		virtual void SetPixelDensity(const float NewDensity) override;
		virtual void UpdateViewportRHIBridge(bool bUseSeparateRenderTarget, const class FViewport& Viewport, FRHIViewport* const ViewportRHI) override;
		virtual void RenderTexture_RenderThread
		(
			class FRHICommandListImmediate & RHICmdList,
			class FRHITexture2D * BackBuffer,
			class FRHITexture2D * SrcTexture,
			FVector2D WindowSize
		) const override;
		virtual bool AllocateRenderTargetTexture(
			uint32 index,
			uint32 sizeX, uint32 sizeY,
			uint8 format,
			uint32 numMips,
			ETextureCreateFlags flags,
			ETextureCreateFlags TargetableTextureFlags,
			FTexture2DRHIRef& outTargetableTexture,
			FTexture2DRHIRef& outShaderResourceTexture,
			uint32 numSamples = 1) override;

		virtual bool NeedReAllocateDepthTexture(const TRefCountPtr<IPooledRenderTarget>& DepthTarget) override;

		virtual bool AllocateDepthTexture(
			uint32 Index,
			uint32 SizeX,
			uint32 SizeY,
			uint8 Format,
			uint32 NumMips,
			ETextureCreateFlags InTexFlags,
			ETextureCreateFlags TargetableTextureFlags,
			FTexture2DRHIRef& OutTargetableTexture,
			FTexture2DRHIRef& OutShaderResourceTexture,
			uint32 NumSamples = 1) override;

		virtual bool GetHMDDistortionEnabled(EShadingPath ShadingPath) const override
		{
			return false;
		}

		virtual bool ShouldUseSeparateRenderTarget() const override
		{
			return IsStereoEnabled();
		}

	private:
		// Handle app suspend requests.
		FDelegateHandle PauseHandle;
		FDelegateHandle ResumeHandle;

		void AppServicePause();
		void AppServiceResume();

		void StartSpeechRecognition();
		void StopSpeechRecognition();

		IRendererModule* RendererModule = nullptr;

		EHMDWornState::Type currentWornState = EHMDWornState::Type::Unknown;
		bool mouseLockedToCenter = true;

	public:
		// Spatial input
		bool IsAvailable();
		bool SupportsSpatialInput();
#if WITH_WINDOWS_MIXED_REALITY
		HMDTrackingStatus GetControllerTrackingStatus(HMDHand hand);
		bool SupportsHandTracking();
		bool SupportsHandedness();
		bool GetControllerOrientationAndPosition(HMDHand hand, FRotator & OutOrientation, FVector & OutPosition, float WorldScale = 1);
		bool GetHandJointOrientationAndPosition(HMDHand hand, HMDHandJoint joint, FRotator& OutOrientation, FVector& OutPosition, float& OutRadius);
		bool PollInput();
		bool PollHandTracking();

		HMDInputPressState GetPressState(
			HMDHand hand,
			HMDInputControllerButtons button,
			bool onlyRegisterClicks = true);
		float GetAxisPosition(
			HMDHand hand,
			HMDInputControllerAxes axis);
		void SubmitHapticValue(
			HMDHand hand,
			float value);
		bool QueryCoordinateSystem(ABI::Windows::Perception::Spatial::ISpatialCoordinateSystem *& pCoordinateSystem);
		bool IsTrackingAvailable();
		virtual bool IsTracking(int32 DeviceId) override;

		void GetPointerPose(EControllerHand hand, PointerPoseInfo& pi);
#endif
		void LockMouseToCenter(bool locked)
		{
			mouseLockedToCenter = locked;
		}

	public:

		virtual EXRDeviceConnectionResult::Type ConnectRemoteXRDevice(const FString& IpAddress, const int32 BitRate) override;
		virtual void DisconnectRemoteXRDevice() override;

		// Remoting
		EXRDeviceConnectionResult::Type ConnectToRemoteHoloLens(const wchar_t* ip, unsigned int bitrate, bool isHoloLens1, unsigned int port=0, bool listen=false);
		void DisconnectFromRemoteHoloLens();
	private:
#if WITH_WINDOWS_MIXED_REALITY
		void UpdateRemotingStatus();
		uint32 HandlerId = 0;
		struct RemotingConnectionDesc
		{
			FString ip;
			unsigned int bitrate;
			bool isHoloLens1;
			unsigned int port;
			bool listen;
		};
		bool isRemotingReconnecting = false;
		RemotingConnectionDesc RemotingDesc;
		HMDRemotingConnectionState prevState = HMDRemotingConnectionState::Undefined;
		void OnConnectionEvent(WindowsMixedReality::MixedRealityInterop::ConnectionEvent evt);
#endif

	public:
#if WITH_WINDOWS_MIXED_REALITY

	public:
		// Speech Recognition
		SpeechRecognizerInterop* CreateSpeechRecognizer()
		{
			return new SpeechRecognizerInterop();
		}
#endif

	private:
		void CreateSpectatorScreenController();

		//At runtime, EnableStereo can call FWindowsMixedRealityStatics::ConnectToRemoteHoloLens, but in the editor FWindowsMixedRealityStatics::ConnectToRemoteHoloLens calls EnableStereo.  This is to prevent a stack overflow
		static bool bEnableStereoReentranceGuard;
	};
}
