// Copyright Epic Games, Inc. All Rights Reserved.

#include "SLensDataPointDialog.h"

#include "Curves/RichCurve.h"
#include "EditorStyleSet.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailPropertyRow.h"
#include "ISinglePropertyView.h"
#include "IStructureDetailsView.h"
#include "LensFile.h"
#include "Models/LensModel.h"
#include "Modules/ModuleManager.h"
#include "PropertyHandle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SNumericEntryBox.h"


#define LOCTEXT_NAMESPACE "LensDataPointDialog"

TWeakPtr<SWindow> SLensDataPointDialog::ExistingWindow;

/**
 * Instanced customization used to display the distortion parameters
 * data structure associated with selected model
 */
class FDistortionInfoCustomization : public IPropertyTypeCustomization
{
public:

	FDistortionInfoCustomization(TSubclassOf<ULensModel> InLensModel)
		: IPropertyTypeCustomization()
		, CurrentLensModel(MoveTemp(InLensModel))
	{}

	// IDetailCustomization interface
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override
	{
	}

	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override
	{
		if (CurrentLensModel)
		{
			FStructureDetailsViewArgs StructureViewArgs;
			FDetailsViewArgs DetailArgs;
			DetailArgs.bAllowSearch = false;
			DetailArgs.bShowScrollBar = true;

			FPropertyEditorModule& PropertyEditor = FModuleManager::Get().LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));

			DistortionParameters = MakeShared<FStructOnScope>(CurrentLensModel.GetDefaultObject()->GetParameterStruct());
			TSharedPtr<IStructureDetailsView> StructureDetailsView = PropertyEditor.CreateStructureDetailView(DetailArgs, StructureViewArgs, DistortionParameters);

			uint32 NumChildren;
			StructPropertyHandle->GetNumChildren(NumChildren);

			for (uint32 ChildIndex = 0; ChildIndex < NumChildren; ++ChildIndex)
			{
				const TSharedRef<IPropertyHandle> ChildHandle = StructPropertyHandle->GetChildHandle(ChildIndex).ToSharedRef();
				const FName PropertyName = ChildHandle->GetProperty()->GetFName();

				if (PropertyName == GET_MEMBER_NAME_CHECKED(FDistortionInfo, Parameters))
				{
					DistortionParametersArrayHandle = ChildHandle;
					IDetailPropertyRow* ParametersRow = StructBuilder.AddExternalStructure(DistortionParameters.ToSharedRef());
					ParametersRow->GetPropertyHandle()->SetOnChildPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FDistortionInfoCustomization::OnDistortionParametersChanged));
				}
				else
				{
					StructBuilder.AddProperty(ChildHandle).ShowPropertyButtons(false);
				}
			}

			//Start with initial parameters value
			OnDistortionParametersChanged();
		}
	}

private:

	void OnDistortionParametersChanged() const
	{
		//Whenever a parameter value has changed, update the associated array
		if (!CurrentLensModel)
		{
			return;
		}

		TArray<void*> RawData;
		DistortionParametersArrayHandle->AccessRawData(RawData);
		if (RawData.Num() == 1 && RawData[0])
		{
			TArray<float>* RawParametersArray = reinterpret_cast<TArray<float>*>(RawData[0]);
			CurrentLensModel.GetDefaultObject()->ToArray(*DistortionParameters, *RawParametersArray);
		}
	}

private:

	/** Handle to the raw distortion parameters array */
	TSharedPtr<IPropertyHandle> DistortionParametersArrayHandle;

	/** Holds named parameters data structure */
	TSharedPtr<FStructOnScope> DistortionParameters;

	/** LensModel being edited */
	TSubclassOf<ULensModel> CurrentLensModel;
};

