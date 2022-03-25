// Copyright Epic Games, Inc. All Rights Reserved.

#include "SNiagaraBakerWidget.h"
#include "SNiagaraBakerTimelineWidget.h"
#include "SNiagaraBakerViewport.h"
#include "NiagaraEditorCommon.h"
#include "NiagaraEditorStyle.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "ViewModels/NiagaraBakerViewModel.h"

#include "Modules/ModuleManager.h"
#include "Widgets/Input/SVectorInputBox.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SViewport.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "EditorWidgetsModule.h"
#include "ITransportControl.h"
#include "IDetailCustomization.h"
#include "PropertyEditorModule.h"

#define LOCTEXT_NAMESPACE "NiagaraBakerWidget"

//-TODO: Remove details panel and replace with customization to make more compact
//-TODO: Improve feedback & warning display
//-TODO: Throttle sim frames on warmup
//-TODO: Turn off playback when out of focus
//-TODO: Fluid systems stop simulation with to previous?
//-TODO: First frame black even though I have non zero start seconds
//-TODO: None should display HDR or something sensible
//-TODO: Support particle attribute generation on GPU
//-TODO: Hook up loading a preview environment / sequence
//-TODO: Add custom post processes for capturing data?

//////////////////////////////////////////////////////////////////////////

namespace NiagaraBakerWidgetLocal
{
	template<typename TType>
	TSharedRef<SWidget> MakeSpinBox(TOptional<TType> MinValue, TOptional<TType> MaxValue, TOptional<TType> MinSliderValue, TOptional<TType> MaxSliderValue, TAttribute<TType> GetValue, typename SSpinBox<TType>::FOnValueChanged SetValue)
	{
		return SNew(SBox)
		.HAlign(HAlign_Right)
		[
			SNew(SBox)
			.Padding(FMargin(4.0f, 0.0f, 0.0f, 0.0f))
			.WidthOverride(100.0f)
			[
				SNew(SSpinBox<float>)
				.Font(FEditorStyle::GetFontStyle(TEXT("MenuItem.Font")))
				.MinValue(MinValue)
				.MaxValue(MaxValue)
				.MinSliderValue(MinSliderValue)
				.MaxSliderValue(MaxSliderValue)
				.Value(GetValue)
				.OnValueChanged(SetValue)
			]
		];
	}

	template<typename TType, int NumElements>
	struct FMakeVectorBoxHelper
	{
		DECLARE_DELEGATE_RetVal(TType, FGetter);
		DECLARE_DELEGATE_OneParam(FSetter, TType);

		static TSharedRef<SWidget> Construct(FGetter GetValue, FSetter SetValue, TOptional<TType> MinValue = TOptional<TType>(), TOptional<TType> MaxValue = TOptional<TType>(), TOptional<TType> MinSliderValue = TOptional<TType>(), TOptional<TType> MaxSliderValue = TOptional<TType>())
		{
			TSharedRef<SHorizontalBox> HorizontalBox = SNew(SHorizontalBox);

			for ( int i=0; i < NumElements; ++i )
			{
				HorizontalBox->AddSlot()
				.FillWidth(1.0f / float(NumElements))
				.MaxWidth(60.0f)
				[
					SNew(SSpinBox<float>)
					.Font(FEditorStyle::GetFontStyle(TEXT("MenuItem.Font")))
					.MinValue(MinValue.IsSet() ? MinValue.GetValue()[i] : TOptional<float>())
					.MaxValue(MaxValue.IsSet() ? MaxValue.GetValue()[i] : TOptional<float>())
					.MinSliderValue(MinSliderValue.IsSet() ? MinSliderValue.GetValue()[i] : TOptional<float>())
					.MaxSliderValue(MaxSliderValue.IsSet() ? MaxSliderValue.GetValue()[i] : TOptional<float>())
					.Value_Lambda([=]() { return GetValue.Execute()[i]; })
					.OnValueChanged_Lambda([=](float InValue) { FVector VectorValue = GetValue.Execute(); VectorValue[i] = InValue; SetValue.Execute(VectorValue); })
				];
			}

			return
				SNew(SBox)
				.WidthOverride(180.0f)
				.HAlign(HAlign_Right)
				[
					HorizontalBox
				];
		}
	};

	using FMakeVectorBox = FMakeVectorBoxHelper<FVector, 3>;

	struct FMakeRotatorBox
	{
		DECLARE_DELEGATE_RetVal(FRotator, FGetter);
		DECLARE_DELEGATE_OneParam(FSetter, FRotator);

