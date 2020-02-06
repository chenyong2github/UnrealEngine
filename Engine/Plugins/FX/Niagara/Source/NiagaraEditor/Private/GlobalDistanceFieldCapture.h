// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"

class FGlobalDistanceFieldReadback;
class UVolumeTexture;

/**
 * Utility interface used by an editor console command to capture the global distance field and store it into a volume
 * texture selected in the Content Browser
 */
class FGlobalDistanceFieldCapture
{
public:
	virtual ~FGlobalDistanceFieldCapture();
	
	/**	Requests a capture. Will overwrite the volume texture selected in the Content Browser or create a new one. */
	static void Request(bool bRangeCompress);

	/**	Requests a capture at the specified camera location. Will overwrite the volume texture selected in the Content Browser or create a new one. */
	static void Request(bool bRangeCompress, const FVector& CameraPos);

private:
	TWeakObjectPtr<UVolumeTexture> VolumeTex;
	FGlobalDistanceFieldReadback* Readback = nullptr;
	FVector StoredCamPos;
	bool bRestoreCamera = false;
	bool bRangeCompress = false;

	// Only allow singleton access of constructors	
	FGlobalDistanceFieldCapture(UVolumeTexture* Tex, bool bCompress, bool bSetCamPos, const FVector& CamPos);
	FGlobalDistanceFieldCapture(const FGlobalDistanceFieldCapture&) = delete;

	void OnReadbackComplete();

	static void RequestCommon(bool bRangeCompress, bool bSetCamPos, const FVector& CamPos);

	static FGlobalDistanceFieldCapture* Singleton;
};