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

#include "Render/Viewport/IDisplayClusterViewportManager.h"

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

	UDisplayClusterConfigurationViewport* GetViewportConfiguration(const FString& ClusterNodeID, const FString& ViewportID);

protected:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// AActor
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void PostLoad() override;
	virtual void PostActorCreated() override;
	virtual void BeginDestroy() override;
	virtual void RerunConstructionScripts() override;

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
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Instanced, Category = "NDisplay", meta = (AllowPrivateAccess = "true"))
	UDisplayClusterConfigurationData* CurrentConfigData;

	/**
	 * The root component for our hierarchy.
	 * Must have CPF_Edit(such as VisibleDefaultsOnly) on property for Live Link.
	 * nDisplay details panel will hide this from actually being visible.
	 */
	UPROPERTY(EditAnywhere, Category = "NDisplay")
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
	UPROPERTY(EditInstanceOnly, EditFixedSize, Category = "In Camera VFX", meta = (TitleProperty = "Name"))
	TArray<FDisplayClusterComponentRef> InnerFrustumPriority;

public:
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
	UPROPERTY(EditAnywhere, Category = "Editor Preview", meta = (DisplayName = "Enable Editor Preview"))
	bool bPreviewEnable = true;
	
	/** Selectively preview a specific viewport or show all/none. */
	UPROPERTY(EditAnywhere, Category = "Editor Preview", meta = (DisplayName = "Preview Node", EditCondition = "bPreviewEnable"))
	FString PreviewNodeId = DisplayClusterConfigurationStrings::gui::preview::PreviewNodeAll;

	/** Render Mode */
	UPROPERTY(EditAnywhere, Category = "Editor Preview", meta = (EditCondition = "bPreviewEnable"))
	EDisplayClusterConfigurationRenderMode RenderMode = EDisplayClusterConfigurationRenderMode::Mono;

	/** Tick Per Frame */
	UPROPERTY(EditAnywhere, Category = "Editor Preview", meta = (ClampMin = "1", UIMin = "1", ClampMax = "200", UIMax = "200", EditCondition = "bPreviewEnable"))
	int TickPerFrame = 1;

	/** Adjust resolution scaling for the editor preview. */
	UPROPERTY(EditAnywhere, Category = "Editor Preview", meta = (DisplayName = "Preview Screen Percentage", ClampMin = "0.05", UIMin = "0.05", ClampMax = "1", UIMax = "1", EditCondition = "bPreviewEnable"))
	float PreviewRenderTargetRatioMult = 0.25;

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

	void GetPreviewRenderTargetableTextures(const TArray<FString>& InViewportNames, TArray<FTextureRHIRef>& OutTextures);

	void UpdateInnerFrustumPriority();
	void ResetInnerFrustumPriority();
	
	virtual bool IsSelectedInEditor() const override;
	void SetIsSelectedInEditor(bool bValue);

protected:
	FString GeneratePreviewComponentName(const FString& NodeId, const FString& ViewportId) const;
	void RenderPreview_Editor();
	bool UpdatePreviewConfiguration_Editor(bool bUpdateAllViewports);

	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditMove(bool bFinished) override;

private:
	/** The number of times to update the render target via deferred update. */
	int32 PreviewRenderTargetUpdatesRequired = 0;
	bool bIsSelectedInEditor = false;
	
private:
	TWeakPtr<IDisplayClusterConfiguratorBlueprintEditor> ToolkitPtr;

	int32 TickPerFrameCounter = 0;

	FOnPreviewUpdated OnPreviewGenerated;
	FOnPreviewUpdated OnPreviewDestroyed;
#endif
};
