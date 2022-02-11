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

};