void SLensDataPointDialog::Construct(const FArguments& InArgs, ULensFile* InLensFile)
{
	LensFile = TStrongObjectPtr<ULensFile>(InLensFile);
	CachedFIZ = InArgs._CachedFIZData;
	OnDataPointAdded = InArgs._OnDataPointAdded;

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.Padding(5.0f, 5.0f)
		.AutoHeight()
		[
			SNew(SBorder)
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Fill)
			.Padding(FMargin(4.f, 4.f, 4.f, 4.f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("DataCategoryLabel", "Data Type "))
				]
				+ SHorizontalBox::Slot()
				.HAlign(HAlign_Fill)
				[
					SNew(STextBlock)
					.Text(MakeAttributeLambda([=]
					{
							switch (SelectedCategory)
							{
								case EDataCategories::Focus: return LOCTEXT("FocusCategory", "Encoder - Focus");
								case EDataCategories::Iris: return LOCTEXT("IrisCategory", "Encoder - Iris");
								case EDataCategories::Zoom: return LOCTEXT("ZoomCategory", "Encoder - Zoom");
								case EDataCategories::Distortion: return LOCTEXT("DistortionCategory", "Distortion Parameters");
								case EDataCategories::ImageCenter: return LOCTEXT("ImageCenterCategory", "Image Center");
								case EDataCategories::NodalOffset: return LOCTEXT("NodalOffsetCategory", "Nodal offset");
								case EDataCategories::STMap: return LOCTEXT("STMapCategory", "STMap");
								default: return LOCTEXT("InvalidCategory", "Invalid");
							}
					}))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(FMargin(4.0f, 0.0f, 0.0f, 0.0f))
				[
					SNew(SComboButton)
					.OnGetMenuContent(this, &SLensDataPointDialog::MakeDataCategoryMenuWidget)
					.ContentPadding(FMargin(4.0, 2.0))
				]
			]
		]
		+ SVerticalBox::Slot()
		.Padding(5.0f, 5.0f)
		.AutoHeight()
		[
			SAssignNew(TrackingDataContainer, SBorder)
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Fill)
			.Padding(FMargin(4.f, 4.f, 4.f, 4.f))
		]
		+ SVerticalBox::Slot()
		.Padding(5.0f, 5.0f)
		.FillHeight(0.7f)
		[
			SAssignNew(LensDataContainer, SBorder)
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Fill)
			.Padding(FMargin(4.f, 4.f, 4.f, 4.f))
		]
		+ SVerticalBox::Slot()
		.Padding(5.0f, 5.0f)
		.AutoHeight()
		[
			SNew(SBorder)
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Fill)
			.Padding(FMargin(4.f, 4.f, 4.f, 4.f))
			[
				MakeButtonsWidget()
			]
		]
	];
	
	SetDataCategory(EDataCategories::Focus);
}

void SLensDataPointDialog::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	RefreshEvaluationData();
	Super::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
}

void SLensDataPointDialog::OpenDialog(ULensFile* InLensFile, TAttribute<FCachedFIZData> InCachedFIZData, const FSimpleDelegate& InOnDataPointAdded)
{
	TSharedPtr<SWindow> ExistingWindowPin = ExistingWindow.Pin();
	if (ExistingWindowPin.IsValid())
	{
		ExistingWindowPin->BringToFront();
	}
	else
	{
		ExistingWindowPin = SNew(SWindow)
			.Title(LOCTEXT("LensEditorAddPointDialog", "Add Lens Data Point"))
			.HasCloseButton(true)
			.SupportsMaximize(false)
			.SupportsMinimize(false)
			.ClientSize(FVector2D(480, 360));

		FSlateApplication::Get().AddWindow(ExistingWindowPin.ToSharedRef());
	}

	TSharedPtr<SLensDataPointDialog> AddPointDialog = 
		SNew(SLensDataPointDialog, InLensFile)
		.CachedFIZData(MoveTemp(InCachedFIZData))
		.OnDataPointAdded(InOnDataPointAdded);

	ExistingWindowPin->SetContent(AddPointDialog.ToSharedRef());
	ExistingWindow = ExistingWindowPin;
}

FReply SLensDataPointDialog::OnAddDataPointClicked()
{
	AddDataToLensFile();

	OnDataPointAdded.ExecuteIfBound();

	CloseDialog();
	return FReply::Handled();
}

FReply SLensDataPointDialog::OnCancelDataPointClicked()
{
	CloseDialog();
	return FReply::Handled();
}