		static TSharedRef<SWidget> Construct(FGetter GetValue, FSetter SetValue)
		{
			return
				SNew(SBox)
				.WidthOverride(180.0f)
				.HAlign(HAlign_Right)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f / float(3.0f))
					.MaxWidth(60.0f)
					[
						SNew(SSpinBox<float>)
						.Font(FEditorStyle::GetFontStyle(TEXT("MenuItem.Font")))
						.Value_Lambda([=]() { return GetValue.Execute().Pitch; })
						.OnValueChanged_Lambda([=](float InValue) { FRotator VectorValue = GetValue.Execute(); VectorValue.Pitch = InValue; SetValue.Execute(VectorValue); })
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f / float(3.0f))
					.MaxWidth(60.0f)
					[
						SNew(SSpinBox<float>)
						.Font(FEditorStyle::GetFontStyle(TEXT("MenuItem.Font")))
						.Value_Lambda([=]() { return GetValue.Execute().Yaw; })
						.OnValueChanged_Lambda([=](float InValue) { FRotator VectorValue = GetValue.Execute(); VectorValue.Yaw = InValue; SetValue.Execute(VectorValue); })
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f / float(3.0f))
					.MaxWidth(60.0f)
					[
						SNew(SSpinBox<float>)
						.Font(FEditorStyle::GetFontStyle(TEXT("MenuItem.Font")))
						.Value_Lambda([=]() { return GetValue.Execute().Roll; })
						.OnValueChanged_Lambda([=](float InValue) { FRotator VectorValue = GetValue.Execute(); VectorValue.Roll = InValue; SetValue.Execute(VectorValue); })
					]
				];
		}
	};

	TSharedRef<SWidget> MakeChannelWidget(TWeakPtr<FNiagaraBakerViewModel> WeakViewModel)
	{
		TSharedRef<SHorizontalBox> HorizontalBox = SNew(SHorizontalBox);

		static const FText ChannelLetter[int(ENiagaraBakerColorChannel::Num)] = { FText::FromString("R"), FText::FromString("G"), FText::FromString("B"), FText::FromString("A") };

		for ( int32 i=0; i < int(ENiagaraBakerColorChannel::Num); ++i )
		{
			const ENiagaraBakerColorChannel Channel = ENiagaraBakerColorChannel(i);

			HorizontalBox->AddSlot()
			.VAlign(VAlign_Center)
			.Padding(2.0f)
			.AutoWidth()
			[
				SNew(SCheckBox)
				.Style(FAppStyle::Get(), "TextureEditor.ChannelButtonStyle")
				//.BorderBackgroundColor_Lambda(
				//	[WeakViewModel, Channel]() -> FLinearColor
				//	{
				//		FNiagaraBakerViewModel* ViewModel = WeakViewModel.Pin().Get();
				//		return ViewModel && ViewModel->IsChannelEnabled(Channel) ? FLinearColor::Gray : FLinearColor::Gray;
				//	}
				//)
				.ForegroundColor_Lambda(
					[WeakViewModel, Channel]() -> FLinearColor
					{
						FNiagaraBakerViewModel* ViewModel = WeakViewModel.Pin().Get();
						if ( ViewModel && ViewModel->IsChannelEnabled(Channel) )
						{
							switch (Channel)
							{
								case ENiagaraBakerColorChannel::Red:	return FLinearColor::Red;
								case ENiagaraBakerColorChannel::Green:	return FLinearColor::Green;
								case ENiagaraBakerColorChannel::Blue:	return FLinearColor::Blue;
								case ENiagaraBakerColorChannel::Alpha:	return FLinearColor::White;
							}
						}
						return FLinearColor::Black;
					}
				)
				.OnCheckStateChanged_Lambda(
					[WeakViewModel, Channel](ECheckBoxState CheckState)
					{
						if ( FNiagaraBakerViewModel* ViewModel = WeakViewModel.Pin().Get() )
						{
							ViewModel->SetChannelEnabled(Channel, CheckState == ECheckBoxState::Checked);
						}
					}
				)
				.IsChecked_Lambda(
					[WeakViewModel, Channel]() -> ECheckBoxState
					{
						FNiagaraBakerViewModel* ViewModel = WeakViewModel.Pin().Get();
						const bool bEnabled = ViewModel ? ViewModel->IsChannelEnabled(Channel) : false;
						return bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
					}
				)
				[
					SNew(STextBlock)
					.Font(FAppStyle::Get().GetFontStyle("TextureEditor.ChannelButtonFont"))
					.Text(ChannelLetter[i])
				]
			];
		}

		return HorizontalBox;
	}
}

//////////////////////////////////////////////////////////////////////////

class FBakerSettingsDetails : public IDetailCustomization
{
public:
	FBakerSettingsDetails(TWeakPtr<FNiagaraBakerViewModel> InWeakViewModel)
		: WeakViewModel(InWeakViewModel)
	{
	}

