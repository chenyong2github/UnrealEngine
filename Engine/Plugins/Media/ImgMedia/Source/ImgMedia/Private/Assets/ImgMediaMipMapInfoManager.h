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
class FSceneView;
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
	 * Get information on all appropriate cameras.
	 *
	 * @return Array of info on each camera.
	 */
	const TArray<FImgMediaMipMapCameraInfo>& GetCameraInfo() { return CameraInfos; };


	/**
	 * See if we want to output some debug.
	 *
	 * @return True if debug should be output.
	 */
	bool IsDebugEnabled() const { return bIsDebugEnabled; }

	//~ FTickableGameObject interface
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(FImgMediaMipMapInfoManager, STATGROUP_Tickables); }
	virtual bool IsTickableInEditor() const override { return true; }

protected:

	/** Array of cameras that can look at img sequences. */
	TArray <TWeakObjectPtr<class UCameraComponent>> Cameras;

	/** Array of info on each camera used for mipmap calculations. */
	TArray<FImgMediaMipMapCameraInfo> CameraInfos;

	/** Do we want to output debug information? */
	bool bIsDebugEnabled;

	/**
	 * Updates our info on the cameras based on current state.
	 */
	void UpdateCameraInfo();

	/**
	 * Add a SceneView to our camera info.
	 * 
	 * @param Viewport		The camera's viewport size rectangle.
	 * @param SceneView		The scene view we want to add.
	 */
	void AddCameraInfo(FViewport* Viewport, FSceneView* SceneView);

	/**
	 * Called when the cvar changes.
	 *
	 * @param Var Cvar that changed.
	 */
	void OnCVarMipMapDebugEnable(IConsoleVariable* Var);
};
