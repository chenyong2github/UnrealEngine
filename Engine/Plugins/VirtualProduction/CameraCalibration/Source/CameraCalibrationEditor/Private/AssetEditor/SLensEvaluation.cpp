// Copyright Epic Games, Inc. All Rights Reserved.

#include "SLensEvaluation.h"

#include "CameraCalibrationStepsController.h"
#include "EngineUtils.h"
#include "Features/IModularFeatures.h"
#include "ILiveLinkClient.h"
#include "LensFile.h"
#include "LiveLinkCameraController.h"
#include "Roles/LiveLinkCameraRole.h"
#include "Roles/LiveLinkCameraTypes.h"
#include "UI/CameraCalibrationEditorStyle.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"


#define LOCTEXT_NAMESPACE "LensEvaluation"

void SLensEvaluation::Construct(const FArguments& InArgs, TWeakPtr<FCameraCalibrationStepsController> StepsController, ULensFile* InLensFile)
{
	LensFile = TStrongObjectPtr<ULensFile>(InLensFile);

	WeakStepsController = StepsController;

	ChildSlot
	[
		SNew(SBorder)
		.Padding(FMargin(4.f, 4.f, 4.f, 4.f))
		[
			SNew(SHorizontalBox)

			//Tracking section
			+ SHorizontalBox::Slot()
			.Padding(5.0f, 5.0f)
			.FillWidth(0.2f)
			[
				MakeTrackingWidget()
			]
			//Raw Input FIZ section
			+ SHorizontalBox::Slot()
			.Padding(5.0f, 5.0f)
			.FillWidth(0.2f)
			[
				MakeRawInputFIZWidget()
			]
			//Evaluated FIZ section
			+ SHorizontalBox::Slot()
			.Padding(5.0f, 5.0f)
			.FillWidth(0.2f)
			[
				MakeEvaluatedFIZWidget()
			]
			//Distortion section
			+ SHorizontalBox::Slot()
			.Padding(5.0f, 5.0f)
			.FillWidth(0.2f)
			[
				MakeDistortionWidget()
			]
			//Image Center section
			+ SHorizontalBox::Slot()
			.Padding(5.0f, 5.0f)
			.FillWidth(0.2f)
			[
				MakeIntrinsicsWidget()
			]
			//Nodal offset section
			+ SHorizontalBox::Slot()
			.Padding(5.0f, 5.0f)
			.FillWidth(0.2f)
			[
				MakeNodalOffsetWidget()	
			]
		]
	];
}

void SLensEvaluation::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	//Cache LiveLink data every tick to be sure we have the right one for the frame during calibration
	CacheLiveLinkData();
	CacheLensFileData();

	Super::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
}

void SLensEvaluation::CacheLiveLinkData()
{
	//Start clean
	CachedLiveLinkData.RawFocus.Reset();
	CachedLiveLinkData.RawIris.Reset();
	CachedLiveLinkData.RawZoom.Reset();
	CachedLiveLinkData.EvaluatedFocus.Reset();
	CachedLiveLinkData.EvaluatedIris.Reset();
	CachedLiveLinkData.EvaluatedZoom.Reset();

	if (ULiveLinkCameraController* CameraController = WeakStepsController.Pin()->FindLiveLinkCameraController())
	{
		bCameraControllerExists = true;

		const FLensFileEvalData& LensFileEvalData = CameraController->GetLensFileEvalDataRef();
		{
			CachedLiveLinkData.RawFocus = LensFileEvalData.Input.Focus;
			if (LensFile->HasFocusEncoderMapping())
			{
				CachedLiveLinkData.EvaluatedFocus = LensFile->EvaluateNormalizedFocus(LensFileEvalData.Input.Focus);
			}
		}
		{
			CachedLiveLinkData.RawIris = LensFileEvalData.Input.Iris;
			if (LensFile->HasIrisEncoderMapping())
			{
				CachedLiveLinkData.EvaluatedIris = LensFile->EvaluateNormalizedIris(LensFileEvalData.Input.Iris);
			}
		}
		{
			CachedLiveLinkData.RawZoom = LensFileEvalData.Input.Zoom;

			FFocalLengthInfo FocalLength;
			if (LensFile->EvaluateFocalLength(LensFileEvalData.Input.Focus, LensFileEvalData.Input.Zoom, FocalLength))
			{
				CachedLiveLinkData.EvaluatedZoom = FocalLength.FxFy.X * LensFile->LensInfo.SensorDimensions.X;
			}
		}
	}
	else
	{
		bCameraControllerExists = false;
	}
}