	static TSharedRef<IDetailCustomization> MakeInstance(TWeakPtr<FNiagaraBakerViewModel> InWeakViewModel)
	{
		return MakeShared<FBakerSettingsDetails>(InWeakViewModel);
	}

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override
	{
		// We only support customization on 1 object
		TArray<TWeakObjectPtr<UObject>> ObjectsCustomized;
		DetailBuilder.GetObjectsBeingCustomized(ObjectsCustomized);
		if (ObjectsCustomized.Num() != 1 || !ObjectsCustomized[0]->IsA<UNiagaraBakerSettings>())
		{
			return;
		}

		FNiagaraBakerViewModel* ViewModel = WeakViewModel.Pin().Get();

		UNiagaraBakerSettings* BakerSettings = CastChecked<UNiagaraBakerSettings>(ObjectsCustomized[0]);

		// Func for only showing information for current camera
		auto PerCameraProperty =
			[&DetailBuilder, BakerSettings](IDetailCategoryBuilder& DetailCategory, TSharedPtr<IPropertyHandle> PropertyHandle, FText DisplayName, FText ToolTip)
			{
				TSharedPtr<IPropertyHandleArray> ArrayPropertyHandle = PropertyHandle->AsArray();
				if (ArrayPropertyHandle == nullptr)
				{
					return;
				}
				uint32 NumElements = 0;
				if ( ArrayPropertyHandle->GetNumElements(NumElements) != FPropertyAccess::Success )
				{
					return;
				}

				DetailBuilder.HideProperty(PropertyHandle->AsShared());
				for (uint32 i=0; i < NumElements; ++i)
				{
					TSharedRef<IPropertyHandle> ArrayValuePropertyHandle = ArrayPropertyHandle->GetElement(i);

					DetailCategory.AddProperty(ArrayValuePropertyHandle)
						.DisplayName(DisplayName)
						.ToolTip(ToolTip)
						.Visibility(TAttribute<EVisibility>::CreateLambda([=] { return uint32(BakerSettings->CameraViewportMode) == i ? EVisibility::Visible : EVisibility::Hidden; }));
				}
			};

		// Settings
		{
			IDetailCategoryBuilder& DetailCategory = DetailBuilder.EditCategory(FName(TEXT("Settings")));

			DetailCategory.AddProperty(GET_MEMBER_NAME_CHECKED(UNiagaraBakerSettings, StartSeconds));
			DetailCategory.AddProperty(GET_MEMBER_NAME_CHECKED(UNiagaraBakerSettings, DurationSeconds));
			DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UNiagaraBakerSettings, FramesPerSecond));
			DetailCategory.AddProperty(GET_MEMBER_NAME_CHECKED(UNiagaraBakerSettings, FramesPerDimension));

			DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UNiagaraBakerSettings, CameraViewportMode));
			DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UNiagaraBakerSettings, CameraViewportLocation));
			DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UNiagaraBakerSettings, CameraViewportRotation));
			DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UNiagaraBakerSettings, CameraOrbitDistance));
			DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UNiagaraBakerSettings, CameraFOV));
			DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UNiagaraBakerSettings, CameraOrthoWidth));
			DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UNiagaraBakerSettings, CameraAspectRatio));

			DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UNiagaraBakerSettings, bPreviewLooping));
			DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UNiagaraBakerSettings, bRenderComponentOnly));

			DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UNiagaraBakerSettings, OutputTextures));
		}

		// Output Settings
		if ( BakerSettings->OutputTextures.IsValidIndex(ViewModel->GetCurrentOutputIndex()) )
		{
			const FNiagaraBakerTextureSettings& OutputTexture = BakerSettings->OutputTextures[ViewModel->GetCurrentOutputIndex()];

			IDetailCategoryBuilder& OutputTextureCategory = DetailBuilder.EditCategory(FName("OutputSettings"));

			TSharedPtr<IPropertyHandleArray> OutputArrayPropertyHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UNiagaraBakerSettings, OutputTextures))->AsArray();
			TSharedPtr<IPropertyHandle> OutputPropertyHandle = OutputArrayPropertyHandle->GetElement(ViewModel->GetCurrentOutputIndex());
			OutputTextureCategory.AddProperty(OutputPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FNiagaraBakerTextureSettings, OutputName)));
			OutputTextureCategory.AddProperty(OutputPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FNiagaraBakerTextureSettings, SourceBinding)));
			OutputTextureCategory.AddProperty(OutputPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FNiagaraBakerTextureSettings, TextureSize)));
			OutputTextureCategory.AddProperty(OutputPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FNiagaraBakerTextureSettings, FrameSize)));
			OutputTextureCategory.AddProperty(OutputPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FNiagaraBakerTextureSettings, GeneratedTexture)));
		}
	}

	TWeakPtr<FNiagaraBakerViewModel> WeakViewModel;
};

//////////////////////////////////////////////////////////////////////////

