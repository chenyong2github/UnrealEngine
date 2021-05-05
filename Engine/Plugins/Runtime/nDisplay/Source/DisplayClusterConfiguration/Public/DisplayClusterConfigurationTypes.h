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
USTRUCT()
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationMasterNodePorts
{
	GENERATED_BODY()

public:
	FDisplayClusterConfigurationMasterNodePorts();

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
	FDisplayClusterConfigurationClusterSync();
	
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
	FDisplayClusterConfigurationNetworkSettings();

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
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationPostprocess
	: public FDisplayClusterConfigurationPolymorphicEntity
{
	GENERATED_BODY()
};


USTRUCT()
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationExternalImage
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = nDisplay)
	FString ImagePath;
};

UCLASS()
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
	UPROPERTY(EditAnywhere, Category = nDisplay, meta = (DisplayName = "Host IP"))
	FString Host;

	UPROPERTY(EditAnywhere, Category = nDisplay)
	bool bIsSoundEnabled;

	UPROPERTY(EditAnywhere, Category = nDisplay)
	bool bIsFullscreen;

	UPROPERTY(EditAnywhere, Category = nDisplay)
	FDisplayClusterConfigurationRectangle WindowRect;

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, Category = nDisplay)
	bool bFixedAspectRatio;
#endif

	UPROPERTY(VisibleInstanceOnly, EditFixedSize, Instanced, Category = nDisplay, meta = (DisplayThumbnail = false, ShowInnerProperties, nDisplayInstanceOnly))
	TMap<FString, UDisplayClusterConfigurationViewport*> Viewports;

	UPROPERTY(VisibleInstanceOnly, Category = nDisplay, meta = (DisplayThumbnail = false, ShowInnerProperties, nDisplayInstanceOnly))
	TMap<FString, FDisplayClusterConfigurationPostprocess> Postprocess;

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditDefaultsOnly, Category = "nDisplay", meta = (nDisplayHidden))
	bool bIsVisible;

	UPROPERTY(EditDefaultsOnly, Category = "nDisplay", meta = (nDisplayHidden))
	bool bIsEnabled;

	UPROPERTY(EditDefaultsOnly, Category = nDisplay)
	FDisplayClusterConfigurationExternalImage PreviewImage;
#endif

protected:
	virtual void GetObjectsToExport(TArray<UObject*>& OutObjects) override;
};

UCLASS()
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
	UPROPERTY(EditAnywhere, Category = nDisplay)
	FText HostName;

	UPROPERTY(EditAnywhere, Category = nDisplay, meta = (EditCondition = "bAllowManualPlacement"))
	FVector2D Position;

	UPROPERTY(EditAnywhere, Category = nDisplay)
	bool bAllowManualPlacement;

	UPROPERTY(EditAnywhere, Category = nDisplay, meta = (EditCondition = "bAllowManualSizing"))
	FVector2D HostResolution;

	UPROPERTY(EditAnywhere, Category = nDisplay)
	bool bAllowManualSizing;

	UPROPERTY(EditAnywhere, Category = nDisplay)
	FVector2D Origin;

	UPROPERTY(EditAnywhere, Category = nDisplay)
	FLinearColor Color;
	
	UPROPERTY()
	bool bIsVisible;

	UPROPERTY()
	bool bIsEnabled;
};

UCLASS()
class DISPLAYCLUSTERCONFIGURATION_API UDisplayClusterConfigurationCluster
	: public UDisplayClusterConfigurationData_Base
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = nDisplay)
	FDisplayClusterConfigurationMasterNode MasterNode;

	UPROPERTY(EditAnywhere, Category = nDisplay)
	FDisplayClusterConfigurationClusterSync Sync;

	UPROPERTY(EditAnywhere, Category = nDisplay)
	FDisplayClusterConfigurationNetworkSettings Network;

	UPROPERTY(VisibleInstanceOnly, EditFixedSize, Instanced, Category = nDisplay, meta = (DisplayThumbnail = false, nDisplayInstanceOnly, ShowInnerProperties))
	TMap<FString, UDisplayClusterConfigurationClusterNode*> Nodes;

	// Apply the global cluster post process settings to all viewports
	UPROPERTY(EditAnywhere, Category = nDisplay)
	bool bUseOverallClusterPostProcess = false;

	// Global cluster post process settings
	UPROPERTY(EditAnywhere, Category = nDisplay, meta = (EditCondition = "bUseOverallClusterPostProcess"))
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
UCLASS(Blueprintable, BlueprintType)
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

	UPROPERTY(EditAnywhere, Category = nDisplay)
	FDisplayClusterConfigurationInfo Info;

	UPROPERTY()
	UDisplayClusterConfigurationScene* Scene;

	UPROPERTY(VisibleInstanceOnly, Instanced, Category = nDisplay, meta = (DisplayThumbnail = false, ShowInnerProperties))
	UDisplayClusterConfigurationCluster* Cluster;

	UPROPERTY(EditAnywhere, Category = nDisplay)
	TMap<FString, FString> CustomParameters;

	UPROPERTY(EditAnywhere, Category = nDisplay)
	FDisplayClusterConfigurationDiagnostics Diagnostics;

	/** Create empty config data. */
	static UDisplayClusterConfigurationData* CreateNewConfigData(UObject* Owner = nullptr, EObjectFlags ObjectFlags = RF_NoFlags);
#if WITH_EDITORONLY_DATA

public:
	UPROPERTY()
	FString PathToConfig;

public:
	const static TSet<FString> RenderSyncPolicies;

	const static TSet<FString> InputSyncPolicies;

	const static TSet<FString> ProjectionPolicies;
	
#endif
};
