// Copyright Epic Games, Inc. All Rights Reserved.

#include "SCameraCalibrationCurveEditorPanel.h"

#include "CurveEditor.h"
#include "CurveEditorCommands.h"
#include "SCurveKeyDetailPanel.h"
#include "SGridLineSpacingList.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Input/SComboButton.h"

#define LOCTEXT_NAMESPACE "CameraCalibrationCurveEditorPanel"

void SCameraCalibrationCurveEditorPanel::Construct(const FArguments& InArgs, TSharedRef<FCurveEditor> InCurveEditor)
{	
	SCurveEditorPanel::Construct(SCurveEditorPanel::FArguments(), InCurveEditor);

	// Cache the curve editor model
	CurveEditorWeakPtr = InCurveEditor;

	// Disable the Key Details all the time
	const TSharedPtr<SCurveKeyDetailPanel> KeyDetailsViewPtr = GetKeyDetailsView();
	check(KeyDetailsViewPtr.IsValid());
	KeyDetailsViewPtr->SetEnabled(false);
}

TSharedRef<SWidget> SCameraCalibrationCurveEditorPanel::MakeViewModeMenu()
{
	// This builds the dropdown menu when looking at the Curve View Modes
	FMenuBuilder MenuBuilder(true, GetCommands());

	MenuBuilder.AddMenuEntry(FCurveEditorCommands::Get().SetViewModeAbsolute);
	MenuBuilder.AddMenuEntry(FCurveEditorCommands::Get().SetViewModeStacked);
	MenuBuilder.AddMenuEntry(FCurveEditorCommands::Get().SetViewModeNormalized);

	return MenuBuilder.MakeWidget();
}