TSharedRef<SWidget> SLensDataPointDialog::MakeTrackingDataWidget()
{
	TSharedPtr<SWidget> TrackingWidget = SNullWidget::NullWidget;

	const auto MakeRowWidget = [this](const FString& Label, int32 Index) -> TSharedRef<SWidget>
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SCheckBox)
				.IsChecked(MakeAttributeLambda([this, Index]() { return IsTrackingDataOverrided(Index) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; }))
				.OnCheckStateChanged(this, &SLensDataPointDialog::OnOverrideTrackingData, Index)
			]
			+ SHorizontalBox::Slot()
			.FillWidth(0.2f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Label))
			]
			+ SHorizontalBox::Slot()
			.FillWidth(0.8f)
			[
				SNew(SNumericEntryBox<float>)
				.IsEnabled(MakeAttributeLambda([this, Index]() { return IsTrackingDataOverrided(Index); }))
				.Value(MakeAttributeLambda([this, Index]() { return TOptional<float>(GetTrackingData(Index)); }))
				.OnValueChanged(this, &SLensDataPointDialog::SetTrackingData, Index)
			];
	};

	//Based on category, either have one tracking input or two
	switch (SelectedCategory)
	{
		case EDataCategories::Focus:
		{
			TrackingWidget = MakeRowWidget(TEXT("Input Focus"), 0);
			break;
		}
		case EDataCategories::Iris:
		{
			TrackingWidget = MakeRowWidget(TEXT("Input Iris"), 0);
			break;
		}
		case EDataCategories::Zoom:
		{
			TrackingWidget = MakeRowWidget(TEXT("Input Zoom"), 0);
			break;
		}
		case EDataCategories::Distortion:
		case EDataCategories::ImageCenter:
		case EDataCategories::NodalOffset:
		case EDataCategories::STMap:
		default:
		{
			TrackingWidget = 
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					MakeRowWidget(TEXT("Input Focus"), 0)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					MakeRowWidget(TEXT("Input Zoom"), 1)
				];
			break;
		}	
	}

	return TrackingWidget.ToSharedRef();
}

TSharedRef<SWidget> SLensDataPointDialog::MakeLensDataWidget()
{
	FStructureDetailsViewArgs StructureViewArgs;
	FDetailsViewArgs DetailArgs;
	DetailArgs.bAllowSearch = false;
	DetailArgs.bShowScrollBar = false;

	FPropertyEditorModule& PropertyEditor = FModuleManager::Get().LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
	TSharedPtr<SWidget> LensDataWidget = SNullWidget::NullWidget;
	
	TSharedPtr<IStructureDetailsView> StructureDetailsView = PropertyEditor.CreateStructureDetailView(DetailArgs, StructureViewArgs, TSharedPtr<FStructOnScope>());
	
	LensData.Reset();
	
	switch (SelectedCategory)
	{
		case EDataCategories::Focus:
		case EDataCategories::Iris:
		case EDataCategories::Zoom:
		{
			LensDataWidget = MakeEncoderMappingWidget();
			break;
		}
		case EDataCategories::Distortion:
		{
			LensData = MakeShared<FStructOnScope>(FDistortionInfoContainer::StaticStruct());
			StructureDetailsView->GetDetailsView()->RegisterInstancedCustomPropertyTypeLayout(FDistortionInfo::StaticStruct()->GetFName(),
				FOnGetPropertyTypeCustomizationInstance::CreateLambda([this]() { return MakeShared<FDistortionInfoCustomization>(LensFile->LensInfo.LensModel); })
			);

			StructureDetailsView->SetStructureData(LensData);
			LensDataWidget = StructureDetailsView->GetWidget();

			break;
		}
		case EDataCategories::ImageCenter:
		{
			LensData = MakeShared<FStructOnScope>(FIntrinsicParameters::StaticStruct());
			StructureDetailsView->SetStructureData(LensData);
			LensDataWidget = StructureDetailsView->GetWidget();
			break;
		}
		case EDataCategories::NodalOffset:
		{
			LensData = MakeShared<FStructOnScope>(FNodalPointOffset::StaticStruct());
			StructureDetailsView->SetStructureData(LensData);
			LensDataWidget = StructureDetailsView->GetWidget();
			break;
		}
		case EDataCategories::STMap:
		{
			LensData = MakeShared<FStructOnScope>(FCalibratedMapInfo::StaticStruct());
			StructureDetailsView->SetStructureData(LensData);
			LensDataWidget = StructureDetailsView->GetWidget();
			break;
		}
		default:
		{
			break;
		}
	}

	return LensDataWidget.ToSharedRef();
}

