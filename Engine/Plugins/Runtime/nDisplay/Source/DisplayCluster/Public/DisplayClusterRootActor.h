// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "SceneTypes.h"

#include "GameFramework/Actor.h"
#include "Camera/PlayerCameraManager.h"

#include "Misc/DisplayClusterObjectRef.h"
#include "DisplayClusterEnums.h"

#include "SceneInterface.h"

#include "DisplayClusterConfigurationStrings.h"
#include "DisplayClusterConfigurationTypes_Viewport.h"
#include "DisplayClusterConfigurationTypes_ICVFX.h"

#include "Render/Viewport/IDisplayClusterViewportManager.h"

#include "DisplayClusterRootActor.generated.h"

#if WITH_EDITOR
class IDisplayClusterConfiguratorBlueprintEditor;
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
class UDisplayClusterPreviewComponent;

/**
 * VR root. This contains nDisplay VR hierarchy in the game.
 */
UCLASS(meta=(DisplayName = "nDisplay Root Actor"))
class DISPLAYCLUSTER_API ADisplayClusterRootActor
	: public AActor
{
	friend class FDisplayClusterRootActorDetailsCustomization;

	GENERATED_BODY()

public:
	ADisplayClusterRootActor(const FObjectInitializer& ObjectInitializer);
	~ADisplayClusterRootActor();

public:
	void InitializeFromConfig(UDisplayClusterConfigurationData* ConfigData);
	void InitializeFromConfig(const FString& ConfigFile);
	void ApplyConfigDataToComponents();

	/**
	 * Update or create the config data object. The config sub object is only instantiated once.
	 * Subsequent calls will only update ConfigDataName unless bForceRecreate is true.
	 *
	 * @param ConfigDataTemplate The config template to use for this actors' config data object.
	 * @param bForceRecreate Deep copies properties from the config data template to this actors' config data object.
	 */
	void UpdateConfigDataInstance(UDisplayClusterConfigurationData* ConfigDataTemplate, bool bForceRecreate = false);

	bool IsRunningGameOrPIE() const;

	UDisplayClusterConfigurationData* GetDefaultConfigDataFromAsset() const;
	UDisplayClusterConfigurationData* GetConfigData() const;

	// Return hidden in game privitives set
	bool GetHiddenInGamePrimitives(TSet<FPrimitiveComponentId>& OutPrimitives);
	bool FindPrimitivesByName(const TArray<FString>& InNames, TSet<FPrimitiveComponentId>& OutPrimitives);

	bool IsBlueprint() const;

	UDisplayClusterSyncTickComponent* GetSyncTickComponent() const { return SyncTickComponent; }

	const FDisplayClusterConfigurationICVFX_StageSettings& GetStageSettings() const;
	const FDisplayClusterConfigurationRenderFrame& GetRenderFrameSettings() const;

	UDisplayClusterConfigurationViewport* GetViewportConfiguration(const FString& ClusterNodeID, const FString& ViewportID);

protected:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// AActor
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void PostLoad() override;
	virtual void PostActorCreated() override;
	virtual void BeginDestroy() override;
	virtual void RerunConstructionScripts() override;

	virtual void Destroyed() override;

	// Cleans current hierarchy
	virtual void CleanupHierarchy();
	virtual void ResetHierarchyMap();

	// Initializes the actor on spawn and load
	void InitializeRootActor();

	// Creates all hierarchy objects declared in a config file
	bool BuildHierarchy();

public:
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get Screens Amount"), Category = "NDisplay|Components")
	int32 GetScreensAmount() const;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get Screen By ID"), Category = "NDisplay|Components")
	UDisplayClusterScreenComponent* GetScreenById(const FString& ScreenId) const;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get All Screens"), Category = "NDisplay|Components")
	void GetAllScreens(TMap<FString, UDisplayClusterScreenComponent*>& OutScreens) const;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get Cameras Amount"), Category = "NDisplay|Components")
	int32 GetCamerasAmount() const;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get Cameras By ID"), Category = "NDisplay|Components")
	UDisplayClusterCameraComponent* GetCameraById(const FString& CameraId) const;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get All Cameras"), Category = "NDisplay|Components")
	void GetAllCameras(TMap<FString, UDisplayClusterCameraComponent*>& OutCameras) const;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get Default Camera"), Category = "NDisplay|Components")
	UDisplayClusterCameraComponent* GetDefaultCamera() const;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set Default Camera"), Category = "NDisplay|Components")
	void SetDefaultCamera(const FString& CameraId);

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get Meshes Amount"), Category = "NDisplay|Components")
	int32 GetMeshesAmount() const;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get Mesh By ID"), Category = "NDisplay|Components")
	UStaticMeshComponent* GetMeshById(const FString& MeshId) const;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get All Meshes"), Category = "NDisplay|Components")
	void GetAllMeshes(TMap<FString, UDisplayClusterMeshComponent*>& OutMeshes) const;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get Xforms Amount"), Category = "NDisplay|Components")
	int32 GetXformsAmount() const;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get Xform By ID"), Category = "NDisplay|Components")
	UDisplayClusterXformComponent* GetXformById(const FString& XformId) const;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get All Xforms"), Category = "NDisplay|Components")
	void GetAllXforms(TMap<FString, UDisplayClusterXformComponent*>& OutXforms) const;

	UE_DEPRECATED(4.27, "Use 'GetComponentsByClass' instead and retrieve the length")
	UFUNCTION(BlueprintCallable, meta = (DeprecatedFunction, DeprecationMessage = "Use 'GetComponentsByClass' instead and retrieve the length", DisplayName = "Get All Components Amount"), Category = "NDisplay|Components")
	int32 GetComponentsAmount() const;

	UE_DEPRECATED(4.27, "Use 'GetComponentsByClass' instead")
	UFUNCTION(BlueprintCallable, meta = (DeprecatedFunction, DeprecationMessage = "Use 'GetComponentsByClass' instead", DisplayName = "Get All Components"), Category = "NDisplay|Components")
	void GetAllComponents(TMap<FString, UDisplayClusterSceneComponent*>& OutComponents) const;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get Component By ID"), Category = "NDisplay|Components")
	UDisplayClusterSceneComponent* GetComponentById(const FString& ComponentId) const;

public:
	IDisplayClusterViewportManager* GetViewportManager() const
	{
		return ViewportManager.IsValid() ? ViewportManager.Get() : nullptr;
	}

protected:
	// Unique viewport manager for this configuration
	TUniquePtr<IDisplayClusterViewportManager> ViewportManager;

private:
	/**
	 * Name of the CurrentConfigData asset. Only required if this is a parent of a DisplayClusterBlueprint.
	 * The name is used to lookup the config data as a default sub-object, specifically in packaged builds.
	 */
	UPROPERTY()
	FName ConfigDataName;

	/**
	 * If set from the DisplayCluster BP Compiler it will be loaded from the class default subobjects in run-time.
	 */
	UPROPERTY(VisibleAnywhere, Instanced, Category = "NDisplay")
	UDisplayClusterConfigurationData* CurrentConfigData;

	/**
	 * The root component for our hierarchy.
	 * Must have CPF_Edit(such as VisibleDefaultsOnly) on property for Live Link.
	 * nDisplay details panel will hide this from actually being visible.
	 */
	UPROPERTY(VisibleDefaultsOnly, Category = "NDisplay")
	USceneComponent* DisplayClusterRootComponent;
	
	UPROPERTY()
	UDisplayClusterSyncTickComponent* SyncTickComponent;

private:
	// Current operation mode
	EDisplayClusterOperationMode OperationMode;
	mutable FCriticalSection InternalsSyncScope;

	TMap<FString, FDisplayClusterSceneComponentRef*> AllComponents;
	TMap<FString, FDisplayClusterSceneComponentRef*> XformComponents;
	TMap<FString, FDisplayClusterSceneComponentRef*> CameraComponents;
	TMap<FString, FDisplayClusterSceneComponentRef*> ScreenComponents;
	TMap<FString, FDisplayClusterSceneComponentRef*> MeshComponents;
	FDisplayClusterSceneComponentRef DefaultCameraComponent;

private:
	template <typename TComp>
	void GetTypedPrimitives(TSet<FPrimitiveComponentId>& OutPrimitives, const TArray<FString>* InCompNames = nullptr, bool bCollectChildrenVisualizationComponent = true) const;

	template <typename TComp, typename TCfgData>
	void SpawnComponents(const TMap<FString, TCfgData*>& InConfigData, TMap<FString, FDisplayClusterSceneComponentRef*>& OutTypedMap, TMap<FString, FDisplayClusterSceneComponentRef*>& OutAllMap);

	template <typename TComp>
	TComp* GetTypedComponentById(const FString& ComponentId, const TMap<FString, FDisplayClusterSceneComponentRef*>& InTypedMap) const;

	template <typename TComp>
	void GetTypedComponents(TMap<FString, TComp*>& OutTypedMap, const TMap<FString, FDisplayClusterSceneComponentRef*>& InTypedMap) const;

//////////////////////////////////////////////////////////////////////////////////////////////
// EDITOR STUFF
//////////////////////////////////////////////////////////////////////////////////////////////
#if WITH_EDITORONLY_DATA 
public:
	// Render single node preview or whole cluster
	UPROPERTY(EditAnywhere, Category = "NDisplay Preview (Editor only)")
	FString PreviewNodeId = DisplayClusterConfigurationStrings::gui::preview::PreviewNodeAll;

	// Render mode for PIE
	UPROPERTY(EditAnywhere, Category = "NDisplay Preview (Editor only)")
	EDisplayClusterConfigurationRenderMode RenderMode = EDisplayClusterConfigurationRenderMode::Mono;

	// Allow preview render
	UPROPERTY(EditAnywhere, Category = "NDisplay Preview (Editor only)")
	bool bPreviewEnable = true;

	// Update preview texture period in tick
	UPROPERTY(EditAnywhere, Category = "NDisplay Preview (Editor only)", meta = (ClampMin = "1", UIMin = "1", ClampMax = "200", UIMax = "200"))
	int TickPerFrame = 1;

	// Preview texture size get from viewport, and scaled by this value
	UPROPERTY(EditAnywhere, Category = "NDisplay Preview (Editor only)", meta = (ClampMin = "0.05", UIMin = "0.05", ClampMax = "1", UIMax = "1"))
	float PreviewRenderTargetRatioMult = 0.25;

	UPROPERTY(EditAnywhere, Category = "NDisplay Preview (Editor only)")
	float XformGizmoScale = 1.0f;

	UPROPERTY(EditAnywhere, Category = "NDisplay Preview (Editor only)")
	bool bAreXformGizmosVisible = true;

private:
	UPROPERTY(Transient)
	TMap<FString, UDisplayClusterPreviewComponent*> PreviewComponents;

	UPROPERTY(Transient)
	bool bDeferPreviewGeneration;
#endif

#if WITH_EDITOR
public:
	DECLARE_DELEGATE(FOnPreviewUpdated);

	/** If the root actor is being displayed in an editor viewport, this variable allows the viewport to scale the xform gizmos independently of the actor's gizmo scale property. */
	float EditorViewportXformGizmoScale = 1.0f;

	/** If the root actor is being displayed in an editor viewport, this variable allows the viewport to hide the xform gizmos independently of the actor's gizmo visibility flag. */
	bool bEditorViewportXformGizmoVisibility = true;

private:
	TWeakPtr<IDisplayClusterConfiguratorBlueprintEditor> ToolkitPtr;

	int32 TickPerFrameCounter = 0;

	FOnPreviewUpdated OnPreviewGenerated;
	FOnPreviewUpdated OnPreviewDestroyed;

public:
	// We need tick in Editor
	virtual bool ShouldTickIfViewportsOnly() const override { return true; }

	FOnPreviewUpdated& GetOnPreviewGenerated() { return OnPreviewGenerated; }
	FOnPreviewUpdated& GetOnPreviewDestroyed() { return OnPreviewDestroyed; }

	// return true, if preview enabled for this actor
	bool IsPreviewEnabled() const;

	void Constructor_Editor();
	void Destructor_Editor();

	void Tick_Editor(float DeltaSeconds);
	void PostLoad_Editor();
	void BeginDestroy_Editor();
	void RerunConstructionScripts_Editor();

	void Destroyed_Editor();

	TWeakPtr<IDisplayClusterConfiguratorBlueprintEditor> GetToolkit() const { return ToolkitPtr; }

	void SetToolkit(TWeakPtr<IDisplayClusterConfiguratorBlueprintEditor> Toolkit) { ToolkitPtr = Toolkit; }

	UDisplayClusterPreviewComponent* GetPreviewComponent(const FString& NodeId, const FString& ViewportId);
	TSharedPtr<TMap<UObject*, FString>> GenerateObjectsNamingMap() const;
	void SelectComponent(const FString& SelectedComponent);

	void UpdatePreviewComponents();
	void ReleasePreviewComponents();

	float GetPreviewRenderTargetRatioMult() const { return PreviewRenderTargetRatioMult; };

	IDisplayClusterViewport* FindPreviewViewport(const FString& InViewportId) const;

	void GetPreviewRenderTargetableTextures(const TArray<FString>& InViewportNames, TArray<FTextureRHIRef>& OutTextures) const;

	float GetXformGizmoScale() const;
	bool GetXformGizmoVisibility() const;

	void UpdateXformGizmos();

	virtual bool IsSelectedInEditor() const override;
	void SetIsSelectedInEditor(bool bValue);

private:
	bool bIsSelectedInEditor = false;
	
protected:
	FString GeneratePreviewComponentName(const FString& NodeId, const FString& ViewportId) const;
	void RenderPreview_Editor();
	bool UpdatePreviewConfiguration_Editor();

	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditMove(bool bFinished) override;

#endif
};