void SLensEvaluation::CacheLensFileData()
{
	//Evaluate LensFile independantly of valid FZ pair. Use default 0.0f like LiveLinkCamera if it's not present
	{
		const float Focus = CachedLiveLinkData.RawFocus.IsSet() ? CachedLiveLinkData.RawFocus.GetValue() : 0.0f;
		const float Zoom = CachedLiveLinkData.RawZoom.IsSet() ? CachedLiveLinkData.RawZoom.GetValue() : 0.0f;
		bCouldEvaluateDistortion = LensFile->EvaluateDistortionParameters(Focus, Zoom, CachedDistortionInfo);
		bCouldEvaluateFocalLength = LensFile->EvaluateFocalLength(Focus, Zoom, CachedFocalLengthInfo);
		bCouldEvaluateImageCenter = LensFile->EvaluateImageCenterParameters(Focus, Zoom, CachedImageCenter);
		bCouldEvaluateNodalOffset = LensFile->EvaluateNodalPointOffset(Focus, Zoom, CachedNodalOffset);
	}
}

TSharedRef<SWidget> SLensEvaluation::MakeTrackingWidget()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SGridPanel)
			.FillColumn(0, 1.0f)

			+ SGridPanel::Slot(0, 0)
			.Padding(0.0f, 5.0f, 0.0f, 5.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("TrackedCameraLabelSection", "Tracked Camera"))
				.Font(FEditorStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
				.ShadowOffset(FVector2D(1.0f, 1.0f))
			]
			+ SGridPanel::Slot(0, 1)
			[
				SNew(STextBlock)
				.Text(this, &SLensEvaluation::GetTrackedCameraLabel)
				.ColorAndOpacity(this, &SLensEvaluation::GetTrackedCameraLabelColor)
				.AutoWrapText(true)
			]
			+ SGridPanel::Slot(0, 2)
			.Padding(0.0f, 15.0f, 0.0f, 5.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("LiveLinkLabelSection", "Selected LiveLink Subject"))
				.Font(FEditorStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
				.ShadowOffset(FVector2D(1.0f, 1.0f))
			]
			+ SGridPanel::Slot(0, 3)
			[
				SNew(STextBlock)
				.Text(this, &SLensEvaluation::GetLiveLinkCameraControllerLabel)
				.ColorAndOpacity(this, &SLensEvaluation::GetLiveLinkCameraControllerLabelColor)
				.AutoWrapText(true)
			]
		];
}

FText SLensEvaluation::GetTrackedCameraLabel() const
{
	if (WeakStepsController.IsValid())
	{
		if (ACameraActor* SelectedCamera = WeakStepsController.Pin()->GetCamera())
		{
			return FText::FromString(SelectedCamera->GetActorLabel());
		}
	}
	return LOCTEXT("NoTrackedCameraPresent", "No camera is selected. Select a CameraActor in the Calibration Steps tab.");
}

FSlateColor SLensEvaluation::GetTrackedCameraLabelColor() const
{
	if (WeakStepsController.IsValid())
	{
		if (ACameraActor* SelectedCamera = WeakStepsController.Pin()->GetCamera())
		{
			return FLinearColor::White;
		}
	}
	return FLinearColor::Yellow;
}


FText SLensEvaluation::GetLiveLinkCameraControllerLabel() const
{
	if (WeakStepsController.IsValid())
	{
		if (ULiveLinkCameraController* CameraController = WeakStepsController.Pin()->FindLiveLinkCameraController())
		{
			return FText::FromName(CameraController->GetSelectedSubject().Subject.Name);
		}
	}
	return LOCTEXT("NoLiveLinkSubject", "The tracked camera does not have a LiveLink camera controller, or the controller's LensFile does not match this one.");
}

FSlateColor SLensEvaluation::GetLiveLinkCameraControllerLabelColor() const
{
	if (WeakStepsController.IsValid())
	{
		if (ULiveLinkCameraController* CameraController = WeakStepsController.Pin()->FindLiveLinkCameraController())
		{
			return FLinearColor::White;
		}
	}
	return FLinearColor::Yellow;
}

