// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

class AActor;
class FTextureRenderTargetResource;
class IDisplayClusterProjectionPolicy;
class FSceneInterface;

struct FDisplayClusterRenderingParameters
{
	FSceneInterface* Scene;

	FVector  ViewLocation = FVector::ZeroVector;
	FRotator ViewRotation = FRotator::ZeroRotator;
	FMatrix  ProjectionMatrix = FMatrix::Identity;

	float FieldOfViewMultiplier = 1.0f;

	FTextureRenderTargetResource* RenderTarget = nullptr;
	FIntRect RenderTargetRect;

	float LODDistanceFactor = 1.0f;

	IDisplayClusterProjectionPolicy* ProjectionPolicy = nullptr;

	bool bAllowWarpBlend = false;

	TArray<AActor*> HiddenActors;
};


class IDisplayClusterRendering
	: public IModuleInterface
{
public:
	static constexpr auto ModuleName = TEXT("DisplayClusterRendering");

public:
	/**
	 * Singleton-like access to this module's interface.  This is just for convenience!
	 * Beware of calling this during the shutdown phase, though.  Your module might have been unloaded already.
	 *
	 * @return Returns singleton instance, loading the module on demand if needed
	 */
	static inline IDisplayClusterRendering& Get()
	{
		return FModuleManager::LoadModuleChecked<IDisplayClusterRendering>(IDisplayClusterRendering::ModuleName);
	}

	/**
	 * Checks to see if this module is loaded and ready.  It is only valid to call Get() if IsAvailable() returns true.
	 *
	 * @return True if the module is loaded and ready to use
	 */
	static inline bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded(IDisplayClusterRendering::ModuleName);
	}

public:
	/**
	 * Render a view with specified parameters
	 */
	virtual void RenderSceneToTexture(const FDisplayClusterRenderingParameters& RenderParams) = 0;
};
