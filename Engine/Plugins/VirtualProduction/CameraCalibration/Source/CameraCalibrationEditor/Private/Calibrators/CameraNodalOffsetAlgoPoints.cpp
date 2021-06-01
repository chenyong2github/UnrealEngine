// Copyright Epic Games, Inc. All Rights Reserved.

#include "CameraNodalOffsetAlgoPoints.h"
#include "AssetEditor/CameraCalibrationStepsController.h"
#include "AssetEditor/NodalOffsetTool.h"
#include "CalibrationPointComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "CameraCalibrationUtils.h"
#include "CineCameraComponent.h"
#include "Editor.h"
#include "GameFramework/Actor.h"
#include "Input/Events.h"
#include "Internationalization/Text.h"
#include "Layout/Geometry.h"
#include "LensFile.h"
#include "Math/Vector.h"
#include "Misc/MessageDialog.h"
#include "PropertyCustomizationHelpers.h"
#include "SphericalLensDistortionModelHandler.h"
#include "UI/CameraCalibrationWidgetHelpers.h"
#include "Widgets/Input/SButton.h"
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

#define LOCTEXT_NAMESPACE "CameraNodalOffsetAlgoPoints"

namespace CameraNodalOffsetAlgoPoints
{
	class SCalibrationRowGenerator
		: public SMultiColumnTableRow<TSharedPtr<UCameraNodalOffsetAlgoPoints::FCalibrationRowData>>
	{
		using FCalibrationRowData = UCameraNodalOffsetAlgoPoints::FCalibrationRowData;

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
			if (ColumnName == TEXT("Name"))
			{
				const FString Text = CalibrationRowData->CalibratorPointData.Name;
				return SNew(STextBlock).Text(FText::FromString(Text));
			}

			if (ColumnName == TEXT("Point2D"))
			{
				const FString Text = FString::Printf(TEXT("(%.2f, %.2f)"),
					CalibrationRowData->Point2D.X,
					CalibrationRowData->Point2D.Y);

				return SNew(STextBlock).Text(FText::FromString(Text));
			}

			if (ColumnName == TEXT("Point3D"))
			{
				const FString Text = FString::Printf(TEXT("(%.0f, %.0f, %.0f)"),
					CalibrationRowData->CalibratorPointData.Location.X,
					CalibrationRowData->CalibratorPointData.Location.Y,
					CalibrationRowData->CalibratorPointData.Location.Z);

				return SNew(STextBlock).Text(FText::FromString(Text));
			}

			return SNullWidget::NullWidget;
		}
		//~End SMultiColumnTableRow


	private:
		TSharedPtr<FCalibrationRowData> CalibrationRowData;
	};
};

void UCameraNodalOffsetAlgoPoints::Initialize(UNodalOffsetTool* InNodalOffsetTool)
{
	NodalOffsetTool = InNodalOffsetTool;

	// Guess which calibrator to use by searching for actors with CalibrationPointComponents.
	SetCalibrator(FindFirstCalibrator());
}

void UCameraNodalOffsetAlgoPoints::Shutdown()
{
	NodalOffsetTool.Reset();
}

void UCameraNodalOffsetAlgoPoints::Tick(float DeltaTime)
{
	if (!NodalOffsetTool.IsValid())
	{
		return;
	}

	FCameraCalibrationStepsController* StepsController = NodalOffsetTool->GetCameraCalibrationStepsController();

	if (!StepsController)
	{
		return;
	}

	// If not paused, cache calibrator 3d point position
	if (!StepsController->IsPaused())
	{
		// Get calibration point data
		do 
		{	
			LastCalibratorPoints.Empty();

			for (const TSharedPtr<FCalibratorPointData>& CalibratorPoint : CurrentCalibratorPoints)
			{
				if (!CalibratorPoint.IsValid())
				{
					continue;
				}

				const UCalibrationPointComponent* CalibrationPointComponent = GetCalibrationPointComponentFromName(CalibratorPoint->Name);

				if (!CalibrationPointComponent)
				{
					continue;
				}

				FCalibratorPointCache PointCache;
				PointCache.Name = CalibratorPoint->Name;
				PointCache.Location = CalibrationPointComponent->GetComponentLocation();
				PointCache.bIsValid = true;

				LastCalibratorPoints.Emplace(MoveTemp(PointCache));
			}
		} while (0);

		// Get camera data
		do
		{
			LastCameraData.bIsValid = false;

			const FLensFileEvalData* LensFileEvalData = StepsController->GetLensFileEvalData();

			// We require lens evaluation data, and that distortion was evaluated so that 2d correlations are valid
			// Note: The comp enforces distortion application.
			if (!LensFileEvalData || !LensFileEvalData->Distortion.bWasEvaluated)
			{
				break;
			}

			const ACameraActor* Camera = StepsController->GetCamera();

			if (!Camera)
			{
				break;
			}

			const UCameraComponent* CameraComponent = Camera->GetCameraComponent();

			if (!CameraComponent)
			{
				break;
			}

			LastCameraData.Pose = CameraComponent->GetComponentToWorld();
			LastCameraData.UniqueId = Camera->GetUniqueID();
			LastCameraData.LensFileEvalData = *LensFileEvalData;
			LastCameraData.bIsValid = true;

		} while (0);
	}
}

