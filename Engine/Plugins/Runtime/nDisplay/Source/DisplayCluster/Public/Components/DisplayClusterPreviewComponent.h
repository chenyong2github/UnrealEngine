// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"
#include "DisplayClusterPreviewComponent.generated.h"

class IDisplayClusterProjectionPolicy;
class UTextureRenderTarget2D;
class UDisplayClusterConfigurationViewport;
class UMaterial;
class UMaterialInstanceDynamic;
class UDisplayClusterProjectionPolicyParameters;
class ADisplayClusterRootActor;
class UMeshComponent;

/**
 * Projection policy preview component (Editor)
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
	void SetConfigData(ADisplayClusterRootActor* RootActor, UDisplayClusterConfigurationViewport* ViewportConfig);
	void SetProjectionPolicy(const FString& ProjPolicy);
	void BuildPreview();

	bool IsPreviewAvailable() const;

	UTextureRenderTarget2D* GetRenderTexture()
	{
		return RenderTarget;
	}

	FIntPoint GetRenderTextureSize() const
	{
		return TextureSize;
	}

	void SetRenderTextureSize(const FIntPoint& Size)
	{
		TextureSize = Size;
	}

	float GetRenderTextureGamma() const
	{
		return TextureGamma;
	}

	void SetRenderTextureGamma(float Gamma)
	{
		TextureGamma = Gamma;
	}

	int32 GetRefreshPeriod() const
	{
		return RefreshPeriod;
	}

	void SetRefreshPeriod(int32 Period)
	{
		RefreshPeriod = Period;
		PrimaryComponentTick.TickInterval = RefreshPeriod / 1000.f;
	}

	bool GetWarpBlendEnabled() const
	{
		return bApplyWarpBlend;
	}

	void SetWarpBlendEnabled(bool bEnabled)
	{
		bApplyWarpBlend = bEnabled;
	}

public:
	virtual void OnComponentCreated() override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void DestroyComponent(bool bPromoteChildren = false) override;

protected:
	void UpdateProjectionPolicy();
	void UpdateRenderTarget();
	void InitializeInternals();
#endif

#if WITH_EDITORONLY_DATA
protected:
	UPROPERTY(EditAnywhere, Category = "Policy")
	FString ProjectionPolicy;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Preview", meta = (DisplayName = "Render Target"))
	UTextureRenderTarget2D* RenderTarget;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview", meta = (DisplayName = "Render Target Size"))
	FIntPoint TextureSize;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview", meta = (DisplayName = "Preview Gamma"))
	float TextureGamma;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview", meta = (ClampMin = "0", ClampMax = "5000", UIMin = "0", UIMax = "5000"), meta = (DisplayName = "Refresh Period (ms)"))
	int32 RefreshPeriod;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview", meta = (DisplayName = "Apply Warp&Blend"))
	bool bApplyWarpBlend;

private:
	UPROPERTY(Transient)
	UDisplayClusterConfigurationViewport* ViewportConfig;

	UPROPERTY(Transient)
	UMeshComponent* PreviewMesh;

	UPROPERTY(Transient)
	UMaterial* OriginalMaterial;

	UPROPERTY(Transient)
	UMaterial* PreviewMaterial;

	UPROPERTY(Transient)
	UMaterialInstanceDynamic* PreviewMaterialInstance;

	UPROPERTY(Transient)
	UDisplayClusterProjectionPolicyParameters* ProjectionPolicyParameters;

	UPROPERTY(Transient)
	ADisplayClusterRootActor* RootActor;
	
	TSharedPtr<IDisplayClusterProjectionPolicy> ProjectionPolicyInstance;
	TArray<FString> ProjectionPolicies;
#endif
};
