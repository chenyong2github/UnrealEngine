// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonObject.h"
#include "Json/GLTFJsonIndex.h"
#include "Json/GLTFJsonVector.h"

struct FGLTFJsonCameraControl : IGLTFJsonObject
{
	EGLTFJsonCameraControlMode Mode;
	FGLTFJsonNodeIndex Target;
	float MaxDistance;
	float MinDistance;
	float MaxPitch;
	float MinPitch;
	float MaxYaw;
	float MinYaw;
	float RotationSensitivity;
	float RotationInertia;
	float DollySensitivity;
	float DollyDuration;

	FGLTFJsonCameraControl()
		: Mode(EGLTFJsonCameraControlMode::FreeLook)
		, MaxDistance(0)
		, MinDistance(0)
		, MaxPitch(90)
		, MinPitch(-90)
		, MaxYaw(360)
		, MinYaw(0)
		, RotationSensitivity(0)
		, RotationInertia(0)
		, DollySensitivity(0)
		, DollyDuration(0)
	{
	}

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override
	{
		Writer.Write(TEXT("mode"), Mode);

		if (Target != INDEX_NONE && Mode == EGLTFJsonCameraControlMode::Orbital)
		{
			Writer.Write(TEXT("target"), Target);
		}

		Writer.Write(TEXT("maxDistance"), MaxDistance);
		Writer.Write(TEXT("minDistance"), MinDistance);

		if (!FMath::IsNearlyEqual(MaxPitch, 90))
		{
			Writer.Write(TEXT("maxPitch"), MaxPitch);
		}

		if (!FMath::IsNearlyEqual(MinPitch, -90))
		{
			Writer.Write(TEXT("minPitch"), MinPitch);
		}

		if (!FMath::IsNearlyEqual(MaxYaw, 360))
		{
			Writer.Write(TEXT("maxYaw"), MaxYaw);
		}

		if (!FMath::IsNearlyEqual(MinYaw, 0))
		{
			Writer.Write(TEXT("minYaw"), MinYaw);
		}

		Writer.Write(TEXT("rotationSensitivity"), RotationSensitivity);
		Writer.Write(TEXT("rotationInertia"), RotationInertia);
		Writer.Write(TEXT("dollySensitivity"), DollySensitivity);
		Writer.Write(TEXT("dollyDuration"), DollyDuration);
	}
};
