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
	UPROPERTY(VisibleAnywhere, Category = NDisplay)
	FString Description;

	UPROPERTY(VisibleAnywhere, Category = NDisplay)
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
USTRUCT()
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

USTRUCT()
struct FDisplayClusterConfigurationMasterNode
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FString Id;

	UPROPERTY(EditAnywhere, Category = NDisplay)
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
	UPROPERTY(EditAnywhere, Category = NDisplay)
	FDisplayClusterConfigurationRenderSyncPolicy RenderSyncPolicy;

	UPROPERTY(EditAnywhere, Category = NDisplay)
	FDisplayClusterConfigurationInputSyncPolicy InputSyncPolicy;
};


USTRUCT()
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationNetworkSettings
{
	GENERATED_BODY()

public:
	FDisplayClusterConfigurationNetworkSettings();

public:
	UPROPERTY(EditAnywhere, Category = NDisplay)
	uint32 ConnectRetriesAmount;

	UPROPERTY(EditAnywhere, Category = NDisplay)
	uint32 ConnectRetryDelay;

	UPROPERTY(EditAnywhere, Category = NDisplay)
	uint32 GameStartBarrierTimeout;

	UPROPERTY(EditAnywhere, Category = NDisplay)
	uint32 FrameStartBarrierTimeout;

	UPROPERTY(EditAnywhere, Category = NDisplay)
	uint32 FrameEndBarrierTimeout;

	UPROPERTY(EditAnywhere, Category = NDisplay)
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

	UPROPERTY(EditAnywhere, Category = NDisplay)
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
	UPROPERTY(EditAnywhere, Category = NDisplay, meta = (DisplayName = "Host IP"))
	FString Host;

	UPROPERTY(EditAnywhere, Category = NDisplay)
	bool bIsSoundEnabled;

	UPROPERTY(EditAnywhere, Category = NDisplay)
	bool bIsFullscreen;

	UPROPERTY(EditAnywhere, Category = NDisplay)
	FDisplayClusterConfigurationRectangle WindowRect;

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, Category = NDisplay)
	bool bFixedAspectRatio;
#endif

	UPROPERTY(VisibleInstanceOnly, EditFixedSize, Instanced, Category = NDisplay, meta = (DisplayThumbnail = false, ShowInnerProperties, nDisplayInstanceOnly))
	TMap<FString, UDisplayClusterConfigurationViewport*> Viewports;

	UPROPERTY(VisibleInstanceOnly, Category = NDisplay, meta = (DisplayThumbnail = false, ShowInnerProperties, nDisplayInstanceOnly))
	TMap<FString, FDisplayClusterConfigurationPostprocess> Postprocess;

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditDefaultsOnly, Category = "NDisplay", meta = (nDisplayHidden))
	bool bIsVisible;

	UPROPERTY(EditDefaultsOnly, Category = "NDisplay", meta = (nDisplayHidden))
	bool bIsEnabled;

	UPROPERTY(EditDefaultsOnly, Category = NDisplay)
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
	UPROPERTY(EditAnywhere, Category = NDisplay)
	FText HostName;

	UPROPERTY(EditAnywhere, Category = NDisplay, meta = (EditCondition = "bAllowManualPlacement"))
	FVector2D Position;

	UPROPERTY(EditAnywhere, Category = NDisplay)
	bool bAllowManualPlacement;

	UPROPERTY(EditAnywhere, Category = NDisplay, meta = (EditCondition = "bAllowManualSizing"))
	FVector2D HostResolution;

	UPROPERTY(EditAnywhere, Category = NDisplay)
	bool bAllowManualSizing;

	UPROPERTY(EditAnywhere, Category = NDisplay)
	FVector2D Origin;

	UPROPERTY(EditAnywhere, Category = NDisplay)
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
	UPROPERTY(EditAnywhere, Category = NDisplay)
	FDisplayClusterConfigurationMasterNode MasterNode;

	UPROPERTY(EditAnywhere, Category = NDisplay)
	FDisplayClusterConfigurationClusterSync Sync;

	UPROPERTY(EditAnywhere, Category = NDisplay)
	FDisplayClusterConfigurationNetworkSettings Network;

	UPROPERTY(VisibleInstanceOnly, EditFixedSize, Instanced, Category = NDisplay, meta = (DisplayThumbnail = false, nDisplayInstanceOnly, ShowInnerProperties))
	TMap<FString, UDisplayClusterConfigurationClusterNode*> Nodes;

	// Apply the global cluster post process settings to all viewports
	UPROPERTY(EditAnywhere, Category = NDisplay)
	bool bUseOverallClusterPostProcess = false;

	// Global cluster post process settings
	UPROPERTY(EditAnywhere, Category = NDisplay, meta = (EditCondition = "bUseOverallClusterPostProcess"))
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
	UPROPERTY(EditAnywhere, Category = NDisplay)
	bool bSimulateLag;

	UPROPERTY(EditAnywhere, Category = NDisplay)
	float MinLagTime;

	UPROPERTY(EditAnywhere, Category = NDisplay)
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

	UPROPERTY(EditAnywhere, Category = NDisplay)
	FDisplayClusterConfigurationInfo Info;

	UPROPERTY()
	UDisplayClusterConfigurationScene* Scene;

	UPROPERTY(VisibleInstanceOnly, Instanced, Category = NDisplay, meta = (DisplayThumbnail = false, ShowInnerProperties))
	UDisplayClusterConfigurationCluster* Cluster;

	UPROPERTY(EditAnywhere, Category = NDisplay)
	TMap<FString, FString> CustomParameters;

	UPROPERTY(EditAnywhere, Category = NDisplay)
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
