// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"

#include "DisplayClusterConfigurationTypes_Base.h"

#include "DisplayClusterPreviewComponent.generated.h"

class IDisplayClusterProjectionPolicy;
class UTextureRenderTarget2D;
class UTexture2D;
class UDisplayClusterConfigurationViewport;
class UMaterial;
class UMaterialInstanceDynamic;
class UDisplayClusterProjectionPolicyParameters;
class ADisplayClusterRootActor;
class UMeshComponent;

/**
 * nDisplay Viewport preview component (Editor)
 */
UCLASS(ClassGroup = (DisplayCluster))
class DISPLAYCLUSTER_API UDisplayClusterPreviewComponent
	: public UActorComponent
{
	friend class FDisplayClusterPreviewComponentDetailsCustomization;

	GENERATED_BODY()

public:
	UDisplayClusterPreviewComponent(const FObjectInitializer& ObjectInitializer);

#if WITH_EDITOR
public:
	virtual void OnComponentCreated() override;
	virtual void DestroyComponent(bool bPromoteChildren = false) override;

public:
	bool InitializePreviewComponent(ADisplayClusterRootActor* RootActor, const FString& ViewportId, UDisplayClusterConfigurationViewport* ViewportConfig);

	bool IsPreviewAvailable() const;

	const FString& GetViewportId() const
	{ 
		return ViewportId; 
	}

	UDisplayClusterConfigurationViewport* GetViewportConfig() const
	{ 
		return ViewportConfig;
	}
	
	UTextureRenderTarget2D* GetRenderTargetTexture() const
	{
		return RenderTarget;
	}

	void UpdatePreviewResources();
	void HandleRenderTargetTextureDeferredUpdate();

	UMeshComponent* GetPreviewMesh() const
	{
		return PreviewMesh;
	}

	/** Create and retrieve a render texture 2d from the render target. */
	UTexture2D* GetOrCreateRenderTexture2D();

protected:
	class IDisplayClusterViewport* GetCurrentViewport() const;
	bool GetPreviewTextureSettings(FIntPoint& OutSize, float& OutGamma) const;

	void UpdatePreviewRenderTarget();
	void ReleasePreviewRenderTarget();

	bool UpdatePreviewMesh(bool bRestoreOriginalMaterial = false);
	void ReleasePreviewMesh();

	bool UpdatePreviewTexture();
	void ReleasePreviewTexture();

	void InitializePreviewMaterial();
	void ReleasePreviewMaterial();
	void UpdatePreviewMaterial();

	void UpdatePreviewMeshMaterial(bool bRestoreOriginalMaterial = false);

protected:
	// Set to true, when RTT surface updated
	int32 RenderTargetSurfaceChangedCnt = 0;
	bool bIsEditingProperty = false;
#endif /* WITH_EDITOR */

#if WITH_EDITORONLY_DATA
protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Preview", meta = (DisplayName = "Render Target"))
	UTextureRenderTarget2D* RenderTarget;

private:
	// Saved mesh policy params
	UPROPERTY()
	FDisplayClusterConfigurationProjection WarpMeshSavedProjectionPolicy;

	UPROPERTY()
	ADisplayClusterRootActor* RootActor = nullptr;

	UPROPERTY()
	FString ViewportId;

	UPROPERTY()
	UDisplayClusterConfigurationViewport* ViewportConfig = nullptr;

	UPROPERTY()
	UMeshComponent* PreviewMesh = nullptr;

	UPROPERTY()
	bool bIsRootActorPreviewMesh = false;

	UPROPERTY()
	UMaterial* OriginalMaterial = nullptr;

	UPROPERTY()
	UMaterial* PreviewMaterial = nullptr;

	UPROPERTY()
	UMaterialInstanceDynamic* PreviewMaterialInstance = nullptr;

	UPROPERTY(Transient)
	UTexture2D* PreviewTexture = nullptr;

#endif /*WITH_EDITORONLY_DATA*/
};
