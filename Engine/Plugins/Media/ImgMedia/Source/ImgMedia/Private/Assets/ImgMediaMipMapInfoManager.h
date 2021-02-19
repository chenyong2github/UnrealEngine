// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IMediaOptions.h"
#include "ImgMediaMipMapInfo.h"
#include "Tickable.h"
#include "Containers/Map.h"
#include "UObject/WeakObjectPtr.h"
#include "UObject/WeakObjectPtrTemplates.h"

class AActor;
class IMediaPlayer;
class UCameraComponent;
class UImgMediaPlaybackComponent;
class UMediaTexture;


/**
 * Helps manage FImgMediaMipMapInfo objects.
 */
class FImgMediaMipMapInfoManager : public FTickableGameObject
{
public:
	FImgMediaMipMapInfoManager();
	virtual ~FImgMediaMipMapInfoManager() {}

	/**
	 * Access the manager here.
	 *
	 * @return Manager
	 */
	static FImgMediaMipMapInfoManager& Get();

	/**
	 * This camera is using all img players.
	 *
	 * @param InActor Must be a ACameraActor.
	 */
	void AddCamera(AActor* InActor);

	/**
	 * This camera is no longer using all img players.
	 *
	 * @param InActor Camera object.
	 */
	void RemoveCamera(AActor* InActor);

	/**
	 * Get all the media textures that a player is outputting to.
	 *
	 * @param OutMediaTextures Textures will be added here.
	 * @param InPlayer Media player to get textures for.
	 */
	void GetMediaTexturesFromPlayer(TArray<UMediaTexture*>& OutMediaTextures, IMediaPlayer* Player);

	/**
	 * Get information on the main viewport.
	 *
	 * @return Distance adjustment for this viewport compared to the reference viewport.
	 */
	float GetViewportInfo() { return ViewportDistAdjust; }

	/**
	 * Get information on all appropriate cameras.
	 *
	 * @return Array of info on each camera.
	 */
	const TArray<FImgMediaMipMapCameraInfo>& GetCameraInfo() { return CameraInfos; };

	/**
	 * Get the width (in physical space) of the reference near plane used in mipmap calculations.
	 *
	 * @return Width
	 */
	float GetRefNearPlaneWidth() const { return RefNearPlaneWidth; }

	/**
	 * Get the distance of the reference near plane from the camera used in mipmap calculations.
	 *
	 * @return Distance.
	 */
	float GetRefNearPlaneDistance() const { return RefNearPlaneDistance; }

	/**
	 * Get the size of the reference object.
	 */
	float GetRefObjectWidth() const { return RefObjectWidth; }

	/**
	 * Get the size of the texture for the reference object.
	 */
	float GetRefObjectTextureWidth() const { return RefObjectTextureWidth; }

	/**
	 * Get the distance of mip level 0 for the reference object.
	 *
	 * @return Distance.
	 */
	float GetMipLevel0Distance() const { return MipLevel0Distance; }

	/**
	 * See if we want to output some debug.
	 *
	 * @return True if debug should be output.
	 */
	bool IsDebugEnabled() const { return bIsDebugEnabled; }

	//~ FTickableGameObject interface
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(FImgMediaMipMapInfoManager, STATGROUP_Tickables); }

protected:

	/** Array of cameras that can look at img sequences. */
	TArray <TWeakObjectPtr<class UCameraComponent>> Cameras;

	/** Array of info on each camera used for mipmap calculations. */
	TArray<FImgMediaMipMapCameraInfo> CameraInfos;

	/** Reference camera FOV used in mipmap calculations. */
	const float RefCameraFOV = 90.0f;
	/** Size of reference near plane (in physical space) used to calculate MipLevelDistance. */
	float RefNearPlaneWidth;
	/** Distance of reference near plan used in mipmap calculations. */
	const float RefNearPlaneDistance = 10.0f;
	/** Size of reference object used to calculate MipLevelDistances. */
	const float RefObjectWidth = 100.0f;
	/** Distance of reference object used to calculate MipLevelDistances. */
	const float RefObjectDistance = 100.0f;
	/** Texture size of reference object used to calculate MipLevelDistances. */
	const float RefObjectTextureWidth = 1024.0f;
	/** Size of reference frame buffer used to calculate MipLevelDistances. */
	const float RefFrameBufferWidth = 1920.0f;

	/** Distance for reference object for mip level 0. */
	float MipLevel0Distance;
	/** Adjustment needed for main viewport. */
	float ViewportDistAdjust;

	/** Do we want to output debug information? */
	bool bIsDebugEnabled;

	/**
	 * Updates our info on the main viewport.
	 */
	void UpdateViewportInfo();

	/**
	 * Updates our info on the cameras based on current state.
	 */
	void UpdateCameraInfo();

	/** Calculate the size of the near plane in physical space.
	 *
	 * @param InFOV FOV of camera in degrees.
	 * @return Size of near plane.
	 */
	float CalculateNearPlaneSize(float InFOV) const;

	/**
	 * Called when the cvar changes.
	 *
	 * @param Var Cvar that changed.
	 */
	void OnCVarMipMapDebugEnable(IConsoleVariable* Var);
};
