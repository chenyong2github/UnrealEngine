// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Camera/PlayerCameraManager.h"

#include "Misc/DisplayClusterObjectRef.h"
#include "DisplayClusterEnums.h"

#include "DisplayClusterRootActor.generated.h"

#if WITH_EDITOR
class IDisplayClusterConfiguratorToolkit;
class UDisplayClusterPreviewComponent;
#endif

class UMaterial;
class UCameraComponent;
class UDisplayClusterConfigurationData;
class UDisplayClusterCameraComponent;
class UDisplayClusterMeshComponent;
class UDisplayClusterSceneComponent;
class UDisplayClusterScreenComponent;
class UDisplayClusterXformComponent;
class UDisplayClusterSyncTickComponent;

// Deprecated
class UDisplayClusterRootComponent;


/**
 * VR root. This contains nDisplay VR hierarchy in the game.
 */
UCLASS()
class DISPLAYCLUSTER_API ADisplayClusterRootActor
	: public AActor
{
	friend class FDisplayClusterRootActorDetailsCustomization;

	GENERATED_BODY()

#if WITH_EDITORONLY_DATA
public:
	static const FString PreviewNodeAll;
	static const FString PreviewNodeNone;
#endif

public:
	ADisplayClusterRootActor(const FObjectInitializer& ObjectInitializer);

public:
	UE_DEPRECATED(4.26, "This feature is no longer supported.")
	inline UDisplayClusterRootComponent* GetDisplayClusterRootComponent() const
	{
		return nullptr;
	}

	UE_DEPRECATED(4.26, "This feature is no longer supported.")
	bool GetShowProjectionScreens() const
	{
		return false;
	}

	UE_DEPRECATED(4.26, "This feature is no longer supported.")
	void SetShowProjectionScreens(bool bShow)
	{ }

	UE_DEPRECATED(4.26, "This feature is no longer supported.")
	UMaterial* GetProjectionScreenMaterial() const
	{
		return nullptr;
	}

	UE_DEPRECATED(4.26, "This feature is no longer supported.")
	void SetProjectionScreenMaterial(UMaterial* NewMaterial)
	{ }

public:
	void InitializeFromConfig(const UDisplayClusterConfigurationData* ConfigData);
	void InitializeFromConfig(const FString& ConfigFile);

public:
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get Screens Amount"), Category = "DisplayCluster|Components")
	int32 GetScreensAmount() const;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get Screen By ID"), Category = "DisplayCluster|Components")
	UDisplayClusterScreenComponent* GetScreenById(const FString& ScreenId) const;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get All Screens"), Category = "DisplayCluster|Components")
	void GetAllScreens(TMap<FString, UDisplayClusterScreenComponent*>& OutScreens) const;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get Cameras Amount"), Category = "DisplayCluster|Components")
	int32 GetCamerasAmount() const;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get Cameras By ID"), Category = "DisplayCluster|Components")
	UDisplayClusterCameraComponent* GetCameraById(const FString& CameraId) const;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get All Cameras"), Category = "DisplayCluster|Components")
	void GetAllCameras(TMap<FString, UDisplayClusterCameraComponent*>& OutCameras) const;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get Default Camera"), Category = "DisplayCluster|Components")
	UDisplayClusterCameraComponent* GetDefaultCamera() const;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set Default Camera"), Category = "DisplayCluster|Components")
	void SetDefaultCamera(const FString& CameraId);

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get Meshes Amount"), Category = "DisplayCluster|Components")
	int32 GetMeshesAmount() const;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get Mesh By ID"), Category = "DisplayCluster|Components")
	UDisplayClusterMeshComponent* GetMeshById(const FString& MeshId) const;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get All Meshes"), Category = "DisplayCluster|Components")
	void GetAllMeshes(TMap<FString, UDisplayClusterMeshComponent*>& OutMeshes) const;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get Xforms Amount"), Category = "DisplayCluster|Components")
	int32 GetXformsAmount() const;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get Xform By ID"), Category = "DisplayCluster|Components")
	UDisplayClusterXformComponent* GetXformById(const FString& XformId) const;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get All Xforms"), Category = "DisplayCluster|Components")
	void GetAllXforms(TMap<FString, UDisplayClusterXformComponent*>& OutXforms) const;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get All Components Amount"), Category = "DisplayCluster|Components")
	int32 GetComponentsAmount() const;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get All Components"), Category = "DisplayCluster|Components")
	void GetAllComponents(TMap<FString, UDisplayClusterSceneComponent*>& OutComponents) const;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get Component By ID"), Category = "DisplayCluster|Components")
	UDisplayClusterSceneComponent* GetComponentById(const FString& ComponentId) const;

protected:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// AActor
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void PostLoad() override;
	virtual void PostActorCreated() override;

protected:
	TMap<FString, FDisplayClusterSceneComponentRef*> AllComponents;
	TMap<FString, FDisplayClusterSceneComponentRef*> XformComponents;
	TMap<FString, FDisplayClusterSceneComponentRef*> CameraComponents;
	TMap<FString, FDisplayClusterSceneComponentRef*> ScreenComponents;
	TMap<FString, FDisplayClusterSceneComponentRef*> MeshComponents;
	FDisplayClusterSceneComponentRef DefaultCameraComponent;

protected:
	// Initializes the actor on spawn and load
	void InitializeRootActor();
	// Creates all hierarchy objects declared in a config file
	virtual bool BuildHierarchy(const UDisplayClusterConfigurationData* ConfigData);
	// Cleans current hierarchy
	virtual void CleanupHierarchy();

private:
	template <typename TComp, typename TCfgData>
	void SpawnComponents(const TMap<FString, TCfgData*>& InConfigData, TMap<FString, FDisplayClusterSceneComponentRef*>& OutTypedMap, TMap<FString, FDisplayClusterSceneComponentRef*>& OutAllMap);

	template <typename TComp>
	TComp* GetTypedComponentById(const FString& ComponentId, const TMap<FString, FDisplayClusterSceneComponentRef*>& InTypedMap) const;

	template <typename TComp>
	void GetTypedComponents(TMap<FString, TComp*>& OutTypedMap, const TMap<FString, FDisplayClusterSceneComponentRef*>& InTypedMap) const;

protected:
	UPROPERTY(EditAnywhere, Category = "DisplayCluster", meta = (DisplayName = "Exit when ESC pressed"))
	bool bExitOnEsc;

private:
	// Current operation mode
	EDisplayClusterOperationMode OperationMode;

	mutable FCriticalSection InternalsSyncScope;

	UPROPERTY(Transient)
	const UDisplayClusterConfigurationData* CurrentConfigData;

	UPROPERTY()
	UDisplayClusterSyncTickComponent* SyncTickComponent;


	//////////////////////////////////////////////////////////////////////////////////////////////
	// EDITOR STUFF
	//////////////////////////////////////////////////////////////////////////////////////////////
#if WITH_EDITOR
public:

	// We need tick in Editor
	virtual bool ShouldTickIfViewportsOnly() const override
	{
		return true;
	}

	FString GetPreviewConfigPath() const
	{
		return PreviewConfigPath.FilePath;
	}

	FString GetPreviewDefaultCamera() const
	{
		return PreviewDefaultCameraId;
	}

	FString GetPreviewNodeId() const
	{
		return PreviewNodeId;
	}

	void SetPreviewNodeId(const FString& NodeId)
	{
		PreviewNodeId = NodeId;
		RebuildPreview();
	}

	const UDisplayClusterConfigurationData* GetConfigData() const
	{
		return CurrentConfigData;
	}

	TWeakPtr<IDisplayClusterConfiguratorToolkit> GetToolkit() const
	{
		return ToolkitPtr;
	}

	void SetToolkit(TWeakPtr<IDisplayClusterConfiguratorToolkit> Toolkit)
	{
		ToolkitPtr = Toolkit;
	}

	UDisplayClusterPreviewComponent* GetPreviewComponent(const FString& NodeId, const FString& ViewportId);

	TSharedPtr<TMap<UObject*, FString>> GenerateObjectsNamingMap() const;
	void SelectComponent(const FString& SelectedComponent);

protected:
	FString GeneratePreviewComponentName(const FString& NodeId, const FString& ViewportId) const;

	void CleanupPreview();
	void RebuildPreview();

protected:
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

#if WITH_EDITORONLY_DATA
protected:
	UPROPERTY(EditAnywhere, Category = "Preview (Editor only)", meta = (DisplayName = "Preview Config File", FilePathFilter = "cfg;*.ndisplay"))
	FFilePath PreviewConfigPath;

	UPROPERTY(EditAnywhere, Category = "Preview (Editor only)", meta = (DisplayName = "Preview Node ID"))
	FString PreviewNodeId;

	UPROPERTY(EditAnywhere, Category = "Preview (Editor only)", meta = (DisplayName = "Preview Default Camera ID"))
	FString PreviewDefaultCameraId;

private:
	UPROPERTY(Transient)
	TMap<FString, UDisplayClusterPreviewComponent*> PreviewComponents;

	TWeakPtr<IDisplayClusterConfiguratorToolkit> ToolkitPtr;
#endif
};
