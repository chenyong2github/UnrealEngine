// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeometryUtil.h"

#include "Vector3D.hpp"

BEGIN_NAMESPACE_UE_AC

FQuat FGeometryUtil::GetRotationQuat(const double matrix[3][4])
{
	float						rotAngle = 0.0f;
	Geometry::Vector3< double > rotAxis;
	if (Geometry::IsNearZero(abs(matrix[0][1] - matrix[1][0])) &&
		Geometry::IsNearZero(abs(matrix[0][2] - matrix[2][0])) &&
		Geometry::IsNearZero(abs(matrix[1][2] - matrix[2][1])))
	{
		if (Geometry::IsNearZero(abs(matrix[0][1] + matrix[1][0]), 0.1) &&
			Geometry::IsNearZero(abs(matrix[0][2] + matrix[2][0]), 0.1) &&
			Geometry::IsNearZero(abs(matrix[1][2] + matrix[2][1]), 0.1) &&
			Geometry::IsNearZero(abs(matrix[0][0] + matrix[1][1] + matrix[2][2] - 3), 0.1))
		{
			// no rotation
			rotAngle = 0.0f;
		}
		else
		{ // 180 degrees rotation
			rotAngle = float(PI);
			const double xx = (matrix[0][0] + 1.0) / 2.0;
			const double yy = (matrix[1][1] + 1.0) / 2.0;
			const double zz = (matrix[2][2] + 1.0) / 2.0;
			const double xy = (matrix[0][1] + matrix[1][0]) / 4.0;
			const double xz = (matrix[0][2] + matrix[2][0]) / 4.0;
			const double yz = (matrix[1][2] + matrix[2][1]) / 4.0;

			if ((xx > yy) && (xx > zz))
			{
				if (Geometry::IsNearZero(xx))
				{
					rotAxis = Geometry::Vector3< double >(0.0, 0.7071, 0.7071);
				}
				else
				{
					rotAxis[0] = sqrt(xx);
					rotAxis[1] = xy / rotAxis[0];
					rotAxis[2] = xz / rotAxis[0];
				}
			}
			else if (yy > zz)
			{
				if (Geometry::IsNearZero(yy))
				{
					rotAxis = Geometry::Vector3< double >(0.7071, 0.0, 0.7071);
				}
				else
				{
					rotAxis[1] = sqrt(yy);
					rotAxis[0] = xy / rotAxis[1];
					rotAxis[2] = yz / rotAxis[1];
				}
			}
			else
			{
				if (Geometry::IsNearZero(zz))
				{
					rotAxis = Geometry::Vector3< double >(0.7071, 0.7071, 0.0);
				}
				else
				{
					rotAxis[2] = sqrt(zz);
					rotAxis[0] = xz / rotAxis[2];
					rotAxis[1] = yz / rotAxis[2];
				}
			}
		}
	}
	else
	{
		rotAngle = (float)(acos((matrix[0][0] + matrix[1][1] + matrix[2][2] - 1.0) / 2.0));
		rotAxis = Geometry::Vector3< double >(
			(matrix[2][1] - matrix[1][2]) / sqrt(sqr(matrix[2][1] - matrix[1][2]) + sqr(matrix[0][2] - matrix[2][0]) +
												 sqr(matrix[1][0] - matrix[0][1])),
			(matrix[0][2] - matrix[2][0]) / sqrt(sqr(matrix[2][1] - matrix[1][2]) + sqr(matrix[0][2] - matrix[2][0]) +
												 sqr(matrix[1][0] - matrix[0][1])),
			(matrix[1][0] - matrix[0][1]) / sqrt(sqr(matrix[2][1] - matrix[1][2]) + sqr(matrix[0][2] - matrix[2][0]) +
												 sqr(matrix[1][0] - matrix[0][1])));
	}
	rotAxis.NormalizeVector();

	return FQuat(FVector((float)-rotAxis.x, (float)rotAxis.y, (float)rotAxis.z), rotAngle).Inverse();
}

FQuat FGeometryUtil::GetRotationQuat(const ModelerAPI::Vector dir)
{
	const Geometry::Vector3< double > defaultDirVec(1.0, 0.0, 0.0);
	Geometry::Vector3< double >		  dirVec(-dir.x, dir.y, dir.z);
	dirVec.NormalizeVector();

	const double distToDirSqr = (dirVec - defaultDirVec).GetLengthSqr();
	const double rotAngle = acos((2.0 - distToDirSqr) / 2.0); // Rotation angle in radian

	Geometry::Vector3< double > rotAxis = defaultDirVec ^ dirVec;
	rotAxis.NormalizeVector();

	return FQuat(FVector((float)rotAxis.x, (float)rotAxis.y, (float)rotAxis.z), (float)rotAngle);
}

FQuat FGeometryUtil::GetRotationQuat(const double pitch, const double yaw, const double roll)
{
	return FQuat(FRotator((float)(-pitch * 180.0 / PI), (float)(-yaw * 180.0 / PI), (float)(-roll * 180.0 / PI)));
}

FVector FGeometryUtil::GetTranslationVector(const double matrix[3][4])
{
	return FVector((float)(matrix[0][3] * -100.0), (float)(matrix[1][3] * 100.0),
				   (float)(matrix[2][3] * 100.0)); // The base unit is centimetre in Unreal
}

FVector FGeometryUtil::GetTranslationVector(const ModelerAPI::Vertex pos)
{
	return FVector((float)(pos.x * -100.0), (float)(pos.y * 100.0),
				   (float)(pos.z * 100.0)); // The base unit is centimetre in Unreal
}

float FGeometryUtil::GetCameraFocalLength(const double sensorWidth, const double viewAngle)
{
	return (float)(sensorWidth / (2.0 * tan(viewAngle / 2.0))); // the sensor width and focal length are in millimetre
}

float FGeometryUtil::GetDistance3D(const double distanceZ, const double distance2D)
{
	return (float)(sqrt(pow(distanceZ, 2.0) + pow(distance2D, 2.0)) * 100.0); // centimetre
}

double FGeometryUtil::GetPitchAngle(const double cameraZ, const double targetZ, const double distance2D)
{
	const double angleSign = (cameraZ < targetZ) ? -1.0 : 1.0;
	const float	 realDistance = GetDistance3D(abs(targetZ - cameraZ), distance2D); // realDistance is in cm

	if (Geometry::IsNotNearZero((distance2D * 100.0) - realDistance))
		return acos((distance2D * 100.0) / realDistance) * angleSign;
	else
		return 0.0;
}

END_NAMESPACE_UE_AC