TSharedRef<SWidget> SLensEvaluation::MakeRawInputFIZWidget() const
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.Padding(0.0f, 5.0f)
		.AutoHeight()
		.HAlign(HAlign_Left)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("FIZ Section", "Raw FIZ Input"))
			.ToolTipText(LOCTEXT("FIZSectionTooltip", "The raw values for Focus/Iris/Zoom (FIZ) used to evaluate the lens file"))
			.Font(FEditorStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
			.ShadowOffset(FVector2D(1.0f, 1.0f))
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SGridPanel)
			.FillColumn(0, 0.2f)
			.FillColumn(1, 0.8f)

			+ SGridPanel::Slot(0, 0)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("FocusLabel", "Focus"))
				.Font(FEditorStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
			]
			+ SGridPanel::Slot(0, 1)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("IrisLabel", "Iris"))
				.Font(FEditorStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
			]
			+ SGridPanel::Slot(0, 2)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ZoomLabel", "Zoom"))
				.Font(FEditorStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
			]

			+ SGridPanel::Slot(1, 0)
			[
				SNew(STextBlock)
				.Text(MakeAttributeLambda([this]
				{
					if (CachedLiveLinkData.RawFocus.IsSet())
					{
						return FText::AsNumber(CachedLiveLinkData.RawFocus.GetValue());
					}
					return LOCTEXT("NoRawFocus", "No Focus From LiveLink");
				}))
				.ColorAndOpacity(MakeAttributeLambda([this]
				{
					if (CachedLiveLinkData.RawFocus.IsSet())
					{
						return FSlateColor(FLinearColor::White);
					}
					return FSlateColor(FLinearColor::Yellow);
				}))
			]
			+ SGridPanel::Slot(1, 1)
			[
				SNew(STextBlock)
				.Text(MakeAttributeLambda([this]
				{
					if (CachedLiveLinkData.RawIris.IsSet())
					{
						return FText::AsNumber(CachedLiveLinkData.RawIris.GetValue());
					}
					return LOCTEXT("NoRawIris", "No Iris From LiveLink");
				}))
				.ColorAndOpacity(MakeAttributeLambda([this]
				{
					if (CachedLiveLinkData.RawIris.IsSet())
					{
						return FSlateColor(FLinearColor::White);
					}
					return FSlateColor(FLinearColor::Yellow);
				}))
			]
			+ SGridPanel::Slot(1, 2)
			[
				SNew(STextBlock)
				.Text(MakeAttributeLambda([this]
				{
					if (CachedLiveLinkData.RawZoom.IsSet())
					{
						return FText::AsNumber(CachedLiveLinkData.RawZoom.GetValue());
					}
					return LOCTEXT("NoRawZoom", "No Zoom From LiveLink");
				}))
				.ColorAndOpacity(MakeAttributeLambda([this]
				{
					if (CachedLiveLinkData.RawZoom.IsSet())
					{
						return FSlateColor(FLinearColor::White);
					}
					return FSlateColor(FLinearColor::Yellow);
				}))
			]
		];
}

