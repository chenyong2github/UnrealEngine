// Copyright Epic Games, Inc. All Rights Reserved.

#include "CameraNodalOffsetAlgoCheckerboard.h"
#include "AssetEditor/CameraCalibrationStepsController.h"
#include "AssetEditor/NodalOffsetTool.h"
#include "CameraCalibrationCheckerboard.h"
#include "Engine/TextureRenderTarget2D.h"
#include "EngineUtils.h"
#include "Misc/MessageDialog.h"
#include "UI/CameraCalibrationWidgetHelpers.h"
#include "UI/SFilterableActorPicker.h"


#if WITH_OPENCV

#include <vector>
#include <algorithm>

#include "OpenCVHelper.h"
OPENCV_INCLUDES_START
#undef check 
#include "opencv2/opencv.hpp"
#include "opencv2/calib3d.hpp"
OPENCV_INCLUDES_END

#endif //WITH_OPENCV

#define LOCTEXT_NAMESPACE "CameraNodalOffsetAlgoCheckerboard"


bool UCameraNodalOffsetAlgoCheckerboard::OnViewportClicked(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	// We only respond to left clicks
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return false;
	}

	{
		const FText TitleError = LOCTEXT("CalibrationError", "CalibrationError");
		FText ErrorMessage;

		if (!PopulatePoints(ErrorMessage))
		{
			FMessageDialog::Open(EAppMsgType::Ok, ErrorMessage, &TitleError);
		}
	}

	return true;
}

TSharedRef<SWidget> UCameraNodalOffsetAlgoCheckerboard::BuildUI()
{
	return SNew(SVerticalBox)

		+ SVerticalBox::Slot() // Checkerboard picker
		.VAlign(EVerticalAlignment::VAlign_Top)
		.AutoHeight()
		.MaxHeight(FCameraCalibrationWidgetHelpers::DefaultRowHeight)
		[FCameraCalibrationWidgetHelpers::BuildLabelWidgetPair(LOCTEXT("Checkerboard", "Checkerboard"), BuildCheckerboardPickerWidget())]

		+ SVerticalBox::Slot() // Calibrator component picker
		.VAlign(EVerticalAlignment::VAlign_Top)
		.AutoHeight()
		.MaxHeight(FCameraCalibrationWidgetHelpers::DefaultRowHeight)
		[FCameraCalibrationWidgetHelpers::BuildLabelWidgetPair(LOCTEXT("CalibratorComponents", "Calibrator Component(s)"), BuildCalibrationComponentPickerWidget())]

		+ SVerticalBox::Slot() // Calibration Rows
		.AutoHeight()
		.MaxHeight(12 * FCameraCalibrationWidgetHelpers::DefaultRowHeight)
		[BuildCalibrationPointsTable()]

		+ SVerticalBox::Slot() // Action buttons (e.g. Remove, Clear)
		.HAlign(EHorizontalAlignment::HAlign_Center)
		.AutoHeight()
		.Padding(0, 20)
		[BuildCalibrationActionButtons()]
		;
}

TSharedRef<SWidget> UCameraNodalOffsetAlgoCheckerboard::BuildCheckerboardPickerWidget()
{
	return SNew(SFilterableActorPicker)
		.OnSetObject_Lambda([&](const FAssetData& AssetData) -> void
		{
			if (AssetData.IsValid())
			{
				SetCalibrator(Cast<ACameraCalibrationCheckerboard>(AssetData.GetAsset()));
			}
		})
		.OnShouldFilterAsset_Lambda([&](const FAssetData& AssetData) -> bool
		{
			return !!Cast<ACameraCalibrationCheckerboard>(AssetData.GetAsset());
		})
		.ActorAssetData_Lambda([&]() -> FAssetData
		{
			return FAssetData(GetCalibrator(), true);
		});
}


