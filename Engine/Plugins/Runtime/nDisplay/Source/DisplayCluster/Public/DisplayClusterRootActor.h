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
#include "DisplayClusterConfigurationTypes_OCIO.h"
#include "DisplayClusterEditorPropertyReference.h"

#include "Render/Viewport/IDisplayClusterViewportManager.h"
#include "Render/Viewport/RenderFrame/DisplayClusterRenderFrame.h"

#include "DisplayClusterRootActor.generated.h"

#if WITH_EDITOR
class IDisplayClusterConfiguratorBlueprintEditor;
#endif

class USceneComponent;
class UDisplayClusterConfigurationData;
class UDisplayClusterCameraComponent;
class UDisplayClusterOriginComponent;
class UDisplayClusterPreviewComponent;
class UDisplayClusterSyncTickComponent;
class UProceduralMeshComponent;


/**
 * VR root. This contains nDisplay VR hierarchy in the game.
 */
UCLASS(HideCategories=(Replication, Collision, Input, Actor, HLOD, Cooking, Physics, Activation, AssetUserData, ActorTick, Advanced, WorldPartition, DataLayers, Events), meta=(DisplayName = "nDisplay Root Actor"))
class DISPLAYCLUSTER_API ADisplayClusterRootActor
	: public AActor
{
	friend class FDisplayClusterRootActorDetailsCustomization;

	GENERATED_BODY()

public:
	ADisplayClusterRootActor(const FObjectInitializer& ObjectInitializer);
	~ADisplayClusterRootActor();

public:
	/**
	 * Initialializes the instance with specified config data
	 *
	 * @param ConfigData - Configuration data
	 */
	void InitializeFromConfig(UDisplayClusterConfigurationData* ConfigData);

	/**
	 * Cherry picking settings from a specified config data
	 *
	 * @param ConfigData - Configuration data
	 */
	void OverrideFromConfig(UDisplayClusterConfigurationData* ConfigData);

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

	UDisplayClusterSyncTickComponent* GetSyncTickComponent() const
	{
		return SyncTickComponent;
	}

	const FDisplayClusterConfigurationICVFX_StageSettings& GetStageSettings() const;
	const FDisplayClusterConfigurationRenderFrame& GetRenderFrameSettings() const;

protected:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// AActor
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void PostLoad() override;
	virtual void PostActorCreated() override;
	virtual void BeginDestroy() override;
#if WITH_EDITOR
	virtual void RerunConstructionScripts() override;
#endif

	// Initializes the actor on spawn and load
	void InitializeRootActor();

	// Creates all hierarchy objects declared in a config file
	bool BuildHierarchy();

public:
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get Default Camera"), Category = "NDisplay|Components")
	UDisplayClusterCameraComponent* GetDefaultCamera() const;

	UFUNCTION(BlueprintCallable, Category = "NDisplay|Render")
	bool SetReplaceTextureFlagForAllViewports(bool bReplace);

	template <typename TComp>
	TComp* GetComponentByName(const FString& ComponentName) const
	{
		static_assert(std::is_base_of<UActorComponent, TComp>::value, "TComp is not derived from UActorComponent");

		TArray<TComp*> FoundComponents;
		this->GetComponents<TComp>(FoundComponents, false);

		for (TComp* Component : FoundComponents)
		{
			// Search for the one that has a specified name
			if (Component->GetName().Equals(ComponentName, ESearchCase::IgnoreCase))
			{
				return Component;
			}
		}

		return nullptr;
	}

	/**
	* Update the geometry of the procedural mesh component(s) referenced inside nDisplay
	*
	* @param InProceduralMeshComponent - (optional) Mark the specified procedural mesh component, not all
	*/
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Update ProceduralMeshComponent data"), Category = "NDisplay|Components")
	void UpdateProceduralMeshComponentData(const UProceduralMeshComponent* InProceduralMeshComponent = nullptr);

public:
	IDisplayClusterViewportManager* GetViewportManager() const
	{
		return ViewportManager.IsValid() ? ViewportManager.Get() : nullptr;
	}
	
	static FName GetCurrentConfigDataMemberName()
	{
		return GET_MEMBER_NAME_CHECKED(ADisplayClusterRootActor, CurrentConfigData);
	}

protected:
	// Unique viewport manager for this configuration
	TUniquePtr<IDisplayClusterViewportManager> ViewportManager;

//////////////////////////////////////////////////////////////////////////////////////////////
// Details Panel Property Referencers
// Placed here to ensure layout builders process referencers first
//////////////////////////////////////////////////////////////////////////////////////////////
#if WITH_EDITORONLY_DATA
private:	
	UPROPERTY(EditAnywhere, Transient, Category = Viewports, meta = (PropertyPath = "CurrentConfigData.RenderFrameSettings.ClusterICVFXOuterViewportBufferRatioMult"))
	FDisplayClusterEditorPropertyReference ViewportScreenPercentageMultiplierRef;

	UPROPERTY(EditInstanceOnly, Transient, Category = Viewports, meta = (DisplayName = "Viewport Screen Percentage", PropertyPath = "CurrentConfigData.Cluster.Nodes.Viewports.RenderSettings.BufferRatio", ToolTip = "Adjust resolution scaling for an individual viewport.  Viewport Screen Percentage Multiplier is applied to this value."))
	FDisplayClusterEditorPropertyReference ViewportScreenPercentageRef;

	UPROPERTY(EditInstanceOnly, Transient, Category = Viewports, meta = (DisplayName = "Viewport Overscan", PropertyPath = "CurrentConfigData.Cluster.Nodes.Viewports.RenderSettings.Overscan", ToolTip = "Render a larger frame than specified in the configuration to achieve continuity across displays when using post-processing effects."))
	FDisplayClusterEditorPropertyReference ViewportOverscanRef;

	UPROPERTY(EditAnywhere, Transient, Category = Viewports, meta = (PropertyPath = "CurrentConfigData.StageSettings.HideList"))
	FDisplayClusterEditorPropertyReference ClusterHideListRef;

	UPROPERTY(EditAnywhere, Transient, Category = Viewports, meta = (PropertyPath = "CurrentConfigData.StageSettings.OuterViewportHideList"))
	FDisplayClusterEditorPropertyReference OuterHideListRef;

	UPROPERTY(EditAnywhere, Transient, Category = "In Camera VFX", meta = (PropertyPath = "CurrentConfigData.StageSettings.bEnableInnerFrustums"))
	FDisplayClusterEditorPropertyReference EnableInnerFrustumsRef;

	UPROPERTY(EditAnywhere, Transient, Category = "Color Grading", meta = (PropertyPath = "CurrentConfigData.StageSettings.EntireClusterColorGrading.bEnableEntireClusterColorGrading"))
	FDisplayClusterEditorPropertyReference EnableClusterColorGradingRef;

	UPROPERTY(EditAnywhere, Transient, Category = "Color Grading", meta = (DisplayName = "Entire Cluster", PropertyPath = "CurrentConfigData.StageSettings.EntireClusterColorGrading.ColorGradingSettings", EditConditionPath = "CurrentConfigData.StageSettings.EntireClusterColorGrading.bEnableEntireClusterColorGrading"))
	FDisplayClusterEditorPropertyReference ClusterColorGradingRef;

	UPROPERTY(EditAnywhere, Transient, Category = "Color Grading", meta = (PropertyPath = "CurrentConfigData.StageSettings.PerViewportColorGrading"))
	FDisplayClusterEditorPropertyReference PerViewportColorGradingRef;

	UPROPERTY(EditAnywhere, Transient, Category = OCIO, meta = (PropertyPath = "CurrentConfigData.StageSettings.bUseOverallClusterOCIOConfiguration"))
	FDisplayClusterEditorPropertyReference EnableClusterOCIORef;

	UPROPERTY(EditAnywhere, Transient, Category = OCIO, meta = (DisplayName = "All Viewports Color Configuration", PropertyPath = "CurrentConfigData.StageSettings.AllViewportsOCIOConfiguration.OCIOConfiguration.ColorConfiguration", ToolTip = "Apply this OpenColorIO configuration to all viewports.", EditConditionPath = "CurrentConfigData.StageSettings.bUseOverallClusterOCIOConfiguration"))
	FDisplayClusterEditorPropertyReference ClusterOCIOColorConfigurationRef;

	UPROPERTY(EditAnywhere, Transient, Category = OCIO, meta = (PropertyPath = "CurrentConfigData.StageSettings.PerViewportOCIOProfiles"))
	FDisplayClusterEditorPropertyReference PerViewportOCIOProfilesRef;

	UPROPERTY(EditAnywhere, Transient, Category = "Light Cards", meta = (PropertyPath = "CurrentConfigData.StageSettings.Lightcard.bEnable"))
	FDisplayClusterEditorPropertyReference EnableLightcardsRef;

	UPROPERTY(EditAnywhere, Transient, Category = "Light Cards", meta = (PropertyPath = "CurrentConfigData.StageSettings.Lightcard.Blendingmode", EditConditionPath = "CurrentConfigData.StageSettings.Lightcard.bEnable"))
	FDisplayClusterEditorPropertyReference LightCardBlendingModeRef;

	UPROPERTY(EditAnywhere, Transient, Category = "Light Cards", meta = (PropertyPath = "CurrentConfigData.StageSettings.Lightcard.ShowOnlyList", EditConditionPath = "CurrentConfigData.StageSettings.Lightcard.bEnable"))
	FDisplayClusterEditorPropertyReference LightCardContentRef;
#endif // WITH_EDITORONLY_DATA

private:
	/**
	 * Name of the CurrentConfigData asset. Only required if this is a parent of a DisplayClusterBlueprint.
	 * The name is used to lookup the config data as a default sub-object, specifically in packaged builds.
	 */
	UPROPERTY()
	FName ConfigDataName;

	/**
	 * The root component for our hierarchy.
	 * Must have CPF_Edit(such as VisibleDefaultsOnly) on property for Live Link.
	 * nDisplay details panel will hide this from actually being visible.
	 */
	UPROPERTY(EditAnywhere, Category = "NDisplay", meta = (HideProperty))
	USceneComponent* DisplayClusterRootComponent;

	/**
	 * Default camera component. It's an outer camera in VP/ICVFX terminology. Always exists on a DCRA instance.
	 */
	UPROPERTY(VisibleAnywhere, Category = "NDisplay")
	UDisplayClusterCameraComponent* DefaultViewPoint;

	/**
	 * Helper sync component. Performs sync procedure during Tick phase.
	 */
	UPROPERTY()
	UDisplayClusterSyncTickComponent* SyncTickComponent;

private:
	// Current operation mode
	EDisplayClusterOperationMode OperationMode;

	float LastDeltaSecondsValue = 0.f;

private:
	template <typename TComp>
	void GetTypedPrimitives(TSet<FPrimitiveComponentId>& OutPrimitives, const TArray<FString>* InCompNames = nullptr, bool bCollectChildrenVisualizationComponent = true) const;

public:
	/** Set the priority for inner frustum rendering if there is any overlap when enabling multiple ICVFX cameras. */
	UPROPERTY(EditInstanceOnly, EditFixedSize, Category = "In Camera VFX", meta = (TitleProperty = "Name", DisplayAfter = "ViewportAllowInnerFrustumRef"))
	TArray<FDisplayClusterComponentRef> InnerFrustumPriority;

	/**
	 * If set from the DisplayCluster BP Compiler it will be loaded from the class default subobjects in run-time.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Instanced, Category = "NDisplay", meta = (AllowPrivateAccess = "true"))
	UDisplayClusterConfigurationData* CurrentConfigData;

public:
	// UObject interface
	static void AddReferencedObjects(class UObject* InThis, class FReferenceCollector& Collector);
	// End of UObject interface

	bool IsInnerFrustumEnabled(const FString& InnerFrustumID) const;

	// Return inner frustum priority by InnerFrustum name (from InnerFrustumPriority property)
	// return -1, if not defined
	int GetInnerFrustumPriority(const FString& InnerFrustumID) const;

	float GetWorldDeltaSeconds() const
	{
		return LastDeltaSecondsValue;
	}

//////////////////////////////////////////////////////////////////////////////////////////////
// EDITOR RELATED SETTINGS
//////////////////////////////////////////////////////////////////////////////////////////////
#if WITH_EDITORONLY_DATA
public:
	/** Render the scene and display it as a preview on the nDisplay root actor in the editor.  This will impact editor performance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Editor Preview", meta = (DisplayName = "Enable Editor Preview"))
	bool bPreviewEnable = true;
	
	/** render preview every frame */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Editor Preview", meta = (DisplayName = "Capture Every Frame", EditCondition = "bPreviewEnable"))
	bool bPreviewRenderEveryFrame = true;

	/** Selectively preview a specific viewport or show all/none. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Editor Preview", meta = (DisplayName = "Preview Node", EditCondition = "bPreviewEnable"))
	FString PreviewNodeId = DisplayClusterConfigurationStrings::gui::preview::PreviewNodeAll;

	/** Render Mode */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Editor Preview", meta = (EditCondition = "bPreviewEnable"))
	EDisplayClusterConfigurationRenderMode RenderMode = EDisplayClusterConfigurationRenderMode::Mono;

	/** Render with mGPU */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Editor Preview", meta = (EditCondition = "bPreviewEnable"))
	bool bAllowMultiGPURendering = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Editor Preview", meta = (EditCondition = "bPreviewEnable"))
	int MinGPUIndex = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Editor Preview", meta = (EditCondition = "bPreviewEnable"))
	int MaxGPUIndex = 1;

	/** Tick Per Frame */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Editor Preview", meta = (ClampMin = "1", UIMin = "1", ClampMax = "200", UIMax = "200", EditCondition = "bPreviewEnable"))
	int TickPerFrame = 1;

	/** Nodes Per Frame */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Editor Preview", meta = (ClampMin = "1", UIMin = "1", ClampMax = "200", UIMax = "200", EditCondition = "bPreviewEnable"))
	int NodesPerFrame = 1;

	/** Viewports Per Frame */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Editor Preview", meta = (ClampMin = "1", UIMin = "1", ClampMax = "200", UIMax = "200", EditCondition = "bPreviewEnable"))
	int ViewportsPerFrame = 1;

	/** Adjust resolution scaling for the editor preview. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Editor Preview", meta = (DisplayName = "Preview Screen Percentage", ClampMin = "0.05", UIMin = "0.05", ClampMax = "1", UIMax = "1", EditCondition = "bPreviewEnable"))
	float PreviewRenderTargetRatioMult = 0.25;

	/** Render ICVFX Frustums */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Editor Preview", meta = (DisplayName = "ICVFX Camera Frustums", EditCondition = "bPreviewEnable"))
	bool bPreviewICVFXFrustums = false;

	/** Render ICVFX Frustums */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Editor Preview", meta = (DisplayName = "ICVFX Camera Frustums Distance", EditCondition = "bPreviewEnable"))
	float PreviewICVFXFrustumsFarDistance = 1000.0f;

	/** The maximum dimension of any internal texture for preview. Use less memory for large preview viewports */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Editor Preview", meta = (DisplayName = "Preview Texture Max Size", ClampMin = "64", UIMin = "64", ClampMax = "4096", UIMax = "4096", EditCondition = "bPreviewEnable"))
	int PreviewMaxTextureSize = 2048;

private:
	UPROPERTY(Transient)
	TMap<FString, UDisplayClusterPreviewComponent*> PreviewComponents;

	UPROPERTY(Transient)
	bool bDeferPreviewGeneration;
#endif

#if WITH_EDITOR
public:
	DECLARE_DELEGATE(FOnPreviewUpdated);

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
	void PostActorCreated_Editor();
	void BeginDestroy_Editor();
	void RerunConstructionScripts_Editor();

	UDisplayClusterPreviewComponent* GetPreviewComponent(const FString& NodeId, const FString& ViewportId);

	void UpdatePreviewComponents();
	void ReleasePreviewComponents();

	float GetPreviewRenderTargetRatioMult() const
	{
		return PreviewRenderTargetRatioMult;
	};

	IDisplayClusterViewport* FindPreviewViewport(const FString& InViewportId) const;

	void GetPreviewRenderTargetableTextures(const EDisplayClusterRenderFrameMode InRenderFrameMode, const TArray<FString>& InViewportNames, TArray<FTextureRHIRef>& OutTextures);

	void UpdateInnerFrustumPriority();
	void ResetInnerFrustumPriority();
	
	virtual bool IsSelectedInEditor() const override;
	void SetIsSelectedInEditor(bool bValue);

	// Don't show actor preview in the level viewport when DCRA actor is selected, but none of its children are.
	virtual bool IsDefaultPreviewEnabled() const override
	{
		return false;
	}

protected:
	FString GeneratePreviewComponentName(const FString& NodeId, const FString& ViewportId) const;

	void ImplRenderPreviewForSceneMaterials_Editor(const int32 InNumNodesForRender);

	void RenderPreviewClusterNode_Editor(const EDisplayClusterRenderFrameMode InRenderFrameMode, const FString& InClusterNodeId);
	bool UpdatePreviewConfiguration_Editor(const EDisplayClusterRenderFrameMode InRenderFrameMode, const FString& InClusterNodeId);

	void ResetPreviewInternals();
	
	void RenderPreviewFrustums();
	void RenderPreviewFrustum(const FMatrix ProjectionMatrix, const FMatrix ViewMatrix, const FVector ViewOrigin);

	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditMove(bool bFinished) override;

private:
	bool bIsSelectedInEditor = false;
	
private:
	TWeakPtr<IDisplayClusterConfiguratorBlueprintEditor> ToolkitPtr;

	int32 TickPerFrameCounter = 0;

	int32 PreviewClusterNodeIndex = 0;
	int32 PreviewViewportIndex = 0;
	TUniquePtr<FDisplayClusterRenderFrame> PreviewRenderFrame;

	FOnPreviewUpdated OnPreviewGenerated;
	FOnPreviewUpdated OnPreviewDestroyed;

#endif
};
