// Copyright Epic Games, Inc. All Rights Reserved.

#include "CameraLensDistortionAlgoCheckerboard.h"

#include "AssetEditor/CameraCalibrationStepsController.h"
#include "AssetEditor/LensDistortionTool.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "CameraCalibrationCheckerboard.h"
#include "CameraCalibrationEditorLog.h"
#include "CameraCalibrationUtils.h"
#include "Editor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Input/Events.h"
#include "Internationalization/Text.h"
#include "Layout/Geometry.h"
#include "LensFile.h"
#include "Math/Color.h"
#include "Math/Vector.h"
#include "Misc/MessageDialog.h"
#include "PropertyCustomizationHelpers.h"
#include "SphericalLensDistortionModelHandler.h"
#include "AssetEditor/SSimulcamViewport.h"
#include "UI/CameraCalibrationWidgetHelpers.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/SWidget.h"

#include <vector>

#if WITH_OPENCV
#include "OpenCVHelper.h"
OPENCV_INCLUDES_START
#undef check 
#include "opencv2/opencv.hpp"
#include "opencv2/calib3d.hpp"
OPENCV_INCLUDES_END
#endif

#define LOCTEXT_NAMESPACE "CameraLensDistortionAlgoCheckerboard"


namespace CameraLensDistortionAlgoCheckerboard
{
	class SCalibrationRowGenerator
		: public SMultiColumnTableRow<TSharedPtr<UCameraLensDistortionAlgoCheckerboard::FCalibrationRowData>>
	{
		using FCalibrationRowData = UCameraLensDistortionAlgoCheckerboard::FCalibrationRowData;

	public:
		SLATE_BEGIN_ARGS(SCalibrationRowGenerator) {}

		/** The list item for this row */
		SLATE_ARGUMENT(TSharedPtr<FCalibrationRowData>, CalibrationRowData)

		SLATE_END_ARGS()

		void Construct(const FArguments& Args, const TSharedRef<STableViewBase>& OwnerTableView)
		{
			CalibrationRowData = Args._CalibrationRowData;

			SMultiColumnTableRow<TSharedPtr<FCalibrationRowData>>::Construct(
				FSuperRowType::FArguments()
				.Padding(1.0f),
				OwnerTableView
			);
		}

		//~Begin SMultiColumnTableRow
		virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override
		{
			if (ColumnName == TEXT("Index"))
			{
				return SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.HAlign(EHorizontalAlignment::HAlign_Center)
					.VAlign(EVerticalAlignment::VAlign_Center)
					[SNew(STextBlock).Text(FText::AsNumber(CalibrationRowData->Index))];
			}

			if (ColumnName == TEXT("Image"))
			{
				if (CalibrationRowData->Thumbnail.IsValid())
				{
					check((CalibrationRowData->ImageHeight) > 0 && (CalibrationRowData->ImageWidth > 0));

					const float AspectRatio = float(CalibrationRowData->ImageWidth) / float(CalibrationRowData->ImageHeight);

					return SNew(SBox)
						.MinAspectRatio(AspectRatio)
						.MaxAspectRatio(AspectRatio)
						.MinDesiredHeight(4 * FCameraCalibrationWidgetHelpers::DefaultRowHeight)
						[CalibrationRowData->Thumbnail.ToSharedRef()];
				}
				else
				{
					const FString Text = FString::Printf(TEXT("Image Unavailable"));
					return SNew(STextBlock).Text(FText::FromString(Text));
				}
			}

			return SNullWidget::NullWidget;
		}
		//~End SMultiColumnTableRow


	private:
		TSharedPtr<FCalibrationRowData> CalibrationRowData;
	};

#if WITH_OPENCV
	static bool ReadMediaPixels(cv::Mat& CvFrame, FCameraCalibrationStepsController* StepsController, FText& OutErrorMessage)
	{
		if (!ensure(StepsController))
		{
			OutErrorMessage = LOCTEXT("InvalidStepsController", "Invalid StepsController");
			return false;
		}

		// Get the media plate texture render target 2d
		UTextureRenderTarget2D* MediaPlateRenderTarget = StepsController->GetMediaPlateRenderTarget();

		if (!MediaPlateRenderTarget)
		{
			OutErrorMessage = LOCTEXT("InvalidMediaPlateRenderTarget", "Invalid MediaPlateRenderTarget");
			return false;
		}

		// Extract its render target resource
		FRenderTarget* RenderTarget = MediaPlateRenderTarget->GameThread_GetRenderTargetResource();

		if (!RenderTarget)
		{
			OutErrorMessage = LOCTEXT("InvalidRenderTargetResource", "MediaPlateRenderTarget did not have a RenderTarget resource");
			return false;
		}

		// Verify that we have the correct pixel format
		if (MediaPlateRenderTarget->RenderTargetFormat != ETextureRenderTargetFormat::RTF_RGBA8)
		{
			OutErrorMessage = LOCTEXT("InvalidFormat", "MediaPlateRenderTarget did not have the expected RTF_RGBA8 format");
			return false;
		}

		// Read the pixels onto CPU
		TArray<FColor> Pixels;
		const bool bReadPixels = RenderTarget->ReadPixels(Pixels);

		if (!bReadPixels)
		{
			OutErrorMessage = LOCTEXT("ReadPixelsFailed", "ReadPixels from render target failed");
			return false;
		}

		FIntPoint RTSize = RenderTarget->GetSizeXY();

		check(Pixels.Num() == RTSize.X * RTSize.Y);

		// Create OpenCV Mat with those pixels
		CvFrame = cv::Mat(cv::Size(RTSize.X, RTSize.Y), CV_8UC4);

		// This copy avoids a crash when/if cv::imwrite is called.
		const int32 PixelSize = 4;
		memcpy(CvFrame.data, Pixels.GetData(), PixelSize * Pixels.Num());

		return true;
	}
#endif //WITH_OPENCV
};

