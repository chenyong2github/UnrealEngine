// Copyright Epic Games, Inc. All Rights Reserved.


#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"

#include "CameraCalibrationSubsystem.h"
#include "CameraCalibrationTypes.h"
#include "Engine/Engine.h"
#include "LensFile.h"
#include "Misc/AutomationTest.h"
#include "SphericalLensDistortionModelHandler.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTestCameraCalibrationCore, "Plugins.CameraCalibrationCore", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)


namespace CameraCalibrationTestUtil
{
	UWorld* GetFirstWorld()
	{
		//Get first valid world
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
#if WITH_EDITOR
			if (GIsEditor)
			{
				if(Context.WorldType == EWorldType::Editor)
				{
					return Context.World();
				}
			}
			else
			{
				if(Context.World() != nullptr)
				{
					return Context.World();
				}
			}
#else
			if(Context.World() != nullptr)
			{
				return Context.World();
			}
#endif
		}

		return nullptr;
	}
	
	void TestDistortionParameterCurveBlending(FAutomationTestBase& Test)
	{
		UWorld* ValidWorld = GetFirstWorld();
		if(ValidWorld == nullptr)
		{
			return;
		}
		
		// Create LensFile container
		const TCHAR* LensFileName = TEXT("AutomationTestLensFile");
		ULensFile* LensFile = NewObject<ULensFile>(GetTransientPackage(), LensFileName);


		const TCHAR* HandlerName = TEXT("AutomationTestLensHandler");
		USphericalLensDistortionModelHandler* ProducedLensDistortionHandler = NewObject<USphericalLensDistortionModelHandler>(ValidWorld, HandlerName, RF_Transient);
		ensure(ProducedLensDistortionHandler);
		if(ProducedLensDistortionHandler == nullptr)
		{
			return;
		}

		const auto TestDualCurveEvaluationResult = [&Test](TConstArrayView<float> InResult, float InBlendingFactor, TConstArrayView<float> InLerpSource1, TConstArrayView<float> InLerpSource2, TConstArrayView<float> InLerpSource3, TConstArrayView<float> InLerpSource4)
		{
			for(int32 Index = 0; Index < InResult.Num(); ++Index)
			{
				const float CurveOneBlending = FMath::Lerp(InLerpSource1[Index], InLerpSource2[Index], InBlendingFactor);
				const float CurveTwoBlending = FMath::Lerp(InLerpSource3[Index], InLerpSource4[Index], InBlendingFactor);
				const float ExpectedValue = FMath::Lerp(CurveOneBlending, CurveTwoBlending, InBlendingFactor);
				Test.TestEqual(FString::Printf(TEXT("Parameter[%d] should be equal to %0.2f"), Index, ExpectedValue), InResult[Index], ExpectedValue);
			}
		};

		const auto TestSingleCurveEvaluationResult = [&Test](TConstArrayView<float> InResult, float InBlendingFactor, TConstArrayView<float> InLerpSource1, TConstArrayView<float> InLerpSource2)
		{
			for(int32 Index = 0; Index < InResult.Num(); ++Index)
			{
				const float ExpectedValue = FMath::Lerp(InLerpSource1[Index], InLerpSource2[Index], InBlendingFactor);
				Test.TestEqual(FString::Printf(TEXT("Parameter[%d] should be equal to %0.2f"), Index, ExpectedValue), InResult[Index], ExpectedValue);
			}
		};

		const auto TestEvaluationResult = [&Test](TConstArrayView<float> InExpected, TConstArrayView<float> InResult)
		{
			for(int32 Index = 0; Index < InResult.Num(); ++Index)
			{
				Test.TestEqual(FString::Printf(TEXT("Parameter[%d] should be equal to %0.2f"), Index, InExpected[Index]), InResult[Index], InExpected[Index]);
			}	
		};

		struct FLensData
		{
			FLensData() = default;
			FLensData(float InFocus, float InZoom, FDistortionInfo InDistortionInfo, FImageCenterInfo InImageCenter, FFocalLengthInfo InFocalLength)
				: Focus(InFocus)
				, Zoom(InZoom)
				, Distortion(InDistortionInfo)
				, ImageCenter(InImageCenter)
				, FocalLength(InFocalLength)
			{}
			
			float Focus;
			float Zoom;
			FDistortionInfo Distortion;
			FImageCenterInfo ImageCenter;
			FFocalLengthInfo FocalLength;
		};

		constexpr int32 InputCount = 4;
		FLensData InputData[InputCount] =
		{
			  FLensData(0.0f, 0.0f, {{TArray<float>({1.0f, -1.0f, 0.0f, 0.0f, 0.0f})}}, {FVector2D(8.0f, 7.0f)}, {FVector2D(3.0f, 2.0f)})
			, FLensData(0.0f, 1.0f, {{TArray<float>({2.0f, 0.0f, 0.0f, 0.0f, 1.0f})}},  {FVector2D(9.0f, 8.0f)}, {FVector2D(5.0f, 3.0f)})
			, FLensData(1.0f, 0.0f, {{TArray<float>({0.0f, 1.0f, 0.0f, 0.0f, -2.0f})}}, {FVector2D(2.0f, 4.0f)}, {FVector2D(4.0f, 6.0f)})
			, FLensData(1.0f, 1.0f, {{TArray<float>({1.0f, 2.0f, 0.0f, 0.0f, 0.0f})}},  {FVector2D(4.0f, 7.0f)}, {FVector2D(9.0f, 8.0f)})
		};

		for(int32 InputIndex = 0; InputIndex < InputCount; ++InputIndex)
		{
			//straight tests
			const FLensData& Input = InputData[InputIndex];
			LensFile->AddDistortionPoint(Input.Focus, Input.Zoom, Input.Distortion, Input.FocalLength);
			LensFile->AddImageCenterPoint(Input.Focus, Input.Zoom, Input.ImageCenter);
		}

		const FLensData& Input0 = InputData[0];
		const FLensData& Input1 = InputData[1];
		const FLensData& Input2 = InputData[2];
		const FLensData& Input3 = InputData[3];
		FLensData EvaluatedData;
		LensFile->EvaluateImageCenterParameters(Input0.Focus, Input0.Zoom, EvaluatedData.ImageCenter);
		LensFile->EvaluateDistortionParameters(Input0.Focus, Input0.Zoom, EvaluatedData.Distortion);
		LensFile->EvaluateFocalLength(Input0.Focus, Input0.Zoom, EvaluatedData.FocalLength);
		LensFile->EvaluateDistortionData(Input0.Focus, Input0.Zoom, LensFile->LensInfo.SensorDimensions, ProducedLensDistortionHandler);
		TestEvaluationResult({Input0.ImageCenter.PrincipalPoint.X, Input0.ImageCenter.PrincipalPoint.Y}, {EvaluatedData.ImageCenter.PrincipalPoint.X, EvaluatedData.ImageCenter.PrincipalPoint.Y});
		TestEvaluationResult({Input0.FocalLength.FxFy.X, Input0.FocalLength.FxFy.Y}, {EvaluatedData.FocalLength.FxFy.X, EvaluatedData.FocalLength.FxFy.Y});
		TestEvaluationResult(Input0.Distortion.Parameters, EvaluatedData.Distortion.Parameters);
		TestEvaluationResult(Input0.Distortion.Parameters, ProducedLensDistortionHandler->GetCurrentDistortionState().DistortionInfo.Parameters);
		TestEvaluationResult({Input0.ImageCenter.PrincipalPoint.X, Input0.ImageCenter.PrincipalPoint.Y}, {ProducedLensDistortionHandler->GetCurrentDistortionState().ImageCenter.PrincipalPoint.X, ProducedLensDistortionHandler->GetCurrentDistortionState().ImageCenter.PrincipalPoint.Y});
		TestEvaluationResult({Input0.FocalLength.FxFy.X, Input0.FocalLength.FxFy.Y}, {ProducedLensDistortionHandler->GetCurrentDistortionState().FocalLengthInfo.FxFy.X, ProducedLensDistortionHandler->GetCurrentDistortionState().FocalLengthInfo.FxFy.Y});

		LensFile->EvaluateImageCenterParameters(Input1.Focus, Input1.Zoom, EvaluatedData.ImageCenter);
		LensFile->EvaluateDistortionParameters(Input1.Focus, Input1.Zoom, EvaluatedData.Distortion);
		LensFile->EvaluateFocalLength(Input1.Focus, Input1.Zoom, EvaluatedData.FocalLength);
		LensFile->EvaluateDistortionData(Input1.Focus, Input1.Zoom, LensFile->LensInfo.SensorDimensions, ProducedLensDistortionHandler);
		TestEvaluationResult({Input1.ImageCenter.PrincipalPoint.X, Input1.ImageCenter.PrincipalPoint.Y}, {EvaluatedData.ImageCenter.PrincipalPoint.X, EvaluatedData.ImageCenter.PrincipalPoint.Y});
		TestEvaluationResult({Input1.FocalLength.FxFy.X, Input1.FocalLength.FxFy.Y}, {EvaluatedData.FocalLength.FxFy.X, EvaluatedData.FocalLength.FxFy.Y});
		TestEvaluationResult(Input1.Distortion.Parameters, EvaluatedData.Distortion.Parameters);
		TestEvaluationResult(Input1.Distortion.Parameters, ProducedLensDistortionHandler->GetCurrentDistortionState().DistortionInfo.Parameters);
		TestEvaluationResult({Input1.ImageCenter.PrincipalPoint.X, Input1.ImageCenter.PrincipalPoint.Y}, {ProducedLensDistortionHandler->GetCurrentDistortionState().ImageCenter.PrincipalPoint.X, ProducedLensDistortionHandler->GetCurrentDistortionState().ImageCenter.PrincipalPoint.Y});
		TestEvaluationResult({Input1.FocalLength.FxFy.X, Input1.FocalLength.FxFy.Y}, {ProducedLensDistortionHandler->GetCurrentDistortionState().FocalLengthInfo.FxFy.X, ProducedLensDistortionHandler->GetCurrentDistortionState().FocalLengthInfo.FxFy.Y});

		
		LensFile->EvaluateImageCenterParameters(Input2.Focus, Input2.Zoom, EvaluatedData.ImageCenter);
		LensFile->EvaluateDistortionParameters(Input2.Focus, Input2.Zoom, EvaluatedData.Distortion);
		LensFile->EvaluateFocalLength(Input2.Focus, Input2.Zoom, EvaluatedData.FocalLength);
		LensFile->EvaluateDistortionData(Input2.Focus, Input2.Zoom, LensFile->LensInfo.SensorDimensions, ProducedLensDistortionHandler);
		TestEvaluationResult({Input2.ImageCenter.PrincipalPoint.X, Input2.ImageCenter.PrincipalPoint.Y}, {EvaluatedData.ImageCenter.PrincipalPoint.X, EvaluatedData.ImageCenter.PrincipalPoint.Y});
		TestEvaluationResult({Input2.FocalLength.FxFy.X, Input2.FocalLength.FxFy.Y}, {EvaluatedData.FocalLength.FxFy.X, EvaluatedData.FocalLength.FxFy.Y});
		TestEvaluationResult(Input2.Distortion.Parameters, EvaluatedData.Distortion.Parameters);
		TestEvaluationResult(Input2.Distortion.Parameters, ProducedLensDistortionHandler->GetCurrentDistortionState().DistortionInfo.Parameters);
		TestEvaluationResult({Input2.ImageCenter.PrincipalPoint.X, Input2.ImageCenter.PrincipalPoint.Y}, {ProducedLensDistortionHandler->GetCurrentDistortionState().ImageCenter.PrincipalPoint.X, ProducedLensDistortionHandler->GetCurrentDistortionState().ImageCenter.PrincipalPoint.Y});
		TestEvaluationResult({Input2.FocalLength.FxFy.X, Input2.FocalLength.FxFy.Y}, {ProducedLensDistortionHandler->GetCurrentDistortionState().FocalLengthInfo.FxFy.X, ProducedLensDistortionHandler->GetCurrentDistortionState().FocalLengthInfo.FxFy.Y});

		LensFile->EvaluateImageCenterParameters(Input3.Focus, Input3.Zoom, EvaluatedData.ImageCenter);
		LensFile->EvaluateDistortionParameters(Input3.Focus, Input3.Zoom, EvaluatedData.Distortion);
		LensFile->EvaluateFocalLength(Input3.Focus, Input3.Zoom, EvaluatedData.FocalLength);
		LensFile->EvaluateDistortionData(Input3.Focus, Input3.Zoom, LensFile->LensInfo.SensorDimensions, ProducedLensDistortionHandler);
		TestEvaluationResult({Input3.ImageCenter.PrincipalPoint.X, Input3.ImageCenter.PrincipalPoint.Y}, {EvaluatedData.ImageCenter.PrincipalPoint.X, EvaluatedData.ImageCenter.PrincipalPoint.Y});
		TestEvaluationResult({Input3.FocalLength.FxFy.X, Input3.FocalLength.FxFy.Y}, {EvaluatedData.FocalLength.FxFy.X, EvaluatedData.FocalLength.FxFy.Y});
		TestEvaluationResult(Input3.Distortion.Parameters, EvaluatedData.Distortion.Parameters);
		TestEvaluationResult(Input3.Distortion.Parameters, ProducedLensDistortionHandler->GetCurrentDistortionState().DistortionInfo.Parameters);
		TestEvaluationResult({Input3.ImageCenter.PrincipalPoint.X, Input3.ImageCenter.PrincipalPoint.Y}, {ProducedLensDistortionHandler->GetCurrentDistortionState().ImageCenter.PrincipalPoint.X, ProducedLensDistortionHandler->GetCurrentDistortionState().ImageCenter.PrincipalPoint.Y});
		TestEvaluationResult({Input3.FocalLength.FxFy.X, Input3.FocalLength.FxFy.Y}, {ProducedLensDistortionHandler->GetCurrentDistortionState().FocalLengthInfo.FxFy.X, ProducedLensDistortionHandler->GetCurrentDistortionState().FocalLengthInfo.FxFy.Y});

		//Sin
		constexpr float BlendFactor = 0.25f;	
		LensFile->EvaluateImageCenterParameters(Input0.Focus, 0.25f, EvaluatedData.ImageCenter);
		LensFile->EvaluateDistortionParameters(Input0.Focus, 0.25f, EvaluatedData.Distortion);
		LensFile->EvaluateFocalLength(Input0.Focus, 0.25f, EvaluatedData.FocalLength);
		LensFile->EvaluateDistortionData(Input0.Focus, 0.25f, LensFile->LensInfo.SensorDimensions, ProducedLensDistortionHandler);
		TestSingleCurveEvaluationResult({EvaluatedData.ImageCenter.PrincipalPoint.X, EvaluatedData.ImageCenter.PrincipalPoint.Y}, BlendFactor, {Input0.ImageCenter.PrincipalPoint.X, Input0.ImageCenter.PrincipalPoint.Y}, {Input1.ImageCenter.PrincipalPoint.X, Input1.ImageCenter.PrincipalPoint.Y});
		TestSingleCurveEvaluationResult({EvaluatedData.FocalLength.FxFy.X, EvaluatedData.FocalLength.FxFy.Y}, BlendFactor, {Input0.FocalLength.FxFy.X, Input0.FocalLength.FxFy.Y}, {Input1.FocalLength.FxFy.X, Input1.FocalLength.FxFy.Y});
		TestSingleCurveEvaluationResult(EvaluatedData.Distortion.Parameters, BlendFactor, Input0.Distortion.Parameters, Input1.Distortion.Parameters);
		TestSingleCurveEvaluationResult(ProducedLensDistortionHandler->GetCurrentDistortionState().DistortionInfo.Parameters, BlendFactor, Input0.Distortion.Parameters, Input1.Distortion.Parameters);
		TestSingleCurveEvaluationResult({ProducedLensDistortionHandler->GetCurrentDistortionState().ImageCenter.PrincipalPoint.X, ProducedLensDistortionHandler->GetCurrentDistortionState().ImageCenter.PrincipalPoint.Y}, BlendFactor, {Input0.ImageCenter.PrincipalPoint.X, Input0.ImageCenter.PrincipalPoint.Y}, {Input1.ImageCenter.PrincipalPoint.X, Input1.ImageCenter.PrincipalPoint.Y});
		TestSingleCurveEvaluationResult({ProducedLensDistortionHandler->GetCurrentDistortionState().FocalLengthInfo.FxFy.X, ProducedLensDistortionHandler->GetCurrentDistortionState().FocalLengthInfo.FxFy.Y}, BlendFactor, {Input0.FocalLength.FxFy.X, Input0.FocalLength.FxFy.Y}, {Input1.FocalLength.FxFy.X, Input1.FocalLength.FxFy.Y});

		
		LensFile->EvaluateImageCenterParameters(Input2.Focus, 0.25f, EvaluatedData.ImageCenter);
		LensFile->EvaluateDistortionParameters(Input2.Focus, 0.25f, EvaluatedData.Distortion);
		LensFile->EvaluateFocalLength(Input2.Focus, 0.25f, EvaluatedData.FocalLength);
		LensFile->EvaluateDistortionData(Input2.Focus, 0.25f, LensFile->LensInfo.SensorDimensions, ProducedLensDistortionHandler);
		TestSingleCurveEvaluationResult({EvaluatedData.ImageCenter.PrincipalPoint.X, EvaluatedData.ImageCenter.PrincipalPoint.Y}, BlendFactor, {Input2.ImageCenter.PrincipalPoint.X, Input2.ImageCenter.PrincipalPoint.Y}, {Input3.ImageCenter.PrincipalPoint.X, Input3.ImageCenter.PrincipalPoint.Y});
		TestSingleCurveEvaluationResult({EvaluatedData.FocalLength.FxFy.X, EvaluatedData.FocalLength.FxFy.Y}, BlendFactor, {Input2.FocalLength.FxFy.X, Input2.FocalLength.FxFy.Y}, {Input3.FocalLength.FxFy.X, Input3.FocalLength.FxFy.Y});
		TestSingleCurveEvaluationResult(EvaluatedData.Distortion.Parameters, BlendFactor, Input2.Distortion.Parameters, Input3.Distortion.Parameters);
		TestSingleCurveEvaluationResult(ProducedLensDistortionHandler->GetCurrentDistortionState().DistortionInfo.Parameters, BlendFactor, Input2.Distortion.Parameters, Input3.Distortion.Parameters);
		TestSingleCurveEvaluationResult({ProducedLensDistortionHandler->GetCurrentDistortionState().ImageCenter.PrincipalPoint.X, ProducedLensDistortionHandler->GetCurrentDistortionState().ImageCenter.PrincipalPoint.Y}, BlendFactor, {Input2.ImageCenter.PrincipalPoint.X, Input2.ImageCenter.PrincipalPoint.Y}, {Input3.ImageCenter.PrincipalPoint.X, Input3.ImageCenter.PrincipalPoint.Y});
		TestSingleCurveEvaluationResult({ProducedLensDistortionHandler->GetCurrentDistortionState().FocalLengthInfo.FxFy.X, ProducedLensDistortionHandler->GetCurrentDistortionState().FocalLengthInfo.FxFy.Y}, BlendFactor, {Input2.FocalLength.FxFy.X, Input2.FocalLength.FxFy.Y}, {Input3.FocalLength.FxFy.X, Input3.FocalLength.FxFy.Y});

		LensFile->EvaluateImageCenterParameters(0.25f, Input0.Zoom, EvaluatedData.ImageCenter);
		LensFile->EvaluateDistortionParameters(0.25f, Input0.Zoom, EvaluatedData.Distortion);
		LensFile->EvaluateFocalLength(0.25f, Input0.Zoom, EvaluatedData.FocalLength);
		LensFile->EvaluateDistortionData(0.25f, Input0.Zoom, LensFile->LensInfo.SensorDimensions, ProducedLensDistortionHandler);
		TestSingleCurveEvaluationResult({EvaluatedData.ImageCenter.PrincipalPoint.X, EvaluatedData.ImageCenter.PrincipalPoint.Y}, BlendFactor, {Input0.ImageCenter.PrincipalPoint.X, Input0.ImageCenter.PrincipalPoint.Y}, {Input2.ImageCenter.PrincipalPoint.X, Input2.ImageCenter.PrincipalPoint.Y});
		TestSingleCurveEvaluationResult({EvaluatedData.FocalLength.FxFy.X, EvaluatedData.FocalLength.FxFy.Y}, BlendFactor, {Input0.FocalLength.FxFy.X, Input0.FocalLength.FxFy.Y}, {Input2.FocalLength.FxFy.X, Input2.FocalLength.FxFy.Y});
		TestSingleCurveEvaluationResult(EvaluatedData.Distortion.Parameters, BlendFactor, Input0.Distortion.Parameters, Input2.Distortion.Parameters);
		TestSingleCurveEvaluationResult(ProducedLensDistortionHandler->GetCurrentDistortionState().DistortionInfo.Parameters, BlendFactor, Input0.Distortion.Parameters, Input2.Distortion.Parameters);
		TestSingleCurveEvaluationResult({ProducedLensDistortionHandler->GetCurrentDistortionState().ImageCenter.PrincipalPoint.X, ProducedLensDistortionHandler->GetCurrentDistortionState().ImageCenter.PrincipalPoint.Y}, BlendFactor, {Input0.ImageCenter.PrincipalPoint.X, Input0.ImageCenter.PrincipalPoint.Y}, {Input2.ImageCenter.PrincipalPoint.X, Input2.ImageCenter.PrincipalPoint.Y});
		TestSingleCurveEvaluationResult({ProducedLensDistortionHandler->GetCurrentDistortionState().FocalLengthInfo.FxFy.X, ProducedLensDistortionHandler->GetCurrentDistortionState().FocalLengthInfo.FxFy.Y}, BlendFactor, {Input0.FocalLength.FxFy.X, Input0.FocalLength.FxFy.Y}, {Input2.FocalLength.FxFy.X, Input2.FocalLength.FxFy.Y});
		
		LensFile->EvaluateImageCenterParameters(0.25f, 0.25f, EvaluatedData.ImageCenter);
		LensFile->EvaluateDistortionParameters(0.25f, 0.25f, EvaluatedData.Distortion);
		LensFile->EvaluateFocalLength(0.25f, 0.25f, EvaluatedData.FocalLength);
		LensFile->EvaluateDistortionData(0.25f, 0.25f, LensFile->LensInfo.SensorDimensions, ProducedLensDistortionHandler);
		TestDualCurveEvaluationResult({ EvaluatedData.ImageCenter.PrincipalPoint.X, EvaluatedData.ImageCenter.PrincipalPoint.Y }, BlendFactor, { Input0.ImageCenter.PrincipalPoint.X, Input0.ImageCenter.PrincipalPoint.Y }, { Input1.ImageCenter.PrincipalPoint.X, Input1.ImageCenter.PrincipalPoint.Y }, { Input2.ImageCenter.PrincipalPoint.X, Input2.ImageCenter.PrincipalPoint.Y }, { Input3.ImageCenter.PrincipalPoint.X, Input3.ImageCenter.PrincipalPoint.Y });
		TestDualCurveEvaluationResult({ EvaluatedData.FocalLength.FxFy.X, EvaluatedData.FocalLength.FxFy.Y }, BlendFactor, { Input0.FocalLength.FxFy.X, Input0.FocalLength.FxFy.Y }, { Input1.FocalLength.FxFy.X, Input1.FocalLength.FxFy.Y }, { Input2.FocalLength.FxFy.X, Input2.FocalLength.FxFy.Y }, { Input3.FocalLength.FxFy.X, Input3.FocalLength.FxFy.Y });
		TestDualCurveEvaluationResult(EvaluatedData.Distortion.Parameters, BlendFactor, Input0.Distortion.Parameters, Input1.Distortion.Parameters, Input2.Distortion.Parameters, Input3.Distortion.Parameters);
		TestDualCurveEvaluationResult(ProducedLensDistortionHandler->GetCurrentDistortionState().DistortionInfo.Parameters, BlendFactor, Input0.Distortion.Parameters, Input1.Distortion.Parameters, Input2.Distortion.Parameters, Input3.Distortion.Parameters);
		TestDualCurveEvaluationResult({ ProducedLensDistortionHandler->GetCurrentDistortionState().ImageCenter.PrincipalPoint.X, ProducedLensDistortionHandler->GetCurrentDistortionState().ImageCenter.PrincipalPoint.Y }, BlendFactor, { Input0.ImageCenter.PrincipalPoint.X, Input0.ImageCenter.PrincipalPoint.Y }, { Input1.ImageCenter.PrincipalPoint.X, Input1.ImageCenter.PrincipalPoint.Y }, { Input2.ImageCenter.PrincipalPoint.X, Input2.ImageCenter.PrincipalPoint.Y }, { Input3.ImageCenter.PrincipalPoint.X, Input3.ImageCenter.PrincipalPoint.Y });
		TestDualCurveEvaluationResult({ ProducedLensDistortionHandler->GetCurrentDistortionState().FocalLengthInfo.FxFy.X, ProducedLensDistortionHandler->GetCurrentDistortionState().FocalLengthInfo.FxFy.Y }, BlendFactor, { Input0.FocalLength.FxFy.X, Input0.FocalLength.FxFy.Y }, { Input1.FocalLength.FxFy.X, Input1.FocalLength.FxFy.Y }, { Input2.FocalLength.FxFy.X, Input2.FocalLength.FxFy.Y }, { Input3.FocalLength.FxFy.X, Input3.FocalLength.FxFy.Y });
	}

	void TestLensFileAddPoints(FAutomationTestBase& Test)
	{
		// Create LensFile container
		const TCHAR* LensFileName = TEXT("AddPointsTestLensFile");
		ULensFile* LensFile = NewObject<ULensFile>(GetTransientPackage(), LensFileName);

		FDistortionInfo TestDistortionParams;
		TestDistortionParams.Parameters = TArray<float>({ 0.0f, 0.0f, 0.0f, 0.0f, 0.0f });

		FFocalLengthInfo TestFocalLength;
		TestFocalLength.FxFy = FVector2D(1.0f, 1.777f);

		TArray<FDistortionFocusPoint>& FocusPoints = LensFile->DistortionTable.GetFocusPoints();

		// Clear the LensFile's distortion table and confirm that the FocusPoints array is empty
		LensFile->ClearAll();
		Test.TestEqual(FString::Printf(TEXT("Num Focus Points")), FocusPoints.Num(), 0);

		// Add a single point at F=0 Z=0 and confirm that FocusPoints array has one curve, and that curve has one ZoomPoint
		LensFile->AddDistortionPoint(0.0f, 0.0f, TestDistortionParams, TestFocalLength);
		Test.TestEqual(FString::Printf(TEXT("Num Focus Points")), FocusPoints.Num(), 1);
		Test.TestEqual(FString::Printf(TEXT("Num Zoom Points")), FocusPoints[0].ZoomPoints.Num(), 1);

		// Add a point at F=0 Z=0.5 and confirm that FocusPoints array has one curve, and that curve has two ZoomPoints
		LensFile->AddDistortionPoint(0.0f, 0.5f, TestDistortionParams, TestFocalLength);
		Test.TestEqual(FString::Printf(TEXT("Num Focus Points")), FocusPoints.Num(), 1);
		Test.TestEqual(FString::Printf(TEXT("Num Zoom Points")), FocusPoints[0].ZoomPoints.Num(), 2);

		// Attempt to add a duplicate point at F=0 Z=0.5 and confirm that FocusPoints array has one curve, and that curve has two ZoomPoints
		LensFile->AddDistortionPoint(0.0f, 0.5f, TestDistortionParams, TestFocalLength);
		Test.TestEqual(FString::Printf(TEXT("Num Focus Points")), FocusPoints.Num(), 1);
		Test.TestEqual(FString::Printf(TEXT("Num Zoom Points")), FocusPoints[0].ZoomPoints.Num(), 2);

		// Test tolerance when adding a new zoom point
		LensFile->AddDistortionPoint(0.0f, 0.49f, TestDistortionParams, TestFocalLength);
		LensFile->AddDistortionPoint(0.0f, 0.4999f, TestDistortionParams, TestFocalLength);
		LensFile->AddDistortionPoint(0.0f, 0.5001f, TestDistortionParams, TestFocalLength);
		LensFile->AddDistortionPoint(0.0f, 0.51f, TestDistortionParams, TestFocalLength);
		Test.TestEqual(FString::Printf(TEXT("Num Focus Points")), FocusPoints.Num(), 1);
		Test.TestEqual(FString::Printf(TEXT("Num Zoom Points")), FocusPoints[0].ZoomPoints.Num(), 4);

		// Add two points at F=1 Z=0 and F=1 Z=0.5 and confirm that FocusPoints array has two curves, and that each curve has two ZoomPoints
		LensFile->AddDistortionPoint(1.0f, 0.0f, TestDistortionParams, TestFocalLength);
		LensFile->AddDistortionPoint(1.0f, 0.5f, TestDistortionParams, TestFocalLength);
		Test.TestEqual(FString::Printf(TEXT("Num Focus Points")), FocusPoints.Num(), 2);
		Test.TestEqual(FString::Printf(TEXT("Num Zoom Points")), FocusPoints[0].ZoomPoints.Num(), 4);
		Test.TestEqual(FString::Printf(TEXT("Num Zoom Points")), FocusPoints[1].ZoomPoints.Num(), 2);

		// Test sorting when adding a new focus point
		LensFile->AddDistortionPoint(0.5f, 0.0f, TestDistortionParams, TestFocalLength);
		Test.TestEqual(FString::Printf(TEXT("Num Focus Points")), FocusPoints.Num(), 3);
		Test.TestEqual(FString::Printf(TEXT("Num Zoom Points")), FocusPoints[0].ZoomPoints.Num(), 4);
		Test.TestEqual(FString::Printf(TEXT("Num Zoom Points")), FocusPoints[1].ZoomPoints.Num(), 1);
		Test.TestEqual(FString::Printf(TEXT("Num Zoom Points")), FocusPoints[2].ZoomPoints.Num(), 2);

		// Test tolerance when adding focus points with slight differences in value
		LensFile->AddDistortionPoint(0.5001f, 0.25f, TestDistortionParams, TestFocalLength);
		LensFile->AddDistortionPoint(0.4999f, 0.5f, TestDistortionParams, TestFocalLength);
		Test.TestEqual(FString::Printf(TEXT("Num Focus Points")), FocusPoints.Num(), 3);
		Test.TestEqual(FString::Printf(TEXT("Num Zoom Points")), FocusPoints[0].ZoomPoints.Num(), 4);
		Test.TestEqual(FString::Printf(TEXT("Num Zoom Points")), FocusPoints[1].ZoomPoints.Num(), 3);
		Test.TestEqual(FString::Printf(TEXT("Num Zoom Points")), FocusPoints[2].ZoomPoints.Num(), 2);

		// Finally, test final state of each focus curve to ensure proper values and sorting
		Test.TestEqual(FString::Printf(TEXT("FocusPoints[0], ZoomPoints[0]")), FocusPoints[0].ZoomPoints[0].Zoom, 0.0f);
		Test.TestEqual(FString::Printf(TEXT("FocusPoints[0], ZoomPoints[1]")), FocusPoints[0].ZoomPoints[1].Zoom, 0.49f);
		Test.TestEqual(FString::Printf(TEXT("FocusPoints[0], ZoomPoints[2]")), FocusPoints[0].ZoomPoints[2].Zoom, 0.5f);
		Test.TestEqual(FString::Printf(TEXT("FocusPoints[0], ZoomPoints[3]")), FocusPoints[0].ZoomPoints[3].Zoom, 0.51f);
		Test.TestEqual(FString::Printf(TEXT("FocusPoints[0], ZoomPoints[0]")), FocusPoints[1].ZoomPoints[0].Zoom, 0.0f);
		Test.TestEqual(FString::Printf(TEXT("FocusPoints[0], ZoomPoints[1]")), FocusPoints[1].ZoomPoints[1].Zoom, 0.25f);
		Test.TestEqual(FString::Printf(TEXT("FocusPoints[0], ZoomPoints[2]")), FocusPoints[1].ZoomPoints[2].Zoom, 0.5f);
		Test.TestEqual(FString::Printf(TEXT("FocusPoints[0], ZoomPoints[0]")), FocusPoints[2].ZoomPoints[0].Zoom, 0.0f);
		Test.TestEqual(FString::Printf(TEXT("FocusPoints[0], ZoomPoints[1]")), FocusPoints[2].ZoomPoints[1].Zoom, 0.5f);
	}
}


bool FTestCameraCalibrationCore::RunTest(const FString& Parameters)
{
	CameraCalibrationTestUtil::TestDistortionParameterCurveBlending(*this);
	CameraCalibrationTestUtil::TestLensFileAddPoints(*this);
	return true;
}
#endif // WITH_DEV_AUTOMATION_TESTS



