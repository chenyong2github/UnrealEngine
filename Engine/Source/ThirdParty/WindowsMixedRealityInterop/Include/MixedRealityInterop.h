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

#include <atomic>
#include <memory>
#include <vector>


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

struct MIXEDREALITYINTEROP_API TransformUpdate
{
	/** Location of this object in UE4 world space */
	float Translation[3] = { 0.f, 0.f, 0.f };
	/** Quaternion rotation. Requires normalization on the UE4 side before use */
	float Rotation[4] = { 0.f, 0.f, 0.f, 0.f };
	/** Scale for this object */
	float Scale[3] = { 0.f, 0.f, 0.f };
};

/**
 * A struct telling UE4 about the update. Also allows UE4 to allocate the buffers
 * to copy vertices/indices into (avoids one copy)
 */
struct MIXEDREALITYINTEROP_API MeshUpdate :
	public TransformUpdate
{
	enum MeshType {	World, Hand	};
	GUID Id;
	MeshType Type = World;

	/** If this is zero, there were no mesh changes */
	int NumVertices = 0;
	/** The vertex array for this mesh in UE4 local space */
	void* Vertices = nullptr;
	/** If this is zero, there were no mesh changes */
	int NumIndices = 0;
	/** The indices for the mesh */
	void* Indices = nullptr;
	int NumNormals = 0;
	void* Normals = nullptr;

	bool IsRightHandMesh = false;
};

/**
 * A struct telling UE4 about the update. The Translation is assumed to be the center of the plane
 */
struct MIXEDREALITYINTEROP_API PlaneUpdate :
	public TransformUpdate
{
	GUID Id;

	/** Width (X) of this plane */	
	float Width = 0.f;
	/** Height (Y) of this plane */
	float Height = 0.f;

	/** Orientation of the plane (horizontal, diagonal, vertical) */
	int Orientation = 0;
	/** Object classification (wall, floor, etc.) */
	int ObjectLabel = 0;
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
	int32_t				Version;
	/** Physical width and height of the QR code in meters (all QR codes are square) */
	float				SizeInMeters;
	/** Timestamp in seconds of the last time this QR code was seen */
	float				LastSeenTimestamp;
	/** Size in wchar_t's of the QR code's data string */
	uint32_t				DataSize;
	/** Data string embedded in the QR code */
	wchar_t*			Data;
};

namespace WindowsMixedReality
{
	enum class HMDEye
	{
		Left = 0,
		Right = 1,
		ThirdCamera = 2
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
	
	enum class HMDRemotingConnectionState
	{
		Connecting,
		Connected,
		Disconnected,
		Unknown,
		Undefined
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

		void Initialize(ID3D11Device* device, float nearPlane = 0.001f);
		void Dispose(bool force = false);
		bool IsStereoEnabled();
		bool IsTrackingAvailable();
		void ResetOrientationAndPosition();

		bool IsInitialized() const;
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
		bool UpdateRenderThreadFrame();

		// Get the latest pose information from our tracking frame.
		bool GetCurrentPoseRenderThread(DirectX::XMMATRIX& leftView, DirectX::XMMATRIX& rightView);
		static bool QueryCoordinateSystem(ABI::Windows::Perception::Spatial::ISpatialCoordinateSystem *& pCoordinateSystem);
		
		void SetTrackingOrigin(HMDTrackingOrigin trackingOrigin);
		HMDTrackingOrigin GetTrackingOrigin();

		DirectX::XMFLOAT4X4 GetProjectionMatrix(HMDEye eye);
		bool GetHiddenAreaMesh(HMDEye eye, DirectX::XMFLOAT2*& vertices, int& length);
		bool GetVisibleAreaMesh(HMDEye eye, DirectX::XMFLOAT2*& vertices, int& length);

		void SetScreenScaleFactor(float scale);
		
		int32_t GetMaxQuadLayerCount() const;

		uint32_t AddQuadLayer(
			uint32_t Id,
			ID3D11Texture2D* quadLayerTexture, 
			float widthM, float heightM,
			DirectX::XMFLOAT3 position,
			DirectX::XMFLOAT4 rotation,
			DirectX::XMFLOAT3 scale,
			HMDLayerType layerType,
			bool preserveAspectRatio,
			int priority);

