// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Misc/DisplayClusterObjectRef.h"
#include "Policy/DisplayClusterProjectionPolicyBase.h"

class ADisplayClusterRootActor;
class USceneComponent;
class UDisplayClusterConfigurationViewport;
class UDisplayClusterCameraComponent;
class UDisplayClusterScreenComponent;

/**
 * Implements math behind the native (simple) quad based projections
 */
class FDisplayClusterProjectionSimplePolicy
	: public FDisplayClusterProjectionPolicyBase
{
public:
	FDisplayClusterProjectionSimplePolicy(const FString& ProjectionPolicyId, const struct FDisplayClusterConfigurationProjection* InConfigurationProjectionPolicy);
	virtual ~FDisplayClusterProjectionSimplePolicy();

	virtual const FString GetTypeId() const
	{ return DisplayClusterProjectionStrings::projection::Simple; }

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterProjectionPolicy
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual bool HandleStartScene(class IDisplayClusterViewport* InViewport) override;
	virtual void HandleEndScene(class IDisplayClusterViewport* InViewport) override;

	virtual bool CalculateView(class IDisplayClusterViewport* InViewport, const uint32 InContextNum, FVector& InOutViewLocation, FRotator& InOutViewRotation, const FVector& ViewOffset, const float WorldToMeters, const float NCP, const float FCP) override;
	virtual bool GetProjectionMatrix(class IDisplayClusterViewport* InViewport, const uint32 InContextNum, FMatrix& OutPrjMatrix) override;

	virtual bool IsWarpBlendSupported() override
	{
		return false;
	}

protected:
	bool InitializeMeshData(class IDisplayClusterViewport* InViewport);
	void ReleaseMeshData(class IDisplayClusterViewport* InViewport);

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

	// Weak ptr screen component
	FDisplayClusterSceneComponentRef ScreenCompRef;

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
	virtual bool HasPreviewMesh() override
	{
		return true;
	}

	virtual class UMeshComponent* GetOrCreatePreviewMeshComponent(class IDisplayClusterViewport* InViewport, bool& bOutIsRootActorComponent) override;
#endif
};
