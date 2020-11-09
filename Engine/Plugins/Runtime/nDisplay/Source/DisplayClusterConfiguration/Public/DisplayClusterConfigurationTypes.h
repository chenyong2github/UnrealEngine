// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"

#include "DisplayClusterConfigurationTypes.generated.h"

class UStaticMesh;
struct FPropertyChangedChainEvent;


UENUM()
enum class EDisplayClusterConfigurationDataSource : uint8
{
	Text UMETA(DisplayName = "Text file"),
	Json UMETA(DisplayName = "JSON file")
};

UENUM()
enum class EDisplayClusterConfigurationKeyboardReflectionType : uint8
{
	None     UMETA(DisplayName = "No reflection"),
	nDisplay UMETA(DisplayName = "nDisplay buttons only"),
	Core     UMETA(DisplayName = "UE core keyboard events only"),
	All      UMETA(DisplayName = "Both nDisplay and UE4 core events")
};

UENUM()
enum class EDisplayClusterConfigurationTrackerMapping
{
	X    UMETA(DisplayName = "Positive X"),
	NX   UMETA(DisplayName = "Negative X"),
	Y    UMETA(DisplayName = "Positive Y"),
	NY   UMETA(DisplayName = "Negative Y"),
	Z    UMETA(DisplayName = "Positive Z"),
	NZ   UMETA(DisplayName = "Negative Z")
};


UENUM()
enum class EDisplayClusterConfigurationEyeStereoOffset : uint8
{
	None  UMETA(DisplayName = "No offset"),
	Left  UMETA(DisplayName = "Left eye of a stereo pair"),
	Right UMETA(DisplayName = "Right eye of a stereo pair")
};

USTRUCT()
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationRectangle
{
	GENERATED_BODY()

public:
	FDisplayClusterConfigurationRectangle()
		: X(0), Y(0), W(0), H(0)
	{ }

	FDisplayClusterConfigurationRectangle(int32 _X, int32 _Y, int32 _W, int32 _H)
		: X(_X), Y(_Y), W(_W), H(_H)
	{ }

	FDisplayClusterConfigurationRectangle(const FDisplayClusterConfigurationRectangle&) = default;

public:
	UPROPERTY(EditAnywhere, Category = nDisplay)
	int32 X;

	UPROPERTY(EditAnywhere, Category = nDisplay)
	int32 Y;

	UPROPERTY(EditAnywhere, Category = nDisplay)
	int32 W;

	UPROPERTY(EditAnywhere, Category = nDisplay)
	int32 H;
};

USTRUCT()
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationInfo
{
	GENERATED_BODY()

public:
	UPROPERTY(VisibleAnywhere, Category = nDisplay)
	FString Description;

	UPROPERTY(VisibleAnywhere, Category = nDisplay)
	FString Version;
};

USTRUCT()
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationPolymorphicEntity
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = nDisplay)
	FString Type;

	UPROPERTY(EditAnywhere, Category = nDisplay)
	TMap<FString, FString> Parameters;
};


// Scene hierarchy
UCLASS()
class DISPLAYCLUSTERCONFIGURATION_API UDisplayClusterConfigurationSceneComponent
	: public UObject
{
	GENERATED_BODY()

public:
	UDisplayClusterConfigurationSceneComponent()
		: UDisplayClusterConfigurationSceneComponent(FString(), FVector::ZeroVector, FRotator::ZeroRotator, FString(), -1)
	{ }

	UDisplayClusterConfigurationSceneComponent(const FString& InParentId, const FVector& InLocation, const FRotator& InRotation, const FString& InTrackerId, const int32& InTrackerCh)
		: ParentId(InParentId)
		, Location(InLocation)
		, Rotation(InRotation)
		, TrackerId(InTrackerId)
		, TrackerChannel(InTrackerCh)
	{ }

public:
	UPROPERTY(EditAnywhere, Category = nDisplay)
	FString ParentId;

	UPROPERTY(EditAnywhere, Category = nDisplay)
	FVector Location;

	UPROPERTY(EditAnywhere, Category = nDisplay)
	FRotator Rotation;

	UPROPERTY(EditAnywhere, Category = nDisplay)
	FString TrackerId;

	UPROPERTY(EditAnywhere, Category = nDisplay)
	int32 TrackerChannel;
};