void UCameraLensDistortionAlgoCheckerboard::Initialize(ULensDistortionTool* InTool)
{
	Tool = InTool;

	// Guess which calibrator to use.
	SetCalibrator(FindFirstCalibrator());
}

void UCameraLensDistortionAlgoCheckerboard::Shutdown()
{
	Tool.Reset();
	CalibrationRows.Reset();
}

void UCameraLensDistortionAlgoCheckerboard::Tick(float DeltaTime)
{
	if (!ensure(Tool.IsValid()))
	{
		return;
	}

	FCameraCalibrationStepsController* StepsController = Tool->GetCameraCalibrationStepsController();

	if (!ensure(StepsController))
	{
		return;
	}

	// If not paused, cache calibrator 3d point position
	if (!StepsController->IsPaused())
	{
		// Cache camera data
		do
		{
			LastCameraData.bIsValid = false;

			const FLensFileEvalData* LensFileEvalData = StepsController->GetLensFileEvalData();

			// We require lens evaluation data.
			if (!LensFileEvalData)
			{
				break;
			}

			LastCameraData.LensFileEvalData = *LensFileEvalData;
			LastCameraData.bIsValid = true;

		} while (0);
	}
}

bool UCameraLensDistortionAlgoCheckerboard::OnViewportClicked(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	// We only respond to left clicks
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return false;
	}

	// Left Mouse button means to add a new calibration row
	{
		FText ErrorMessage;

		if (!AddCalibrationRow(ErrorMessage))
		{
			FMessageDialog::Open(EAppMsgType::Ok, ErrorMessage);
		}

		// force play (whether we succeeded or not)
		if (FCameraCalibrationStepsController* StepsController = GetStepsController())
		{
			StepsController->Play();
		}
	}

	return true;
}

FCameraCalibrationStepsController* UCameraLensDistortionAlgoCheckerboard::GetStepsController() const
{
	if (!ensure(Tool.IsValid()))
	{
		return nullptr;
	}

	return Tool->GetCameraCalibrationStepsController();
}

