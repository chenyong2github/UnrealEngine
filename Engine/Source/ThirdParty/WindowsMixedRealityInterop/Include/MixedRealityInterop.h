// Copyright (c) Microsoft Corporation. All rights reserved.

#pragma once

#ifdef __UNREAL__
#include "Windows/AllowWindowsPlatformTypes.h"
#pragma warning(push)
#endif

#if !PLATFORM_HOLOLENS
#ifdef MIXEDREALITYINTEROP_EXPORTS
#define MIXEDREALITYINTEROP_API __declspec(dllexport)
#else
#define MIXEDREALITYINTEROP_API __declspec(dllimport)
#endif
#else
#define MIXEDREALITYINTEROP_API
#endif

#pragma warning(disable:4668)
#pragma warning(disable:4005)  

//wrap for non-windows platforms
#if !__UNREAL__ || (PLATFORM_HOLOLENS || PLATFORM_WINDOWS)
#include <Windows.h>
#include <string>

#include <d3d11.h>

#include <DirectXMath.h>

#include <spatialaudioclient.h>

#include <functional>
#endif

#pragma warning(default:4005)
#pragma warning(default:4668)

//forward declaration from windows.ui.input.spatial.h
namespace ABI {
	namespace Windows {
		namespace Perception {
			namespace Spatial {
				interface ISpatialCoordinateSystem;
			} /* Spatial */
		} /* Perception */
	} /* Windows */
} /* ABI */


/**
 * A struct telling UE4 about the update. Also allows UE4 to allocate the buffers
 * to copy vertices/indices into (avoids one copy)
 */
struct MIXEDREALITYINTEROP_API MeshUpdate
{
	GUID Id;

	/** Location of this mesh in UE4 world space */
	float Translation[3] = { 0.f, 0.f, 0.f };
	/** Quaternion rotation. Requires normalization on the UE4 side before use */
	float Rotation[4] = { 0.f, 0.f, 0.f, 0.f };
	/** Scale for this mesh */
	float Scale[3] = { 0.f, 0.f, 0.f };

	/** If this is zero, there were no mesh changes */
	int NumVertices = 0;
	/** The vertex array for this mesh in UE4 local space */
	void* Vertices = nullptr;
	/** If this is zero, there were no mesh changes */
	int NumIndices = 0;
	/** The indices for the mesh */
	void* Indices = nullptr;
};

// QR code data structure to pass data back to UE4
struct MIXEDREALITYINTEROP_API QRCodeData
{
	GUID				Id;

	/** Location of this QR code in UE4 world space */
	float Translation[3] = { 0.f, 0.f, 0.f };
	/** Quaternion rotation of this QR code - requires normalization on the UE4 side before use */
	float Rotation[4] = { 0.f, 0.f, 0.f, 1.f };

	/** Version number of the QR code */
	int32				Version;
	/** Physical width and height of the QR code in meters (all QR codes are square) */
	float				SizeInMeters;
	/** Timestamp in seconds of the last time this QR code was seen */
	float				LastSeenTimestamp;
	/** Size in wchar_t's of the QR code's data string */
	uint32				DataSize;
	/** Data string embedded in the QR code */
	wchar_t*			Data;
};

namespace WindowsMixedReality
{
	enum class HMDEye
	{
		Left = 0,
		Right = 1
	};

	enum class HMDTrackingOrigin
	{
		Eye,
		Floor
	};

	enum class HMDHand
	{
		Left = 0,
		Right = 1,
		AnyHand = 2
	};

	enum class HMDTrackingStatus
	{
		NotTracked,
		InertialOnly,
		Tracked
	};

	enum class HMDLayerType
	{
		WorldLocked,
		FaceLocked
	};

	// Match EHMDWornState
	enum class UserPresence
	{
		Unknown,
		Worn,
		NotWorn
	};

	enum class HMDInputPressState
	{
		NotApplicable = 0,
		Pressed = 1,
		Released = 2
	};

	enum class HMDInputControllerButtons
	{
		Select,
		Grasp,
		Menu,
		Thumbstick,
		Touchpad,
		TouchpadIsTouched
	};

	enum class HMDInputControllerAxes
	{
		SelectValue,
		ThumbstickX,
		ThumbstickY,
		TouchpadX,
		TouchpadY
	};

