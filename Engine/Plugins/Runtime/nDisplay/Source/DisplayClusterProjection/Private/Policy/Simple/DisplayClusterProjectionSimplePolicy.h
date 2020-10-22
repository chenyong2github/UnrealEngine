// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Policy/DisplayClusterProjectionPolicyBase.h"

#include "DisplayClusterProjectionSimplePolicy.generated.h"

class ADisplayClusterRootActor;
class USceneComponent;
class UDisplayClusterConfigurationViewport;
class UDisplayClusterCameraComponent;
class UDisplayClusterScreenComponent;


UCLASS()
class DISPLAYCLUSTERPROJECTION_API UDisplayClusterProjectionPolicySimpleParameters
	: public UDisplayClusterProjectionPolicyParameters
{
	GENERATED_BODY()

public:
	UDisplayClusterProjectionPolicySimpleParameters();

#if WITH_EDITOR
public:
	virtual bool Parse(ADisplayClusterRootActor* RootActor, const FDisplayClusterConfigurationProjection& ConfigData) override;
#endif

#if WITH_EDITORONLY_DATA
public:
	UPROPERTY(EditAnywhere, Category = "Simple")
	ADisplayClusterRootActor* RootActor;

	UPROPERTY(EditAnywhere, Category = "Simple")
	UDisplayClusterScreenComponent* Screen;
	FCriticalSection InternalsSyncScope;
#endif
};



/**
 * Implements math behind the native (simple) quad based projections
 */
class FDisplayClusterProjectionSimplePolicy
	: public FDisplayClusterProjectionPolicyBase
{
public:
	FDisplayClusterProjectionSimplePolicy(const FString& ViewportId, const TMap<FString, FString>& Parameters);
	virtual ~FDisplayClusterProjectionSimplePolicy();

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

	virtual bool IsWarpBlendSupported() override
	{
		return false;
	}

protected:
	void InitializeMeshData();
	void ReleaseMeshData();

protected:
	// Custom projection screen geometry (hw - half-width, hh - half-height of projection screen)
	// Left bottom corner (from camera point view)
	virtual FVector GetProjectionScreenGeometryLBC(const float& hw, const float& hh) const
	{
		return FVector(0.f, -hw, -hh);
	}
	
	// Right bottom corner (from camera point view)
	virtual FVector GetProjectionScreenGeometryRBC(const float& hw, const float& hh) const
	{
		return FVector(0.f, hw, -hh);
	}

	// Left top corner (from camera point view)
	virtual FVector GetProjectionScreenGeometryLTC(const float& hw, const float& hh) const
	{
		return FVector(0.f, -hw, hh);
	}

private:
	// Screen ID taken from the nDisplay config file
	FString ScreenId;
	// Screen component
	UDisplayClusterScreenComponent* ScreenComp = nullptr;

	struct FViewData
	{
		FVector  ViewLoc;
		float    NCP;
		float    FCP;
		float    WorldToMeters;
	};

	TArray<FViewData> ViewData;


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
