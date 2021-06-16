// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"

#include "DisplayClusterConfigurationTypes_Base.h"
#include "DisplayClusterConfigurationTypes_ICVFX.h"
#include "DisplayClusterConfigurationTypes_Viewport.h"

#include "DisplayClusterConfigurationTypes.generated.h"

class UStaticMesh;
struct FPropertyChangedChainEvent;


USTRUCT(Blueprintable)
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationInfo
{
	GENERATED_BODY()

public:
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = NDisplay)
	FString Description;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = NDisplay)
	FString Version;
};

// Scene hierarchy
UCLASS()
class DISPLAYCLUSTERCONFIGURATION_API UDisplayClusterConfigurationSceneComponent
	: public UDisplayClusterConfigurationData_Base
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
	UPROPERTY(EditAnywhere, Category = NDisplay)
	FString ParentId;

	UPROPERTY(EditAnywhere, Category = NDisplay)
	FVector Location;

	UPROPERTY(EditAnywhere, Category = NDisplay)
	FRotator Rotation;

	UPROPERTY(EditAnywhere, Category = NDisplay)
	FString TrackerId;

	UPROPERTY(EditAnywhere, Category = NDisplay)
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
	UPROPERTY(EditAnywhere, Category = NDisplay)
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
	UPROPERTY(EditAnywhere, Category = NDisplay)
	float InterpupillaryDistance;

	UPROPERTY(EditAnywhere, Category = NDisplay)
	bool bSwapEyes;

	UPROPERTY(EditAnywhere, Category = NDisplay)
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
	UPROPERTY(EditAnywhere, Category = NDisplay)
	UStaticMesh* Asset;
#endif
};

UCLASS()
class DISPLAYCLUSTERCONFIGURATION_API UDisplayClusterConfigurationScene
	: public UDisplayClusterConfigurationData_Base
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

protected:
	virtual void GetObjectsToExport(TArray<UObject*>& OutObjects) override;

};


////////////////////////////////////////////////////////////////
// Cluster
USTRUCT(Blueprintable)
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationMasterNodePorts
{
	GENERATED_BODY()

public:
	FDisplayClusterConfigurationMasterNodePorts();

public:
	UPROPERTY(EditAnywhere, Category = NDisplay)
	uint16 ClusterSync;

	UPROPERTY(EditAnywhere, Category = NDisplay)
	uint16 RenderSync;

	UPROPERTY(EditAnywhere, Category = NDisplay)
	uint16 ClusterEventsJson;

	UPROPERTY(EditAnywhere, Category = NDisplay)
	uint16 ClusterEventsBinary;
};

USTRUCT(Blueprintable)
struct FDisplayClusterConfigurationMasterNode
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly, Category = NDisplay)
	FString Id;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = NDisplay)
	FDisplayClusterConfigurationMasterNodePorts Ports;
};

USTRUCT(Blueprintable)
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationRenderSyncPolicy
	: public FDisplayClusterConfigurationPolymorphicEntity
{
	GENERATED_BODY()
};

USTRUCT(Blueprintable)
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationInputSyncPolicy
	: public FDisplayClusterConfigurationPolymorphicEntity
{
	GENERATED_BODY()
};

USTRUCT(Blueprintable)
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationClusterSync
{
	GENERATED_BODY()

public:
	FDisplayClusterConfigurationClusterSync();
	
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = NDisplay)
	FDisplayClusterConfigurationRenderSyncPolicy RenderSyncPolicy;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = NDisplay)
	FDisplayClusterConfigurationInputSyncPolicy InputSyncPolicy;
};


USTRUCT(Blueprintable)
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationNetworkSettings
{
	GENERATED_BODY()

public:
	FDisplayClusterConfigurationNetworkSettings();

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = NDisplay)
	int32 ConnectRetriesAmount;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = NDisplay)
	int32 ConnectRetryDelay;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = NDisplay)
	int32 GameStartBarrierTimeout;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = NDisplay)
	int32 FrameStartBarrierTimeout;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = NDisplay)
	int32 FrameEndBarrierTimeout;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = NDisplay)
	int32 RenderSyncBarrierTimeout;
};



USTRUCT(Blueprintable)
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationPostprocess
	: public FDisplayClusterConfigurationPolymorphicEntity
{
	GENERATED_BODY()
};


