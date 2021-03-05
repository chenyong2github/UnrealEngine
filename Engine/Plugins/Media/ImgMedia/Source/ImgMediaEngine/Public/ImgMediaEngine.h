// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"
#include "UObject/WeakObjectPtrTemplates.h"

class UMediaTexture;
struct FImgMediaMipMapCameraInfo;
struct FImgMediaMipMapObjectInfo;

/**
 * Handles interface between ImgMedia and the engine we are using.
 */
class FImgMediaEngine
{
public:
	/**
	 * Access engine from here.
	 *
	 * @return Engine.
	 */
	IMGMEDIAENGINE_API static FImgMediaEngine& Get();

	virtual ~FImgMediaEngine() {}
	
	/**
	 * Get information on cameras.
	 *
	 * @param CameraInfos Cameras in the engine will be added here.
	 */
	IMGMEDIAENGINE_API virtual void GetCameraInfo(TArray<FImgMediaMipMapCameraInfo>& CameraInfos) {}

	/**
	 * Each object should call this for each media texture that the object has.
	 *
	 * @param InInfo Object that has the texture.
	 * @param InTexture Texture to register.
	 */
	IMGMEDIAENGINE_API void RegisterTexture(TSharedPtr<FImgMediaMipMapObjectInfo, ESPMode::ThreadSafe>& InInfo, UMediaTexture* InTexture);
	
	/**
	 * Each object should call this for each media texture that the object has.
	 *
	 * @param InInfo Object that has the texture.
	 * @param InTexture Texture to unregister.
	 */
	IMGMEDIAENGINE_API void UnregisterTexture(TSharedPtr<FImgMediaMipMapObjectInfo, ESPMode::ThreadSafe>& InInfo, UMediaTexture* InTexture);

	/**
	 * Get which objects are usnig a specific media texture.
	 *
	 * @param InTexture Media texture to check.
	 * @return Nullptr if nothing found, or a pointer to an array of objects.
	 */
	IMGMEDIAENGINE_API const TArray<TWeakPtr<FImgMediaMipMapObjectInfo, ESPMode::ThreadSafe>>* GetObjects(UMediaTexture* InTexture) const;
	
	/**
	 * Get a list of media textures we know about.
	 *
	 * @ return List of textures.
	 */
	IMGMEDIAENGINE_API const TArray<TWeakObjectPtr<UMediaTexture>>& GetTextures() { return MediaTextures; }

protected:
	/** Maps a media texture to an array of playback components. */
	TMap<TWeakObjectPtr<UMediaTexture>, TArray<TWeakPtr<FImgMediaMipMapObjectInfo, ESPMode::ThreadSafe>>> MapTextureToObject;
	/** Array of media textures that we know about. */
	TArray<TWeakObjectPtr<UMediaTexture>> MediaTextures;
};