UCLASS()
class DISPLAYCLUSTERCONFIGURATION_API UDisplayClusterConfigurationSceneComponentXform
	: public UDisplayClusterConfigurationSceneComponent
{
	GENERATED_BODY()

public:
	UDisplayClusterConfigurationSceneComponentXform()
		: UDisplayClusterConfigurationSceneComponentXform(FString(), FVector::ZeroVector, FRotator::ZeroRotator, FString(), -1)
	{ }

	UDisplayClusterConfigurationSceneComponentXform(const FString& InParentId, const FVector& InLocation, const FRotator& InRotation, const FString& InTrackerId, int32 InTrackerCh)
		: UDisplayClusterConfigurationSceneComponent(InParentId , InLocation, InRotation, InTrackerId, InTrackerCh)
	{ }
};

UCLASS()
class DISPLAYCLUSTERCONFIGURATION_API UDisplayClusterConfigurationSceneComponentScreen
	: public UDisplayClusterConfigurationSceneComponent
{
	GENERATED_BODY()

public:
	UDisplayClusterConfigurationSceneComponentScreen()
		: UDisplayClusterConfigurationSceneComponentScreen(FString(), FVector::ZeroVector, FRotator::ZeroRotator, FString(), -1, FVector2D(100.f, 100.f))
	{ }

	UDisplayClusterConfigurationSceneComponentScreen(const FString& InParentId, const FVector& InLocation, const FRotator& InRotation, const FString& InTrackerId, int32 InTrackerCh, const FVector2D& InSize)
		: UDisplayClusterConfigurationSceneComponent(InParentId, InLocation, InRotation, InTrackerId, InTrackerCh)
		, Size(InSize)
	{ }

public:
	UPROPERTY(EditAnywhere, Category = nDisplay)
	FVector2D Size;
};

UCLASS()
class DISPLAYCLUSTERCONFIGURATION_API UDisplayClusterConfigurationSceneComponentCamera
	: public UDisplayClusterConfigurationSceneComponent
{
	GENERATED_BODY()

public:
	UDisplayClusterConfigurationSceneComponentCamera()
		: UDisplayClusterConfigurationSceneComponentCamera(FString(), FVector::ZeroVector, FRotator::ZeroRotator, FString(), -1, 6.4f, false, EDisplayClusterConfigurationEyeStereoOffset::None)
	{ }

	UDisplayClusterConfigurationSceneComponentCamera(const FString& InParentId, const FVector& InLocation, const FRotator& InRotation, const FString& InTrackerId, int32 InTrackerCh,
		float InInterpupillaryDistance, bool bInSwapEyes, EDisplayClusterConfigurationEyeStereoOffset InStereoOffset)
		: UDisplayClusterConfigurationSceneComponent(InParentId, InLocation, InRotation, InTrackerId, InTrackerCh)
		, InterpupillaryDistance(InInterpupillaryDistance)
		, bSwapEyes(bInSwapEyes)
		, StereoOffset(InStereoOffset)
	{ }

public:
	UPROPERTY(EditAnywhere, Category = nDisplay)
	float InterpupillaryDistance;

	UPROPERTY(EditAnywhere, Category = nDisplay)
	bool bSwapEyes;

	UPROPERTY(EditAnywhere, Category = nDisplay)
	EDisplayClusterConfigurationEyeStereoOffset StereoOffset;
};