bool UCameraLensDistortionAlgoCheckerboard::AddCalibrationRow(FText& OutErrorMessage)
{
#if WITH_OPENCV
	using namespace CameraLensDistortionAlgoCheckerboard;

	if (!Tool.IsValid())
	{
		OutErrorMessage = LOCTEXT("InvalidTool", "Invalid Tool");
		return false;
	}

	FCameraCalibrationStepsController* StepsController = Tool->GetCameraCalibrationStepsController();

	if (!StepsController)
	{
		OutErrorMessage = LOCTEXT("InvalidStepsController", "Invalid StepsController");
		return false;
	}

	if (!LastCameraData.bIsValid)
	{
		OutErrorMessage = LOCTEXT("InvalidLastCameraData", "Invalid LastCameraData");
		return false;
	}

	if (!Calibrator.IsValid())
	{
		OutErrorMessage = LOCTEXT("InvalidCalibrator", "Invalid Calibrator");
		return false;
	}

	// Read pixels
	cv::Mat CvFrame;

	if (!ReadMediaPixels(CvFrame, StepsController, OutErrorMessage))
	{
		return false;
	}

	cv::Mat CvGray;
	cv::cvtColor(CvFrame, CvGray, cv::COLOR_RGBA2GRAY);

	// Create the row that we're going to add
	TSharedPtr<FCalibrationRowData> Row = MakeShared<FCalibrationRowData>();

	Row->CameraData = LastCameraData;

	Row->Index = CalibrationRows.Num();
	Row->ImageHeight = CvGray.rows;
	Row->ImageWidth = CvGray.cols;

	Row->NumCornerRows = Calibrator->NumCornerRows;
	Row->NumCornerCols = Calibrator->NumCornerCols;
	Row->SquareSideInCm = Calibrator->SquareSideLength;

	// Fill out checkerboard 3d points
	for (int32 RowIdx = 0; RowIdx < Row->NumCornerRows; ++RowIdx)
	{
		for (int32 ColIdx = 0; ColIdx < Row->NumCornerCols; ++ColIdx)
		{
			Row->Points3d.Add(Row->SquareSideInCm * FVector(ColIdx, RowIdx, 0));
		}
	}

	// Identify checkerboard
	{
		cv::Size CheckerboardSize(Row->NumCornerCols, Row->NumCornerRows);

		std::vector<cv::Point2f> Corners;
		Corners.reserve(CheckerboardSize.height * CheckerboardSize.width); // reserve avoids a crash when this block goes out of scope

		const bool bCornersFound = cv::findChessboardCorners(
			CvGray, 
			CheckerboardSize,
			Corners,
			CV_CALIB_CB_ADAPTIVE_THRESH | CV_CALIB_CB_NORMALIZE_IMAGE
		);
		
		if (!bCornersFound)
		{
			OutErrorMessage = LOCTEXT("InvalidCheckerboard", "Could not identify the expected checkerboard points of interest");
			return false;
		}
		
		// CV_TERMCRIT_EPS will stop the search when the error is under the given epsilon.
		// CV_TERMCRIT_ITER will stop after the specified number of iterations regardless of epsilon.
		cv::TermCriteria Criteria(CV_TERMCRIT_EPS | CV_TERMCRIT_ITER, 30, 0.001);
		cv::cornerSubPix(CvGray, Corners, cv::Size(11, 11), cv::Size(-1, -1), Criteria);

		for (cv::Point2f& Corner : Corners)
		{
			Row->Points2d.Add(FVector2D(Corner.x, Corner.y));
		}

		cv::drawChessboardCorners(CvFrame, CheckerboardSize, Corners, bCornersFound);
	}

	// Create thumbnail
	do
	{
		// Resize the frame
		cv::Mat CvThumbnail;
		cv::resize(CvFrame, CvThumbnail, cv::Size(CvFrame.cols / 4, CvFrame.rows / 4));

		UTexture2D* Texture = UTexture2D::CreateTransient(CvThumbnail.cols, CvThumbnail.rows, PF_R8G8B8A8);

		if (!Texture)
		{
			break;
		}

#if WITH_EDITORONLY_DATA
		Texture->MipGenSettings = TMGS_NoMipmaps;
#endif
		Texture->NeverStream = true;
		Texture->SRGB = 0;

		FTexture2DMipMap& Mip0 = Texture->PlatformData->Mips[0];
		void* TextureData = Mip0.BulkData.Lock(LOCK_READ_WRITE);

		const int32 PixelStride = 4;
		FMemory::Memcpy(TextureData, CvThumbnail.data, CvThumbnail.cols * CvThumbnail.rows * PixelStride);

		Mip0.BulkData.Unlock();
		Texture->UpdateResource();

		Row->Thumbnail = SNew(SSimulcamViewport, Texture);
	} while (0);

	// Validate the new row, show a message if validation fails.
	{
		FText ErrorMessage;

		if (!ValidateNewRow(Row, ErrorMessage))
		{
			return false;
		}
	}

	// Add this data point
	CalibrationRows.Add(Row);

	// Notify the ListView of the new data
	if (CalibrationListView.IsValid())
	{
		CalibrationListView->RequestListRefresh();
		CalibrationListView->RequestNavigateToItem(Row);
	}

	return true;
#endif //WITH_OPENCV

	OutErrorMessage = LOCTEXT("OpenCVRequired", "OpenCV is required");
	return false;
}