TSharedRef<SWidget> SLensDataPointDialog::MakeButtonsWidget()
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		[
			SNew(SButton)
			.ContentPadding(FEditorStyle::GetMargin("StandardDialog.ContentPadding"))
			.OnClicked(this, &SLensDataPointDialog::OnAddDataPointClicked)
			.HAlign(HAlign_Center)
			.Text(LOCTEXT("AddDataPoint", "Add"))
		]
		+ SHorizontalBox::Slot()
		[
			SNew(SButton)
			.ContentPadding(FEditorStyle::GetMargin("StandardDialog.ContentPadding"))
			.OnClicked(this, &SLensDataPointDialog::OnCancelDataPointClicked)
			.HAlign(HAlign_Center)
			.Text(LOCTEXT("CancelAddingDataPoint", "Cancel"))
		];
}

TSharedRef<SWidget> SLensDataPointDialog::MakeDataCategoryMenuWidget()
{
	constexpr bool bShouldCloseWindowAfterClosing = true;
	FMenuBuilder MenuBuilder(bShouldCloseWindowAfterClosing, nullptr);

	MenuBuilder.BeginSection("LensDataCategories", LOCTEXT("LensDataCategories", "Lens data categories"));
	{
		const auto AddMenuEntryFn = [&MenuBuilder, this](const FString& Label, EDataCategories Category)
		{
			MenuBuilder.AddMenuEntry
			(
				FText::FromString(Label)
				, FText::FromString(Label)
				, FSlateIcon()
				, FUIAction
				(
					FExecuteAction::CreateLambda([this, Category] { SetDataCategory(Category); })
					, FCanExecuteAction()
					, FIsActionChecked::CreateLambda([this, Category] { return SelectedCategory == Category; })
				)
				, NAME_None
				, EUserInterfaceActionType::RadioButton
			);
		};
		
		//Add different categories that we support
		AddMenuEntryFn(TEXT("Focus"), EDataCategories::Focus);
		AddMenuEntryFn(TEXT("Iris"), EDataCategories::Iris);
		AddMenuEntryFn(TEXT("Distortion Parameters"), EDataCategories::Distortion);
		AddMenuEntryFn(TEXT("Image Center"), EDataCategories::ImageCenter);
		AddMenuEntryFn(TEXT("Nodal Offset"), EDataCategories::NodalOffset);
		AddMenuEntryFn(TEXT("STMap"), EDataCategories::STMap);
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

TSharedRef<SWidget> SLensDataPointDialog::MakeEncoderMappingWidget()
{
	return 
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SSplitter)
			.Style(FEditorStyle::Get(), "DetailsView.Splitter")
			.PhysicalSplitterHandleSize(1.0f)
			.HitDetectionSplitterHandleSize(5.0f)
			+ SSplitter::Slot()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.Padding(FMargin(0, 1, 0, 1))
				.FillWidth(1)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("EncoderMappingValueLabel", "Encoder mapping :"))
				]
			]
			+ SSplitter::Slot()
			[
				SNew(SNumericEntryBox<float>)
				.AllowSpin(true)
				.Value_Lambda([this]() { return EncoderMappingValue; })
				.OnValueChanged(SNumericEntryBox<float>::FOnValueChanged::CreateLambda([this](float NewValue) { EncoderMappingValue = NewValue; }))
				.OnValueCommitted(SNumericEntryBox<float>::FOnValueCommitted::CreateLambda([this](float NewValue, ETextCommit::Type CommitType) { EncoderMappingValue = NewValue; }))
			]
		];
}

void SLensDataPointDialog::SetDataCategory(EDataCategories NewCategory)
{
	if (NewCategory != SelectedCategory)
	{
		SelectedCategory = NewCategory;
		EncoderMappingValue = 0.0f;
	}

	//Updates displayed widgets based on category
	TrackingDataContainer->SetContent(MakeTrackingDataWidget());
	LensDataContainer->SetContent(MakeLensDataWidget());
}