	enum class HMDHandJoint
	{
		Palm = 0,
		Wrist = 1,
		ThumbMetacarpal = 2,
		ThumbProximal = 3,
		ThumbDistal = 4,
		ThumbTip = 5,
		IndexMetacarpal = 6,
		IndexProximal = 7,
		IndexIntermediate = 8,
		IndexDistal = 9,
		IndexTip = 10,
		MiddleMetacarpal = 11,
		MiddleProximal = 12,
		MiddleIntermediate = 13,
		MiddleDistal = 14,
		MiddleTip = 15,
		RingMetacarpal = 16,
		RingProximal = 17,
		RingIntermediate = 18,
		RingDistal = 19,
		RingTip = 20,
		LittleMetacarpal = 21,
		LittleProximal = 22,
		LittleIntermediate = 23,
		LittleDistal = 24,
		LittleTip = 25
	};

	static const int NumHMDHandJoints = 26;

	enum class GestureStage : unsigned int { Started, Updated, Completed, Canceled };
	enum class SourceKind : unsigned int { Other = 0, Hand = 1, Voice = 2, Controller = 3 };
	enum class SourceState : unsigned int { Detected, Lost };

	enum class HMDSpatialLocatability : unsigned int
	{
		Unavailable = 0,
		OrientationOnly = 1,
		PositionalTrackingActivating = 2,
		PositionalTrackingActive = 3,
		PositionalTrackingInhibited = 4,
	};

	struct EyeGazeRay
	{
		DirectX::XMFLOAT3 origin;
		DirectX::XMFLOAT3 direction;
	};

	struct PointerPoseInfo
	{
		DirectX::XMFLOAT3 origin;
		DirectX::XMFLOAT3 direction;
		DirectX::XMFLOAT3 up;
		DirectX::XMFLOAT4 orientation;
	};

	class MIXEDREALITYINTEROP_API SpeechRecognizerInterop
	{
	private:
		int id;

	public:
		SpeechRecognizerInterop();
		~SpeechRecognizerInterop()
		{
			StopSpeechRecognition();
		}

		void AddKeyword(const wchar_t* keyword, std::function<void()> callback);
		void StartSpeechRecognition();
		void StopSpeechRecognition();
	};

	class MIXEDREALITYINTEROP_API GestureRecognizerInterop
	{
	private:
		int id;

	public:

		struct Tap 
		{ 
			int Count; 
			HMDHand Hand;
		};

		struct Hold
		{
			HMDHand Hand;
		};

		struct Manipulation
		{
			DirectX::XMFLOAT3  Delta;
			HMDHand Hand;
		};

		struct Navigation
		{
			DirectX::XMFLOAT3  NormalizedOffset;
			HMDHand Hand;
		};

		struct SourceStateDesc
		{
			HMDHand Hand;
		};

		enum GestureSettings
		{
			NavigationX = 0x10,
			NavigationY = 0x20,
			NavigationZ = 0x40,
			NavigationRailsX = 0x80,
			NavigationRailsY = 0x100,
			NavigationRailsZ = 0x200,
		};


		typedef std::function<void(SourceState, SourceKind, const SourceStateDesc&)> SourceStateCallback;

		typedef std::function<void(GestureStage, SourceKind, const Tap&)> TapCallback;
		typedef std::function<void(GestureStage, SourceKind, const Hold&)> HoldCallback;
		typedef std::function<void(GestureStage, SourceKind, const Manipulation&)> ManipulationCallback;
		typedef std::function<void(GestureStage, SourceKind, const Navigation&)> NavigationCallback;

		GestureRecognizerInterop();
		~GestureRecognizerInterop();

		bool SubscribeSourceStateChanges(SourceStateCallback callback);
		bool SubscribeInteration(std::function<void()> callback);
		void Reset();

		bool SubscribeTap(TapCallback callback);
		bool SubscribeHold(HoldCallback callback);
		bool SubscribeManipulation(ManipulationCallback callback);
		bool SubscribeNavigation(NavigationCallback callback, unsigned int settings);
	};

	class MIXEDREALITYINTEROP_API MixedRealityInterop
	{
	public:
		MixedRealityInterop();
		~MixedRealityInterop() {}

		UINT64 GraphicsAdapterLUID();