TSharedRef<SWidget> UCameraLensDistortionAlgoCheckerboard::BuildUI()
{
	return SNew(SVerticalBox)

		+ SVerticalBox::Slot() // Calibrator picker
		.VAlign(EVerticalAlignment::VAlign_Top)
		.AutoHeight()
		.MaxHeight(FCameraCalibrationWidgetHelpers::DefaultRowHeight)
		[ FCameraCalibrationWidgetHelpers::BuildLabelWidgetPair(LOCTEXT("Checkerboard", "Checkerboard"), BuildCalibrationDevicePickerWidget()) ]
				
		+ SVerticalBox::Slot() // Calibration Rows
		.AutoHeight()
		.MaxHeight(12 * FCameraCalibrationWidgetHelpers::DefaultRowHeight)
		[ BuildCalibrationPointsTable() ]
		
		+ SVerticalBox::Slot() // Action buttons (e.g. Remove, Clear)
		.HAlign(EHorizontalAlignment::HAlign_Center)
		.AutoHeight()
		.Padding(0,20)
		[ BuildCalibrationActionButtons() ];
}

bool UCameraLensDistortionAlgoCheckerboard::ValidateNewRow(TSharedPtr<FCalibrationRowData>& Row, FText& OutErrorMessage) const
{
	if (!Row.IsValid())
	{
		OutErrorMessage = LOCTEXT("InvalidRowPointer", "Invalid row pointer");
		return false;
	}

	if (!Tool.IsValid())
	{
		return false;
	}

	// Camera data is valid
	if (!Row->CameraData.bIsValid)
	{
		OutErrorMessage = LOCTEXT("InvalidCameraData", "Invalid CameraData");
		return false;
	}

	// FZ inputs are valid
	if ((!Row->CameraData.LensFileEvalData.Input.Focus.IsSet()) || (!Row->CameraData.LensFileEvalData.Input.Zoom.IsSet()))
	{
		OutErrorMessage = LOCTEXT("LutInputsNotValid", "FZ Lut inputs are not valid. Make sure you are providing Focus and Zoom values via LiveLink");
		return false;
	}

	// Valid image dimensions
	if ((Row->ImageHeight < 1) || (Row->ImageWidth < 1))
	{
		OutErrorMessage = LOCTEXT("InvalidImageDimensions", "Invalid image dimensions");
		return false;
	}

	// Valid image pattern size
	if (Row->SquareSideInCm < 1)
	{
		OutErrorMessage = LOCTEXT("InvalidPatternSize", "Invalid pattern size");
		return false;
	}

	// valid number of rows and columns
	if ((Row->NumCornerCols < 3) || (Row->NumCornerRows < 3))
	{
		OutErrorMessage = LOCTEXT("InvalidPatternRowsandCols", "Invalid number of rows/columns in the pattern");
		return false;
	}

	// If we have no existing rows to compare this one with, we're good to go
	if (!CalibrationRows.Num())
	{
		return true;
	}

	FCameraCalibrationStepsController* StepsController = Tool->GetCameraCalibrationStepsController();

	if (!StepsController)
	{
		return false;
	}

	const ULensFile* LensFile = StepsController->GetLensFile();

	if (!LensFile)
	{
		OutErrorMessage = LOCTEXT("LensFileNotFound", "LensFile not found");
		return false;
	}

	const TSharedPtr<FCalibrationRowData>& FirstRow = CalibrationRows[0];

	// NumRows didn't change
	if (Row->NumCornerRows != FirstRow->NumCornerRows)
	{
		OutErrorMessage = LOCTEXT("NumRowsChanged", "Number of rows changed");
		return false;
	}

	// NumCols didn't change
	if (Row->NumCornerCols != FirstRow->NumCornerCols)
	{
		OutErrorMessage = LOCTEXT("NumColsChanged", "Number of columns changed");
		return false;
	}

	// Square side length didn't change
	if (!FMath::IsNearlyEqual(Row->SquareSideInCm, FirstRow->SquareSideInCm))
	{
		OutErrorMessage = LOCTEXT("PatternSizeChanged", "Physical size of the pattern changed");
		return false;
	}

	// Image dimensions did not change
	if ((Row->ImageWidth != FirstRow->ImageWidth) || (Row->ImageHeight != FirstRow->ImageHeight))
	{
		OutErrorMessage = LOCTEXT("ImageDimensionsChanged", "The dimensions of the media plate changed");
		return false;
	}

	//@todo Focus and zoom did not change much (i.e. inputs to distortion and nodal offset). 
	//      Threshold for physical units should differ from normalized encoders.

	return true;
}