bool UCameraNodalOffsetAlgoPoints::OnViewportClicked(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	// We only respond to left clicks
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return false;
	}

	if (!ensure(NodalOffsetTool.IsValid()))
	{
		return true;
	}

	FCameraCalibrationStepsController* StepsController = NodalOffsetTool->GetCameraCalibrationStepsController();

	if (!ensure(StepsController))
	{
		return true;
	}

	// Get currently selected calibrator point
	FCalibratorPointCache LastCalibratorPoint;
	LastCalibratorPoint.bIsValid = false;
	{
		TSharedPtr<FCalibratorPointData> CalibratorPoint = CalibratorPointsComboBox->GetSelectedItem();

		if (!CalibratorPoint.IsValid())
		{
			return true;
		}

		// Find its values in the cache
		for (const FCalibratorPointCache& PointCache : LastCalibratorPoints)
		{
			if (PointCache.bIsValid && (PointCache.Name == CalibratorPoint->Name))
			{
				LastCalibratorPoint = PointCache;
				break;
			}
		}
	}

	// Check that we have a valid calibrator 3dpoint or camera data
	if (!LastCalibratorPoint.bIsValid || !LastCameraData.bIsValid)
	{
		return true;
	}

	// Create the row that we're going to add
	TSharedPtr<FCalibrationRowData> Row = MakeShared<FCalibrationRowData>();

	Row->CameraData = LastCameraData;
	Row->CalibratorPointData = LastCalibratorPoint;

	// Get the mouse click 2d position
	if (!StepsController->CalculateNormalizedMouseClickPosition(MyGeometry, MouseEvent, Row->Point2D))
	{
		return true;
	}

	// Validate the new row, show a message if validation fails.
	{
		FText ErrorMessage;

		if (!ValidateNewRow(Row, ErrorMessage))
		{
			const FText TitleError = LOCTEXT("NewRowError", "New Row Error");
			FMessageDialog::Open(EAppMsgType::Ok, ErrorMessage, &TitleError);
			return true;
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

	// Auto-advance to the next calibration point (if it exists)
	if (AdvanceCalibratorPoint())
	{
		// Play media if this was the last point in the object
		StepsController->Play();
	}

	return true;
}

bool UCameraNodalOffsetAlgoPoints::AdvanceCalibratorPoint()
{
	TSharedPtr<FCalibratorPointData> CurrentItem = CalibratorPointsComboBox->GetSelectedItem();

	if (!CurrentItem.IsValid())
	{
		return false;
	}

	for (int32 PointIdx = 0; PointIdx < CurrentCalibratorPoints.Num(); PointIdx++)
	{
		if (CurrentCalibratorPoints[PointIdx]->Name == CurrentItem->Name)
		{
			const int32 NextIdx = (PointIdx + 1) % CurrentCalibratorPoints.Num();
			CalibratorPointsComboBox->SetSelectedItem(CurrentCalibratorPoints[NextIdx]);

			// return true if we wrapped around (NextIdx is zero)
			return !NextIdx;
		}
	}

	return false;
}

bool UCameraNodalOffsetAlgoPoints::GetCurrentCalibratorPointLocation(FVector& OutLocation)
{
	TSharedPtr<FCalibratorPointData> CalibratorPointData = CalibratorPointsComboBox->GetSelectedItem();

	if (!CalibratorPointData.IsValid())
	{
		return false;
	}
	const UCalibrationPointComponent* CalibrationPointComponent = GetCalibrationPointComponentFromName(CalibratorPointData->Name);

	if (!CalibrationPointComponent)
	{
		return false;
	}

	OutLocation = CalibrationPointComponent->GetComponentLocation();

	return true;
}

TSharedRef<SWidget> UCameraNodalOffsetAlgoPoints::BuildUI()
{
	return SNew(SVerticalBox)

		+ SVerticalBox::Slot() // Calibrator picker
		.VAlign(EVerticalAlignment::VAlign_Top)
		.AutoHeight()
		.MaxHeight(FCameraCalibrationWidgetHelpers::DefaultRowHeight)
		[ FCameraCalibrationWidgetHelpers::BuildLabelWidgetPair(LOCTEXT("Calibrator", "Calibrator"), BuildCalibrationDevicePickerWidget()) ]
		
		+SVerticalBox::Slot() // Calibrator point names
		.AutoHeight()
		.MaxHeight(FCameraCalibrationWidgetHelpers::DefaultRowHeight)
		[ FCameraCalibrationWidgetHelpers::BuildLabelWidgetPair(LOCTEXT("CalibratorPoint", "Calibrator Point"), BuildCalibrationPointsComboBox()) ]
		
		+ SVerticalBox::Slot() // Calibration Rows
		.AutoHeight()
		.MaxHeight(12 * FCameraCalibrationWidgetHelpers::DefaultRowHeight)
		[ BuildCalibrationPointsTable() ]
		
		+ SVerticalBox::Slot() // Action buttons (e.g. Remove, Clear)
		.HAlign(EHorizontalAlignment::HAlign_Center)
		.AutoHeight()
		.Padding(0,20)
		[ BuildCalibrationActionButtons() ]
		;
}

bool UCameraNodalOffsetAlgoPoints::ValidateNewRow(TSharedPtr<FCalibrationRowData>& Row, FText& OutErrorMessage) const
{
	if (!CalibrationRows.Num())
	{
		return true;
	}

	if (!Row.IsValid())
	{
		OutErrorMessage = LOCTEXT("InvalidRowPointer", "Invalid row pointer");
		return false;
	}

	if (!NodalOffsetTool.IsValid())
	{
		return false;
	}

	FCameraCalibrationStepsController* StepsController = NodalOffsetTool->GetCameraCalibrationStepsController();

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

	// Same LensFile

	const TSharedPtr<FCalibrationRowData>& FirstRow = CalibrationRows[0];

	if (Row->CameraData.LensFileEvalData.LensFile != LensFile)
	{
		OutErrorMessage = LOCTEXT("LensFileWasNotTheSame", "Lens file was not the same");
		return false;
	}

	// Same camera in view

	const ACameraActor* Camera = StepsController->GetCamera();

	if (!Camera || !Camera->GetCameraComponent())
	{
		OutErrorMessage = LOCTEXT("MissingCamera", "Missing camera");
		return false;
	}

	if (Camera->GetUniqueID() != Row->CameraData.UniqueId)
	{
		OutErrorMessage = LOCTEXT("DifferentCameraAsSelected", "Different camera as selected");
		return false;
	}

	// Same camera as before

	if (FirstRow->CameraData.UniqueId != Row->CameraData.UniqueId)
	{
		OutErrorMessage = LOCTEXT("CameraChangedDuringTheTest", "Camera changed during the test");
		return false;
	}

	// Camera did not move much
	{
		// Location check

		const FVector LocationDelta = Row->CameraData.Pose.GetLocation() - FirstRow->CameraData.Pose.GetLocation();

		const float MaxLocationDeltaInCm = 2;
		const float LocationDeltaInCm = LocationDelta.Size();

		if (LocationDeltaInCm > MaxLocationDeltaInCm)
		{
			FFormatOrderedArguments Arguments;
			Arguments.Add(FText::FromString(FString::Printf(TEXT("%.1f"), MaxLocationDeltaInCm)));
			Arguments.Add(FText::FromString(FString::Printf(TEXT("%.1f"), LocationDeltaInCm)));

			OutErrorMessage = FText::Format(LOCTEXT("CameraMovedLocation", "Camera moved more than {0} cm during the calibration ({1} cm)"), Arguments);

			return false;
		}

		// Rotation check

		const float AngularDistanceRadians = FirstRow->CameraData.Pose.GetRotation().AngularDistance(Row->CameraData.Pose.GetRotation());
		const float MaxAngularDistanceRadians = 2 * (180 / PI);

		if (AngularDistanceRadians > MaxAngularDistanceRadians)
		{
			FFormatOrderedArguments Arguments;
			Arguments.Add(FText::FromString(FString::Printf(TEXT("%.1f"), MaxAngularDistanceRadians)));
			Arguments.Add(FText::FromString(FString::Printf(TEXT("%.1f"), AngularDistanceRadians)));

			OutErrorMessage = FText::Format(LOCTEXT("CameraMovedRotation", "Camera moved more than {0} radians during the calibration ({1} radians)"), Arguments);

			return false;
		}
	}

	// FZ inputs are valid
	if ((!Row->CameraData.LensFileEvalData.Input.Focus.IsSet()) || (!Row->CameraData.LensFileEvalData.Input.Zoom.IsSet()))
	{
		OutErrorMessage = LOCTEXT("LutInputsNotValid", "FZ Lut inputs are not valid. Make sure you are providing Focus and Zoom values via LiveLink");
		return false;
	}

	// bApplyNodalOffset did not change.
	//
	// It can't change because we need to know if the camera pose is being affected or not by the current nodal offset evaluation.
	// And we need to know that because the offset we calculate will need to either subtract or not the current evaluation when adding it to the LUT.
	if (FirstRow->CameraData.LensFileEvalData.NodalOffset.bWasApplied != Row->CameraData.LensFileEvalData.NodalOffset.bWasApplied)
	{
		OutErrorMessage = LOCTEXT("ApplyNodalOffsetChanged", "Apply nodal offset changed");
		return false;
	}

	//@todo Focus and zoom did not change much (i.e. inputs to distortion and nodal offset). 
	//      Threshold for physical units should differ from normalized encoders.

	return true;
}

bool UCameraNodalOffsetAlgoPoints::GetNodalOffset(FNodalPointOffset& OutNodalOffset, float& OutFocus, float& OutZoom, float& OutError, FText& OutErrorMessage)
{
	// Sanity checks
	//

	// Enough points
	if (CalibrationRows.Num() < 4)
	{
		OutErrorMessage = LOCTEXT("NotEnoughSamples", "At least 4 correspondence points are required");
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

	if (!ensure(NodalOffsetTool.IsValid()))
	{
		return false;
	}

	FCameraCalibrationStepsController* StepsController = NodalOffsetTool->GetCameraCalibrationStepsController();

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

	// Get camera.

	const ACameraActor* Camera = StepsController->GetCamera();

	if (!Camera || !Camera->GetCameraComponent())
	{
		OutErrorMessage = LOCTEXT("MissingCamera", "Missing camera");
		return false;
	}

	UCameraComponent* CameraComponent = Camera->GetCameraComponent();
	check(CameraComponent);

	UCineCameraComponent* CineCameraComponent = Cast<UCineCameraComponent>(CameraComponent);

	if (!CineCameraComponent)
	{
		OutErrorMessage = LOCTEXT("OnlyCineCamerasSupported", "Only cine cameras are supported");
		return false;
	}

	const TSharedPtr<FCalibrationRowData>& FirstRow = CalibrationRows[0];

	// Still same camera (since we need it to get the distortion handler, which much be the same)

	if (Camera->GetUniqueID() != FirstRow->CameraData.UniqueId)
	{
		OutErrorMessage = LOCTEXT("DifferentCameraAsSelected", "Different camera as selected");
		return false;
	}

	//@todo Should cache the distortion instead of getting it live, since the user may have changed it.

	// Only spherical lens distortion is currently supported at the moment.

	const USphericalLensDistortionModelHandler* SphericalHandler = Cast<USphericalLensDistortionModelHandler>(StepsController->GetDistortionHandler());

	if (!SphericalHandler)
	{
		OutErrorMessage = LOCTEXT("OnlySphericalDistortionSupported", "Only spherical distortion is currently supported.");
		return false;
	}

	// Get parameters from the handler
	FLensDistortionState DistortionState = SphericalHandler->GetCurrentDistortionState();

	// Extract named distortion parameters
	FSphericalDistortionParameters SphericalParameters;
	USphericalLensModel::StaticClass()->GetDefaultObject<ULensModel>()->FromArray(DistortionState.DistortionInfo.Parameters, SphericalParameters);

#if !WITH_OPENCV
	{
		OutErrorMessage = LOCTEXT("OpenCVRequired", "OpenCV is required");
		return false;
	}
#endif

#if WITH_OPENCV

	// Find the pose that minimizes the reprojection error

	// Populate the 3d/2d correlation points

	std::vector<cv::Point3f> Points3d;
	std::vector<cv::Point2f> Points2d;

	for (const TSharedPtr<FCalibrationRowData>& Row : CalibrationRows)
	{
		// Convert from UE coordinates to OpenCV coordinates

		FTransform Transform;
		Transform.SetIdentity();
		Transform.SetLocation(Row->CalibratorPointData.Location);

		FCameraCalibrationUtils::ConvertUnrealToOpenCV(Transform);

		// Calibrator 3d points
		Points3d.push_back(cv::Point3f(
			Transform.GetLocation().X, 
			Transform.GetLocation().Y, 
			Transform.GetLocation().Z));

		// Image 2d points
		Points2d.push_back(cv::Point2f(
			Row->Point2D.X,
			Row->Point2D.Y));
	}

	// Populate camera matrix

	cv::Mat CameraMatrix(3, 3, cv::DataType<double>::type);
	cv::setIdentity(CameraMatrix);

	// Note: cv::Mat uses (row,col) indexing.
	//
	//  Fx  0  Cx
	//  0  Fy  Cy
	//  0   0   1

	CameraMatrix.at<double>(0, 0) = DistortionState.FocalLengthInfo.FxFy.X;
	CameraMatrix.at<double>(1, 1) = DistortionState.FocalLengthInfo.FxFy.Y;

	CameraMatrix.at<double>(0, 2) = DistortionState.ImageCenter.PrincipalPoint.X;
	CameraMatrix.at<double>(1, 2) = DistortionState.ImageCenter.PrincipalPoint.Y;

	// Populate distortion coefficients
	// Note: solvePnP expects k1, k2, p1, p2, k3

	cv::Mat DistortionCoefficients(5, 1, cv::DataType<double>::type);

	DistortionCoefficients.at<double>(0) = SphericalParameters.K1;
	DistortionCoefficients.at<double>(1) = SphericalParameters.K2;
	DistortionCoefficients.at<double>(2) = SphericalParameters.P1;
	DistortionCoefficients.at<double>(3) = SphericalParameters.P2;
	DistortionCoefficients.at<double>(4) = SphericalParameters.K3;

	// Solve for camera position

	cv::Mat Rrod = cv::Mat::zeros(3, 1, cv::DataType<double>::type); // Rotation vector in Rodrigues notation. 3x1.
	cv::Mat Tobj = cv::Mat::zeros(3, 1, cv::DataType<double>::type); // Translation vector. 3x1.

	if (!cv::solvePnP(Points3d, Points2d, CameraMatrix, DistortionCoefficients, Rrod, Tobj))
	{
		OutErrorMessage = LOCTEXT("SolvePnpFailed", "SolvePnP failed");
		return false;
	}

	// Check for invalid data
	{
		const double Tx = Tobj.at<double>(0);
		const double Ty = Tobj.at<double>(1);
		const double Tz = Tobj.at<double>(2);

		const double MaxValue = 1e16;

		if (abs(Tx) > MaxValue || abs(Ty) > MaxValue || abs(Tz) > MaxValue)
		{
			OutErrorMessage = LOCTEXT("DataOutOfBounds", "Data Out Of Bounds");
			return false;
		}
	}

	// Convert to camera pose

	// [R|t]' = [R'|-R'*t]

	// Convert from Rodrigues to rotation matrix
	cv::Mat Robj;
	cv::Rodrigues(Rrod, Robj); // Robj is 3x3

	// Calculate camera translation
	cv::Mat Tcam = -Robj.t() * Tobj;

	// Invert/transpose to get camera orientation
	cv::Mat Rcam = Robj.t();

	// Convert back to UE coordinates

	FMatrix M = FMatrix::Identity;

	// Fill rotation matrix
	for (int32 Column = 0; Column < 3; ++Column)
	{
		M.SetColumn(Column, FVector(
			Rcam.at<double>(Column, 0), 
			Rcam.at<double>(Column, 1), 
			Rcam.at<double>(Column, 2))
		);
	}

	// Fill translation vector
	M.M[3][0] = Tcam.at<double>(0);
	M.M[3][1] = Tcam.at<double>(1);
	M.M[3][2] = Tcam.at<double>(2);

	FTransform DesiredCameraTransform;
	DesiredCameraTransform.SetFromMatrix(M);

	FCameraCalibrationUtils::ConvertOpenCVToUnreal(DesiredCameraTransform);

	// This is how we update the offset even when the camera is evaluating the current
	// nodal offset curve in the Lens File:
	// 
	// CameraTransform = ExistingOffset * CameraTransformWithoutOffset
	// => CameraTransformWithoutOffset = ExistingOffset' * CameraTransform
	//
	// DesiredTransform = Offset * CameraTransformWithoutOffset
	// => Offset = DesiredTransform * CameraTransformWithoutOffset'
	// => Offset = DesiredTransform * (ExistingOffset' * CameraTransform)'
	// => Offset = DesiredTransform * (CameraTransform' * ExistingOffset)

	// Evaluate nodal offset

	// Determine the input values to the LUT (focus and zoom)

	checkSlow(FirstRow->CameraData.LensFileEvalData.Input.Focus.IsSet());
	checkSlow(FirstRow->CameraData.LensFileEvalData.Input.Zoom.IsSet());

	OutFocus = *FirstRow->CameraData.LensFileEvalData.Input.Focus;
	OutZoom  = *FirstRow->CameraData.LensFileEvalData.Input.Zoom;

	// See if the camera already had an offset applied, in which case we need to account for it.

	FTransform ExistingOffset = FTransform::Identity;

	if (FirstRow->CameraData.LensFileEvalData.NodalOffset.bWasApplied)
	{
		FNodalPointOffset NodalPointOffset;

		if (LensFile->EvaluateNodalPointOffset(OutFocus, OutZoom, NodalPointOffset))
		{
			ExistingOffset.SetTranslation(NodalPointOffset.LocationOffset);
			ExistingOffset.SetRotation(NodalPointOffset.RotationOffset);
		}
	}

	FTransform DesiredOffset = DesiredCameraTransform * LastCameraData.Pose.Inverse() * ExistingOffset;

	OutNodalOffset.LocationOffset = DesiredOffset.GetLocation();
	OutNodalOffset.RotationOffset = DesiredOffset.GetRotation();

	return true;

#endif //WITH_OPENCV

	OutErrorMessage = LOCTEXT("OpenCVNotAvailable", "OpenCV was not available");
	return false;
}

TSharedRef<SWidget> UCameraNodalOffsetAlgoPoints::BuildCalibrationDevicePickerWidget()
{
	return SNew(SObjectPropertyEntryBox)
		.AllowedClass(AActor::StaticClass()) //@todo Consider populating this by searching for actors with CalibrationPoint components.
		.OnObjectChanged_Lambda([&](const FAssetData& AssetData) -> void
		{
			if (AssetData.IsValid())
			{
				SetCalibrator(Cast<AActor>(AssetData.GetAsset()));
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

TSharedRef<SWidget> UCameraNodalOffsetAlgoPoints::BuildCalibrationActionButtons()
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

TSharedRef<SWidget> UCameraNodalOffsetAlgoPoints::BuildCalibrationPointsComboBox()
{
	CalibratorPointsComboBox = SNew(SComboBox<TSharedPtr<FCalibratorPointData>>)
		.OptionsSource(&CurrentCalibratorPoints)
		.OnGenerateWidget_Lambda([&](TSharedPtr<FCalibratorPointData> InOption) -> TSharedRef<SWidget>
		{
			return SNew(STextBlock).Text(FText::FromString(*InOption->Name));
		})
		.InitiallySelectedItem(nullptr)
		[
			SNew(STextBlock)
			.Text_Lambda([&]() -> FText
			{
				if (CalibratorPointsComboBox.IsValid() && CalibratorPointsComboBox->GetSelectedItem().IsValid())
				{
					return FText::FromString(CalibratorPointsComboBox->GetSelectedItem()->Name);
				}

				return LOCTEXT("InvalidComboOption", "None");
			})
		];

	// Update combobox from current calibrator
	SetCalibrator(GetCalibrator());

	return CalibratorPointsComboBox.ToSharedRef();
}

TSharedRef<SWidget> UCameraNodalOffsetAlgoPoints::BuildCalibrationPointsTable()
{
	CalibrationListView = SNew(SListView<TSharedPtr<FCalibrationRowData>>)
		.ItemHeight(24)
		.ListItemsSource(&CalibrationRows)
		.OnGenerateRow_Lambda([&](TSharedPtr<FCalibrationRowData> InItem, const TSharedRef<STableViewBase>& OwnerTable) -> TSharedRef<ITableRow>
		{
			return SNew(CameraNodalOffsetAlgoPoints::SCalibrationRowGenerator, OwnerTable).CalibrationRowData(InItem);
		})
		.SelectionMode(ESelectionMode::SingleToggle)
		.HeaderRow
		(
			SNew(SHeaderRow)

			+ SHeaderRow::Column("Name")
			.DefaultLabel(LOCTEXT("Name", "Name"))

			+ SHeaderRow::Column("Point2D")
			.DefaultLabel(LOCTEXT("Point2D", "Point2D"))

			+ SHeaderRow::Column("Point3D")
			.DefaultLabel(LOCTEXT("Point3D", "Point3D"))
		);

	return CalibrationListView.ToSharedRef();
}

AActor* UCameraNodalOffsetAlgoPoints::FindFirstCalibrator() const
{
	// We find the first UCalibrationPointComponent object and return its actor owner.

	if (!NodalOffsetTool.IsValid())
	{
		return nullptr;
	}

	const FCameraCalibrationStepsController* StepsController = NodalOffsetTool->GetCameraCalibrationStepsController();

	if (!StepsController)
	{
		return nullptr;
	}

	const UWorld* World = StepsController->GetWorld();
	const EObjectFlags ExcludeFlags = RF_ClassDefaultObject; // We don't want the calibrator CDOs.

	for (TObjectIterator<UCalibrationPointComponent> It(ExcludeFlags, true, EInternalObjectFlags::PendingKill); It; ++It)
	{
		AActor* Actor = It->GetOwner();

		if (Actor && (Actor->GetWorld() == World))
		{
			return Actor;
		}
	}

	return nullptr;
}

const UCalibrationPointComponent* UCameraNodalOffsetAlgoPoints::GetCalibrationPointComponentFromName(FString& Name) const
{
	if (!Calibrator.IsValid())
	{
		return nullptr;
	}

	TArray<UCalibrationPointComponent*, TInlineAllocator<4>> CalibrationPoints;
	Calibrator->GetComponents<UCalibrationPointComponent, TInlineAllocator<4>>(CalibrationPoints);

	for (const UCalibrationPointComponent* CalibrationPoint : CalibrationPoints)
	{
		if (CalibrationPoint->GetName() == Name)
		{
			return CalibrationPoint;
		}
	}

	return nullptr;
}

void UCameraNodalOffsetAlgoPoints::SetCalibrator(AActor* InCalibrator)
{
	Calibrator = InCalibrator;

	// Update the list of points

	CurrentCalibratorPoints.Empty();

	if (!Calibrator.IsValid())
	{
		return;
	}

	TArray<UCalibrationPointComponent*, TInlineAllocator<4>> CalibrationPoints;
	Calibrator->GetComponents<UCalibrationPointComponent, TInlineAllocator<4>>(CalibrationPoints);

	for (const UCalibrationPointComponent* CalibrationPoint : CalibrationPoints)
	{
		CurrentCalibratorPoints.Add(MakeShared<FCalibratorPointData>(CalibrationPoint->GetName()));
	}

	// Notify combobox

	if (!CalibratorPointsComboBox)
	{
		return;
	}

	CalibratorPointsComboBox->RefreshOptions();

	if (CurrentCalibratorPoints.Num())
	{
		CalibratorPointsComboBox->SetSelectedItem(CurrentCalibratorPoints[0]);
	}
	else
	{
		CalibratorPointsComboBox->SetSelectedItem(nullptr);
	}
}

AActor* UCameraNodalOffsetAlgoPoints::GetCalibrator() const
{
	return Calibrator.Get();
}

void UCameraNodalOffsetAlgoPoints::OnSavedNodalOffset()
{
	// Since the nodal point was saved, there is no further use for the current samples.
	ClearCalibrationRows();
}

void UCameraNodalOffsetAlgoPoints::ClearCalibrationRows()
{
	CalibrationRows.Empty();

	if (CalibrationListView.IsValid())
	{
		CalibrationListView->RequestListRefresh();
	}
}

#undef LOCTEXT_NAMESPACE