bool UCameraNodalOffsetAlgoCheckerboard::PopulatePoints(FText& OutErrorMessage)
{
#if !WITH_OPENCV
	OutErrorMessage = LOCTEXT("OpenCVRequired", "OpenCV required");
	return false;
#else

	const FCameraCalibrationStepsController* StepsController;

	if (!ensure(GetStepsControllerAndLensFile(&StepsController, nullptr)))
	{
		OutErrorMessage = LOCTEXT("ToolNotFound", "Tool not found");
		return false;
	}

	if (!Calibrator.IsValid())
	{
		OutErrorMessage = LOCTEXT("InvalidCalibrator", "Please select a calibrator with Aruco markers in the given combo box.");
		return false;
	}

	TArray<FColor> Pixels;
	FIntPoint Size;
	ETextureRenderTargetFormat PixelFormat;

	if (!StepsController->ReadMediaPixels(Pixels, Size, PixelFormat, OutErrorMessage))
	{
		return false;
	}

	if (PixelFormat != ETextureRenderTargetFormat::RTF_RGBA8)
	{
		OutErrorMessage = LOCTEXT("InvalidFormat", "MediaPlateRenderTarget did not have the expected RTF_RGBA8 format");
		return false;
	}

	// Create OpenCV Mat with those pixels
	cv::Mat CvFrame(cv::Size(Size.X, Size.Y), CV_8UC4, Pixels.GetData());

	// Convert to Gray
	cv::Mat CvGray;
	cv::cvtColor(CvFrame, CvGray, cv::COLOR_RGBA2GRAY);

	// Populate the 3d/2d correlation points

	const ACameraCalibrationCheckerboard* Checkerboard = Cast<ACameraCalibrationCheckerboard>(Calibrator.Get());
	check(Checkerboard); // This should be ensured by the picker.

	std::vector<cv::Point2f> Points2d;

	// Identify checkerboard
	{
		cv::Size CheckerboardSize(Checkerboard->NumCornerCols, Checkerboard->NumCornerRows);

		std::vector<cv::Point2f> Corners;

		const bool bCornersFound = cv::findChessboardCorners(
			CvGray,
			CheckerboardSize,
			Corners,
			CV_CALIB_CB_ADAPTIVE_THRESH | CV_CALIB_CB_NORMALIZE_IMAGE
		);

		if (!bCornersFound)
		{
			OutErrorMessage = FText::FromString(FString::Printf(TEXT(
				"Could not identify the expected checkerboard points of interest. "
				"The expected checkerboard has %dx%d inner corners."),
				Checkerboard->NumCornerCols, Checkerboard->NumCornerRows)
			);

			return false;
		}

		// CV_TERMCRIT_EPS will stop the search when the error is under the given epsilon.
		// CV_TERMCRIT_ITER will stop after the specified number of iterations regardless of epsilon.
		cv::TermCriteria Criteria(CV_TERMCRIT_EPS | CV_TERMCRIT_ITER, 30, 0.001);
		cv::cornerSubPix(CvGray, Corners, cv::Size(11, 11), cv::Size(-1, -1), Criteria);

		Points2d.reserve(Corners.size());

		for (cv::Point2f& Corner : Corners)
		{
			Points2d.push_back(cv::Point2f(Corner.x, Corner.y));
		}
	}

	check(Points2d.size() == Checkerboard->NumCornerRows * Checkerboard->NumCornerCols);

	// Force first one to be top-left
	if (Points2d.front().y > Points2d.back().y) 
	{
		std::reverse(Points2d.begin(), Points2d.end());
	}

	// Create and populate the new calibration rows that we're going to add
	for (int32 RowIdx = 0; RowIdx < Checkerboard->NumCornerRows; ++RowIdx)
	{
		for (int32 ColIdx = 0; ColIdx < Checkerboard->NumCornerCols; ++ColIdx)
		{
			const int32 PointIdx = RowIdx * Checkerboard->NumCornerCols + ColIdx;

			TSharedPtr<FCalibrationRowData> Row = MakeShared<FCalibrationRowData>();

			Row->Point2D.X = float(Points2d[PointIdx].x) / Size.X;
			Row->Point2D.Y = float(Points2d[PointIdx].y) / Size.Y;

			FTransform LocalPoint3d;
			LocalPoint3d.SetLocation(Checkerboard->SquareSideLength * FVector(0, ColIdx, Checkerboard->NumCornerRows - RowIdx - 1));

			Row->CalibratorPointData.Location = (LocalPoint3d * Calibrator->GetTransform()).GetLocation();
			Row->CalibratorPointData.Name = FString::Printf(TEXT("Corner[%d][%d]"), RowIdx, ColIdx);
			Row->CalibratorPointData.bIsValid = true;

			Row->CameraData = LastCameraData;

			if (!ValidateNewRow(Row, OutErrorMessage))
			{
				// Notify the ListView of the new data
				if (CalibrationListView.IsValid())
				{
					CalibrationListView->RequestListRefresh();
				}

				return false;
			}

			CalibrationRows.Add(Row);
		}
	}

	// Notify the ListView of the new data
	if (CalibrationListView.IsValid())
	{
		CalibrationListView->RequestListRefresh();
	}

	return true;

#endif //WITH_OPENCV
}

AActor* UCameraNodalOffsetAlgoCheckerboard::FindFirstCalibrator() const
{
	const FCameraCalibrationStepsController* StepsController;

	if (!ensure(GetStepsControllerAndLensFile(&StepsController, nullptr)))
	{
		return nullptr;
	}

	UWorld* World = StepsController->GetWorld();

	const EActorIteratorFlags Flags = EActorIteratorFlags::SkipPendingKill;

	for (TActorIterator<AActor> It(World, ACameraCalibrationCheckerboard::StaticClass(), Flags); It; ++It)
	{
		return CastChecked<ACameraCalibrationCheckerboard>(*It);
	}

	return nullptr;
}

TSharedRef<SWidget> UCameraNodalOffsetAlgoCheckerboard::BuildHelpWidget()
{
	return SNew(STextBlock)
		.Text(LOCTEXT("CameraNodalOffsetAlgoCheckerboardHelp",
			"This nodal offset algorithm is equivalent to the 'Nodal Offset Points Method', except that\n"
			"instead of explicitly clicking on the 2d calibrator point locations, it will automatically\n"
			"detect the chessboard corners when the viewport is clicked and add them to the calibration\n"
			"points table.\n\n"

			"Notes:\n\n"

			"- Due the symmetrical nature of chessboards, it is required that the chessboard appears\n"
			"  sufficiently upright in the media plate, in order to avoid orientation ambiguities.\n\n"
			
			"- In addition, due to the planar nature of the chessboard, the yielded nodal offset may not\n"
			"  be as accurate as when using other calibrator devices.\n"
		));
}

#undef LOCTEXT_NAMESPACE