bool UCameraLensDistortionAlgoCheckerboard::GetLensDistortion(
	float& OutFocus,
	float& OutZoom,
	FDistortionInfo& OutDistortionInfo,
	FFocalLengthInfo& OutFocalLengthInfo,
	FImageCenterInfo& OutImageCenterInfo,
	TSubclassOf<ULensModel>& OutLensModel,
	double& OutError,
	FText& OutErrorMessage)
{
	// Sanity checks
	//

	// Enough points
	if (CalibrationRows.Num() < 4)
	{
		OutErrorMessage = LOCTEXT("NotEnoughSamples", "At least 4 calibration rows are required");
		return false;
	}

	// All points are valid
	for (const TSharedPtr<FCalibrationRowData>& Row : CalibrationRows)
	{
		if (!ensure(Row.IsValid()))
		{
			return false;
		}
	}

	if (!ensure(Tool.IsValid()))
	{
		return false;
	}

	FCameraCalibrationStepsController* StepsController = Tool->GetCameraCalibrationStepsController();

	if (!ensure(StepsController))
	{
		return false;
	}

	const ULensFile* LensFile = StepsController->GetLensFile();

	if (!ensure(LensFile))
	{
		OutErrorMessage = LOCTEXT("LensFileNotFound", "LensFile not found");
		return false;
	}

	const TSharedPtr<FCalibrationRowData>& LastRow = CalibrationRows.Last();

	// Only parameters data mode supported at the moment
	if (LensFile->DataMode != ELensDataMode::Parameters)
	{
		OutErrorMessage = LOCTEXT("OnlyParametersDataModeSupported", "Only Parameters Data Mode supported");
		return false;
	}

	// Only spherical lens distortion is currently supported at the moment.

	const USphericalLensDistortionModelHandler* SphericalHandler = Cast<USphericalLensDistortionModelHandler>(StepsController->GetDistortionHandler());

	if (!SphericalHandler)
	{
		OutErrorMessage = LOCTEXT("OnlySphericalDistortionSupported", "Only spherical distortion is currently supported. Please update the distortion model used by the camera.");
		return false;
	}

#if WITH_OPENCV

	cv::Mat CameraMatrix = cv::Mat::eye(3, 3, CV_64F);;
	cv::Mat DistortionCoefficients;

	std::vector<cv::Mat> Rvecs;
	std::vector<cv::Mat> Tvecs;

	Rvecs.reserve(CalibrationRows.Num());
	Tvecs.reserve(CalibrationRows.Num());

	std::vector<std::vector<cv::Point2f>> Samples2d;
	std::vector<std::vector<cv::Point3f>> Samples3d;

	for (const TSharedPtr<FCalibrationRowData>& Row : CalibrationRows)
	{
		// add 2d (image) points
		{
			std::vector<cv::Point2f> Points2d;

			for (FVector2D& Point2d : Row->Points2d)
			{
				Points2d.push_back(cv::Point2f(Point2d.X, Point2d.Y));
			}

			Samples2d.push_back(Points2d);
		}

		// add 3d points
		{
			std::vector<cv::Point3f> Points3d;

			for (FVector& Point3d : Row->Points3d)
			{
				Points3d.push_back(cv::Point3f(Point3d.X, Point3d.Y, Point3d.Z));
			}

			Samples3d.push_back(Points3d);
		}
	}

	OutError = cv::calibrateCamera(
		Samples3d,
		Samples2d,
		cv::Size(LastRow->ImageWidth, LastRow->ImageHeight), 
		CameraMatrix, 
		DistortionCoefficients, 
		Rvecs, 
		Tvecs
	);

	check(DistortionCoefficients.total() == 5);
	check((CameraMatrix.rows == 3) && (CameraMatrix.cols == 3));

	// Focus and Zoom validity were verified when adding the calibration rows.
	checkSlow(LastRow->CameraData.LensFileEvalData.Input.Focus.IsSet());
	checkSlow(LastRow->CameraData.LensFileEvalData.Input.Zoom.IsSet());

	// Valid image sizes were verified when adding the calibration rows.
	checkSlow(LastRow->ImageWidth > 0);
	checkSlow(LastRow->ImageHeight > 0);

	// FZ inputs to LUT
	OutFocus = *LastRow->CameraData.LensFileEvalData.Input.Focus;
	OutZoom = *LastRow->CameraData.LensFileEvalData.Input.Zoom;

	// FocalLengthInfo
	OutFocalLengthInfo.FxFy = FVector2D(
		float(CameraMatrix.at<double>(0, 0) / LastRow->ImageWidth),
		float(CameraMatrix.at<double>(1, 1) / LastRow->ImageHeight)
	);

	// DistortionInfo
	{
		FSphericalDistortionParameters SphericalParameters;

		SphericalParameters.K1 = DistortionCoefficients.at<double>(0);
		SphericalParameters.K2 = DistortionCoefficients.at<double>(1);
		SphericalParameters.P1 = DistortionCoefficients.at<double>(2);
		SphericalParameters.P2 = DistortionCoefficients.at<double>(3);
		SphericalParameters.K3 = DistortionCoefficients.at<double>(4);

		USphericalLensModel::StaticClass()->GetDefaultObject<ULensModel>()->ToArray(
			SphericalParameters, 
			OutDistortionInfo.Parameters
		);
	}

	// ImageCenterInfo
	OutImageCenterInfo.PrincipalPoint = FVector2D(
		float(CameraMatrix.at<double>(0, 2) / LastRow->ImageWidth),
		float(CameraMatrix.at<double>(1, 2) / LastRow->ImageHeight)
	);

	// Lens Model
	OutLensModel = USphericalLensModel::StaticClass();

	return true;
#else
	{
		OutErrorMessage = LOCTEXT("OpenCVRequired", "OpenCV is required");
		return false;
	}
#endif //WITH_OPENCV
}

