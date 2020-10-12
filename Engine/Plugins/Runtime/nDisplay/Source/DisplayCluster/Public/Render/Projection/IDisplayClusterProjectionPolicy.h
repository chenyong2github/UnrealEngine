// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "DisplayClusterRootActor.h"

#include "RHI.h"
#include "RHIDefinitions.h"
#include "RHIResources.h"

#include "IDisplayClusterProjectionPolicy.generated.h"

class FRHICommandListImmediate;
struct FDisplayClusterConfigurationProjection;

#if WITH_EDITORONLY_DATA
class UDisplayClusterConfigurationViewport;
class FTextureRenderTargetResource;
class IDetailCategoryBuilder;
class IDetailLayoutBuilder;
class UMeshComponent;
#endif


UCLASS(Abstract)
class DISPLAYCLUSTER_API UDisplayClusterProjectionPolicyParameters
	: public UObject
{
	GENERATED_BODY()

public:
	virtual bool Parse(ADisplayClusterRootActor* RootActor, const FDisplayClusterConfigurationProjection& ConfigData)
	{
		return false;
	}
};



/**
 * nDisplay projection policy
 */
class IDisplayClusterProjectionPolicy
{
public:
	virtual ~IDisplayClusterProjectionPolicy() = 0
	{ }

public:
	/**
	* Called each time a new game level starts
	*
	* @param World - A new world that is being started
	*/
	virtual void StartScene(UWorld* World)
	{ }

	/**
	* Called when current level is going to be closed (i.e. before loading a new map)
	*/
	virtual void EndScene()
	{ }

	/**
	* Called once the viewport is added
	*
	* @param ViewportSize - Size of a new viewport
	* @param ViewsAmount  - Amount of views depending on a rendering device
	*
	* @return - True if success
	*/
	virtual bool HandleAddViewport(const FIntPoint& ViewportSize, const uint32 ViewsAmount) = 0;

	/**
	* Called before remove the viewport
	*/
	virtual void HandleRemoveViewport()
	{ }

	/**
	* @param ViewIdx           - Index of view that is being processed for this viewport
	* @param InOutViewLocation - (in/out) View location with ViewOffset (i.e. left eye pre-computed location)
	* @param InOutViewRotation - (in/out) View rotation
	* @param ViewOffset        - Offset applied ot a camera location that gives us InOutViewLocation (i.e. right offset in world to compute right eye location)
	* @param WorldToMeters     - Current world scale (units (cm) in meter)
	* @param NCP               - Distance to the near clipping plane
	* @param FCP               - Distance to the far  clipping plane
	*
	* @return - True if success
	*/
	virtual bool CalculateView(const uint32 ViewIdx, FVector& InOutViewLocation, FRotator& InOutViewRotation, const FVector& ViewOffset, const float WorldToMeters, const float NCP, const float FCP) = 0;

	/**
	* @param ViewIdx      - Index of view that is being processed for this viewport
	* @param OutPrjMatrix - (out) projection matrix
	*
	* @return - True if success
	*/
	virtual bool GetProjectionMatrix(const uint32 ViewIdx, FMatrix& OutPrjMatrix) = 0;

	/**
	* Returns if a policy provides warp&blend feature
	*
	* @return - True if warp&blend operations are supported by the policy implementation
	*/
	virtual bool IsWarpBlendSupported()
	{
		return false;
	}

	/**
	* Performs warp&blend. Called if IsWarpBlendSupported() returns true
	*
	* @param ViewIdx      - Index of view that is being processed for this viewport
	* @param RHICmdList   - RHI commands
	* @param SrcTexture   - Source texture
	* @param ViewportRect - Region of the SrcTexture to perform warp&blend operations
	*
	*/
	virtual void ApplyWarpBlend_RenderThread(const uint32 ViewIdx, FRHICommandListImmediate& RHICmdList, FRHITexture2D* SrcTexture, const FIntRect& ViewportRect)
	{ }


#if WITH_EDITOR

	/**
	* Provide with projection policy specific parameters class
	*
	* @return - UDisplayClusterProjectionPolicyParameters based parameters class
	*/
	virtual UDisplayClusterProjectionPolicyParameters* CreateParametersObject(UObject* Owner)
	{
		return nullptr;
	}

	/**
	* Allows policy instance to perform any additional initialization
	*
	* @param PolicyParameters - Projection specific parameters. It needs to be Cast-ed to internal parameters class type returned by GetParametersClass().
	*/
	virtual void InitializePreview(UDisplayClusterProjectionPolicyParameters* PolicyParameters)
	{ }

	/**
	* Ask projection policy instance if it has any mesh based preview
	*
	* @return - True if mesh based preview is available
	*/
	virtual bool HasMeshPreview()
	{
		return false;
	}

	/**
	* Build preview mesh
	*
	* @param PolicyParameters - Projection specific parameters. It needs to be Cast-ed to internal parameters class type returned by GetParametersClass().
	*/
	virtual UMeshComponent* BuildMeshPreview(UDisplayClusterProjectionPolicyParameters* PolicyParameters)
	{
		return nullptr;
	}

	/**
	* Ask projection policy instance if it has any frustum visualization
	*
	* @return - True if frustum visualization is supported
	*/
	virtual bool HasFrustum()
	{
		return false;
	}

	/**
	* Visualize frustum
	*/
	virtual void BuildFrustum()
	{ }

	/**
	* Ask projection policy instance if it supports preview rendering
	*
	* @return - True if preview rendering is supported
	*/
	virtual bool HasPreviewRendering()
	{
		return false;
	}

	/**
	* Render preview frame
	*
	* @param Camera           - View point
	* @param PolicyParameters - Projection specific parameters. It needs to be Cast-ed to internal parameters class type returned by GetParametersClass().
	* @param RenderTarget     - RenderTarget to render to
	* @param RenderRegion     - RenderTarget region to render to
	*/
	virtual void RenderFrame(USceneComponent* Camera, UDisplayClusterProjectionPolicyParameters* PolicyParameters, FTextureRenderTargetResource* RenderTarget, FIntRect RenderRegion, bool bApplyWarpBlend)
	{ }

#endif
};