		void RemoveQuadLayer(uint32_t Id);

		bool CreateRenderingParameters();
		ID3D11Texture2D* GetBackBufferTexture();
		bool CommitDepthBuffer(ID3D11Texture2D* depthTexture);
		bool CommitThirdCameraDepthBuffer(ID3D11Texture2D* depthTexture);

		void SetFocusPointForFrame(DirectX::XMFLOAT3 position);

		// Use double-width stereo texture for the viewport texture.
		bool CopyResources(ID3D11DeviceContext* context, ID3D11Texture2D* viewportTexture);
		bool Present();

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
			DirectX::XMFLOAT3& position,
			float& radius);

		void PollInput();
		void PollHandTracking();
		HMDInputPressState GetPressState(HMDHand hand, HMDInputControllerButtons button, bool onlyRegisterClicks = true);
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
		bool CreateAnchor(const wchar_t* anchorId, const DirectX::XMFLOAT3 position, DirectX::XMFLOAT4 rotationQuat);
		void RemoveAnchor(const wchar_t* anchorId);
		bool DoesAnchorExist(const wchar_t* anchorId) const;
		bool GetAnchorPose(const wchar_t* anchorId, DirectX::XMFLOAT3& outScale, DirectX::XMFLOAT4& outRot, DirectX::XMFLOAT3& outTrans) const;
		bool SaveAnchor(const wchar_t* saveId, const wchar_t* anchorId);
		void RemoveSavedAnchor(const wchar_t* saveId);
		bool LoadAnchors(std::function<void(const wchar_t* saveId, const wchar_t* anchorId)> anchorIdWritingFunctionPointer);
		void ClearSavedAnchors();
		bool DidAnchorCoordinateSystemChange();

		// Remoting
		enum class ConnectionEvent
		{
			Connected,
			DisconnectedFromPeer,
			Listening
		};

		typedef std::function<void(ConnectionEvent)> ConnectionCallback;

		HMDRemotingConnectionState GetConnectionState();
		void SetLogCallback(void (*functionPointer)(const wchar_t* text));
		void ConnectToRemoteHoloLens(ID3D11Device* device, const wchar_t* ip, int bitrate, bool IsHoloLens1 = false, int listenPort = 8265, bool listen = false);
		void ConnectToLocalWMRHeadset();
		void ConnectToLocalHoloLens();
		void DisconnectFromDevice();
		bool IsRemoting();
		bool IsHololens1Remoting();
		bool IsRemotingConnected();
		uint32_t SubscribeConnectionEvent(ConnectionCallback callback);
		void UnsubscribeConnectionEvent(uint32_t id);
		wchar_t* GetFailureString();

		// Spatial Mapping
		bool StartSpatialMapping(float InTriangleDensity, float InVolumeSize, void(*StartFunctionPointer)(),
			void(*AllocFunctionPointer)(MeshUpdate*),
			void(*RemovedMeshPointer)(MeshUpdate*),
			void(*FinishFunctionPointer)());
		bool StopSpatialMapping();
		//~ Spatial Mapping

		// Hand Mesh
		bool StartHandMesh(void(*StartFunctionPointer)(),
			void(*AllocFunctionPointer)(MeshUpdate*),
			void(*FinishFunctionPointer)());
		void StopHandMesh();
		//~ Hand Mesh

		// Scene understanding
		void StartSceneUnderstanding(
			bool bGeneratePlanes,
			bool bGenerateSceneMeshes,
			float InVolumeSize,
			void(*StartFunctionPointer)(),
			void(*AddPlaneFunctionPointer)(PlaneUpdate*),
			void(*RemovePlaneFunctionPointer)(PlaneUpdate*),
			void(*AllocMeshFunctionPointer)(MeshUpdate*),
			void(*RemoveMeshFunctionPointer)(MeshUpdate*),
			void(*FinishFunctionPointer)()
		);
		void StopSceneUnderstanding();
		void SetSUCoordinateSystem();
		//~Scene understanding