UCLASS()
class DISPLAYCLUSTERCONFIGURATION_API UDisplayClusterConfigurationSceneComponentMesh
	: public UDisplayClusterConfigurationSceneComponent
{
	GENERATED_BODY()

public:
	UDisplayClusterConfigurationSceneComponentMesh()
		: UDisplayClusterConfigurationSceneComponentMesh(FString(), FVector::ZeroVector, FRotator::ZeroRotator, FString(), -1, FString())
	{ }

	UDisplayClusterConfigurationSceneComponentMesh(const FString& InParentId, const FVector& InLocation, const FRotator& InRotation, const FString& InTrackerId, int32 InTrackerCh, const FString& InAssetPath)
		: UDisplayClusterConfigurationSceneComponent(InParentId, InLocation, InRotation, InTrackerId, InTrackerCh)
		, AssetPath(InAssetPath)
#if WITH_EDITOR
		, Asset(nullptr)
#endif
	{ }

#if WITH_EDITOR
	/** Load static mesh from AssetPath */
	void LoadAssets();
#endif

public:
	UPROPERTY()
	FString AssetPath;

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, Category = nDisplay)
	UStaticMesh* Asset;
#endif
};

UCLASS()
class DISPLAYCLUSTERCONFIGURATION_API UDisplayClusterConfigurationScene
	: public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TMap<FString, UDisplayClusterConfigurationSceneComponentXform*> Xforms;

	UPROPERTY()
	TMap<FString, UDisplayClusterConfigurationSceneComponentScreen*> Screens;

	UPROPERTY()
	TMap<FString, UDisplayClusterConfigurationSceneComponentCamera*> Cameras;

	UPROPERTY()
	TMap<FString, UDisplayClusterConfigurationSceneComponentMesh*> Meshes;
};


////////////////////////////////////////////////////////////////
// Cluster
USTRUCT()
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationMasterNodePorts
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = nDisplay)
	uint16 ClusterSync;

	UPROPERTY(EditAnywhere, Category = nDisplay)
	uint16 RenderSync;

	UPROPERTY(EditAnywhere, Category = nDisplay)
	uint16 ClusterEventsJson;

	UPROPERTY(EditAnywhere, Category = nDisplay)
	uint16 ClusterEventsBinary;
};

USTRUCT()
struct FDisplayClusterConfigurationMasterNode
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FString Id;

	UPROPERTY(EditAnywhere, Category = nDisplay)
	FDisplayClusterConfigurationMasterNodePorts Ports;
};

USTRUCT()
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationRenderSyncPolicy
	: public FDisplayClusterConfigurationPolymorphicEntity
{
	GENERATED_BODY()
};

USTRUCT()
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationInputSyncPolicy
	: public FDisplayClusterConfigurationPolymorphicEntity
{
	GENERATED_BODY()
};

USTRUCT()
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationClusterSync
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = nDisplay)
	FDisplayClusterConfigurationRenderSyncPolicy RenderSyncPolicy;

	UPROPERTY(EditAnywhere, Category = nDisplay)
	FDisplayClusterConfigurationInputSyncPolicy InputSyncPolicy;
};


USTRUCT()
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationNetworkSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = nDisplay)
	uint32 ConnectRetriesAmount;

	UPROPERTY(EditAnywhere, Category = nDisplay)
	uint32 ConnectRetryDelay;

	UPROPERTY(EditAnywhere, Category = nDisplay)
	uint32 GameStartBarrierTimeout;

	UPROPERTY(EditAnywhere, Category = nDisplay)
	uint32 FrameStartBarrierTimeout;

	UPROPERTY(EditAnywhere, Category = nDisplay)
	uint32 FrameEndBarrierTimeout;

	UPROPERTY(EditAnywhere, Category = nDisplay)
	uint32 RenderSyncBarrierTimeout;
};


USTRUCT()
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationProjection
	: public FDisplayClusterConfigurationPolymorphicEntity
{
	GENERATED_BODY()
};


UCLASS()
class DISPLAYCLUSTERCONFIGURATION_API UDisplayClusterConfigurationViewport
	: public UObject
{
	GENERATED_BODY()
public:
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnPostEditChangeChainProperty, const FPropertyChangedChainEvent&);

	FOnPostEditChangeChainProperty OnPostEditChangeChainProperty;