USTRUCT()
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationExternalImage
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = NDisplay)
	FString ImagePath;
};

UCLASS(Blueprintable)
class DISPLAYCLUSTERCONFIGURATION_API UDisplayClusterConfigurationClusterNode
	: public UDisplayClusterConfigurationData_Base
{
	GENERATED_BODY()

public:
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnPostEditChangeChainProperty, const FPropertyChangedChainEvent&);

	FOnPostEditChangeChainProperty OnPostEditChangeChainProperty;

public:
	UDisplayClusterConfigurationClusterNode();

	// Return all references to meshes from policy, and other
	void GetReferencedMeshNames(TArray<FString>& OutMeshNames) const;
	
private:
#if WITH_EDITOR
	// UObject interface
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent) override;
	// End of UObject interface
#endif

public:
	UPROPERTY(EditAnywhere, BlueprintReadonly, Category = "Configuration", meta = (DisplayName = "Host IP"))
	FString Host;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Configuration")
	bool bIsSoundEnabled;

	UPROPERTY(EditAnywhere, BlueprintReadonly, Category = "Configuration")
	bool bIsFullscreen;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Configuration")
	FDisplayClusterConfigurationRectangle WindowRect;

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, Category = "Configuration")
	bool bFixedAspectRatio;
#endif

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, EditFixedSize, Instanced, Category = "Configuration", meta = (DisplayThumbnail = false, ShowInnerProperties, nDisplayInstanceOnly))
	TMap<FString, UDisplayClusterConfigurationViewport*> Viewports;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Configuration", meta = (DisplayThumbnail = false, ShowInnerProperties, nDisplayInstanceOnly))
	TMap<FString, FDisplayClusterConfigurationPostprocess> Postprocess;

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditDefaultsOnly, Category = "Configuration", meta = (nDisplayHidden))
	bool bIsVisible;

	UPROPERTY(EditDefaultsOnly, Category = "Configuration", meta = (nDisplayHidden))
	bool bIsEnabled;

	UPROPERTY(EditDefaultsOnly, Category = "Configuration")
	FDisplayClusterConfigurationExternalImage PreviewImage;
#endif

protected:
	virtual void GetObjectsToExport(TArray<UObject*>& OutObjects) override;
};

UCLASS(Blueprintable)
class DISPLAYCLUSTERCONFIGURATION_API UDisplayClusterConfigurationHostDisplayData : public UObject
{
	GENERATED_BODY()

public:
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnPostEditChangeChainProperty, const FPropertyChangedChainEvent&);

	FOnPostEditChangeChainProperty OnPostEditChangeChainProperty;

public:
	UDisplayClusterConfigurationHostDisplayData();

private:
	#if WITH_EDITOR
	// UObject interface
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent) override;
	// End of UObject interface
	#endif

public:
	UPROPERTY(EditAnywhere, Category = "Configuration")
	FText HostName;

	UPROPERTY(EditAnywhere, Category = "Configuration", meta = (EditCondition = "bAllowManualPlacement"))
	FVector2D Position;

	UPROPERTY(EditAnywhere, Category = "Configuration")
	bool bAllowManualPlacement;

	UPROPERTY(EditAnywhere, Category = "Configuration", meta = (EditCondition = "bAllowManualSizing"))
	FVector2D HostResolution;

	UPROPERTY(EditAnywhere, Category = "Configuration")
	bool bAllowManualSizing;

	UPROPERTY(EditAnywhere, Category = "Configuration")
	FVector2D Origin;

	UPROPERTY(EditAnywhere, Category = "Configuration")
	FLinearColor Color;
	
	UPROPERTY()
	bool bIsVisible;

	UPROPERTY()
	bool bIsEnabled;
};