void SLensDataPointDialog::CloseDialog()
{
	TSharedPtr<SWindow> ExistingWindowPin = ExistingWindow.Pin();
	if (ExistingWindowPin.IsValid())
	{
		ExistingWindowPin->RequestDestroyWindow();
		ExistingWindowPin = nullptr;
	}
}

bool SLensDataPointDialog::IsTrackingDataOverrided(int32 Index) const
{
	if (ensure(Index >= 0 && Index <= 1))
	{
		return TrackingInputData[Index].bIsOverrided;
	}

	return false;
}
void SLensDataPointDialog::OnOverrideTrackingData(ECheckBoxState NewState, int32 Index)
{
	if (ensure(Index >= 0 && Index <= 1))
	{
		TrackingInputData[Index].bIsOverrided = NewState == ECheckBoxState::Checked;
	}
}

float SLensDataPointDialog::GetTrackingData(int32 Index) const
{
	if (ensure(Index >= 0 && Index <= 1))
	{
		return TrackingInputData[Index].Value;
	}

	return 0.0f;
}

void SLensDataPointDialog::SetTrackingData(float Value, int32 Index)
{
	if (ensure(Index >= 0 && Index <= 1))
	{
		TrackingInputData[Index].Value = Value;
	}
}

void SLensDataPointDialog::RefreshEvaluationData()
{
	//Get FIZ coming from LiveLink. If normalized values are available, take them. Otherwise, look for precalibrated values or default to 0.0f
	const FCachedFIZData& CachedData = CachedFIZ.Get();
	const float Focus = CachedData.NormalizedFocus.IsSet() ? CachedData.NormalizedFocus.GetValue() : CachedData.Focus.IsSet() ? CachedData.Focus.GetValue() : 0.0f;
	const float Iris = CachedData.NormalizedIris.IsSet() ? CachedData.NormalizedIris.GetValue() : CachedData.Iris.IsSet() ? CachedData.Iris.GetValue() : 0.0f;
	const float Zoom = CachedData.NormalizedZoom.IsSet() ? CachedData.NormalizedZoom.GetValue() : CachedData.Zoom.IsSet() ? CachedData.Zoom.GetValue() : 0.0f;
	
	switch (SelectedCategory)
	{
		case EDataCategories::Focus:
		{
			if (IsTrackingDataOverrided(0) == false)
			{
				TrackingInputData[0].Value = Focus;
			}
			break;
		}
		case EDataCategories::Iris:
		{
			if (IsTrackingDataOverrided(0) == false)
			{
				TrackingInputData[0].Value = Iris;
			}
			break;
		}
		break;
		case EDataCategories::Zoom:
		{
			if (IsTrackingDataOverrided(0) == false)
			{
				TrackingInputData[0].Value = Zoom;
			}
			break;
		}
		break;
		case EDataCategories::Distortion:
		case EDataCategories::ImageCenter:
		case EDataCategories::NodalOffset:
		case EDataCategories::STMap:
		default:
		{
			if (IsTrackingDataOverrided(0) == false)
			{
				TrackingInputData[0].Value = Focus;
			}
			if (IsTrackingDataOverrided(1) == false)
			{
				TrackingInputData[1].Value = Zoom;
			}
			break;
		}
	}
}

void SLensDataPointDialog::AddDataToLensFile()
{
	//Logic to add points to LensFile is still todo. Requires updating LensFile API
	switch (SelectedCategory)
	{
		case EDataCategories::Focus:
		{
			LensFile->EncoderMapping.Focus.AddKey(TrackingInputData[0].Value, EncoderMappingValue);
			break;
		}
		case EDataCategories::Iris:
		{
			LensFile->EncoderMapping.Iris.AddKey(TrackingInputData[0].Value, EncoderMappingValue);
			break;
		}
		break;
		case EDataCategories::Zoom:
		{
			LensFile->EncoderMapping.Zoom.AddKey(TrackingInputData[0].Value, EncoderMappingValue);
			break;
		}
		break;
		case EDataCategories::Distortion:
		{
			break;
		}	
		case EDataCategories::ImageCenter:
		{
			break;
		}
		case EDataCategories::NodalOffset:
		{
			break;
		}
		case EDataCategories::STMap:
		{
			break;
		}
		default:
			break;
	}
}

#undef LOCTEXT_NAMESPACE 