void SNiagaraBakerWidget::Construct(const FArguments& InArgs)
{
	WeakViewModel = InArgs._WeakViewModel;
	FNiagaraBakerViewModel* ViewModel = WeakViewModel.Pin().Get();
	check(ViewModel);

	OnCurrentOutputIndexChangedHandle = ViewModel->OnCurrentOutputChanged.AddSP(this, &SNiagaraBakerWidget::RefreshWidget);

	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FEditorWidgetsModule& EditorWidgetsModule = FModuleManager::Get().LoadModuleChecked<FEditorWidgetsModule>("EditorWidgets");

	// Baker Toolbar
	FSlimHorizontalToolBarBuilder BakerToolbarBuilder(MakeShareable(new FUICommandList), FMultiBoxCustomization::None);
	BakerToolbarBuilder.AddToolBarButton(
		FUIAction(FExecuteAction::CreateLambda([=]() { OnCapture(); })),
		NAME_None,
		FText::GetEmpty(),	//LOCTEXT("Bake", "Bake"),
		LOCTEXT("BakeTooltip", "Run the bake process"),
		FSlateIcon(FNiagaraEditorStyle::Get().GetStyleSetName(), "NiagaraEditor.BakerIcon")
	);

	BakerToolbarBuilder.AddComboButton(
		FUIAction(),
		FOnGetContent::CreateSP(this, &SNiagaraBakerWidget::MakeCameraModeMenu),
		TAttribute<FText>::CreateSP(ViewModel, &FNiagaraBakerViewModel::GetCurrentCameraModeText),
		LOCTEXT("CameraModeToolTip", "Change the camera used to render from"),
		TAttribute<FSlateIcon>::CreateSP(ViewModel, &FNiagaraBakerViewModel::GetCurrentCameraModeIcon)
	);

	BakerToolbarBuilder.AddComboButton(
		FUIAction(),
		FOnGetContent::CreateSP(this, &SNiagaraBakerWidget::MakeViewOptionsMenu),
		FText::GetEmpty(),
		LOCTEXT("ViewOptionsToolTip", "Modify view options"),
		FSlateIcon(FAppStyle::Get().GetStyleSetName(), "Icons.Visibility")
	);

	// Outputs
	BakerToolbarBuilder.BeginSection(NAME_None);
	{
		BakerToolbarBuilder.AddComboButton(
			FUIAction(),
			FOnGetContent::CreateSP(this, &SNiagaraBakerWidget::MakeOutputMenu),
			TAttribute<FText>::CreateSP(ViewModel, &FNiagaraBakerViewModel::GetCurrentOutputText),
			LOCTEXT("CurrentOutputTooltip", "Select which output is currently visible in the preview area"),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Edit")
		);
		BakerToolbarBuilder.AddToolBarButton(
			FUIAction(FExecuteAction::CreateSP(ViewModel, &FNiagaraBakerViewModel::AddOutput)),
			NAME_None,
			FText::GetEmpty(),
			LOCTEXT("AddOutputTooltip", "Add a new output"),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Plus")
		);
		BakerToolbarBuilder.AddToolBarButton(
			FUIAction(
				FExecuteAction::CreateSP(ViewModel, &FNiagaraBakerViewModel::RemoveCurrentOutput),
				FCanExecuteAction::CreateSP(ViewModel, &FNiagaraBakerViewModel::CanRemoveCurrentOutput)
			),
			NAME_None,
			FText::GetEmpty(),
			LOCTEXT("RemoveOutputTooltip", "Delete the currently selected output"),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Delete")
		);
	}
	BakerToolbarBuilder.EndSection();

	// Color Channels
	BakerToolbarBuilder.BeginSection(NAME_None);
	{
		BakerToolbarBuilder.AddWidget(NiagaraBakerWidgetLocal::MakeChannelWidget(WeakViewModel));
	}
	BakerToolbarBuilder.EndSection();

	// Warnings
	BakerToolbarBuilder.BeginSection(NAME_None);
	{
		BakerToolbarBuilder.AddComboButton(
			FUIAction(
				FExecuteAction(),
				FCanExecuteAction(),
				FIsActionChecked(),
				FIsActionButtonVisible::CreateSP(this, &SNiagaraBakerWidget::HasWarnings)
			),
			FOnGetContent::CreateSP(this, &SNiagaraBakerWidget::MakeWarningsMenu),
			LOCTEXT("Warnings", "Warnings"),
			LOCTEXT("WarningsToolTip", "Contains any warnings that may need attention"),
			FSlateIcon(FAppStyle::Get().GetStyleSetName(), "Icons.WarningWithColor")
		);
	}
	BakerToolbarBuilder.EndSection();


	// Baker Settings
	FDetailsViewArgs DetailsArgs;
	DetailsArgs.bHideSelectionTip = true;
	DetailsArgs.bAllowSearch = false;

	FOnGetDetailCustomizationInstance BakerSettingsCustomizeDetails = FOnGetDetailCustomizationInstance::CreateStatic(&FBakerSettingsDetails::MakeInstance, WeakViewModel);
	BakerSettingsDetails = PropertyModule.CreateDetailView(DetailsArgs);
	BakerSettingsDetails->RegisterInstancedCustomPropertyLayout(UNiagaraBakerSettings::StaticClass(), BakerSettingsCustomizeDetails);

	// Transport control args
	{
		FTransportControlArgs TransportControlArgs;
		TransportControlArgs.OnGetPlaybackMode.BindLambda([&]() -> EPlaybackMode::Type { return bIsPlaying ? EPlaybackMode::PlayingForward : EPlaybackMode::Stopped; } );
		TransportControlArgs.OnBackwardEnd.BindSP(this, &SNiagaraBakerWidget::OnTransportBackwardEnd);
		TransportControlArgs.OnBackwardStep.BindSP(this, &SNiagaraBakerWidget::OnTransportBackwardStep);
		TransportControlArgs.OnForwardPlay.BindSP(this, &SNiagaraBakerWidget::OnTransportForwardPlay);
		TransportControlArgs.OnForwardStep.BindSP(this, &SNiagaraBakerWidget::OnTransportForwardStep);
		TransportControlArgs.OnForwardEnd.BindSP(this, &SNiagaraBakerWidget::OnTransportForwardEnd);
		TransportControlArgs.OnToggleLooping.BindSP(this, &SNiagaraBakerWidget::OnTransportToggleLooping);
		TransportControlArgs.OnGetLooping.BindSP(ViewModel, &FNiagaraBakerViewModel::IsPlaybackLooping);

		TransportControlArgs.WidgetsToCreate.Add(FTransportControlWidget(ETransportControlWidgetType::BackwardEnd));
		TransportControlArgs.WidgetsToCreate.Add(FTransportControlWidget(ETransportControlWidgetType::BackwardStep));
		TransportControlArgs.WidgetsToCreate.Add(FTransportControlWidget(ETransportControlWidgetType::ForwardPlay));
		TransportControlArgs.WidgetsToCreate.Add(FTransportControlWidget(ETransportControlWidgetType::ForwardStep));
		TransportControlArgs.WidgetsToCreate.Add(FTransportControlWidget(ETransportControlWidgetType::ForwardEnd));
		TransportControlArgs.WidgetsToCreate.Add(FTransportControlWidget(ETransportControlWidgetType::Loop));

		TransportControlArgs.bAreButtonsFocusable = false;

		TransportControls = EditorWidgetsModule.CreateTransportControl(TransportControlArgs);
	}

	//////////////////////////////////////////////////////////////////////////
	// Widgets
	this->ChildSlot
	[
		SNew(SVerticalBox)
		+SVerticalBox::Slot()
		.AutoHeight()
		[
			BakerToolbarBuilder.MakeWidget()
		]
		+SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SSplitter)
			.Orientation(EOrientation::Orient_Vertical)
			.PhysicalSplitterHandleSize(1.0f)
			.HitDetectionSplitterHandleSize(5.0f)
			.ResizeMode(ESplitterResizeMode::Fill)
			+ SSplitter::Slot()
			.Value(0.70f)
			[
				SNew(SOverlay)
				+ SOverlay::Slot()
				[
					SAssignNew(ViewportWidget, SNiagaraBakerViewport)
					.WeakViewModel(WeakViewModel)
				]
			]
			+SSplitter::Slot()
			.Value(0.30f)
			[
				BakerSettingsDetails.ToSharedRef()
			]
		]
		+SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.AutoWidth()
			[
				TransportControls.ToSharedRef()
			]
			+SHorizontalBox::Slot()
			[
				SAssignNew(TimelineWidget, SNiagaraBakerTimelineWidget)
				.WeakViewModel(WeakViewModel)
			]
		]
	];

	RefreshWidget();
}

