// Copyright Epic Games, Inc. All Rights Reserved.

#include "MagicLeapUtils.h"
#include "MagicLeapMath.h"
#include "HAL/UnrealMemory.h"

namespace MagicLeap
{
#if WITH_MLSDK
	void ResetClipExtentsInfoArray(MLGraphicsClipExtentsInfoArrayEx& UpdateInfoArray)
	{
		MLGraphicsClipExtentsInfoArrayExInit(&UpdateInfoArray);

		for (MLGraphicsClipExtentsInfo &ViewportInfo : UpdateInfoArray.virtual_camera_extents)
		{
			FMemory::Memcpy(ViewportInfo.projection.matrix_colmajor, MagicLeap::kIdentityMatColMajor, sizeof(MagicLeap::kIdentityMatColMajor));
			FMemory::Memcpy(&ViewportInfo.transform, &MagicLeap::kIdentityTransform, sizeof(MagicLeap::kIdentityTransform));
		}

		FMemory::Memcpy(UpdateInfoArray.full_extents.projection.matrix_colmajor, MagicLeap::kIdentityMatColMajor, sizeof(MagicLeap::kIdentityMatColMajor));
		FMemory::Memcpy(&UpdateInfoArray.full_extents.transform, &MagicLeap::kIdentityTransform, sizeof(MagicLeap::kIdentityTransform));
	}

	void ResetFrameInfo(MLGraphicsFrameInfo& FrameInfo)
	{
		FrameInfo.num_virtual_cameras = 0;
		FrameInfo.color_id = 0;
		FrameInfo.depth_id = 0;
		FrameInfo.viewport.x = 0;
		FrameInfo.viewport.y = 0;
		FrameInfo.viewport.w = 0;
		FrameInfo.viewport.h = 0;
		for (MLGraphicsVirtualCameraInfo &ViewportInfo : FrameInfo.virtual_cameras)
		{
			FMemory::Memcpy(ViewportInfo.projection.matrix_colmajor, MagicLeap::kIdentityMatColMajor, sizeof(MagicLeap::kIdentityMatColMajor));
			FMemory::Memcpy(&ViewportInfo.transform, &MagicLeap::kIdentityTransform, sizeof(MagicLeap::kIdentityTransform));
		}
	}
#endif //WITH_MLSDK
}