		// Used by the AR system to receive notifications of tracking change
		void SetTrackingChangedCallback(void(*CallbackPointer)(WindowsMixedReality::HMDSpatialLocatability));
		WindowsMixedReality::HMDSpatialLocatability GetTrackingState();

		// QR code tracking
		bool StartQRCodeTracking(void(*AddedFunctionPointer)(QRCodeData*), void(*UpdatedFunctionPointer)(QRCodeData*), void(*RemovedFunctionPointer)(QRCodeData*));
		bool StopQRCodeTracking();

#if PLATFORM_HOLOLENS
		void SetHolographicSpace(Windows::Graphics::Holographic::HolographicSpace^ inHolographicSpace);
#else
		bool CreateHolographicSpace(HWND hwnd);
#endif
		void SetInteractionManagerForCurrentView();

		// Third camera
		bool IsThirdCameraActive();
		bool GetThirdCameraPoseRenderThread(DirectX::XMMATRIX& thirdCameraViewLeft, DirectX::XMMATRIX& thirdCameraViewRight);

		bool SetEnabledMixedRealityCamera(bool enabled);
		bool ResizeMixedRealityCamera(/*inout*/ SIZE& sz);
		void GetThirdCameraDimensions(int& width, int& height);

	private:
		//For returning more descriptive error messages
		wchar_t failureString[MAX_PATH];
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

		// Returns the number of dynamic objects supported by the audio client renderer
		UINT32 GetMaxDynamicObjects() const;

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

		int32_t sacId;
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

	bool StartCameraCapture(void(*FunctionPointer)(void*, DirectX::XMFLOAT4X4), int DesiredWidth, int DesiredHeight, int DesiredFPS);
	bool StopCameraCapture();

	void NotifyReceivedFrame(void* handle, DirectX::XMFLOAT4X4 CamToTracking);

	void Log(const wchar_t* LogMsg);

	bool GetCameraIntrinsics(DirectX::XMFLOAT2& focalLength, int& width, int& height, DirectX::XMFLOAT2& principalPoint, DirectX::XMFLOAT3& radialDistortion, DirectX::XMFLOAT2& tangentialDistortion);
	DirectX::XMFLOAT2 UnprojectPVCamPointAtUnitDepth(DirectX::XMFLOAT2 pixelCoordinate);

private:
	CameraImageCapture();
	~CameraImageCapture();

	static CameraImageCapture* CaptureInstance;
	/** Function pointer for logging */
	void(*OnLog)(const wchar_t*);
	/** Function pointer for when new frames have arrived */
	void(*OnReceivedFrame)(void*, DirectX::XMFLOAT4X4);
};


/** Singleton for AsureSpatialAnchors */
class MIXEDREALITYINTEROP_API AzureSpatialAnchorsInterop
{
public:
	typedef int CloudAnchorID;
	static const CloudAnchorID CloudAnchorID_Invalid = -1;
	typedef int32_t WatcherID;
	typedef const wchar_t* LocalAnchorID;

	typedef void(*LogFunctionPtr)(const wchar_t* LogMsg);
	typedef std::function<void(int32 WatcherIdentifier, int32 LocateAnchorStatus, AzureSpatialAnchorsInterop::CloudAnchorID CloudAnchorID)> AnchorLocatedCallbackPtr;
	typedef std::function<void(int32 InWatcherIdentifier, bool InWasCanceled)> LocateAnchorsCompletedCallbackPtr;
	typedef std::function<void(float InReadyForCreateProgress, float InRecommendedForCreateProgress, int InSessionCreateHash, int InSessionLocateHash, int32 InSessionUserFeedback)> SessionUpdatedCallbackPtr;

	// Interop Lifecycle
	static void Create(
		WindowsMixedReality::MixedRealityInterop& interop, 
		LogFunctionPtr LogFunctionPointer,
		AnchorLocatedCallbackPtr AnchorLocatedCallback,
		LocateAnchorsCompletedCallbackPtr LocateAnchorsCompletedCallback,
		SessionUpdatedCallbackPtr SessionUpdatedCallback
	);
	static AzureSpatialAnchorsInterop& Get();
	static void Release();

