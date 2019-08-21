// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#if PLATFORM_WINDOWS
#define XR_USE_PLATFORM_WIN32		1
#define XR_USE_GRAPHICS_API_D3D11	1
#define XR_USE_GRAPHICS_API_D3D12	1
#endif

#define XR_USE_GRAPHICS_API_OPENGL	1
#define XR_USE_GRAPHICS_API_VULKAN	1

#include <openxr/openxr.h>
#include <openxr/openxr_reflection.h>

#define XR_ENUM_CASE_STR(name, val) case name: return TEXT(#name);
constexpr const TCHAR* OpenXRResultToString(XrResult e)
{
	switch (e)
	{
		XR_LIST_ENUM_XrResult(XR_ENUM_CASE_STR)
		default: return TEXT("Unknown");
	}
}

#if DO_CHECK
#define XR_ENSURE(x) [] (XrResult Result) \
	{ \
		return ensureMsgf(XR_SUCCEEDED(Result), TEXT("OpenXR call failed with result %s"), OpenXRResultToString(Result)); \
	} (x)
#else
#define XR_ENSURE(x) XR_SUCCEEDED(x)
#endif

FORCEINLINE FQuat ToFQuat(XrQuaternionf Quat)
{
	return FQuat(-Quat.z, Quat.x, Quat.y, -Quat.w);
}

FORCEINLINE XrQuaternionf ToXrQuat(FQuat Quat)
{
	return XrQuaternionf{ Quat.Y, Quat.Z, -Quat.X, -Quat.W };
}

FORCEINLINE FVector ToFVector(XrVector3f Vector, float Scale = 1.0f)
{
	return FVector(-Vector.z * Scale, Vector.x * Scale, Vector.y * Scale);
}

FORCEINLINE XrVector3f ToXrVector(FVector Vector, float Scale = 1.0f)
{
	if (Vector.IsZero())
		return XrVector3f{ 0.0f, 0.0f, 0.0f };

	return XrVector3f{ Vector.Y / Scale, Vector.Z / Scale, -Vector.X / Scale };
}

FORCEINLINE FTransform ToFTransform(XrPosef Transform, float Scale = 1.0f)
{
	return FTransform(ToFQuat(Transform.orientation), ToFVector(Transform.position, Scale));
}

FORCEINLINE XrPosef ToXrPose(FTransform Transform, float Scale = 1.0f)
{
	return XrPosef{ ToXrQuat(Transform.GetRotation()), ToXrVector(Transform.GetTranslation(), Scale) };
}