UCLASS(Blueprintable)
class DISPLAYCLUSTERCONFIGURATION_API UDisplayClusterConfigurationCluster
	: public UDisplayClusterConfigurationData_Base
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Configuration")
	FDisplayClusterConfigurationMasterNode MasterNode;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Configuration")
	FDisplayClusterConfigurationClusterSync Sync;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Configuration")
	FDisplayClusterConfigurationNetworkSettings Network;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, EditFixedSize, Instanced, Category = NDisplay, meta = (DisplayThumbnail = false, nDisplayInstanceOnly, ShowInnerProperties))
	TMap<FString, UDisplayClusterConfigurationClusterNode*> Nodes;

	// Apply the global cluster post process settings to all viewports
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Post Process", meta = (DisplayName = "Enable Enitre Cluster Color Grading"))
	bool bUseOverallClusterPostProcess = true;

	//!
	// Global cluster post process settings
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Post Process", meta = (DisplayName = "Cluster Color Grading Settings", EditCondition = "bUseOverallClusterPostProcess"))
	FDisplayClusterConfigurationViewport_PerViewportSettings OverallClusterPostProcessSettings;


#if WITH_EDITORONLY_DATA
	UPROPERTY(Instanced)
	TMap<FString, UDisplayClusterConfigurationHostDisplayData*> HostDisplayData;
#endif

protected:
	virtual void GetObjectsToExport(TArray<UObject*>& OutObjects) override;

public:
	// Return all references to meshes from policy, and other
	void GetReferencedMeshNames(TArray<FString>& OutMeshNames) const;
};

USTRUCT(Blueprintable)
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationDiagnostics
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = NDisplay)
	bool bSimulateLag;

	UPROPERTY(EditAnywhere, Category = NDisplay)
	float MinLagTime;

	UPROPERTY(EditAnywhere, Category = NDisplay)
	float MaxLagTime;
};

struct FDisplayClusterConfigurationDataMetaInfo
{
	EDisplayClusterConfigurationDataSource ImportDataSource;
	FString ImportFilePath;
	FString ExportAssetPath;
};

////////////////////////////////////////////////////////////////
// Main configuration data container
UCLASS(Blueprintable, BlueprintType, PerObjectConfig, config = EditorPerProjectUserSettings)
class DISPLAYCLUSTERCONFIGURATION_API UDisplayClusterConfigurationData
	: public UDisplayClusterConfigurationData_Base
{
	GENERATED_BODY()

public:
	UDisplayClusterConfigurationData();

public:
	// Facade API
	const UDisplayClusterConfigurationClusterNode* GetClusterNode(const FString& NodeId) const;
	const UDisplayClusterConfigurationViewport*    GetViewport(const FString& NodeId, const FString& ViewportId) const;

	UDisplayClusterConfigurationViewport* GetViewportConfiguration(const FString& NodeId, const FString& ViewportId);

	bool GetPostprocess(const FString& NodeId, const FString& PostprocessId, FDisplayClusterConfigurationPostprocess& OutPostprocess) const;
	bool GetProjectionPolicy(const FString& NodeId, const FString& ViewportId, FDisplayClusterConfigurationProjection& OutProjection) const;

	// Return all references to meshes from policy, and other
	void GetReferencedMeshNames(TArray<FString>& OutMeshNames) const;

public:
	FDisplayClusterConfigurationDataMetaInfo Meta;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Advanced)
	FDisplayClusterConfigurationInfo Info;

	UPROPERTY()
	UDisplayClusterConfigurationScene* Scene;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Instanced, Category = Advanced, meta = (DisplayThumbnail = false, ShowInnerProperties))
	UDisplayClusterConfigurationCluster* Cluster;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Advanced)
	TMap<FString, FString> CustomParameters;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Advanced)
	FDisplayClusterConfigurationDiagnostics Diagnostics;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = NDisplay)
	FDisplayClusterConfigurationICVFX_StageSettings StageSettings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Advanced)
	FDisplayClusterConfigurationRenderFrame RenderFrameSettings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Advanced, meta = (DisplayName = "Follow Local Player Camera"))
	bool bFollowLocalPlayerCamera = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Advanced, meta = (DisplayName = "Exit when ESC pressed"))
	bool bExitOnEsc = true;

	/** Create empty config data. */
	static UDisplayClusterConfigurationData* CreateNewConfigData(UObject* Owner = nullptr, EObjectFlags ObjectFlags = RF_NoFlags);
#if WITH_EDITORONLY_DATA

public:
	UPROPERTY(config)
	FString PathToConfig;

public:
	const static TSet<FString> RenderSyncPolicies;

	const static TSet<FString> InputSyncPolicies;

	const static TSet<FString> ProjectionPolicies;
	
#endif
};