	// AzureSpatialAnchorSession Lifecycle
	virtual bool CreateSession() = 0;
	virtual void DestroySession() = 0;

	enum class ASAResult : uint8
	{
		Success,
		NotStarted,
		Started,
		FailAlreadyStarted,
		FailNoARPin,
		FailBadLocalAnchorID,
		FailBadCloudAnchorIdentifier,
		FailAnchorIdAlreadyUsed,
		FailAnchorDoesNotExist,
		FailAnchorAlreadyTracked,
		FailNoAnchor,
		FailNoLocalAnchor,
		FailNoCloudAnchor,
		FailNoSession,
		FailNoWatcher,
		FailNotEnoughData,
		FailBadLifetime,
		FailSeeErrorString,
		NotLocated,
		Canceled
	};

	struct LocateCriteria
	{
		bool bBypassCache = false;
		int NumIdentifiers = 0;
		const wchar_t** Identifiers = nullptr;
		CloudAnchorID NearCloudAnchorID = CloudAnchorID_Invalid;
		float NearCloudAnchorDistance = 5.0f;
		int NearCloudAnchorMaxResultCount = 20;
		bool SearchNearDevice = false;
		float NearDeviceDistance = 5.0f;
		int NearDeviceMaxResultCount = 20;
		int AzureSpatialAnchorDataCategory = 0;
		int AzureSptialAnchorsLocateStrategy = 0;

		//uncopyable, due to char*'s.
		LocateCriteria() {}
	private:
		LocateCriteria(const LocateCriteria&) = delete;
		LocateCriteria& operator=(const LocateCriteria&) = delete;
	};

	struct SessionConfig
	{
		const wchar_t* AccessToken = nullptr;
		const wchar_t* AccountDomain = nullptr;
		const wchar_t* AccountId = nullptr;
		const wchar_t* AccountKey = nullptr;
		const wchar_t* AuthenticationToken = nullptr;

		//uncopyable, due to char*'s.
		SessionConfig() {}
	private:
		SessionConfig(const SessionConfig&) = delete;
		SessionConfig& operator=(const SessionConfig&) = delete;
	};

	struct LocationProviderConfig
	{
		bool bCoarseLocalizationEnabled = false;
		bool bEnableGPS = false;
		bool bEnableWifi = false;
		int NumBLEBeaconUUIDs = 0;
		const wchar_t** BLEBeaconUUIDs = nullptr;

		//uncopyable, due to char*'s.
		LocationProviderConfig() {}
	private:
		LocationProviderConfig(const LocationProviderConfig&) = delete;
		LocationProviderConfig& operator=(const LocationProviderConfig&) = delete;
	};

	struct DiagnosticsConfig
	{
		bool bImagesEnabled = false;
		const wchar_t* LogDirectory = nullptr;
		int32_t LogLevel = 0;
		int32_t MaxDiskSizeInMB = 0;

		//uncopyable, due to char*'s.
		DiagnosticsConfig() {}
	private:
		DiagnosticsConfig(const DiagnosticsConfig&) = delete;
		DiagnosticsConfig& operator=(const DiagnosticsConfig&) = delete;
	};

	struct SessionStatus
	{
		float ReadyForCreateProgress = 0.0f;
		float RecommendedForCreateProgress = 0.0f;
		int32_t SessionCreateHash = 0;
		int32_t SessionLocateHash = 0;
		int32_t UserFeedback = 0;
	};

	// Create this on the UE4 side, pass by reference, set the deleter and fill it in on the ASA side.
	class StringOutParam
	{
	public:
		StringOutParam() {}

		void Set(void(*InDeleter)(const void*), uint32_t NumChars, const wchar_t* Chars)
		{
			assert(InDeleter);
			Deleter = InDeleter;
			assert(String == nullptr);
			assert(NumChars >= 0);
			assert(Chars != nullptr);
			wchar_t* Buffer = new wchar_t[NumChars + 1];
			wcsncpy_s(Buffer, NumChars + 1, Chars, NumChars);
			String = Buffer;
		}
		~StringOutParam()
		{
			if (String) Deleter(String);
		}

