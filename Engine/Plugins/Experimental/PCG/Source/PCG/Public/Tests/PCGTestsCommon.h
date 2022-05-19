// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

class AActor;
class UPCGPointData;
class UPCGPolyLineData;
class UPCGSurfaceData;
class UPCGVolumeData;
class UPCGPrimitiveData;
class UPCGParamData;
class UPCGSettings;
struct FPCGDataCollection;

namespace PCGTestsCommon
{
	static const int TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	AActor* CreateTemporaryActor();
	UPCGPointData* CreateEmptyPointData();
	UPCGParamData* CreateEmptyParamData();

	UPCGPointData* CreatePointData();
	UPCGPolyLineData* CreatePolyLineData();
	UPCGSurfaceData* CreateSurfaceData();
	UPCGVolumeData* CreateVolumeData(const FBox& InBounds = FBox::BuildAABB(FVector::ZeroVector, FVector::OneVector * 100));
	UPCGPrimitiveData* CreatePrimitiveData();
}

class FPCGTestBaseClass : public FAutomationTestBase
{
public:
	using FAutomationTestBase::FAutomationTestBase;

protected:
	/** Generates all valid input combinations */
	bool SmokeTestAnyValidInput(UPCGSettings* InSettings, TFunction<bool(const FPCGDataCollection&, const FPCGDataCollection&)> ValidationFn = TFunction<bool(const FPCGDataCollection&, const FPCGDataCollection&)>());
};