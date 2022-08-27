// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonCore.h"

struct GLTFEXPORTER_API FGLTFJsonCameraControl : IGLTFJsonObject
{
	EGLTFJsonCameraControlMode Mode;
	FGLTFJsonNode* Target;

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
		, Target(nullptr)
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

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override;
};