TSharedRef<SWidget> UCameraLensDistortionAlgoCheckerboard::BuildCalibrationDevicePickerWidget()
{
	return SNew(SObjectPropertyEntryBox)
		.AllowedClass(ACameraCalibrationCheckerboard::StaticClass())
		.OnObjectChanged_Lambda([&](const FAssetData& AssetData) -> void
		{
			if (AssetData.IsValid())
			{
				SetCalibrator(Cast<ACameraCalibrationCheckerboard>(AssetData.GetAsset()));
			}
		})
		.ObjectPath_Lambda([&]() -> FString
		{
			if (AActor* TheCalibrator = GetCalibrator())
			{
				FAssetData AssetData(TheCalibrator, true);
				return AssetData.ObjectPath.ToString();
			}

			return TEXT("");
		});
}

TSharedRef<SWidget> UCameraLensDistortionAlgoCheckerboard::BuildCalibrationActionButtons()
{
	return SNew(SHorizontalBox)

		+ SHorizontalBox::Slot() // Button to remove last row
		.AutoWidth()
		[
			SNew(SButton)
			.Text(LOCTEXT("RemoveLast", "Remove Last"))
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.OnClicked_Lambda([&]() -> FReply
			{
				if (CalibrationRows.Num())
				{
					CalibrationRows.RemoveAt(CalibrationRows.Num() - 1);
					if (CalibrationListView.IsValid())
					{
						CalibrationListView->RequestListRefresh();
					}
				}

				return FReply::Handled();
			})
		]

		+ SHorizontalBox::Slot() // Button to clear all rows
		.AutoWidth()
		[ 
			SNew(SButton)
			.Text(LOCTEXT("ClearAll", "Clear All"))
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.OnClicked_Lambda([&]() -> FReply
			{
				ClearCalibrationRows();
				return FReply::Handled();
			})
		]
		;
}

