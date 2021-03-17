// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AddonTools.h"

#include "Quat.h"
#include "ModelVector.hpp"
#include "Vertex.hpp"

struct FQuat;
struct FVector;

BEGIN_NAMESPACE_UE_AC

class FGeometryUtil
{
  public:
	FGeometryUtil() = delete;

	static FQuat   GetRotationQuat(const double Matrix[3][4]);
	static FQuat   GetRotationQuat(const ModelerAPI::Vector Dir);
	static FQuat   GetRotationQuat(const double Pitch, const double Yaw, const double Roll);
	static FVector GetTranslationVector(const double Matrix[3][4]);
	static FVector GetTranslationVector(const ModelerAPI::Vertex Pos);

	static float  GetDistance3D(const double DistanceZ, const double Distance2D);
	static float  GetCameraFocalLength(const double SensorWidth, const double ViewAngle);
	static double GetPitchAngle(const double CameraZ, const double TargetZ, const double Distance2D);
};

END_NAMESPACE_UE_AC
