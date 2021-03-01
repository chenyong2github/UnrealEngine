// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "CoreMinimal.h"
#include "LensFile.h"
#include "LensInterpolationUtils.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTestLensDistortion, "Plugins.LensDistortion", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace LensDistortionTestUtil
{
	void TestBilinearFindGrid(FAutomationTestBase& Test)
	{
		TArray<FDistortionMapPoint> DistortionMapping;

		FDistortionMapPoint TempPoint;
		TempPoint.Focus = 0.0f;
		TempPoint.Zoom = 0.0f;

		float MaxFocus = 100.0f;
		float MaxZoom = 100.0f;
		int32 FocusDiv = 10;
		int32 ZoomDiv = 10;

		//Asking for indices should fail with empty array
		int32 MinMinPoint, MinMaxPoint, MaxMinPoint, MaxMaxPoint;
		float DesiredFocus = 0.0f;
		float DesiredZoom = 0.0f;
		bool bSuccess = LensInterpolationUtils::FindBilinearInterpIndices<FDistortionMapPoint>(DesiredFocus, DesiredZoom, DistortionMapping, MinMinPoint, MinMaxPoint, MaxMinPoint, MaxMaxPoint);
		Test.TestEqual(TEXT("FindInterp - Empty"), bSuccess, false);

		DistortionMapping.Reserve(FocusDiv * ZoomDiv);
		for (int32 FocusIndex = 0; FocusIndex < FocusDiv; ++FocusIndex)
		{
			const float FocusValue = MaxFocus / (float)FocusDiv * (float)FocusIndex;

			if (FocusIndex != 7)
			{
				for (int32 ZoomIndex = 0; ZoomIndex < ZoomDiv; ++ZoomIndex)
				{
					const float ZoomValue = MaxZoom / (float)ZoomDiv * (float)ZoomIndex;
					TempPoint.Focus = FocusValue;
					TempPoint.Zoom = ZoomValue;
					TempPoint.Parameters.K1 = DistortionMapping.Num();
					DistortionMapping.Add(TempPoint);
				}
			}
			else
			{
				//Skip points for zoom
				for (int32 ZoomIndex = 0; ZoomIndex < 2; ++ZoomIndex)
				{
					const float ZoomValue = MaxZoom / (float)ZoomDiv * (float)ZoomIndex;
					TempPoint.Focus = FocusValue;
					TempPoint.Zoom = ZoomValue;
					TempPoint.Parameters.K1 = DistortionMapping.Num();
					DistortionMapping.Add(TempPoint);
				}
			}
		}

		Test.TestEqual(TEXT("FindInterp - Empty"), bSuccess, false);

		
		FDistortionMapPoint Result;

		{
			DesiredFocus = 5.0f;
			DesiredZoom = 5.0f;
			bSuccess = LensInterpolationUtils::FindBilinearInterpIndices<FDistortionMapPoint>(DesiredFocus, DesiredZoom, DistortionMapping, MinMinPoint, MinMaxPoint, MaxMinPoint, MaxMaxPoint);
			LensInterpolationUtils::FIZMappingBilinearInterpolation<FDistortionMapPoint>(DesiredFocus, DesiredZoom, DistortionMapping, Result);

			Test.TestEqual(*FString::Printf(TEXT("Find Indices - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), bSuccess, true);
			Test.TestEqual(*FString::Printf(TEXT("MinMin - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), MinMinPoint, 0);
			Test.TestEqual(*FString::Printf(TEXT("MinMax - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), MinMaxPoint, 1);
			Test.TestEqual(*FString::Printf(TEXT("MaxMin - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), MaxMinPoint, 10);
			Test.TestEqual(*FString::Printf(TEXT("MaxMax - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), MaxMaxPoint, 11);
			Test.TestEqual(*FString::Printf(TEXT("InterpResult - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), Result.Parameters.K1, 5.5f);
		}
		
		{
			DesiredFocus = 2.5f;
			DesiredZoom = 5.0f;
			bSuccess = LensInterpolationUtils::FindBilinearInterpIndices<FDistortionMapPoint>(DesiredFocus, DesiredZoom, DistortionMapping, MinMinPoint, MinMaxPoint, MaxMinPoint, MaxMaxPoint);
			LensInterpolationUtils::FIZMappingBilinearInterpolation<FDistortionMapPoint>(DesiredFocus, DesiredZoom, DistortionMapping, Result);

			Test.TestEqual(*FString::Printf(TEXT("Find Indices - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), bSuccess, true);
			Test.TestEqual(*FString::Printf(TEXT("MinMin - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), MinMinPoint, 0);
			Test.TestEqual(*FString::Printf(TEXT("MinMax - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), MinMaxPoint, 1);
			Test.TestEqual(*FString::Printf(TEXT("MaxMin - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), MaxMinPoint, 10);
			Test.TestEqual(*FString::Printf(TEXT("MaxMax - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), MaxMaxPoint, 11);
			Test.TestEqual(*FString::Printf(TEXT("InterpResult - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), Result.Parameters.K1, 3.0f);
		}
	

		{
			DesiredFocus = 0.0f;
			DesiredZoom = 5.0f;
			bSuccess = LensInterpolationUtils::FindBilinearInterpIndices<FDistortionMapPoint>(DesiredFocus, DesiredZoom, DistortionMapping, MinMinPoint, MinMaxPoint, MaxMinPoint, MaxMaxPoint);
			LensInterpolationUtils::FIZMappingBilinearInterpolation<FDistortionMapPoint>(DesiredFocus, DesiredZoom, DistortionMapping, Result);

			Test.TestEqual(*FString::Printf(TEXT("Find Indices - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), bSuccess, true);
			Test.TestEqual(*FString::Printf(TEXT("MinMin - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), MinMinPoint, 0);
			Test.TestEqual(*FString::Printf(TEXT("MinMax - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), MinMaxPoint, 1);
			Test.TestEqual(*FString::Printf(TEXT("MaxMin - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), MaxMinPoint, 0);
			Test.TestEqual(*FString::Printf(TEXT("MaxMax - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), MaxMaxPoint, 1);
			Test.TestEqual(*FString::Printf(TEXT("InterpResult - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), Result.Parameters.K1, 0.5f);
		}

		{
			DesiredFocus = 5.0f;
			DesiredZoom = 0.0f;
			bSuccess = LensInterpolationUtils::FindBilinearInterpIndices<FDistortionMapPoint>(DesiredFocus, DesiredZoom, DistortionMapping, MinMinPoint, MinMaxPoint, MaxMinPoint, MaxMaxPoint);
			LensInterpolationUtils::FIZMappingBilinearInterpolation<FDistortionMapPoint>(DesiredFocus, DesiredZoom, DistortionMapping, Result);

			Test.TestEqual(*FString::Printf(TEXT("Find Indices - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), bSuccess, true);
			Test.TestEqual(*FString::Printf(TEXT("MinMin - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), MinMinPoint, 0);
			Test.TestEqual(*FString::Printf(TEXT("MinMax - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), MinMaxPoint, 0);
			Test.TestEqual(*FString::Printf(TEXT("MaxMin - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), MaxMinPoint, 10);
			Test.TestEqual(*FString::Printf(TEXT("MaxMax - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), MaxMaxPoint, 10);
			Test.TestEqual(*FString::Printf(TEXT("InterpResult - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), Result.Parameters.K1, 5.0f);
		}
		
		{
			DesiredFocus = 30.0f;
			DesiredZoom = 10.0f;
			bSuccess = LensInterpolationUtils::FindBilinearInterpIndices<FDistortionMapPoint>(DesiredFocus, DesiredZoom, DistortionMapping, MinMinPoint, MinMaxPoint, MaxMinPoint, MaxMaxPoint);
			LensInterpolationUtils::FIZMappingBilinearInterpolation<FDistortionMapPoint>(DesiredFocus, DesiredZoom, DistortionMapping, Result);

			Test.TestEqual(*FString::Printf(TEXT("Find Indices - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), bSuccess, true);
			Test.TestEqual(*FString::Printf(TEXT("MinMin - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), MinMinPoint, 31);
			Test.TestEqual(*FString::Printf(TEXT("MinMax - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), MinMaxPoint, 31);
			Test.TestEqual(*FString::Printf(TEXT("MaxMin - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), MaxMinPoint, 31);
			Test.TestEqual(*FString::Printf(TEXT("MaxMax - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), MaxMaxPoint, 31);
			Test.TestEqual(*FString::Printf(TEXT("InterpResult - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), Result.Parameters.K1, 31.0f);
		}

		{
			DesiredFocus = 70.0f;
			DesiredZoom = 20.0f;
			bSuccess = LensInterpolationUtils::FindBilinearInterpIndices<FDistortionMapPoint>(DesiredFocus, DesiredZoom, DistortionMapping, MinMinPoint, MinMaxPoint, MaxMinPoint, MaxMaxPoint);
			LensInterpolationUtils::FIZMappingBilinearInterpolation<FDistortionMapPoint>(DesiredFocus, DesiredZoom, DistortionMapping, Result);

			Test.TestEqual(*FString::Printf(TEXT("Find Indices - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), bSuccess, true);
			Test.TestEqual(*FString::Printf(TEXT("MinMin - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), MinMinPoint, 62);
			Test.TestEqual(*FString::Printf(TEXT("MinMax - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), MinMaxPoint, 62);
			Test.TestEqual(*FString::Printf(TEXT("MaxMin - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), MaxMinPoint, 74);
			Test.TestEqual(*FString::Printf(TEXT("MaxMax - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), MaxMaxPoint, 74);
			Test.TestEqual(*FString::Printf(TEXT("InterpResult - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), Result.Parameters.K1, 68.0f);
		}

		{
			DesiredFocus = 70.0f;
			DesiredZoom = 5.0f;
			bSuccess = LensInterpolationUtils::FindBilinearInterpIndices<FDistortionMapPoint>(DesiredFocus, DesiredZoom, DistortionMapping, MinMinPoint, MinMaxPoint, MaxMinPoint, MaxMaxPoint);
			LensInterpolationUtils::FIZMappingBilinearInterpolation<FDistortionMapPoint>(DesiredFocus, DesiredZoom, DistortionMapping, Result);

			Test.TestEqual(*FString::Printf(TEXT("Find Indices - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), bSuccess, true);
			Test.TestEqual(*FString::Printf(TEXT("MinMin - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), MinMinPoint, 70);
			Test.TestEqual(*FString::Printf(TEXT("MinMax - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), MinMaxPoint, 71);
			Test.TestEqual(*FString::Printf(TEXT("MaxMin - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), MaxMinPoint, 70);
			Test.TestEqual(*FString::Printf(TEXT("MaxMax - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), MaxMaxPoint, 71);
			Test.TestEqual(*FString::Printf(TEXT("InterpResult - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), Result.Parameters.K1, 70.5f);
		}

		{
			DesiredFocus = 70.0f;
			DesiredZoom = 10.0f;
			bSuccess = LensInterpolationUtils::FindBilinearInterpIndices<FDistortionMapPoint>(DesiredFocus, DesiredZoom, DistortionMapping, MinMinPoint, MinMaxPoint, MaxMinPoint, MaxMaxPoint);
			LensInterpolationUtils::FIZMappingBilinearInterpolation<FDistortionMapPoint>(DesiredFocus, DesiredZoom, DistortionMapping, Result);

			Test.TestEqual(*FString::Printf(TEXT("Find Indices - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), bSuccess, true);
			Test.TestEqual(*FString::Printf(TEXT("MinMin - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), MinMinPoint, 71);
			Test.TestEqual(*FString::Printf(TEXT("MinMax - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), MinMaxPoint, 71);
			Test.TestEqual(*FString::Printf(TEXT("MaxMin - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), MaxMinPoint, 71);
			Test.TestEqual(*FString::Printf(TEXT("MaxMax - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), MaxMaxPoint, 71);
			Test.TestEqual(*FString::Printf(TEXT("InterpResult - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), Result.Parameters.K1, 71.0f);
		}

		{
			DesiredFocus = 70.0f;
			DesiredZoom = 25.0f;
			bSuccess = LensInterpolationUtils::FindBilinearInterpIndices<FDistortionMapPoint>(DesiredFocus, DesiredZoom, DistortionMapping, MinMinPoint, MinMaxPoint, MaxMinPoint, MaxMaxPoint);
			LensInterpolationUtils::FIZMappingBilinearInterpolation<FDistortionMapPoint>(DesiredFocus, DesiredZoom, DistortionMapping, Result);

			Test.TestEqual(*FString::Printf(TEXT("Find Indices - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), bSuccess, true);
			Test.TestEqual(*FString::Printf(TEXT("MinMin - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), MinMinPoint, 62);
			Test.TestEqual(*FString::Printf(TEXT("MinMax - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), MinMaxPoint, 63);
			Test.TestEqual(*FString::Printf(TEXT("MaxMin - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), MaxMinPoint, 74);
			Test.TestEqual(*FString::Printf(TEXT("MaxMax - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), MaxMaxPoint, 75);
			Test.TestEqual(*FString::Printf(TEXT("InterpResult - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), Result.Parameters.K1, 68.5f);
		}

		{
			DesiredFocus = -1.0f;
			DesiredZoom = -1.0f;
			bSuccess = LensInterpolationUtils::FindBilinearInterpIndices<FDistortionMapPoint>(DesiredFocus, DesiredZoom, DistortionMapping, MinMinPoint, MinMaxPoint, MaxMinPoint, MaxMaxPoint);
			LensInterpolationUtils::FIZMappingBilinearInterpolation<FDistortionMapPoint>(DesiredFocus, DesiredZoom, DistortionMapping, Result);

			Test.TestEqual(*FString::Printf(TEXT("Find Indices - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), bSuccess, true);
			Test.TestEqual(*FString::Printf(TEXT("MinMin - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), MinMinPoint, 0);
			Test.TestEqual(*FString::Printf(TEXT("MinMax - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), MinMaxPoint, 0);
			Test.TestEqual(*FString::Printf(TEXT("MaxMin - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), MaxMinPoint, 0);
			Test.TestEqual(*FString::Printf(TEXT("MaxMax - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), MaxMaxPoint, 0);
			Test.TestEqual(*FString::Printf(TEXT("InterpResult - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), Result.Parameters.K1, 0.0f);
		}

		{
			DesiredFocus = -1.0f;
			DesiredZoom = 35.0f;
			bSuccess = LensInterpolationUtils::FindBilinearInterpIndices<FDistortionMapPoint>(DesiredFocus, DesiredZoom, DistortionMapping, MinMinPoint, MinMaxPoint, MaxMinPoint, MaxMaxPoint);
			LensInterpolationUtils::FIZMappingBilinearInterpolation<FDistortionMapPoint>(DesiredFocus, DesiredZoom, DistortionMapping, Result);

			Test.TestEqual(*FString::Printf(TEXT("Find Indices - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), bSuccess, true);
			Test.TestEqual(*FString::Printf(TEXT("MinMin - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), MinMinPoint, 3);
			Test.TestEqual(*FString::Printf(TEXT("MinMax - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), MinMaxPoint, 4);
			Test.TestEqual(*FString::Printf(TEXT("MaxMin - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), MaxMinPoint, 3);
			Test.TestEqual(*FString::Printf(TEXT("MaxMax - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), MaxMaxPoint, 4);
			Test.TestEqual(*FString::Printf(TEXT("InterpResult - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), Result.Parameters.K1, 3.5f);
		}

		{
			DesiredFocus = -1.0f;
			DesiredZoom = 110.0f;
			bSuccess = LensInterpolationUtils::FindBilinearInterpIndices<FDistortionMapPoint>(DesiredFocus, DesiredZoom, DistortionMapping, MinMinPoint, MinMaxPoint, MaxMinPoint, MaxMaxPoint);
			LensInterpolationUtils::FIZMappingBilinearInterpolation<FDistortionMapPoint>(DesiredFocus, DesiredZoom, DistortionMapping, Result);

			Test.TestEqual(*FString::Printf(TEXT("Find Indices - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), bSuccess, true);
			Test.TestEqual(*FString::Printf(TEXT("MinMin - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), MinMinPoint, 9);
			Test.TestEqual(*FString::Printf(TEXT("MinMax - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), MinMaxPoint, 9);
			Test.TestEqual(*FString::Printf(TEXT("MaxMin - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), MaxMinPoint, 9);
			Test.TestEqual(*FString::Printf(TEXT("MaxMax - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), MaxMaxPoint, 9);
			Test.TestEqual(*FString::Printf(TEXT("InterpResult - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), Result.Parameters.K1, 9.0f);
		}

		{
			DesiredFocus = 35.0f;
			DesiredZoom = 110.0f;
			bSuccess = LensInterpolationUtils::FindBilinearInterpIndices<FDistortionMapPoint>(DesiredFocus, DesiredZoom, DistortionMapping, MinMinPoint, MinMaxPoint, MaxMinPoint, MaxMaxPoint);
			LensInterpolationUtils::FIZMappingBilinearInterpolation<FDistortionMapPoint>(DesiredFocus, DesiredZoom, DistortionMapping, Result);

			Test.TestEqual(*FString::Printf(TEXT("Find Indices - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), bSuccess, true);
			Test.TestEqual(*FString::Printf(TEXT("MinMin - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), MinMinPoint, 39);
			Test.TestEqual(*FString::Printf(TEXT("MinMax - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), MinMaxPoint, 39);
			Test.TestEqual(*FString::Printf(TEXT("MaxMin - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), MaxMinPoint, 49);
			Test.TestEqual(*FString::Printf(TEXT("MaxMax - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), MaxMaxPoint, 49);
			Test.TestEqual(*FString::Printf(TEXT("InterpResult - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), Result.Parameters.K1, 44.0f);
		}

		{
			DesiredFocus = 110.0f;
			DesiredZoom = 110.0f;
			bSuccess = LensInterpolationUtils::FindBilinearInterpIndices<FDistortionMapPoint>(DesiredFocus, DesiredZoom, DistortionMapping, MinMinPoint, MinMaxPoint, MaxMinPoint, MaxMaxPoint);
			LensInterpolationUtils::FIZMappingBilinearInterpolation<FDistortionMapPoint>(DesiredFocus, DesiredZoom, DistortionMapping, Result);

			Test.TestEqual(*FString::Printf(TEXT("Find Indices - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), bSuccess, true);
			Test.TestEqual(*FString::Printf(TEXT("MinMin - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), MinMinPoint, 91);
			Test.TestEqual(*FString::Printf(TEXT("MinMax - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), MinMaxPoint, 91);
			Test.TestEqual(*FString::Printf(TEXT("MaxMin - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), MaxMinPoint, 91);
			Test.TestEqual(*FString::Printf(TEXT("MaxMax - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), MaxMaxPoint, 91);
			Test.TestEqual(*FString::Printf(TEXT("InterpResult - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), Result.Parameters.K1, 91.0f);
		}

		{
			DesiredFocus = 110.0f;
			DesiredZoom = 35.0f;
			bSuccess = LensInterpolationUtils::FindBilinearInterpIndices<FDistortionMapPoint>(DesiredFocus, DesiredZoom, DistortionMapping, MinMinPoint, MinMaxPoint, MaxMinPoint, MaxMaxPoint);
			LensInterpolationUtils::FIZMappingBilinearInterpolation<FDistortionMapPoint>(DesiredFocus, DesiredZoom, DistortionMapping, Result);

			Test.TestEqual(*FString::Printf(TEXT("Find Indices - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), bSuccess, true);
			Test.TestEqual(*FString::Printf(TEXT("MinMin - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), MinMinPoint, 85);
			Test.TestEqual(*FString::Printf(TEXT("MinMax - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), MinMaxPoint, 86);
			Test.TestEqual(*FString::Printf(TEXT("MaxMin - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), MaxMinPoint, 85);
			Test.TestEqual(*FString::Printf(TEXT("MaxMax - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), MaxMaxPoint, 86);
			Test.TestEqual(*FString::Printf(TEXT("InterpResult - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), Result.Parameters.K1, 85.5f);
		}

		{
			DesiredFocus = 110.0f;
			DesiredZoom = -1.0f;
			bSuccess = LensInterpolationUtils::FindBilinearInterpIndices<FDistortionMapPoint>(DesiredFocus, DesiredZoom, DistortionMapping, MinMinPoint, MinMaxPoint, MaxMinPoint, MaxMaxPoint);
			LensInterpolationUtils::FIZMappingBilinearInterpolation<FDistortionMapPoint>(DesiredFocus, DesiredZoom, DistortionMapping, Result);

			Test.TestEqual(*FString::Printf(TEXT("Find Indices - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), bSuccess, true);
			Test.TestEqual(*FString::Printf(TEXT("MinMin - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), MinMinPoint, 82);
			Test.TestEqual(*FString::Printf(TEXT("MinMax - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), MinMaxPoint, 82);
			Test.TestEqual(*FString::Printf(TEXT("MaxMin - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), MaxMinPoint, 82);
			Test.TestEqual(*FString::Printf(TEXT("MaxMax - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), MaxMaxPoint, 82);
			Test.TestEqual(*FString::Printf(TEXT("InterpResult - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), Result.Parameters.K1, 82.0f);
		}

		{
			DesiredFocus = 35.0f;
			DesiredZoom = -1.0f;
			bSuccess = LensInterpolationUtils::FindBilinearInterpIndices<FDistortionMapPoint>(DesiredFocus, DesiredZoom, DistortionMapping, MinMinPoint, MinMaxPoint, MaxMinPoint, MaxMaxPoint);
			LensInterpolationUtils::FIZMappingBilinearInterpolation<FDistortionMapPoint>(DesiredFocus, DesiredZoom, DistortionMapping, Result);

			Test.TestEqual(*FString::Printf(TEXT("Find Indices - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), bSuccess, true);
			Test.TestEqual(*FString::Printf(TEXT("MinMin - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), MinMinPoint, 30);
			Test.TestEqual(*FString::Printf(TEXT("MinMax - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), MinMaxPoint, 30);
			Test.TestEqual(*FString::Printf(TEXT("MaxMin - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), MaxMinPoint, 40);
			Test.TestEqual(*FString::Printf(TEXT("MaxMax - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), MaxMaxPoint, 40);
			Test.TestEqual(*FString::Printf(TEXT("InterpResult - (%0.2f,%0.2f)"), DesiredFocus, DesiredZoom), Result.Parameters.K1, 35.0f);
		}
	}
}


bool FTestLensDistortion::RunTest(const FString& Parameters)
{
	LensDistortionTestUtil::TestBilinearFindGrid(*this);

    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