SNiagaraBakerWidget::~SNiagaraBakerWidget()
{
	if ( FNiagaraBakerViewModel* ViewModel = WeakViewModel.Pin().Get() )
	{
		ViewModel->OnCurrentOutputChanged.Remove(OnCurrentOutputIndexChangedHandle);
	}
}

TSharedRef<SWidget> SNiagaraBakerWidget::MakeCameraModeMenu()
{
	FNiagaraBakerViewModel* ViewModel = WeakViewModel.Pin().Get();
	check(ViewModel != nullptr);

	FMenuBuilder MenuBuilder(true, nullptr);

	for (int i = 0; i < int(ENiagaraBakerViewMode::Num); ++i)
	{
		MenuBuilder.AddMenuEntry(
			FNiagaraBakerViewModel::GetCameraModeText(ENiagaraBakerViewMode(i)),
			FText::GetEmpty(),
			FNiagaraBakerViewModel::GetCameraModeIcon(ENiagaraBakerViewMode(i)),
			FUIAction(
				FExecuteAction::CreateSP(ViewModel, &FNiagaraBakerViewModel::SetCameraViewMode, ENiagaraBakerViewMode(i)),
				FCanExecuteAction(),
				FIsActionChecked::CreateSP(ViewModel, &FNiagaraBakerViewModel::IsCameraViewMode, ENiagaraBakerViewMode(i))
			),
			NAME_None,
			EUserInterfaceActionType::ToggleButton
		);
	}

	return MenuBuilder.MakeWidget();
}