TSharedRef<SWidget> UCameraLensDistortionAlgoCheckerboard::BuildCalibrationPointsTable()
{
	CalibrationListView = SNew(SListView<TSharedPtr<FCalibrationRowData>>)
		.ItemHeight(24)
		.ListItemsSource(&CalibrationRows)
		.OnGenerateRow_Lambda([&](TSharedPtr<FCalibrationRowData> InItem, const TSharedRef<STableViewBase>& OwnerTable) -> TSharedRef<ITableRow>
		{
			return SNew(CameraLensDistortionAlgoCheckerboard::SCalibrationRowGenerator, OwnerTable)
				.CalibrationRowData(InItem);
		})
		.SelectionMode(ESelectionMode::SingleToggle)
		.HeaderRow
		(
			SNew(SHeaderRow)

			+ SHeaderRow::Column("Index")
			.DefaultLabel(LOCTEXT("Index", "Index"))

			+ SHeaderRow::Column("Image")
			.DefaultLabel(LOCTEXT("Image", "Image"))
		);

	return CalibrationListView.ToSharedRef();
}

ACameraCalibrationCheckerboard* UCameraLensDistortionAlgoCheckerboard::FindFirstCalibrator() const
{
	if (!ensure(Tool.IsValid()))
	{
		return nullptr;
	}

	const FCameraCalibrationStepsController* StepsController = Tool->GetCameraCalibrationStepsController();

	if (!ensure(StepsController))
	{
		return nullptr;
	}

	UWorld* World = StepsController->GetWorld();

	if (!ensure(World))
	{
		return nullptr;
	}

	const EActorIteratorFlags Flags = EActorIteratorFlags::SkipPendingKill;

	for (TActorIterator<AActor> It(World, ACameraCalibrationCheckerboard::StaticClass(), Flags); It; ++It)
	{
		return CastChecked<ACameraCalibrationCheckerboard>(*It);
	}

	return nullptr;
}

void UCameraLensDistortionAlgoCheckerboard::SetCalibrator(ACameraCalibrationCheckerboard* InCalibrator)
{
	Calibrator = InCalibrator;
}

ACameraCalibrationCheckerboard* UCameraLensDistortionAlgoCheckerboard::GetCalibrator() const
{
	return Calibrator.Get();
}

void UCameraLensDistortionAlgoCheckerboard::OnDistortionSavedToLens()
{
	// Since the calibration result was saved, there is no further use for the current samples.
	ClearCalibrationRows();
}

void UCameraLensDistortionAlgoCheckerboard::ClearCalibrationRows()
{
	CalibrationRows.Empty();

	if (CalibrationListView.IsValid())
	{
		CalibrationListView->RequestListRefresh();
	}
}

#undef LOCTEXT_NAMESPACE
