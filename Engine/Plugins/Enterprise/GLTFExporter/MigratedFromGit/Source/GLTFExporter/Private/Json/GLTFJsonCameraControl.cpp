// Copyright Epic Games, Inc. All Rights Reserved.

#include "Json/GLTFJsonCameraControl.h"
#include "Json/GLTFJsonNode.h"

void FGLTFJsonCameraControl::WriteObject(IGLTFJsonWriter& Writer) const
{
	Writer.Write(TEXT("mode"), Mode);

	if (Target != nullptr && Mode == EGLTFJsonCameraControlMode::Orbital)
	{
		Writer.Write(TEXT("target"), Target);
	}

	Writer.Write(TEXT("maxDistance"), MaxDistance);
	Writer.Write(TEXT("minDistance"), MinDistance);

	if (!FMath::IsNearlyEqual(MaxPitch, 90, Writer.DefaultTolerance))
	{
		Writer.Write(TEXT("maxPitch"), MaxPitch);
	}

	if (!FMath::IsNearlyEqual(MinPitch, -90, Writer.DefaultTolerance))
	{
		Writer.Write(TEXT("minPitch"), MinPitch);
	}

	if (!FMath::IsNearlyEqual(MaxYaw, 360, Writer.DefaultTolerance))
	{
		Writer.Write(TEXT("maxYaw"), MaxYaw);
	}

	if (!FMath::IsNearlyEqual(MinYaw, 0, Writer.DefaultTolerance))
	{
		Writer.Write(TEXT("minYaw"), MinYaw);
	}

	Writer.Write(TEXT("rotationSensitivity"), RotationSensitivity);
	Writer.Write(TEXT("rotationInertia"), RotationInertia);
	Writer.Write(TEXT("dollySensitivity"), DollySensitivity);
	Writer.Write(TEXT("dollyDuration"), DollyDuration);
}