TSharedRef<SWidget> SCameraCalibrationCurveEditorPanel::MakeCurveEditorCurveViewOptionsMenu()
{
	const TSharedPtr<FCurveEditor> CurveEditorPtr = CurveEditorWeakPtr.Pin();
	check(CurveEditorPtr.IsValid());
	
	// This builds the dropdown menu when looking at the Curve View Options combobox.
	FMenuBuilder MenuBuilder(true, CurveEditorPtr->GetCommands());

	MenuBuilder.BeginSection("TangentVisibility", LOCTEXT("CurveEditorMenuTangentVisibilityHeader", "Tangent Visibility"));
	{
		MenuBuilder.AddMenuEntry(FCurveEditorCommands::Get().SetAllTangentsVisibility);
		MenuBuilder.AddMenuEntry(FCurveEditorCommands::Get().SetSelectedKeysTangentVisibility);
		MenuBuilder.AddMenuEntry(FCurveEditorCommands::Get().SetNoTangentsVisibility);
	}
	MenuBuilder.EndSection();

	MenuBuilder.AddMenuSeparator();
	MenuBuilder.AddMenuEntry(FCurveEditorCommands::Get().ToggleAutoFrameCurveEditor);
	MenuBuilder.AddMenuEntry(FCurveEditorCommands::Get().ToggleShowCurveEditorCurveToolTips);

	MenuBuilder.BeginSection("Organize", LOCTEXT("CurveEditorMenuOrganizeHeader", "Organize"));
	{
		MenuBuilder.AddMenuEntry(FCurveEditorCommands::Get().ToggleExpandCollapseNodes);
		MenuBuilder.AddMenuEntry(FCurveEditorCommands::Get().ToggleExpandCollapseNodesAndDescendants);
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

TSharedRef<SWidget> SCameraCalibrationCurveEditorPanel::MakeTimeSnapMenu()
{
	struct FPresetValue
	{
		FPresetValue(const int32 InPercentage, const FText& InText, const FText& InDescription)
			: Percentage(InPercentage)
			, Text(InText)
			, Description(InDescription)
		{}
		
		int32 Percentage = 0;
		FText Text;
		FText Description;
	};

	// Test Values before full implementation
	TArray<FPresetValue> PresetValues;
	PresetValues.Add({0, LOCTEXT("Snap_Input_Zero", "0%"), LOCTEXT("Snap_Input_Description_Zero", "Snap time values to 0%")});
	PresetValues.Add({25, LOCTEXT("Snap_Input_TwentyFive", "25%"), LOCTEXT("Snap_Input_Description_TwentyFive", "Snap time values to 25%")});
	PresetValues.Add({50, LOCTEXT("Snap_Input_Fifty", "50%"), LOCTEXT("Snap_Input_Description_Fifty", "Snap time values to50%")});
	PresetValues.Add({100, LOCTEXT("Snap_Input_OneHundred", "100%"), LOCTEXT("Snap_Input_Description_OneHundred", "Snap time values to 100%")});

	return SNew(SComboButton)
		.OnGetMenuContent_Lambda([this, PresetValues]()-> TSharedRef<SWidget>// copy PresetValues for testing
		{
			FMenuBuilder MenuBuilder(true, nullptr);

			for (const FPresetValue& PresetValue : PresetValues)
			{
				// No action for now
				FUIAction MenuAction;
				MenuBuilder.AddMenuEntry(
					PresetValue.Text,
					PresetValue.Description,
					FSlateIcon(),
					MenuAction,
					NAME_None,
					EUserInterfaceActionType::RadioButton
				);
			}
			
			return MenuBuilder.MakeWidget();
		})
		.ButtonContent()
		[
			SNew(STextBlock)
			.Text(PresetValues[0].Text)// First Value for now
		];
}

TSharedRef<SWidget> SCameraCalibrationCurveEditorPanel::MakeGridSpacingMenu()
{
	const TSharedPtr<FCurveEditor> CurveEditorPtr = CurveEditorWeakPtr.Pin();
	check(CurveEditorPtr.IsValid());
	
	TArray<SGridLineSpacingList::FNamedValue> SpacingAmounts;
	SpacingAmounts.Add(SGridLineSpacingList::FNamedValue(0.1f, LOCTEXT("OneTenth", "0.1"), LOCTEXT("Description_OneTenth", "Set grid spacing to 1/10th")));
	SpacingAmounts.Add(SGridLineSpacingList::FNamedValue(0.5f, LOCTEXT("OneHalf", "0.5"), LOCTEXT("Description_OneHalf", "Set grid spacing to 1/2")));
	SpacingAmounts.Add(SGridLineSpacingList::FNamedValue(1.0f, LOCTEXT("One", "1"), LOCTEXT("Description_One", "Set grid spacing to 1")));
	SpacingAmounts.Add(SGridLineSpacingList::FNamedValue(2.0f, LOCTEXT("Two", "2"), LOCTEXT("Description_Two", "Set grid spacing to 2")));
	SpacingAmounts.Add(SGridLineSpacingList::FNamedValue(5.0f, LOCTEXT("Five", "5"), LOCTEXT("Description_Five", "Set grid spacing to 5")));
	SpacingAmounts.Add(SGridLineSpacingList::FNamedValue(10.0f, LOCTEXT("Ten", "10"), LOCTEXT("Description_Ten", "Set grid spacing to 10")));
	SpacingAmounts.Add(SGridLineSpacingList::FNamedValue(50.0f, LOCTEXT("Fifty", "50"), LOCTEXT("Description_50", "Set grid spacing to 50")));
	SpacingAmounts.Add(SGridLineSpacingList::FNamedValue(100.0f, LOCTEXT("OneHundred", "100"), LOCTEXT("Description_OneHundred", "Set grid spacing to 100")));
	SpacingAmounts.Add(SGridLineSpacingList::FNamedValue(TOptional<float>(), LOCTEXT("Automatic", "Automatic"), LOCTEXT("Description_Automatic", "Set grid spacing to automatic")));

	TSharedRef<SWidget> OutputSnapWidget =
		SNew(SGridLineSpacingList)
		.DropDownValues(SpacingAmounts)
		.MinDesiredValueWidth(60)
		.Value_Lambda([CurveEditorPtr]() -> TOptional<float> { return CurveEditorPtr->FixedGridSpacingAttribute.Get(); })
		.OnValueChanged_Lambda([CurveEditorPtr](TOptional<float> InNewOutputSnap) { CurveEditorPtr->FixedGridSpacingAttribute = InNewOutputSnap; })
		.HeaderText(LOCTEXT("CurveEditorMenuGridSpacingHeader", "Grid Spacing"));

	return OutputSnapWidget;
}

TSharedPtr<FExtender> SCameraCalibrationCurveEditorPanel::GetToolbarExtender()
{
	// We're going to create a new Extender and add the main Curve Editor icons to it.
	// We combine this with the extender provided by the Curve Editor Module as that extender has been extended by tools
	ICurveEditorModule& CurveEditorModule = FModuleManager::Get().LoadModuleChecked<ICurveEditorModule>("CurveEditor");
	TArray<TSharedPtr<FExtender>> ToolbarExtenders;
	for (ICurveEditorModule::FCurveEditorMenuExtender& ExtenderCallback : CurveEditorModule.GetAllToolBarMenuExtenders())
	{
		ToolbarExtenders.Add(ExtenderCallback.Execute(GetCommands().ToSharedRef()));
	}
	TSharedPtr<FExtender> Extender = FExtender::Combine(ToolbarExtenders);

	struct Local
	{
		static void FillToolbar(FToolBarBuilder& ToolBarBuilder, TSharedRef<SCurveKeyDetailPanel> InKeyDetailsPanel, TSharedRef<SCameraCalibrationCurveEditorPanel> InEditorPanel)
		{
			ToolBarBuilder.BeginSection("View");
			{
				// Dropdown Menu for choosing your viewing mode
				TAttribute<FSlateIcon> ViewModeIcon;
				ViewModeIcon.Bind(TAttribute<FSlateIcon>::FGetter::CreateLambda([InEditorPanel] {
					switch (InEditorPanel->GetViewMode())
					{
					case ECurveEditorViewID::Absolute:
						return FCurveEditorCommands::Get().SetViewModeAbsolute->GetIcon();
					case ECurveEditorViewID::Stacked:
						return FCurveEditorCommands::Get().SetViewModeStacked->GetIcon();
					case ECurveEditorViewID::Normalized:
						return FCurveEditorCommands::Get().SetViewModeNormalized->GetIcon();
					default: // EKeyGroupMode::None
						return FCurveEditorCommands::Get().SetAxisSnappingNone->GetIcon();
					}
				}));

				ToolBarBuilder.AddComboButton(
					FUIAction(),
					FOnGetContent::CreateSP(InEditorPanel, &SCameraCalibrationCurveEditorPanel::MakeViewModeMenu),
					LOCTEXT("ViewModeDropdown", "Curve View Modes"),
					LOCTEXT("ViewModeDropdownToolTip", "Choose the viewing mode for the curves."),
					ViewModeIcon);
			}
			ToolBarBuilder.EndSection();

			ToolBarBuilder.BeginSection("Framing");
			{
				// Framing
				ToolBarBuilder.AddToolBarButton(FCurveEditorCommands::Get().ZoomToFit);
			}
			ToolBarBuilder.EndSection();

			ToolBarBuilder.BeginSection("Visibility");
			{
				// Curve Visibility
				ToolBarBuilder.AddComboButton(
					FUIAction(),
					FOnGetContent::CreateSP(InEditorPanel, &SCameraCalibrationCurveEditorPanel::MakeCurveEditorCurveViewOptionsMenu),
					LOCTEXT("CurveEditorCurveOptions", "Curves Options"),
					LOCTEXT("CurveEditorCurveOptionsToolTip", "Curve Options"),
					FSlateIcon(FEditorStyle::GetStyleSetName(), "GenericCurveEditor.VisibilityOptions"));

			}
			ToolBarBuilder.EndSection();

			ToolBarBuilder.BeginSection("Key Details");
			{
				ToolBarBuilder.AddWidget(InKeyDetailsPanel);
			}
			ToolBarBuilder.EndSection();

			ToolBarBuilder.BeginSection("Tools");
			{
				ToolBarBuilder.AddToolBarButton(FCurveEditorCommands::Get().DeactivateCurrentTool);
			}
			ToolBarBuilder.EndSection();
	
			ToolBarBuilder.BeginSection("Adjustment");
			{
				// Toggle Button for Time Snapping
				ToolBarBuilder.AddToolBarButton(FCurveEditorCommands::Get().ToggleInputSnapping);
				
				// Dropdown Menu to choose the snapping scale.
				const FUIAction TimeSnapMenuAction;
				ToolBarBuilder.AddComboButton(
					TimeSnapMenuAction,
					FOnGetContent::CreateSP(InEditorPanel, &SCameraCalibrationCurveEditorPanel::MakeTimeSnapMenu),
					LOCTEXT("TimeSnappingOptions", "Time Snapping"),
				    LOCTEXT("TimeSnappingOptionsToolTip", "Choose what precision the Time axis is snapped to while moving keys."),
					TAttribute<FSlateIcon>(),
					true);
				
				// Toggle Button for Value Snapping
				ToolBarBuilder.AddToolBarButton(FCurveEditorCommands::Get().ToggleOutputSnapping);
				
				// Dropdown Menu to choose the snapping scale.
				ToolBarBuilder.AddComboButton(
					FUIAction(),
					FOnGetContent::CreateSP(InEditorPanel, &SCameraCalibrationCurveEditorPanel::MakeGridSpacingMenu),
					LOCTEXT("GridSnappingOptions", "Grid Snapping"),
					LOCTEXT("GridSnappingOptionsToolTip", "Choose the spacing between horizontal grid lines."),
					TAttribute<FSlateIcon>(),
					true);
			}
			ToolBarBuilder.EndSection();
			ToolBarBuilder.BeginSection("Tangents");
			{
				ToolBarBuilder.AddToolBarButton(FCurveEditorCommands::Get().InterpolationCubicAuto);
				ToolBarBuilder.AddToolBarButton(FCurveEditorCommands::Get().InterpolationCubicUser);
				ToolBarBuilder.AddToolBarButton(FCurveEditorCommands::Get().InterpolationCubicBreak);
				ToolBarBuilder.AddToolBarButton(FCurveEditorCommands::Get().InterpolationLinear);
				ToolBarBuilder.AddToolBarButton(FCurveEditorCommands::Get().InterpolationConstant);
				ToolBarBuilder.AddToolBarButton(FCurveEditorCommands::Get().InterpolationToggleWeighted);
			}
			ToolBarBuilder.EndSection();

			ToolBarBuilder.BeginSection("Filters");
			{
				ToolBarBuilder.AddToolBarButton(FCurveEditorCommands::Get().FlattenTangents);
				ToolBarBuilder.AddToolBarButton(FCurveEditorCommands::Get().StraightenTangents);
				ToolBarBuilder.AddToolBarButton(FCurveEditorCommands::Get().OpenUserImplementableFilterWindow);
			}
			ToolBarBuilder.EndSection();
		}
	};

	const TSharedPtr<SCurveKeyDetailPanel> KeyDetailsViewPtr = GetKeyDetailsView();
	check(KeyDetailsViewPtr.IsValid());

	Extender->AddToolBarExtension(
		"Asset",
		EExtensionHook::After,
		GetCommands(),
		FToolBarExtensionDelegate::CreateStatic(&Local::FillToolbar, KeyDetailsViewPtr.ToSharedRef(), SharedThis(this))
	);

	return Extender;
}

#undef LOCTEXT_NAMESPACE