// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tests/PCGTestsCommon.h"
#include "Data/PCGIntersectionData.h"
#include "Data/PCGPointData.h"
#include "Data/PCGVolumeData.h"

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPCGIntersectionDataBasicTest, FPCGTestBaseClass, "pcg.tests.Intersection.Basic", PCGTestsCommon::TestFlags)

bool FPCGIntersectionDataBasicTest::RunTest(const FString& Parameters)
{
	UPCGPointData* InsidePoint = PCGTestsCommon::CreatePointData();
	check(InsidePoint->GetPoints().Num() == 1);

	UPCGPointData* OutsidePoint = PCGTestsCommon::CreatePointData();
	check(OutsidePoint->GetPoints().Num() == 1);
	OutsidePoint->GetMutablePoints()[0].Transform.SetLocation(FVector::OneVector * 10000);

	UPCGVolumeData* Volume = PCGTestsCommon::CreateVolumeData(FBox::BuildAABB(FVector::ZeroVector, FVector::OneVector * 100));

	// Create intersections
	UPCGIntersectionData* InsideVolume = InsidePoint->IntersectWith(Volume);
	UPCGIntersectionData* VolumeInside = Volume->IntersectWith(InsidePoint);
	UPCGIntersectionData* OutsideVolume = OutsidePoint->IntersectWith(Volume);
	UPCGIntersectionData* VolumeOutside = Volume->IntersectWith(OutsidePoint);

	auto ValidateInsideIntersection = [this, InsidePoint](UPCGIntersectionData* Intersection)
	{
		// Basic data validations
		TestTrue("Valid intersection", Intersection != nullptr);

		if (!Intersection)
		{
			return;
		}

		TestTrue("Valid dimension", Intersection->GetDimension() == 0);
		TestTrue("Valid bounds", Intersection->GetBounds() == InsidePoint->GetBounds());
		TestTrue("Valid strict bounds", Intersection->GetStrictBounds() == InsidePoint->GetStrictBounds());

		// Validate sample point		
		const FPCGPoint& Point = InsidePoint->GetPoints()[0];

		FPCGPoint SampledPoint;
		TestTrue("Successful point sampling", Intersection->SamplePoint(Point.Transform, Point.GetLocalBounds(), SampledPoint, nullptr));
		// TODO: should do a full point comparison, not only on a positional basis
		TestTrue("Correct sampled point", (Point.Transform.GetLocation() - SampledPoint.Transform.GetLocation()).SquaredLength() < KINDA_SMALL_NUMBER);

		// Validate create point data
		const UPCGPointData* OutputPointData = Intersection->ToPointData(nullptr);
		TestTrue("Successful ToPoint", OutputPointData != nullptr);
		
		if (OutputPointData)
		{
			TestTrue("Valid number of points in ToPoint", OutputPointData->GetPoints().Num() == 1);
			if (OutputPointData->GetPoints().Num() == 1)
			{
				TestTrue("Correct point in ToPoint", (Point.Transform.GetLocation() - OutputPointData->GetPoints()[0].Transform.GetLocation()).SquaredLength() < KINDA_SMALL_NUMBER);
			}
		}
	};

	ValidateInsideIntersection(InsideVolume);
	ValidateInsideIntersection(VolumeInside);

	auto ValidateOutsideIntersection = [this, OutsidePoint](UPCGIntersectionData* Intersection)
	{
		TestTrue("Valid intersection", Intersection != nullptr);

		if (!Intersection)
		{
			return;
		}

		TestTrue("Valid dimension", Intersection->GetDimension() == 0);
		TestTrue("Null bounds", !Intersection->GetBounds().IsValid);
		TestTrue("Null strict bounds", !Intersection->GetStrictBounds().IsValid);

		// Validate that we're not able to sample a point
		const FPCGPoint& Point = OutsidePoint->GetPoints()[0];

		FPCGPoint SampledPoint;
		TestTrue("Unsucessful point sampling", !Intersection->SamplePoint(Point.Transform, Point.GetLocalBounds(), SampledPoint, nullptr));

		// Validate empty point data
		const UPCGPointData* OutputPointData = Intersection->ToPointData(nullptr);
		TestTrue("Successful ToPoint", OutputPointData != nullptr);

		if (OutputPointData)
		{
			TestTrue("Empty point data", OutputPointData->GetPoints().Num() == 0);
		}
	};

	ValidateOutsideIntersection(OutsideVolume);
	ValidateOutsideIntersection(VolumeOutside);

	return true;
}

//TOODs:
// Test with one/two data that do not have a trivial transformation (e.g. projection, surfaces, ...)