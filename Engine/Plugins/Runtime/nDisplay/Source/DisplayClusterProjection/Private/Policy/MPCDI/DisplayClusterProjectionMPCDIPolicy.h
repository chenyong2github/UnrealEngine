// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Policy/DisplayClusterProjectionPolicyBase.h"

#include "IMPCDI.h"

#include "DisplayClusterProjectionMPCDIPolicy.generated.h"

class USceneComponent;
class UDisplayClusterSceneComponent;
class UDisplayClusterConfigurationViewport;


UCLASS()
class DISPLAYCLUSTERPROJECTION_API UDisplayClusterProjectionPolicyMPCDIParameters
	: public UDisplayClusterProjectionPolicyParameters
{
	GENERATED_BODY()

public:
	UDisplayClusterProjectionPolicyMPCDIParameters();

#if WITH_EDITOR
public:
	virtual bool Parse(ADisplayClusterRootActor* RootActor, const FDisplayClusterConfigurationProjection& ConfigData) override;
#endif

#if WITH_EDITORONLY_DATA
public:
	UPROPERTY(EditAnywhere, Category = "MPCDI")
	ADisplayClusterRootActor* RootActor;

	UPROPERTY(EditAnywhere, Category = "MPCDI")
	FString File;

	UPROPERTY(EditAnywhere, Category = "MPCDI")
	FString Buffer;

	UPROPERTY(EditAnywhere, Category = "MPCDI")
	FString Region;

	UPROPERTY(EditAnywhere, Category = "MPCDI")
	USceneComponent* Origin;

	IMPCDI::FRegionLocator MPCDILocator;
	FCriticalSection InternalsSyncScope;
#endif
};



/**
 * MPCDI projection policy
 */
class FDisplayClusterProjectionMPCDIPolicy
	: public FDisplayClusterProjectionPolicyBase
{
public:
	enum class EWarpType : uint8
	{
		mpcdi = 0,
		mesh
	};

public:
	FDisplayClusterProjectionMPCDIPolicy(const FString& ViewportId, const TMap<FString, FString>& Parameters);
	virtual ~FDisplayClusterProjectionMPCDIPolicy();

public:
	virtual EWarpType GetWarpType() const
	{
		return EWarpType::mpcdi;
	}

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterProjectionPolicy
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual void StartScene(UWorld* World) override;
	virtual void EndScene() override;
	virtual bool HandleAddViewport(const FIntPoint& ViewportSize, const uint32 ViewsAmount) override;
	virtual void HandleRemoveViewport() override;

	virtual bool CalculateView(const uint32 ViewIdx, FVector& InOutViewLocation, FRotator& InOutViewRotation, const FVector& ViewOffset, const float WorldToMeters, const float NCP, const float FCP) override;
	virtual bool GetProjectionMatrix(const uint32 ViewIdx, FMatrix& OutPrjMatrix) override;

	virtual bool IsWarpBlendSupported() override;
	virtual void ApplyWarpBlend_RenderThread(const uint32 ViewIdx, FRHICommandListImmediate& RHICmdList, FRHITexture2D* SrcTexture, const FIntRect& ViewportRect) override;

protected:
	bool InitializeResources_RenderThread();

protected:
	FString OriginCompId;
	FIntPoint ViewportSize;

	IMPCDI& MPCDIAPI;

	IMPCDI::FRegionLocator WarpRef;
	mutable FCriticalSection WarpRefCS;

	struct FViewData
	{
		IMPCDI::FFrustum Frustum;
		FTexture2DRHIRef RTTexture;
	};

	TArray<FViewData> Views;

	mutable bool bIsRenderResourcesInitialized;
	FCriticalSection RenderingResourcesInitializationCS;


#if WITH_EDITOR
protected:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterProjectionPolicyPreview
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual bool HasMeshPreview() override
	{
		return true;
	}

	virtual bool HasPreviewRendering() override
	{
		return true;
	}

	virtual UDisplayClusterProjectionPolicyParameters* CreateParametersObject(UObject* Owner) override;
	virtual void InitializePreview(UDisplayClusterProjectionPolicyParameters* PolicyParameters) override;
	virtual UMeshComponent* BuildMeshPreview(UDisplayClusterProjectionPolicyParameters* PolicyParameters) override;
	virtual void RenderFrame(USceneComponent* Camera, UDisplayClusterProjectionPolicyParameters* PolicyParameters, FTextureRenderTargetResource* RenderTarget, FIntRect RenderRegion, bool bApplyWarpBlend) override;
#endif
};