private:
#if WITH_EDITOR
	// UObject interface
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent) override;
	// End of UObject interface
#endif

public:
	UPROPERTY(EditAnywhere, Category = nDisplay)
	FString Camera;

	UPROPERTY(EditAnywhere, Category = nDisplay, meta = (ClampMin = "0.05", UIMin = "0.05", ClampMax = "10.0", UIMax = "10.0"))
	float BufferRatio;

	UPROPERTY(EditAnywhere, Category = nDisplay)
	int GPUIndex;

	UPROPERTY(EditAnywhere, Category = nDisplay)
	bool bAllowCrossGPUTransfer;

	UPROPERTY(EditAnywhere, Category = nDisplay)
	bool bIsShared;

	UPROPERTY(EditAnywhere, Category = nDisplay)
	FDisplayClusterConfigurationRectangle Region;

	UPROPERTY(EditAnywhere, Category = nDisplay)
	FDisplayClusterConfigurationProjection ProjectionPolicy;
};


USTRUCT()
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationPostprocess
	: public FDisplayClusterConfigurationPolymorphicEntity
{
	GENERATED_BODY()
};


UCLASS()
class DISPLAYCLUSTERCONFIGURATION_API UDisplayClusterConfigurationClusterNode
	: public UObject
{
	GENERATED_BODY()

public:
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnPostEditChangeChainProperty, const FPropertyChangedChainEvent&);

	FOnPostEditChangeChainProperty OnPostEditChangeChainProperty;

private:
#if WITH_EDITOR
	// UObject interface
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent) override;
	// End of UObject interface
#endif

public:
	UPROPERTY(EditAnywhere, Category = nDisplay)
	FString Host;

	UPROPERTY(EditAnywhere, Category = nDisplay)
	bool bIsSoundEnabled;

	UPROPERTY(EditAnywhere, Category = nDisplay)
	bool bIsFullscreen;

	UPROPERTY(EditAnywhere, Category = nDisplay)
	FDisplayClusterConfigurationRectangle WindowRect;

	UPROPERTY()
	TMap<FString, UDisplayClusterConfigurationViewport*> Viewports;

	UPROPERTY()
	TMap<FString, FDisplayClusterConfigurationPostprocess> Postprocess;
};

UCLASS()
class DISPLAYCLUSTERCONFIGURATION_API UDisplayClusterConfigurationCluster
	: public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = nDisplay)
	FDisplayClusterConfigurationMasterNode MasterNode;

	UPROPERTY(EditAnywhere, Category = nDisplay)
	FDisplayClusterConfigurationClusterSync Sync;

	UPROPERTY(EditAnywhere, Category = nDisplay)
	FDisplayClusterConfigurationNetworkSettings Network;

	UPROPERTY()
	TMap<FString, UDisplayClusterConfigurationClusterNode*> Nodes;
};


////////////////////////////////////////////////////////////////
// Input
UCLASS()
class DISPLAYCLUSTERCONFIGURATION_API UDisplayClusterConfigurationInputDevice
	: public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = nDisplay)
	FString Address;

	UPROPERTY(EditAnywhere, Category = nDisplay)
	TMap<int32, int32> ChannelRemapping;
};

UCLASS()
class DISPLAYCLUSTERCONFIGURATION_API UDisplayClusterConfigurationInputDeviceAnalog
	: public UDisplayClusterConfigurationInputDevice
{
	GENERATED_BODY()
};

UCLASS()
class DISPLAYCLUSTERCONFIGURATION_API UDisplayClusterConfigurationInputDeviceButton
	: public UDisplayClusterConfigurationInputDevice
{
	GENERATED_BODY()
};

UCLASS()
class DISPLAYCLUSTERCONFIGURATION_API UDisplayClusterConfigurationInputDeviceKeyboard
	: public UDisplayClusterConfigurationInputDevice
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = nDisplay)
	EDisplayClusterConfigurationKeyboardReflectionType ReflectionType;
};

