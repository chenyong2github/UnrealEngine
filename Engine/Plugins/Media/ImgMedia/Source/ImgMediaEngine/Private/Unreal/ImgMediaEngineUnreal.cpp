// Copyright Epic Games, Inc. All Rights Reserved.

#include "ImgMediaEngineUnreal.h"

#if WITH_ENGINE

#include "ImgMediaMipMapInfo.h"
#include "ContentStreaming.h"

#if WITH_EDITOR
#include "Editor.h"
#include "LevelEditorViewport.h"
#endif

void FImgMediaEngineUnreal::GetCameraInfo(TArray<FImgMediaMipMapCameraInfo>& CameraInfos)
{
	// Get cameras from streaming manager.
	const FString CameraName = "StreamingCamera";
	IStreamingManager& StreamingManager = IStreamingManager::Get();
	int32 NumViews = StreamingManager.GetNumViews();
	for (int32 Index = 0; Index < NumViews; ++Index)
	{
		const FStreamingViewInfo& ViewInfo = StreamingManager.GetViewInformation(Index);
		FVector Location = ViewInfo.ViewOrigin;
		
#if WITH_EDITOR
		// Ignore if this is the editor window and we have another window available.
		// If we are running PIE in the selected window,
		// then the position of the camera does not update and so we need to ignore it.
		if ((NumViews > 1) &&
			(GCurrentLevelEditingViewportClient != nullptr) &&
			(Location == GCurrentLevelEditingViewportClient->GetViewLocation()))
		{
			continue;
		}
#endif

		if (ViewInfo.FOVScreenSize > 0.0f)
		{
			float DistAdjust = ViewInfo.ScreenSize / ViewInfo.FOVScreenSize;
			CameraInfos.Emplace(CameraName, Location, ViewInfo.ScreenSize, DistAdjust);
		}
	}
}

#endif // WITH_ENGINE