TSharedRef<SWidget> SLensEvaluation::MakeEvaluatedFIZWidget() const
{
	FNumberFormattingOptions FloatOptions;
	FloatOptions.MinimumFractionalDigits = 3;
	FloatOptions.MaximumFractionalDigits = 3;

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.Padding(0.0f, 5.0f)
		.AutoHeight()
		.HAlign(HAlign_Left)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("EvaluatedFIZSection", "Evaluated Camera Settings"))
			.ToolTipText(LOCTEXT("EvaluatedFIZSectionTooltip", "Camera settings that were evaluated from the lens file using the raw input FIZ data"))
			.Font(FEditorStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
			.ShadowOffset(FVector2D(1.0f, 1.0f))
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SGridPanel)
			+ SGridPanel::Slot(0, 0)
			[
				SNew(STextBlock)
				.MinDesiredWidth(100.0f)
				.Text(LOCTEXT("FocusDistanceLabel", "Focus Distance"))
				.Font(FEditorStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
			]
			+ SGridPanel::Slot(0, 1)
			[
				SNew(STextBlock)
				.MinDesiredWidth(100.0f)
				.Text(LOCTEXT("ApertureLabel", "Aperture"))
				.Font(FEditorStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
			]
			+ SGridPanel::Slot(0, 2)
			[
				SNew(STextBlock)
				.MinDesiredWidth(100.0f)
				.Text(LOCTEXT("FocalLengthLabel", "Focal Length"))
				.Font(FEditorStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
			]
			+ SGridPanel::Slot(0, 3)
			[
				SNew(STextBlock)
				.MinDesiredWidth(100.0f)
				.Text(LOCTEXT("FOVLabel", "Horizontal FOV"))
				.Font(FEditorStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
			]

			+ SGridPanel::Slot(1, 0)
			[
				SNew(STextBlock)
				.Text(MakeAttributeLambda([this, FloatOptions]
				{
					if (CachedLiveLinkData.EvaluatedFocus.IsSet())
					{
						const FText ValueText = FText::AsNumber(CachedLiveLinkData.EvaluatedFocus.GetValue(), &FloatOptions);
						return FText::Format(LOCTEXT("PhysicalUnitsFocusValue", "{0} cm"), ValueText);
					}
					else if (!bCameraControllerExists)
					{
						return LOCTEXT("UndefinedValue", "N/A");
					}
					else if (!LensFile->HasSamples(ELensDataCategory::Focus))
					{
						return LOCTEXT("NoFocusCurve", "No Focus Curve");
					}
					return LOCTEXT("UndefinedValue", "N/A");
				}))
				.ColorAndOpacity(MakeAttributeLambda([this]
				{
					if (CachedLiveLinkData.EvaluatedFocus.IsSet())
					{
						return FSlateColor(FLinearColor::White);
					}
					else if (!bCameraControllerExists)
					{
						return FSlateColor(FLinearColor::White);
					}
					else if (!LensFile->HasSamples(ELensDataCategory::Focus))
					{
						return FSlateColor(FLinearColor::Yellow);
					}
					return FSlateColor(FLinearColor::White);
				}))
			]
			+ SGridPanel::Slot(1, 1)
			[
				SNew(STextBlock)
				.Text(MakeAttributeLambda([this]
				{
					if (CachedLiveLinkData.EvaluatedIris.IsSet())
					{
						FNumberFormattingOptions IrisOptions;
						IrisOptions.MinimumFractionalDigits = 1;
						IrisOptions.MaximumFractionalDigits = 1;
						const FText ValueText = FText::AsNumber(CachedLiveLinkData.EvaluatedIris.GetValue(), &IrisOptions);
						return FText::Format(LOCTEXT("PhysicalUnitsIrisValue", "{0} F-Stop"), ValueText);
					}
					else if (!bCameraControllerExists)
					{
						return LOCTEXT("UndefinedValue", "N/A");
					}
					else if (!LensFile->HasSamples(ELensDataCategory::Iris))
					{
						return LOCTEXT("NoIrisCurve", "No Iris Curve");
					}
					return LOCTEXT("UndefinedValue", "N/A");
				}))
				.ColorAndOpacity(MakeAttributeLambda([this]
				{
					if (CachedLiveLinkData.EvaluatedIris.IsSet())
					{
						return FSlateColor(FLinearColor::White);
					}
					else if (!bCameraControllerExists)
					{
						return FSlateColor(FLinearColor::White);
					}
					else if (!LensFile->HasSamples(ELensDataCategory::Iris))
					{
						return FSlateColor(FLinearColor::Yellow);
					}
					return FSlateColor(FLinearColor::White);
				}))
			]
			+ SGridPanel::Slot(1, 2)
			[
				SNew(STextBlock)
				.Text(MakeAttributeLambda([this, FloatOptions]
				{
					if (CachedLiveLinkData.EvaluatedZoom.IsSet())
					{
						const FText ValueText = FText::AsNumber(CachedLiveLinkData.EvaluatedZoom.GetValue(), &FloatOptions);
						return FText::Format(LOCTEXT("PhysicalUnitsZoomValue", "{0} mm"), ValueText);
					}
					else if (!bCameraControllerExists)
					{
						return LOCTEXT("UndefinedValue", "N/A");
					}
					else if (!LensFile->HasSamples(ELensDataCategory::Zoom))
					{
						return LOCTEXT("NoZoomCurve", "No Focal Length Curve");
					}
					return LOCTEXT("UndefinedValue", "N/A");
				}))
				.ColorAndOpacity(MakeAttributeLambda([this]
				{
					if (CachedLiveLinkData.EvaluatedZoom.IsSet())
					{
						return FSlateColor(FLinearColor::White);
					}
					else if (!bCameraControllerExists)
					{
						return FSlateColor(FLinearColor::White);
					}
					else if (!LensFile->HasSamples(ELensDataCategory::Zoom))
					{
						return FSlateColor(FLinearColor::Yellow);
					}
					return FSlateColor(FLinearColor::White);
				}))
			]
			+ SGridPanel::Slot(1, 3)
			[
				SNew(STextBlock)
				.Text(MakeAttributeLambda([this, FloatOptions]
				{
					if (CachedLiveLinkData.EvaluatedZoom.IsSet())
					{
						const float FOV = FMath::RadiansToDegrees(2.f * FMath::Atan(LensFile->LensInfo.SensorDimensions.X / (2.f * CachedLiveLinkData.EvaluatedZoom.GetValue())));
						const FText ValueText = FText::AsNumber(FOV, &FloatOptions);
						return FText::Format(LOCTEXT("PhysicalUnitsZoomValue", "{0} deg"), ValueText);
					}
					return LOCTEXT("UndefinedValue", "N/A");
				}))
			]
		];
}

TSharedRef<SWidget> SLensEvaluation::MakeDistortionWidget() const
{
	//Find the named distortion parameters the current model has
	TArray<FText> Parameters;
	if (LensFile->LensInfo.LensModel)
	{
		Parameters = LensFile->LensInfo.LensModel.GetDefaultObject()->GetParameterDisplayNames();
	}

	const TSharedRef<SWidget> Title = SNew(STextBlock)
		.Text(LOCTEXT("DistortionSection", "Distortion Parameters"))
		.ToolTipText(LOCTEXT("DistortionSectionTooltip", "Coefficients associated with the distortion equation of the selected lens model"))
		.Font(FEditorStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
		.ShadowOffset(FVector2D(1.0f, 1.0f));

	//if there are no parameters, create a simpler widget
	if (Parameters.Num() <= 0)
	{
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.Padding(0.0f, 5.0f)
			.AutoHeight()
			.HAlign(HAlign_Left)
			[
				Title
			]
			+ SVerticalBox::Slot()
			.Padding(0.0f, 5.0f)
			.AutoHeight()
			.HAlign(HAlign_Left)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("NoParameters", "No parameters"))
			];
	}

	TSharedRef<SGridPanel> ParameterGrid = SNew(SGridPanel);

	for (int32 Index = 0; Index < Parameters.Num(); ++Index)
	{
		ParameterGrid->AddSlot(0, Index)
			[
				SNew(STextBlock)
				.MinDesiredWidth(35.0f)
				.Text(MakeAttributeLambda([Parameters, Index]
				{
					if (Parameters.IsValidIndex(Index))
					{
						return Parameters[Index];
					}
					else
					{
						return LOCTEXT("InvalidParam", "Invalid");
					}
				}))
				.Font(FEditorStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
			];

		ParameterGrid->AddSlot(1, Index)
			[
				SNew(STextBlock)
				.Text(MakeAttributeLambda([this, Index]
					{
						if (bCouldEvaluateDistortion && CachedDistortionInfo.Parameters.IsValidIndex(Index))
						{
							return FText::AsNumber(CachedDistortionInfo.Parameters[Index]);
						}
						return LOCTEXT("UndefinedValue", "N/A");
					}))
			];
	}

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.Padding(0.0f, 5.0f)
		.AutoHeight()
		.HAlign(HAlign_Left)
		[
			Title
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Left)
		[
			ParameterGrid
		];
}

TSharedRef<SWidget> SLensEvaluation::MakeIntrinsicsWidget() const
{
	FNumberFormattingOptions FloatOptions;
	FloatOptions.MinimumFractionalDigits = 2;
	FloatOptions.MaximumFractionalDigits = 2;

	return
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.Padding(0.0f, 5.0f)
		.AutoHeight()
		.HAlign(HAlign_Left)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("IntrinsicsSection", "Normalized Camera Intrinsics"))
			.ToolTipText(LOCTEXT("IntrinsicsSectionTooltip", "Normalized values from the camera intrinsic matrix"))
			.Font(FEditorStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
			.ShadowOffset(FVector2D(1.0f, 1.0f))
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Left)
		[
			SNew(SGridPanel)

			+ SGridPanel::Slot(0, 0)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ImageCenterLabel", "Image Center"))
				.ToolTipText(LOCTEXT("ImageCenterTooltip", "Normalized Center in the range [0, 1], with (0, 0) representing the top left corner of the image"))
				.MinDesiredWidth(100.0f)
				.Font(FEditorStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
			]
			+ SGridPanel::Slot(1, 0)
			[
				SNew(STextBlock)
				.Text(MakeAttributeLambda([this, FloatOptions]
				{
					if (bCouldEvaluateImageCenter)
					{
						const FText CxText = FText::AsNumber(CachedImageCenter.PrincipalPoint.X, &FloatOptions);
						const FText CyText = FText::AsNumber(CachedImageCenter.PrincipalPoint.Y, &FloatOptions);
						return FText::Format(LOCTEXT("LocationOffsetValue", "({0}, {1})"), CxText, CyText);
					}
					return LOCTEXT("UndefinedValue", "N/A");
				}))
			]
			+ SGridPanel::Slot(0, 1)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("FxFyLabel", "FxFy"))
				.ToolTipText(LOCTEXT("FxFyTooltip", "Normalized values representing the camera focal length divided by the sensor/image size. The ratio of Fx to Fy should roughly equal the camera's aspect ratio"))
				.MinDesiredWidth(100.0f)
				.Font(FEditorStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
			]
			+ SGridPanel::Slot(1, 1)
			[
				SNew(STextBlock)
				.Text(MakeAttributeLambda([this, FloatOptions]
				{
					if (bCouldEvaluateFocalLength)
					{
						const FText FxText = FText::AsNumber(CachedFocalLengthInfo.FxFy.X, &FloatOptions);
						const FText FyText = FText::AsNumber(CachedFocalLengthInfo.FxFy.Y, &FloatOptions);
						return FText::Format(LOCTEXT("LocationOffsetValue", "({0}, {1})"), FxText, FyText);
					}
					return LOCTEXT("UndefinedValue", "N/A");
				}))
			]
		];
}