		const wchar_t* String = nullptr;

	private:
		// No copy
		StringOutParam(const StringOutParam&) = delete;
		StringOutParam& operator=(const StringOutParam&) = delete;
		void(*Deleter)(const void*) = nullptr;
	};

	// Create this on the UE4 side, pass by reference, fill it in on the ASA side.
	class StringArrayOutParam
	{
	public:
		StringArrayOutParam() {}
		void SetArraySize(void(*InDeleter)(const void*), uint32_t Num)
		{
			assert(InDeleter);
			Deleter = InDeleter;
			assert(ArraySize == 0);
			assert(Array == nullptr);
			ArraySize = Num;
			if (Num > 0)
			{
				Array = new wchar_t* [Num];
			}
		}
		void SetArrayElement(uint32_t Index, uint32_t NumChars, const wchar_t* Chars)
		{
			assert(Index >= 0);
			assert(Index < ArraySize);
			assert(NumChars >= 0);
			assert(Chars != nullptr);
			wchar_t* Buffer = new wchar_t[NumChars + 1];
			wcsncpy_s(Buffer, NumChars + 1, Chars, NumChars);
			Array[Index] = Buffer;
		}
		~StringArrayOutParam()
		{
			if (Array)
			{
				for (uint32_t i = 0; i < ArraySize; i++)
				{
					Deleter(Array[i]);
				}
			}
			if (Array) Deleter(Array);
		};

		uint32_t ArraySize = 0;
		wchar_t** Array = nullptr;

	private:
		// No copy
		StringArrayOutParam(const StringArrayOutParam&) = delete;
		StringArrayOutParam& operator=(const StringArrayOutParam&) = delete;
		void(*Deleter)(const void*) = nullptr;
	};

	// Create this on the UE4 side, pass by reference, fill it in on the ASA side.
	class IntArrayOutParam
	{
	public:
		IntArrayOutParam() {}
		void SetArraySize(void(*InDeleter)(const void*), uint32_t Num)
		{
			assert(InDeleter);
			Deleter = InDeleter;
			assert(ArraySize == 0);
			assert(Array == nullptr);
			ArraySize = Num;
			if (Num > 0)
			{
				Array = new int32_t [Num];
			}
		}
		void SetArrayElement(uint32_t Index, int32_t Value)
		{
			assert(Index >= 0);
			assert(Index < ArraySize);
			Array[Index] = Value;
		}
		~IntArrayOutParam()
		{
			if (Array) Deleter(Array);
		};

		uint32_t ArraySize = 0;
		int32_t* Array = nullptr;

	private:
		// No copy
		IntArrayOutParam(const IntArrayOutParam&) = delete;
		IntArrayOutParam& operator=(const IntArrayOutParam&) = delete;
		void(*Deleter)(const void*) = nullptr;
	};

	// Callback types.
	typedef std::function<void(ASAResult Result, const wchar_t* ErrorString)> Callback_Result;
	typedef std::function<void(ASAResult Result, const wchar_t* ErrorString, SessionStatus SessionStatus)> Callback_Result_SessionStatus;
	typedef std::function<void(ASAResult Result, const wchar_t* ErrorString, CloudAnchorID InCloudAnchorID)> Callback_Result_CloudAnchorID;
	typedef std::function<void(ASAResult Result, const wchar_t* ErrorString, const wchar_t* String)> Callback_Result_String;