		void Initialize(ID3D11Device* device, float nearPlane = 0.001f, float farPlane = 650.0f);
		void Dispose(bool force = false);
		bool IsStereoEnabled();
		bool IsTrackingAvailable();
		void ResetOrientationAndPosition();

		bool IsInitialized();
		bool IsImmersiveWindowValid();
		bool IsAvailable();
		bool IsCurrentlyImmersive();
		void EnableStereo(bool enableStereo);

		bool HasUserPresenceChanged();
		UserPresence GetCurrentUserPresence();

		void CreateHiddenVisibleAreaMesh();

		bool IsDisplayOpaque();
		bool GetDisplayDimensions(int& width, int& height);
		const wchar_t* GetDisplayName();

		bool IsActiveAndValid();

		void BlockUntilNextFrame();
		void UpdateRenderThreadFrame();

		// Get the latest pose information from our tracking frame.
		bool GetCurrentPoseRenderThread(DirectX::XMMATRIX& leftView, DirectX::XMMATRIX& rightView, HMDTrackingOrigin& trackingOrigin);
		bool QueryCoordinateSystem(ABI::Windows::Perception::Spatial::ISpatialCoordinateSystem *& pCoordinateSystem, HMDTrackingOrigin& trackingOrigin);
		
		DirectX::XMFLOAT4X4 GetProjectionMatrix(HMDEye eye);
		bool GetHiddenAreaMesh(HMDEye eye, DirectX::XMFLOAT2*& vertices, int& length);
		bool GetVisibleAreaMesh(HMDEye eye, DirectX::XMFLOAT2*& vertices, int& length);

		void SetScreenScaleFactor(float scale);
		
		int32 GetMaxQuadLayerCount() const;

		uint32 AddQuadLayer(
			uint32 Id,
			ID3D11Texture2D* quadLayerTexture, 
			float widthM, float heightM,
			DirectX::XMFLOAT3 position,
			DirectX::XMFLOAT4 rotation,
			DirectX::XMFLOAT3 scale,
			HMDLayerType layerType,
			bool preserveAspectRatio,
			int priority);

		void RemoveQuadLayer(uint32 Id);

		// Use double-width stereo texture for the depth texture or nullptr to ignore.
		bool CreateRenderingParameters(ID3D11Texture2D* depthTexture);

		// Use double-width stereo texture for the viewport texture.
		bool Present(ID3D11DeviceContext* context, ID3D11Texture2D* viewportTexture);

		// Spatial Input
		bool SupportsSpatialInput();

		bool SupportsHandedness();
		bool SupportsHandTracking() const;

		// Eye gaze tracking
		bool SupportsEyeTracking() const;
		bool IsEyeTrackingAllowed() const;
		void RequestUserPermissionForEyeTracking();
		bool GetEyeGaze(EyeGazeRay& eyeRay);
		//~ Eye gaze tracking

		HMDTrackingStatus GetControllerTrackingStatus(HMDHand hand);

		bool GetPointerPose(
			HMDHand hand,
			PointerPoseInfo& pose);

		bool GetControllerOrientationAndPosition(
			HMDHand hand,
			DirectX::XMFLOAT4& orientation,
			DirectX::XMFLOAT3& position);

		bool GetHandJointOrientationAndPosition(
			HMDHand hand,
			HMDHandJoint joint,
			DirectX::XMFLOAT4& orientation,
			DirectX::XMFLOAT3& position);

		void PollInput();
		void PollHandTracking();
		HMDInputPressState GetPressState(HMDHand hand, HMDInputControllerButtons button);
		void ResetButtonStates();

		float GetAxisPosition(HMDHand hand, HMDInputControllerAxes axis);

		struct Bone
		{
			DirectX::XMFLOAT4 outRotQuat;
			DirectX::XMFLOAT3 outTrans;
			float outRadius;
		};

		void SubmitHapticValue(HMDHand hand, float value);