TSharedRef<SWidget> SNiagaraBakerWidget::MakeViewOptionsMenu()
{
	using namespace NiagaraBakerWidgetLocal;

	FNiagaraBakerViewModel* ViewModel = WeakViewModel.Pin().Get();
	check(ViewModel != nullptr);

	FMenuBuilder MenuBuilder(true, nullptr);

	MenuBuilder.BeginSection(NAME_None, LOCTEXT("PreviewSettings", "Preview Settings"));
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("ShowRealtimePreview", "Show Realtime Preview"),
			LOCTEXT("ShowRealtimePreviewTooltip", "When enabled shows a live preview of what will be rendered, this may not be accurate with all visualization modes."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(ViewModel, &FNiagaraBakerViewModel::ToggleRealtimePreview),
				FCanExecuteAction(),
				FIsActionChecked::CreateSP(ViewModel, &FNiagaraBakerViewModel::ShowRealtimePreview)
			),
			NAME_None,
			EUserInterfaceActionType::ToggleButton
		);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("ShowBakedView", "Show Baked View"),
			LOCTEXT("ShowBakedViewTooltip", "When enabled shows the baked texture."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(ViewModel, &FNiagaraBakerViewModel::ToggleBakedView),
				FCanExecuteAction(),
				FIsActionChecked::CreateSP(ViewModel, &FNiagaraBakerViewModel::ShowBakedView)
			),
			NAME_None,
			EUserInterfaceActionType::ToggleButton
		);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("ShowCheckerboard", "Show Checkerboard"),
			LOCTEXT("ShowCheckerboardTooltip", "Show a checkerboard rather than a solid color to easily visualize alpha blending."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(ViewModel, &FNiagaraBakerViewModel::ToggleCheckerboardEnabled),
				FCanExecuteAction(),
				FIsActionChecked::CreateSP(ViewModel, &FNiagaraBakerViewModel::IsCheckerboardEnabled)
			),
			NAME_None,
			EUserInterfaceActionType::ToggleButton
		);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("ShowInfoText", "Show Info Text"),
			LOCTEXT("ShowInfoTextTooltip", "Shows information about the preview and baked outputs."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(ViewModel, &FNiagaraBakerViewModel::ToggleInfoText),
				FCanExecuteAction(),
				FIsActionChecked::CreateSP(ViewModel, &FNiagaraBakerViewModel::ShowInfoText)
			),
			NAME_None,
			EUserInterfaceActionType::ToggleButton
		);

		//DetailCategory.HideProperty(GET_MEMBER_NAME_CHECKED(UNiagaraBakerSettings, bPreviewLooping));
		//DetailCategory.HideProperty(GET_MEMBER_NAME_CHECKED(UNiagaraBakerSettings, bRenderComponentOnly));
	}

	MenuBuilder.BeginSection(NAME_None, LOCTEXT("Camera", "Camera"));
	{
		MenuBuilder.AddWidget(
			FMakeVectorBox::Construct(
				FMakeVectorBox::FGetter::CreateSP(ViewModel, &FNiagaraBakerViewModel::GetCurrentCameraLocation),
				FMakeVectorBox::FSetter::CreateSP(ViewModel, &FNiagaraBakerViewModel::SetCurrentCameraLocation)
			),
			LOCTEXT("CameraLocation", "Camera Location")
		);

		MenuBuilder.AddWidget(
			FMakeRotatorBox::Construct(
				FMakeRotatorBox::FGetter::CreateSP(ViewModel, &FNiagaraBakerViewModel::GetCurrentCameraRotation),
				FMakeRotatorBox::FSetter::CreateSP(ViewModel, &FNiagaraBakerViewModel::SetCurrentCameraRotation)
			),
			LOCTEXT("CameraLocation", "Camera Rotation")
		);

		if ( ViewModel->IsCameraViewMode(ENiagaraBakerViewMode::Perspective) )
		{
			MenuBuilder.AddWidget(
				MakeSpinBox<float>(1.0f, 170.0f, 1.0f, 170.0f, TAttribute<float>::CreateSP(ViewModel, &FNiagaraBakerViewModel::GetCameraFOV), SSpinBox<float>::FOnValueChanged::CreateSP(ViewModel, &FNiagaraBakerViewModel::SetCameraFOV)),
				LOCTEXT("FOVAngle", "Field of View (H)")
			);

			MenuBuilder.AddWidget(
				MakeSpinBox<float>(0.0f, TOptional<float>(), 0.0f, 1000.0f, TAttribute<float>::CreateSP(ViewModel, &FNiagaraBakerViewModel::GetCameraOrbitDistance), SSpinBox<float>::FOnValueChanged::CreateSP(ViewModel, &FNiagaraBakerViewModel::SetCameraOrbitDistance)),
				LOCTEXT("OrbitDistance", "Orbit Distance")
			);
		}
		else
		{
			MenuBuilder.AddWidget(
				MakeSpinBox<float>(0.1f, TOptional<float>(), 0.1f, 1000.0f, TAttribute<float>::CreateSP(ViewModel, &FNiagaraBakerViewModel::GetCameraOrthoWidth), SSpinBox<float>::FOnValueChanged::CreateSP(ViewModel, &FNiagaraBakerViewModel::SetCameraOrthoWidth)),
				LOCTEXT("CameraOrthoWidth", "Camera Orthographic Width")
			);
		}

		//-TODO: Clean this up
		MenuBuilder.AddMenuEntry(
			LOCTEXT("CustomAspectRatio", "Custom Aspect Ratio"),
			LOCTEXT("CustomAspectRatioTooltip", "Use a custom aspect ratio to render with."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(ViewModel, &FNiagaraBakerViewModel::ToggleCameraAspectRatioEnabled),
				FCanExecuteAction(),
				FIsActionChecked::CreateSP(ViewModel, &FNiagaraBakerViewModel::IsCameraAspectRatioEnabled)
			),
			NAME_None,
			EUserInterfaceActionType::ToggleButton
		);

		if ( ViewModel->IsCameraAspectRatioEnabled() )
		{
			MenuBuilder.AddWidget(
				MakeSpinBox<float>(0.1f, 5.0f, 0.1f, 5.0f, TAttribute<float>::CreateSP(ViewModel, &FNiagaraBakerViewModel::GetCameraAspectRatio), SSpinBox<float>::FOnValueChanged::CreateSP(ViewModel, &FNiagaraBakerViewModel::SetCameraAspectRatio)),
				LOCTEXT("CameraAspectRatio", "Camera Aspect Ratio")
			);
		}
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

TSharedRef<SWidget> SNiagaraBakerWidget::MakeOutputMenu()
{
	FNiagaraBakerViewModel* ViewModel = WeakViewModel.Pin().Get();
	check(ViewModel != nullptr);

	FMenuBuilder MenuBuilder(true, nullptr);

	if ( UNiagaraBakerSettings* BakerSettings = ViewModel->GetBakerSettings() )
	{
		for (int i = 0; i < BakerSettings->OutputTextures.Num(); ++i)
		{
			MenuBuilder.AddMenuEntry(
				ViewModel->GetOutputText(i),
				FText::GetEmpty(),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateSP(ViewModel, &FNiagaraBakerViewModel::SetCurrentOutputIndex, i),
					FCanExecuteAction(),
					FIsActionChecked::CreateSP(ViewModel, &FNiagaraBakerViewModel::IsCurrentOutputIndex, i)
				),
				NAME_None,
				EUserInterfaceActionType::ToggleButton
			);
		}
	}

	return MenuBuilder.MakeWidget();
}