	// CloudSpatialAnchorSession methods.
	virtual void GetAccessTokenWithAccountKeyAsync(const wchar_t* AccountKey, Callback_Result_String Callback) = 0;
	virtual void GetAccessTokenWithAuthenticationTokenAsync(const wchar_t* AuthenticationToken, Callback_Result_String Callback) = 0;
	virtual ASAResult StartSession() = 0;
	virtual void StopSession() = 0;
	virtual ASAResult ResetSession() = 0;
	virtual void DisposeSession() = 0;
	virtual void GetSessionStatusAsync(Callback_Result_SessionStatus Callback) = 0;
	virtual ASAResult ConstructAnchor(const LocalAnchorID& InLocalAnchorID, CloudAnchorID& OutCloudAnchorID) = 0;
	virtual void CreateAnchorAsync(CloudAnchorID InCloudAnchorID, Callback_Result Callback) = 0;  // note this 'creates' the anchor in the azure cloud, aka saves it to the cloud.
	virtual void DeleteAnchorAsync(CloudAnchorID InCloudAnchorID, Callback_Result Callback) = 0;
	virtual ASAResult CreateWatcher(const LocateCriteria& InLocateCriteria, WatcherID& OutWatcherID, StringOutParam& OutErrorString) = 0;
	virtual ASAResult GetActiveWatchers(IntArrayOutParam& OutWatcherIDs) = 0;
	virtual void GetAnchorPropertiesAsync(const wchar_t* InCloudAnchorIdentifier, Callback_Result_CloudAnchorID Callback) = 0;
	virtual void RefreshAnchorPropertiesAsync(CloudAnchorID InCloudAnchorID, Callback_Result Callback) = 0;
	virtual void UpdateAnchorPropertiesAsync(CloudAnchorID InCloudAnchorID, Callback_Result Callback) = 0;
	virtual ASAResult GetConfiguration(SessionConfig& OutConfig) = 0;
	virtual ASAResult SetConfiguration(const SessionConfig& InConfig) = 0;
	virtual ASAResult SetLocationProvider(const LocationProviderConfig& InConfig) = 0;
	virtual ASAResult GetLogLevel(int32_t& OutLogVerbosity) = 0;
	virtual ASAResult SetLogLevel(int32_t InLogVerbosity) = 0;
	//virtual ASAResult GetSession() = 0;
	//virtual ASAResult SetSession() = 0;
	virtual ASAResult GetSessionId(std::wstring& OutSessionID) = 0;

	// CloudSpatialAnchorWatcher methods.
	virtual ASAResult StopWatcher(WatcherID WatcherIdentifier) = 0;

	// CloudSpatialAnchor methods.
	virtual ASAResult GetCloudSpatialAnchorIdentifier(CloudAnchorID InCloudAnchorID, StringOutParam& OutCloudAnchorIdentifier) = 0;
	virtual ASAResult SetCloudAnchorExpiration(CloudAnchorID InCloudAnchorID, float InLifetimeInSeconds) = 0;
	virtual ASAResult GetCloudAnchorExpiration(CloudAnchorID InCloudAnchorID, float& OutLifetimeInSeconds) = 0;
	virtual ASAResult SetCloudAnchorAppProperties(CloudAnchorID InCloudAnchorID, int InNumAppProperties, const wchar_t** InAppProperties_KeyValueInterleaved) = 0;
	virtual ASAResult GetCloudAnchorAppProperties(CloudAnchorID InCloudAnchorID, StringArrayOutParam& OutAppProperties_KeyValueInterleaved) = 0;

	// Diagnostics methods.
	virtual ASAResult SetDiagnosticsConfig(DiagnosticsConfig& InConfig) = 0;
	virtual void CreateDiagnosticsManifestAsync(const wchar_t* Description, Callback_Result_String Callback) = 0;
	virtual void SubmitDiagnosticsManifestAsync(const wchar_t* ManifestPath, Callback_Result Callback) = 0;


	virtual bool HasEnoughDataForSaving() = 0;  // This is deprecated.

	virtual bool CreateARPinAroundAzureCloudSpatialAnchor(const LocalAnchorID& localAnchorId, CloudAnchorID cloudAnchorID) = 0;

protected:
	AzureSpatialAnchorsInterop() {};
	virtual ~AzureSpatialAnchorsInterop() {};
	AzureSpatialAnchorsInterop(const AzureSpatialAnchorsInterop&) = delete;
	AzureSpatialAnchorsInterop& operator=(const AzureSpatialAnchorsInterop&) = delete;
};


#ifdef __UNREAL__
#pragma warning(pop)
#include "Windows/HideWindowsPlatformTypes.h"
#endif
