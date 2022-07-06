// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

class AActor;
class UPCGParamData;
class UPCGPointData;
class UPCGPolyLineData;
class UPCGPrimitiveData;
class UPCGSurfaceData;
class UPCGVolumeData;
class UPCGSettings;
struct FPCGDataCollection;
struct FPCGPinProperties;
struct FPCGPoint;

namespace PCGTestsCommon
{
	static const int TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	AActor* CreateTemporaryActor();
	UPCGPointData* CreateEmptyPointData();
	UPCGParamData* CreateEmptyParamData();

	/** Creates a PointData with a single point at the origin */
	UPCGPointData* CreatePointData();
	/** Creates a PointData with a single point at the provided location */
	UPCGPointData* CreatePointData(const FVector& InLocation);

	/** Creates a PointData with PointCount many points, and randomizes the Transform and Color */
	UPCGPointData* CreateRandomPointData(int32 PointCount, int32 Seed);

	UPCGPolyLineData* CreatePolyLineData();
	UPCGSurfaceData* CreateSurfaceData();
	UPCGVolumeData* CreateVolumeData(const FBox& InBounds = FBox::BuildAABB(FVector::ZeroVector, FVector::OneVector * 100));
	UPCGPrimitiveData* CreatePrimitiveData();

	TArray<FPCGDataCollection> GenerateAllowedData(const FPCGPinProperties& PinProperties);

	/** Validates that two Spatial Points are identical */
	bool PointsAreIdentical(const FPCGPoint& FirstPoint, const FPCGPoint& SecondPoint);
}

class FPCGTestBaseClass : public FAutomationTestBase
{
public:
	using FAutomationTestBase::FAutomationTestBase;

protected:
	/** Generates all valid input combinations */
	bool SmokeTestAnyValidInput(UPCGSettings* InSettings, TFunction<bool(const FPCGDataCollection&, const FPCGDataCollection&)> ValidationFn = TFunction<bool(const FPCGDataCollection&, const FPCGDataCollection&)>());
};