UCLASS()
class DISPLAYCLUSTERCONFIGURATION_API UDisplayClusterConfigurationInputDeviceTracker
	: public UDisplayClusterConfigurationInputDevice
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = nDisplay)
	FVector  OriginLocation;

	UPROPERTY(EditAnywhere, Category = nDisplay)
	FRotator OriginRotation;

	UPROPERTY(EditAnywhere, Category = nDisplay)
	FString  OriginComponent;

	UPROPERTY(EditAnywhere, Category = nDisplay)
	EDisplayClusterConfigurationTrackerMapping Front;

	UPROPERTY(EditAnywhere, Category = nDisplay)
	EDisplayClusterConfigurationTrackerMapping Right;

	UPROPERTY(EditAnywhere, Category = nDisplay)
	EDisplayClusterConfigurationTrackerMapping Up;
};


USTRUCT()
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationInputBinding
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = nDisplay)
	FString DeviceId;

	UPROPERTY(EditAnywhere, Category = nDisplay)
	int32 Channel;

	UPROPERTY(EditAnywhere, Category = nDisplay)
	FString Key;

	UPROPERTY(EditAnywhere, Category = nDisplay)
	FString BindTo;
};


UCLASS(autoexpandcategories = nDisplay)
class DISPLAYCLUSTERCONFIGURATION_API UDisplayClusterConfigurationInput
	: public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TMap<FString, UDisplayClusterConfigurationInputDeviceAnalog*> AnalogDevices;

	UPROPERTY()
	TMap<FString, UDisplayClusterConfigurationInputDeviceButton*> ButtonDevices;

	UPROPERTY()
	TMap<FString, UDisplayClusterConfigurationInputDeviceKeyboard*> KeyboardDevices;

	UPROPERTY()
	TMap<FString, UDisplayClusterConfigurationInputDeviceTracker*> TrackerDevices;

	UPROPERTY(EditAnywhere, Category = nDisplay)
	TArray<FDisplayClusterConfigurationInputBinding> InputBinding;
};


USTRUCT()
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationDiagnostics
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = nDisplay)
	bool bSimulateLag;

	UPROPERTY(EditAnywhere, Category = nDisplay)
	float MinLagTime;

	UPROPERTY(EditAnywhere, Category = nDisplay)
	float MaxLagTime;
};

struct FDisplayClusterConfigurationDataMetaInfo
{
	EDisplayClusterConfigurationDataSource DataSource;
	FString FilePath;
};


////////////////////////////////////////////////////////////////
// Main configuration data container
UCLASS()
class DISPLAYCLUSTERCONFIGURATION_API UDisplayClusterConfigurationData
	: public UObject
{
	GENERATED_BODY()

public:
	UDisplayClusterConfigurationData();

public:
	// Facade API
	const UDisplayClusterConfigurationClusterNode* GetClusterNode(const FString& NodeId) const;
	const UDisplayClusterConfigurationViewport*    GetViewport(const FString& NodeId, const FString& ViewportId) const;

	bool GetPostprocess(const FString& NodeId, const FString& PostprocessId, FDisplayClusterConfigurationPostprocess& OutPostprocess) const;
	bool GetProjectionPolicy(const FString& NodeId, const FString& ViewportId, FDisplayClusterConfigurationProjection& OutProjection) const;

public:
	FDisplayClusterConfigurationDataMetaInfo Meta;

	UPROPERTY(EditAnywhere, Category = nDisplay)
	FDisplayClusterConfigurationInfo Info;

	UPROPERTY()
	UDisplayClusterConfigurationScene* Scene;

	UPROPERTY()
	UDisplayClusterConfigurationCluster* Cluster;

	UPROPERTY()
	UDisplayClusterConfigurationInput* Input;

	UPROPERTY(EditAnywhere, Category = nDisplay)
	TMap<FString, FString> CustomParameters;

	UPROPERTY(VisibleAnywhere, Category = nDisplay)
	FDisplayClusterConfigurationDiagnostics Diagnostics;
};