TSharedRef<SWidget> SLensEvaluation::MakeNodalOffsetWidget() const
{
	FNumberFormattingOptions FloatOptions;
	FloatOptions.MinimumFractionalDigits = 2;
	FloatOptions.MaximumFractionalDigits = 2;

	const FRotator CachedRotationOffset = CachedNodalOffset.RotationOffset.Rotator();

	return
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.Padding(0.0f, 5.0f)
		.AutoHeight()
		.HAlign(HAlign_Left)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("NodalOffsetSection", "Nodal Point Offset"))
			.ToolTipText(LOCTEXT("NodalOffsetTooltip", "The offset required to go from the tracked camera transform to the nodal point of the physical lens"))
			.Font(FEditorStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
			.ShadowOffset(FVector2D(1.0f, 1.0f))
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Left)
		[
			SNew(SGridPanel)

			+ SGridPanel::Slot(0, 0)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("LocationOffsetLabel", "Location"))
				.MinDesiredWidth(75.0f)
				.Font(FEditorStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
			]
			+ SGridPanel::Slot(1, 0)
			[
				SNew(STextBlock)
				.MinDesiredWidth(15.0f)
				.Text(MakeAttributeLambda([this, FloatOptions]
				{
					if (bCouldEvaluateNodalOffset)
					{
						const FText LocXText = FText::AsNumber(CachedNodalOffset.LocationOffset.X, &FloatOptions);
						const FText LocYText = FText::AsNumber(CachedNodalOffset.LocationOffset.Y, &FloatOptions);
						const FText LocZText = FText::AsNumber(CachedNodalOffset.LocationOffset.Z, &FloatOptions);
						return FText::Format(LOCTEXT("LocationOffsetValue", "({0}, {1}, {2})"), LocXText, LocYText, LocZText);
					}
					return LOCTEXT("UndefinedValue", "N/A");
				}))
			]

			+ SGridPanel::Slot(0, 1)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("RotationOffsetLabel", "Rotation"))
				.MinDesiredWidth(75.0f)
				.Font(FEditorStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
			]
			+ SGridPanel::Slot(1, 1)
			[
				SNew(STextBlock)
				.MinDesiredWidth(15.0f)
				.Text(MakeAttributeLambda([this, FloatOptions]
				{
					if (bCouldEvaluateNodalOffset)
					{
						const FText RotXText = FText::AsNumber(CachedNodalOffset.RotationOffset.Rotator().GetComponentForAxis(EAxis::X), &FloatOptions);
						const FText RotYText = FText::AsNumber(CachedNodalOffset.RotationOffset.Rotator().GetComponentForAxis(EAxis::Y), &FloatOptions);
						const FText RotZText = FText::AsNumber(CachedNodalOffset.RotationOffset.Rotator().GetComponentForAxis(EAxis::Z), &FloatOptions);
						return FText::Format(LOCTEXT("LocationOffsetValue", "({0}, {1}, {2})"), RotXText, RotYText, RotZText);
					}
					return LOCTEXT("UndefinedValue", "N/A");
				}))
			]
		];
}

#undef LOCTEXT_NAMESPACE /* LensDataViewer */