		// Anchors
		bool IsSpatialAnchorStoreLoaded() const;
		bool CreateAnchor(const wchar_t* anchorId, const DirectX::XMFLOAT3 position, DirectX::XMFLOAT4 rotationQuat, HMDTrackingOrigin trackingOrigin);
		void RemoveAnchor(const wchar_t* anchorId);
		bool DoesAnchorExist(const wchar_t* anchorId) const;
		bool GetAnchorPose(const wchar_t* anchorId, DirectX::XMFLOAT3& outScale, DirectX::XMFLOAT4& outRot, DirectX::XMFLOAT3& outTrans, HMDTrackingOrigin trackingOrigin) const;
		bool SaveAnchor(const wchar_t* anchorId);
		void RemoveSavedAnchor(const wchar_t* anchorId);
		bool SaveAnchors();
		bool LoadAnchors(std::function<void(const wchar_t* text)> anchorIdWritingFunctionPointer);
		void ClearSavedAnchors();
		bool DidAnchorCoordinateSystemChange();

		// Remoting
		void SetLogCallback(void (*functionPointer)(const wchar_t* text));
		void ConnectToRemoteHoloLens(ID3D11Device* device, const wchar_t* ip, int bitrate, bool IsHoloLens1 = false);
		void ConnectToLocalWMRHeadset();
		void ConnectToLocalHoloLens();
		void DisconnectFromDevice();
		bool IsRemoting();
		bool IsRemotingConnected();

		// Spatial Mapping
		void StartSpatialMapping(float InTriangleDensity, float InVolumeSize, void(*StartFunctionPointer)(),
			void(*AllocFunctionPointer)(MeshUpdate*),
			void(*FinishFunctionPointer)());
		void StopSpatialMapping();
		//~ Spatial Mapping

		// Used by the AR system to receive notifications of tracking change
		void SetTrackingChangedCallback(void(*CallbackPointer)(WindowsMixedReality::HMDSpatialLocatability));
		WindowsMixedReality::HMDSpatialLocatability GetTrackingState();

		// QR code tracking
		void StartQRCodeTracking(void(*AddedFunctionPointer)(QRCodeData*), void(*UpdatedFunctionPointer)(QRCodeData*), void(*RemovedFunctionPointer)(QRCodeData*));
		void StopQRCodeTracking();

#if PLATFORM_HOLOLENS
		void SetHolographicSpace(Windows::Graphics::Holographic::HolographicSpace^ inHolographicSpace);
#else
		bool CreateHolographicSpace(HWND hwnd);
#endif
		void SetInteractionManagerForCurrentView();
	};

	class SpatialAudioClientRenderer;

	/** Singleton that performs spatial audio rendering */
	class MIXEDREALITYINTEROP_API SpatialAudioClient
	{
	public:
		static SpatialAudioClient* CreateSpatialAudioClient()
		{
			return new SpatialAudioClient();
		}
		
		void Release();

		// Starts the spatial audio client rendering
		bool Start(UINT32 InNumSources, UINT32 InSampleRate);

		// Stops the spatial audio client rendering
		bool Stop();

		// Returns whether or not the spatial audio client is active
		bool IsActive();

		// Activates and returns a dynamic object handle
		ISpatialAudioObject* ActivatDynamicSpatialAudioObject();

		// Begins the update loop
		bool BeginUpdating(UINT32* OutAvailableDynamicObjectCount, UINT32* OutFrameCountPerBuffer);

		// Ends the update loop
		bool EndUpdating();

		// Pause the thread until buffer completion event
		bool WaitTillBufferCompletionEvent();

	private:
		SpatialAudioClient();
		~SpatialAudioClient();

		int32 sacId;
	};
}



/** Singleton that provides access to the camera images as they come in */
class MIXEDREALITYINTEROP_API CameraImageCapture
{
public:
	static CameraImageCapture& Get();
	static void Release();

	/** To route logging messages back to the UE_LOG() macros */
	void SetOnLog(void(*FunctionPointer)(const wchar_t* LogMsg));

	void StartCameraCapture(void(*FunctionPointer)(ID3D11Texture2D*), int DesiredWidth, int DesiredHeight, int DesiredFPS);
	void StopCameraCapture();

	void NotifyReceivedFrame(ID3D11Texture2D* ReceivedFrame);

	void Log(const wchar_t* LogMsg);

private:
	CameraImageCapture();
	~CameraImageCapture();

	static CameraImageCapture* CaptureInstance;
	/** Function pointer for logging */
	void(*OnLog)(const wchar_t*);
	/** Function pointer for when new frames have arrived */
	void(*OnReceivedFrame)(ID3D11Texture2D*);
};


#ifdef __UNREAL__
#pragma warning(pop)
#include "Windows/HideWindowsPlatformTypes.h"
#endif