bool SNiagaraBakerWidget::FindWarnings(TArray<FText>* OutWarnings) const
{
	if ( FNiagaraBakerViewModel* ViewModel = WeakViewModel.Pin().Get() )
	{
		// Check determinism
		if ( UNiagaraComponent* PreviewComponent = ViewModel->GetPreviewComponent() )
		{
			if ( UNiagaraSystem* NiagaraSystem = PreviewComponent->GetAsset() )
			{
				if ( NiagaraSystem->NeedsDeterminism() == false )
				{
					if ( OutWarnings == nullptr )
					{
						return true;
					}
					OutWarnings->Emplace(LOCTEXT("SystemDeterminism", "System is not set to deterministic, results will vary each bake"));
				}
				for (const FNiagaraEmitterHandle& EmitterHandle : NiagaraSystem->GetEmitterHandles() )
				{
					UNiagaraEmitter* NiagaraEmitter = EmitterHandle.GetInstance();
					if (NiagaraEmitter == nullptr || NiagaraEmitter->bDeterminism)
					{
						continue;
					}

					if (OutWarnings == nullptr)
					{
						return true;
					}
					OutWarnings->Emplace(FText::Format(LOCTEXT("EmitterDeterminismFormat", "Emitter '{0}' is not set to deterministic, results will vary each bake"), FText::FromString(NiagaraEmitter->GetUniqueEmitterName())));
				}
			}
		}

		// Check outputs divide correctly by atlas frame count
		if ( UNiagaraBakerSettings* BakerSettings = ViewModel->GetBakerSettings() )
		{
			for ( int32 i=0; i < BakerSettings->OutputTextures.Num(); ++i )
			{
				const FNiagaraBakerTextureSettings& TextureSettings = BakerSettings->OutputTextures[i];
				const bool bXSizeIssue = (TextureSettings.TextureSize.X % BakerSettings->FramesPerDimension.X) == 0;
				const bool bYSizeIssue = (TextureSettings.TextureSize.Y % BakerSettings->FramesPerDimension.Y) == 0;
				if (bXSizeIssue && bYSizeIssue)
				{
					continue;
				}

				if (OutWarnings == nullptr)
				{
					return true;
				}
				OutWarnings->Emplace(FText::Format(LOCTEXT("TextureSizeWarningText", "Output '{0}' frames per dimension {1}x{2} does not divide into texture size {3}x{4}, this can result in jitter on the flipbook if not taken into account"),
					ViewModel->GetOutputText(i),
					BakerSettings->FramesPerDimension.X, BakerSettings->FramesPerDimension.Y,
					TextureSettings.TextureSize.X, TextureSettings.TextureSize.Y
				));
			}
		}
	}

	return OutWarnings ? OutWarnings->Num() > 0 : false;
}

