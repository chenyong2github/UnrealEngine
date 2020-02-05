// Copyright Epic Games, Inc. All Rights Reserved.
 
#include "LidarPointCloudShared.h"
#include "LidarPointCloud.h"

double FBenchmarkTimer::Time = 0;

FArchive& operator<<(FArchive& Ar, FLidarPointCloudPoint& P)
{
	Ar << P.Location << P.Color;

	if (Ar.CustomVer(ULidarPointCloud::PointCloudFileGUID) > 8)
	{
		uint8 bVisible = P.bVisible;
		Ar << bVisible;
		P.bVisible = bVisible;
	}

	if (Ar.CustomVer(ULidarPointCloud::PointCloudFileGUID) > 12)
	{
		uint8 ClassificationID = P.ClassificationID;
		Ar << ClassificationID;
		P.ClassificationID = ClassificationID;
	}

	return Ar;
}

const FDoubleVector FDoubleVector::ZeroVector = FDoubleVector(FVector::ZeroVector);
const FDoubleVector FDoubleVector::OneVector = FDoubleVector(FVector::OneVector);
const FDoubleVector FDoubleVector::UpVector = FDoubleVector(FVector::UpVector);
const FDoubleVector FDoubleVector::ForwardVector = FDoubleVector(FVector::ForwardVector);
const FDoubleVector FDoubleVector::RightVector = FDoubleVector(FVector::RightVector);