TSharedRef<SWidget> SNiagaraBakerWidget::MakeWarningsMenu()
{
	FNiagaraBakerViewModel* ViewModel = WeakViewModel.Pin().Get();
	check(ViewModel != nullptr);

	FMenuBuilder MenuBuilder(true, nullptr);

	TArray<FText> FoundWarnings;
	if ( FindWarnings(&FoundWarnings) )
	{
		for (const FText& Warning : FoundWarnings)
		{
			MenuBuilder.AddMenuEntry(
				Warning,
				FText::GetEmpty(),
				FSlateIcon(),
				FUIAction()
			);
		}
	}

	return MenuBuilder.MakeWidget();
}

void SNiagaraBakerWidget::RefreshWidget()
{
	BakerSettingsDetails->SetObject(GetBakerSettings(), true);
}

void SNiagaraBakerWidget::Tick(const FGeometry& AllottedGeometry, const double CurrentTime, const float DeltaTime)
{
	UNiagaraBakerSettings* BakerSettings = GetBakerSettings();
	if (BakerSettings == nullptr)
	{
		return;
	}

	const float DurationSeconds = BakerSettings->DurationSeconds;
	if ( DurationSeconds > 0.0f )
	{
		if (bIsPlaying)
		{
			PreviewRelativeTime += DeltaTime;
			if ( BakerSettings->bPreviewLooping )
			{
				PreviewRelativeTime = FMath::Fmod(PreviewRelativeTime, DurationSeconds);
			}
			else if ( PreviewRelativeTime >= DurationSeconds )
			{
				PreviewRelativeTime = DurationSeconds;
				bIsPlaying = false;
			}
		}
		else
		{
			PreviewRelativeTime = FMath::Min(PreviewRelativeTime, DurationSeconds);
		}

		ViewportWidget->RefreshView(PreviewRelativeTime, DeltaTime);
		TimelineWidget->SetRelativeTime(PreviewRelativeTime);
	}
}

void SNiagaraBakerWidget::SetPreviewRelativeTime(float RelativeTime)
{
	bIsPlaying = false;
	PreviewRelativeTime = RelativeTime;
}

FReply SNiagaraBakerWidget::OnCapture()
{
	if (auto ViewModel = WeakViewModel.Pin())
	{
		ViewModel->RenderBaker();
	}
	return FReply::Handled();
}

UNiagaraBakerSettings* SNiagaraBakerWidget::GetBakerSettings() const
{
	if (auto ViewModel = WeakViewModel.Pin())
	{
		return ViewModel->GetBakerSettings();
	}
	return nullptr;
}

const UNiagaraBakerSettings* SNiagaraBakerWidget::GetBakerGeneratedSettings() const
{
	if (auto ViewModel = WeakViewModel.Pin())
	{
		return ViewModel->GetBakerGeneratedSettings();
	}
	return nullptr;
}

FReply SNiagaraBakerWidget::OnTransportBackwardEnd()
{
	bIsPlaying = false;
	PreviewRelativeTime = 0.0f;

	return FReply::Handled();
}

FReply SNiagaraBakerWidget::OnTransportBackwardStep()
{
	bIsPlaying = false;
	if (auto ViewModel = WeakViewModel.Pin())
	{
		if(UNiagaraBakerSettings* Settings = ViewModel->GetBakerSettings())
		{
			const auto DisplayData = Settings->GetDisplayInfo(PreviewRelativeTime, Settings->bPreviewLooping);
			const int NumFrames = Settings->GetNumFrames();
			const int NewFrame = DisplayData.Interp > 0.25f ? DisplayData.FrameIndexA : FMath::Max(DisplayData.FrameIndexA - 1, 0);
			PreviewRelativeTime = (Settings->DurationSeconds / float(NumFrames)) * float(NewFrame);
		}
	}

	return FReply::Handled();
}

FReply SNiagaraBakerWidget::OnTransportForwardPlay()
{
	bIsPlaying = !bIsPlaying;
	return FReply::Handled();
}

FReply SNiagaraBakerWidget::OnTransportForwardStep()
{
	bIsPlaying = false;
	if (auto ViewModel = WeakViewModel.Pin())
	{
		if (UNiagaraBakerSettings* Settings = ViewModel->GetBakerSettings())
		{
			const auto DisplayData = Settings->GetDisplayInfo(PreviewRelativeTime, Settings->bPreviewLooping);
			const int NumFrames = Settings->GetNumFrames();
			const int NewFrame = DisplayData.Interp < 0.75f ? DisplayData.FrameIndexB : FMath::Min(DisplayData.FrameIndexB + 1, NumFrames - 1);
			PreviewRelativeTime = (Settings->DurationSeconds / float(NumFrames)) * float(NewFrame);
		}
	}

	return FReply::Handled();
}

FReply SNiagaraBakerWidget::OnTransportForwardEnd()
{
	bIsPlaying = false;
	if (UNiagaraBakerSettings* BakerSettings = GetBakerSettings())
	{
		PreviewRelativeTime = BakerSettings->DurationSeconds;
	}

	return FReply::Handled();
}

FReply SNiagaraBakerWidget::OnTransportToggleLooping() const
{
	if (auto ViewModel = WeakViewModel.Pin())
	{
		ViewModel->TogglePlaybackLooping();
	}
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
