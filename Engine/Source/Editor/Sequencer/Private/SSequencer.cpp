// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SSequencer.h"
#include "Engine/Blueprint.h"
#include "MovieSceneSequence.h"
#include "Sections/MovieSceneSubSection.h"
#include "MovieScene.h"
#include "Framework/MultiBox/MultiBoxDefs.h"
#include "Widgets/Text/STextBlock.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "GameFramework/Actor.h"
#include "Misc/FeedbackContext.h"
#include "Modules/ModuleManager.h"
#include "Editor.h"
#include "IDetailsView.h"
#include "Widgets/Layout/SBorder.h"
#include "ISequencerEditTool.h"
#include "Layout/WidgetPath.h"
#include "Framework/Application/MenuStack.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SScrollBar.h"
#include "Widgets/Layout/SScrollBorder.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Docking/SDockTab.h"
#include "EditorStyleSet.h"
#include "Engine/Selection.h"
#include "LevelEditorViewport.h"
#include "Widgets/Navigation/SBreadcrumbTrail.h"
#include "SequencerCommands.h"
#include "DisplayNodes/SequencerObjectBindingNode.h"
#include "DisplayNodes/SequencerTrackNode.h"
#include "Widgets/Input/SNumericDropDown.h"
#include "SequencerCommonHelpers.h"
#include "SSequencerLabelBrowser.h"
#include "ISequencerWidgetsModule.h"
#include "ScopedTransaction.h"
#include "SequencerTimeSliderController.h"
#include "SSequencerSectionOverlay.h"
#include "SSequencerTrackArea.h"
#include "SSequencerTrackOutliner.h"
#include "DragAndDrop/AssetDragDropOp.h"
#include "DragAndDrop/ActorDragDropGraphEdOp.h"
#include "DragAndDrop/ClassDragDropOp.h"
#include "Widgets/Input/SSearchBox.h"
#include "SSequencerTreeView.h"
#include "MovieSceneSequence.h"
#include "SSequencerSplitterOverlay.h"
#include "SequencerHotspots.h"
#include "SSequencerTimePanel.h"
#include "VirtualTrackArea.h"
#include "Framework/Commands/GenericCommands.h"
#include "SequencerContextMenus.h"
#include "Math/UnitConversion.h"
#include "Widgets/Input/NumericUnitTypeInterface.inl"
#include "FrameNumberDetailsCustomization.h"
#include "SequencerSettings.h"
#include "SSequencerTransformBox.h"
#include "SSequencerStretchBox.h"
#include "SSequencerDebugVisualizer.h"
#include "ISequencerModule.h"
#include "IVREditorModule.h"
#include "EditorFontGlyphs.h"
#include "HAL/PlatformApplicationMisc.h"
#include "SSequencerPlayRateCombo.h"
#include "Camera/CameraActor.h"
#include "SCurveEditorPanel.h"
#include "Tree/SCurveEditorTree.h"
#include "Tree/CurveEditorTreeFilter.h"
#include "Tree/SCurveEditorTreeTextFilter.h"
#include "Tree/SCurveEditorTreeFilterStatusBar.h"
#include "SequencerSelectionCurveFilter.h"
#include "SCurveKeyDetailPanel.h"
#include "MovieSceneTimeHelpers.h"
#include "FrameNumberNumericInterface.h"
#include "LevelSequence.h"
#include "SequencerLog.h"
#include "MovieSceneCopyableBinding.h"
#include "SExposedBindingsWidget.h"
#include "MovieSceneCopyableTrack.h"
#include "IPropertyRowGenerator.h"
#include "Fonts/FontMeasure.h"
#include "SequencerTrackFilters.h"
#include "SequencerTrackFilterExtension.h"

#define LOCTEXT_NAMESPACE "Sequencer"

const FName SSequencer::CurveEditorTabName = FName(TEXT("SequencerGraphEditor"));

TSharedRef<IPropertyTypeCustomization> CreateFrameNumberCustomization(TWeakPtr<FSequencer> WeakSequencer)
{
	TSharedPtr<ISequencer> SequencerPtr = WeakSequencer.Pin();
	return MakeShared<FFrameNumberDetailsCustomization>(SequencerPtr->GetNumericTypeInterface());
}

class SSequencerCurveEditor : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SSequencerCurveEditor)
	{}
	SLATE_END_ARGS()
public:
	void Construct(const FArguments& InArgs, TSharedRef<SCurveEditorPanel> InEditorPanel)
	{
		ChildSlot
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				MakeToolbar(InEditorPanel)
			]
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				InEditorPanel
			]
		];
	}

	TSharedRef<SWidget> MakeToolbar(TSharedRef<SCurveEditorPanel> InEditorPanel)
	{
		FToolBarBuilder ToolBarBuilder(InEditorPanel->GetCommands(), FMultiBoxCustomization::None, InEditorPanel->GetToolbarExtender(), EOrientation::Orient_Horizontal, true);
		ToolBarBuilder.BeginSection("Asset");
		ToolBarBuilder.EndSection();
		// We just use all of the extenders as our toolbar, we don't have a need to create a separate toolbar.
		return ToolBarBuilder.MakeWidget();
	}
};

class FSequencerCurveEditorTimeSliderController : public FSequencerTimeSliderController
{
public:

	FSequencerCurveEditorTimeSliderController(const FTimeSliderArgs& InArgs, TWeakPtr<FSequencer> InWeakSequencer, TSharedRef<FCurveEditor> InCurveEditor)
		: FSequencerTimeSliderController(InArgs, InWeakSequencer)
	{
		WeakSequencer = InWeakSequencer;
		WeakCurveEditor = InCurveEditor;
	}

	virtual void ClampViewRange(double& NewRangeMin, double& NewRangeMax) override
	{
		// Since the CurveEditor uses a different view range (potentially) we have to be careful about which one we clamp.
		TSharedPtr<ISequencer> Sequencer = WeakSequencer.Pin();
		if (!Sequencer.IsValid())
		{
			return;
		}

		const bool bLinkedTimeRange = Sequencer->GetSequencerSettings()->GetLinkCurveEditorTimeRange();
		if (bLinkedTimeRange)
		{
			return FSequencerTimeSliderController::ClampViewRange(NewRangeMin, NewRangeMax);
		}
		else
		{
			TSharedPtr<FCurveEditor> CurveEditor = WeakCurveEditor.Pin();
			if (CurveEditor.IsValid())
			{
				double InputMin, InputMax;
				CurveEditor->GetBounds().GetInputBounds(InputMin, InputMax);

				bool bNeedsClampSet = false;
				double NewClampRangeMin = InputMin;
				if (NewRangeMin < InputMin)
				{
					NewClampRangeMin = NewRangeMin;
					bNeedsClampSet = true;
				}

				double NewClampRangeMax = InputMax;
				if (NewRangeMax > InputMax)
				{
					NewClampRangeMax = NewRangeMax;
					bNeedsClampSet = true;
				}

				if (bNeedsClampSet)
				{
					CurveEditor->GetBounds().SetInputBounds(NewClampRangeMin, NewClampRangeMax);
				}

			}
		}
	}

	virtual void SetViewRange(double NewRangeMin, double NewRangeMax, EViewRangeInterpolation Interpolation) override
	{
		TSharedPtr<ISequencer> Sequencer = WeakSequencer.Pin();
		if (!Sequencer.IsValid())
		{
			return;
		}

		const bool bLinkedTimeRange = Sequencer->GetSequencerSettings()->GetLinkCurveEditorTimeRange();
		if (bLinkedTimeRange)
		{
			return FSequencerTimeSliderController::SetViewRange(NewRangeMin, NewRangeMax, Interpolation);
		}
		else
		{
			// Clamp to a minimum size to avoid zero-sized or negative visible ranges
			double MinVisibleTimeRange = FFrameNumber(1) / GetTickResolution();
			TRange<double> ExistingViewRange = GetViewRange();

			if (NewRangeMax == ExistingViewRange.GetUpperBoundValue())
			{
				if (NewRangeMin > NewRangeMax - MinVisibleTimeRange)
				{
					NewRangeMin = NewRangeMax - MinVisibleTimeRange;
				}
			}
			else if (NewRangeMax < NewRangeMin + MinVisibleTimeRange)
			{
				NewRangeMax = NewRangeMin + MinVisibleTimeRange;
			}

			TSharedPtr<FCurveEditor> CurveEditor = WeakCurveEditor.Pin();
			if (CurveEditor.IsValid())
			{
				CurveEditor->GetBounds().SetInputBounds(NewRangeMin, NewRangeMax);
			}
		}
	}



	virtual FAnimatedRange GetViewRange() const override
	{ 
		// If they've linked the Sequencer timerange we can return the internal controller's view range, otherwise we return the bounds (which internally does the same check)
		TSharedPtr<ISequencer> Sequencer = WeakSequencer.Pin();
		if (!Sequencer.IsValid())
		{
			return FAnimatedRange();
		}
		const bool bLinkedTimeRange = Sequencer->GetSequencerSettings()->GetLinkCurveEditorTimeRange();
		if (bLinkedTimeRange)
		{
			return FSequencerTimeSliderController::GetViewRange();
		}
		else
		{
			TSharedPtr<FCurveEditor> CurveEditor = WeakCurveEditor.Pin();
			if (CurveEditor.IsValid())
			{
				double InputMin, InputMax;
				CurveEditor->GetBounds().GetInputBounds(InputMin, InputMax);

				return FAnimatedRange(InputMin, InputMax);
			}
		}

		return FAnimatedRange();
	}

private:
	TWeakPtr<ISequencer> WeakSequencer;
	TWeakPtr<FCurveEditor> WeakCurveEditor;
};

/* SSequencer interface
 *****************************************************************************/
PRAGMA_DISABLE_OPTIMIZATION
void SSequencer::Construct(const FArguments& InArgs, TSharedRef<FSequencer> InSequencer)
{
	SequencerPtr = InSequencer;
	bIsActiveTimerRegistered = false;
	bUserIsSelecting = false;
	CachedClampRange = TRange<double>::Empty();
	CachedViewRange = TRange<double>::Empty();

	Settings = InSequencer->GetSequencerSettings();

	InitializeTrackFilters();

	ISequencerWidgetsModule& SequencerWidgets = FModuleManager::Get().LoadModuleChecked<ISequencerWidgetsModule>( "SequencerWidgets" );

	OnPlaybackRangeBeginDrag = InArgs._OnPlaybackRangeBeginDrag;
	OnPlaybackRangeEndDrag = InArgs._OnPlaybackRangeEndDrag;
	OnSelectionRangeBeginDrag = InArgs._OnSelectionRangeBeginDrag;
	OnSelectionRangeEndDrag = InArgs._OnSelectionRangeEndDrag;
	OnMarkBeginDrag = InArgs._OnMarkBeginDrag;
	OnMarkEndDrag = InArgs._OnMarkEndDrag;

	OnReceivedFocus = InArgs._OnReceivedFocus;

	USequencerSettings* SequencerSettings = Settings;

	// Get the desired display format from the user's settings each time.
	TAttribute<EFrameNumberDisplayFormats> GetDisplayFormatAttr = MakeAttributeLambda(
		[SequencerSettings]
		{
			if (SequencerSettings)
			{
				return SequencerSettings->GetTimeDisplayFormat();
			}
			return EFrameNumberDisplayFormats::Frames;
		}
	);

	// Get the number of zero pad frames from the user's settings as well.
	TAttribute<uint8> GetZeroPadFramesAttr = MakeAttributeLambda(
		[SequencerSettings]()->uint8
		{
			if (SequencerSettings)
			{
				return SequencerSettings->GetZeroPadFrames();
			}
			return 0;
		}
	);

	TAttribute<FFrameRate> GetTickResolutionAttr = TAttribute<FFrameRate>(InSequencer, &FSequencer::GetFocusedTickResolution);
	TAttribute<FFrameRate> GetDisplayRateAttr    = TAttribute<FFrameRate>(InSequencer, &FSequencer::GetFocusedDisplayRate);

	// Create our numeric type interface so we can pass it to the time slider below.
	NumericTypeInterface = MakeShareable(new FFrameNumberInterface(GetDisplayFormatAttr, GetZeroPadFramesAttr, GetTickResolutionAttr, GetDisplayRateAttr));

	FTimeSliderArgs TimeSliderArgs;
	{
		TimeSliderArgs.ViewRange = InArgs._ViewRange;
		TimeSliderArgs.ClampRange = InArgs._ClampRange;
		TimeSliderArgs.PlaybackRange = InArgs._PlaybackRange;
		TimeSliderArgs.DisplayRate = TAttribute<FFrameRate>(InSequencer, &FSequencer::GetFocusedDisplayRate);
		TimeSliderArgs.TickResolution = TAttribute<FFrameRate>(InSequencer, &FSequencer::GetFocusedTickResolution);
		TimeSliderArgs.SelectionRange = InArgs._SelectionRange;
		TimeSliderArgs.OnPlaybackRangeChanged = InArgs._OnPlaybackRangeChanged;
		TimeSliderArgs.OnPlaybackRangeBeginDrag = OnPlaybackRangeBeginDrag;
		TimeSliderArgs.OnPlaybackRangeEndDrag = OnPlaybackRangeEndDrag;
		TimeSliderArgs.OnSelectionRangeChanged = InArgs._OnSelectionRangeChanged;
		TimeSliderArgs.OnSelectionRangeBeginDrag = OnSelectionRangeBeginDrag;
		TimeSliderArgs.OnSelectionRangeEndDrag = OnSelectionRangeEndDrag;
		TimeSliderArgs.OnMarkBeginDrag = OnMarkBeginDrag;
		TimeSliderArgs.OnMarkEndDrag = OnMarkEndDrag;
		TimeSliderArgs.OnViewRangeChanged = InArgs._OnViewRangeChanged;
		TimeSliderArgs.OnClampRangeChanged = InArgs._OnClampRangeChanged;
		TimeSliderArgs.OnGetNearestKey = InArgs._OnGetNearestKey;
		TimeSliderArgs.IsPlaybackRangeLocked = InArgs._IsPlaybackRangeLocked;
		TimeSliderArgs.OnTogglePlaybackRangeLocked = InArgs._OnTogglePlaybackRangeLocked;
		TimeSliderArgs.ScrubPosition = InArgs._ScrubPosition;
		TimeSliderArgs.OnBeginScrubberMovement = InArgs._OnBeginScrubbing;
		TimeSliderArgs.OnEndScrubberMovement = InArgs._OnEndScrubbing;
		TimeSliderArgs.OnScrubPositionChanged = InArgs._OnScrubPositionChanged;
		TimeSliderArgs.PlaybackStatus = InArgs._PlaybackStatus;
		TimeSliderArgs.SubSequenceRange = InArgs._SubSequenceRange;
		TimeSliderArgs.VerticalFrames = InArgs._VerticalFrames;
		TimeSliderArgs.MarkedFrames = InArgs._MarkedFrames;
		TimeSliderArgs.OnSetMarkedFrame = InArgs._OnSetMarkedFrame;
		TimeSliderArgs.OnMarkedFrameChanged = InArgs._OnMarkedFrameChanged;
		TimeSliderArgs.OnClearAllMarkedFrames = InArgs._OnClearAllMarkedFrames;

		TimeSliderArgs.Settings = Settings;
		TimeSliderArgs.NumericTypeInterface = GetNumericTypeInterface();
	}

	TimeSliderController = MakeShareable( new FSequencerTimeSliderController( TimeSliderArgs, SequencerPtr ) );
	
	TSharedRef<FSequencerTimeSliderController> TimeSliderControllerRef = TimeSliderController.ToSharedRef();

	bool bMirrorLabels = false;
	
	// Create the top and bottom sliders
	TopTimeSlider = SequencerWidgets.CreateTimeSlider( TimeSliderControllerRef, bMirrorLabels );
	bMirrorLabels = true;
	TSharedRef<ITimeSlider> BottomTimeSlider = SequencerWidgets.CreateTimeSlider( TimeSliderControllerRef, TAttribute<EVisibility>(this, &SSequencer::GetBottomTimeSliderVisibility), bMirrorLabels );

	// Create bottom time range slider
	TSharedRef<ITimeSlider> BottomTimeRange = SequencerWidgets.CreateTimeRange(
		FTimeRangeArgs(
			EShowRange(EShowRange::WorkingRange | EShowRange::ViewRange),
			TimeSliderControllerRef,
			TAttribute<EVisibility>(this, &SSequencer::GetTimeRangeVisibility),
			NumericTypeInterface.ToSharedRef()
		),
		SequencerWidgets.CreateTimeRangeSlider(TimeSliderControllerRef)
	);

	OnGetAddMenuContent = InArgs._OnGetAddMenuContent;
	OnBuildCustomContextMenuForGuid = InArgs._OnBuildCustomContextMenuForGuid;
	AddMenuExtender = InArgs._AddMenuExtender;
	ToolbarExtender = InArgs._ToolbarExtender;

	ColumnFillCoefficients[0] = 0.3f;
	ColumnFillCoefficients[1] = 0.7f;

	TAttribute<float> FillCoefficient_0, FillCoefficient_1;
	{
		FillCoefficient_0.Bind(TAttribute<float>::FGetter::CreateSP(this, &SSequencer::GetColumnFillCoefficient, 0));
		FillCoefficient_1.Bind(TAttribute<float>::FGetter::CreateSP(this, &SSequencer::GetColumnFillCoefficient, 1));
	}

	TSharedRef<SScrollBar> ScrollBar = SNew(SScrollBar)
		.Thickness(FVector2D(9.0f, 9.0f));
	SAssignNew(TrackOutliner, SSequencerTrackOutliner);

	SAssignNew(PinnedTrackArea, SSequencerTrackArea, TimeSliderControllerRef, InSequencer);
	SAssignNew(PinnedTreeView, SSequencerTreeView, InSequencer->GetNodeTree(), PinnedTrackArea.ToSharedRef())
		.Clipping(EWidgetClipping::ClipToBounds)
		.OnGetContextMenuContent(FOnGetContextMenuContent::CreateSP(this, &SSequencer::GetContextMenuContent));

	PinnedTrackArea->SetTreeView(PinnedTreeView);
	PinnedTrackArea->SetShowPinned(true);
	PinnedTreeView->SetShowPinned(true);

	SAssignNew(TrackArea, SSequencerTrackArea, TimeSliderControllerRef, InSequencer);
	SAssignNew(TreeView, SSequencerTreeView, InSequencer->GetNodeTree(), TrackArea.ToSharedRef())
		.ExternalScrollbar(ScrollBar)
		.Clipping(EWidgetClipping::ClipToBounds)
		.OnGetContextMenuContent(FOnGetContextMenuContent::CreateSP(this, &SSequencer::GetContextMenuContent));

	TrackArea->SetTreeView(TreeView);

	TreeView->AddSlaveTreeView(PinnedTreeView);

	TAttribute<FAnimatedRange> ViewRangeAttribute = InArgs._ViewRange;
	
	// We create a custom Time Slider Controller which is just a wrapper around the actual one, but is aware of our custom bounds logic. Currently the range the
	// bar displays is tied to Sequencer timeline and not the Bounds, so we need a way of changing it to look at the Bounds but only for the Curve Editor time
	// slider controller. We want everything else to just pass through though.
	TSharedRef<ITimeSliderController> CurveEditorTimeSliderController = MakeShared<FSequencerCurveEditorTimeSliderController>(TimeSliderArgs, SequencerPtr, InSequencer->GetCurveEditor().ToSharedRef());

	// Initialize the Curve Editor Widget if there is a tab manager to spawn our extra tab in.
	// Some areas that use Sequencer don't use our curve editor. In this case no button is shown on the UI.
	if (InSequencer->GetToolkitHost().IsValid())
	{
		CurveEditorTree = SNew(SCurveEditorTree, InSequencer->GetCurveEditor());
		TSharedRef<SCurveEditorPanel> CurveEditorWidget = SNew(SCurveEditorPanel, InSequencer->GetCurveEditor().ToSharedRef())
			// Grid lines match the color specified in FSequencerTimeSliderController::OnPaintViewArea
			.GridLineTint(FLinearColor(0.f, 0.f, 0.f, 0.3f))
			.ExternalTimeSliderController(CurveEditorTimeSliderController)
			.TabManager(InSequencer->GetToolkitHost()->GetTabManager())
			.DisabledTimeSnapTooltip(LOCTEXT("CurveEditorTimeSnapDisabledTooltip", "Time Snapping is currently driven by Sequencer."))
			.TreeContent()
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SAssignNew(CurveEditorSearchBox, SCurveEditorTreeTextFilter, InSequencer->GetCurveEditor())
				]

				+ SVerticalBox::Slot()
				[
					SNew(SScrollBorder, CurveEditorTree.ToSharedRef())
					[
						CurveEditorTree.ToSharedRef()
					]
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SCurveEditorTreeFilterStatusBar, InSequencer->GetCurveEditor())
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Center)
				[
					InSequencer->MakeTransportControls(true)
				]
			];

		// Register an instanced custom property type layout to handle converting FFrameNumber from Tick Resolution to Display Rate.
		CurveEditorWidget->GetKeyDetailsView()->GetPropertyRowGenerator()->RegisterInstancedCustomPropertyTypeLayout("FrameNumber", FOnGetPropertyTypeCustomizationInstance::CreateStatic(CreateFrameNumberCustomization, SequencerPtr));
		TAttribute<bool> IsEnabledAttribute = TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateSP(this, &SSequencer::GetIsCurveEditorEnabled));
	
		CurveEditorPanel = SNew(SSequencerCurveEditor, CurveEditorWidget);
		CurveEditorPanel->SetEnabled(IsEnabledAttribute);
		CurveEditorWidget->SetEnabled(IsEnabledAttribute);

		// Check to see if the tab is already opened due to the saved window layout.
		TSharedPtr<SDockTab> ExistingCurveEditorTab = InSequencer->GetToolkitHost()->GetTabManager()->FindExistingLiveTab(FTabId(SSequencer::CurveEditorTabName));
		if (ExistingCurveEditorTab)
		{
			ExistingCurveEditorTab->SetContent(CurveEditorPanel.ToSharedRef());
		}
	}


	const int32 Column0 = 0, Column1 = 1;
	const int32 Row0 = 0, Row1 = 1, Row2 = 2, Row3 = 3, Row4 = 4;

	const float CommonPadding = 3.f;
	const FMargin ResizeBarPadding(4.f, 0, 0, 0);

	ChildSlot
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		[
			SNew(SSplitter)
			.Orientation(Orient_Horizontal)
			
			+ SSplitter::Slot()
			.Value(0.1f)
			[
				SNew(SBorder)
				.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
				.Visibility(this, &SSequencer::HandleLabelBrowserVisibility)
				[
					// track label browser
					SAssignNew(LabelBrowser, SSequencerLabelBrowser, InSequencer)
					.OnSelectionChanged(this, &SSequencer::HandleLabelBrowserSelectionChanged)
				]
			]

			+ SSplitter::Slot()
			.Value(0.9f)
			[
				SNew(SOverlay)

				+ SOverlay::Slot()
				[
					// track area grid panel
					SNew( SGridPanel )
					.FillRow( 2, 1.f )
					.FillColumn( 0, FillCoefficient_0 )
					.FillColumn( 1, FillCoefficient_1 )

					// Toolbar
					+ SGridPanel::Slot( Column0, Row0, SGridPanel::Layer(10) )
					.ColumnSpan(2)
					[
						SNew(SBorder)
						.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
						.Padding(FMargin(CommonPadding, 0.f))
						[
							SNew(SWrapBox)
							.UseAllottedWidth(true)
							.InnerSlotPadding(FVector2D(5, 2))

							+ SWrapBox::Slot()
							.FillEmptySpace(true)
							.FillLineWhenWidthLessThan(600)
							[
								MakeToolBar()
							]

							+ SWrapBox::Slot()
							.FillEmptySpace(true)
							[
								SNew(SBorder)
								.Padding(FMargin(3))
								.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
								[
									SNew(SHorizontalBox)

									// Right Aligned Breadcrumbs
									+ SHorizontalBox::Slot()
									.HAlign(HAlign_Right)
									.VAlign(VAlign_Center)
									[
										SNew(SSpacer)
									]

									+ SHorizontalBox::Slot()
									.HAlign(HAlign_Right)
									.VAlign(VAlign_Center)
									[
										SAssignNew(BreadcrumbPickerButton, SComboButton)
										.Visibility(this, &SSequencer::GetBreadcrumbTrailVisibility)
										.ButtonStyle(FEditorStyle::Get(), "FlatButton")
										.ForegroundColor(FLinearColor::White)
										.OnGetMenuContent(this, &SSequencer::GetBreadcrumbPickerContent)
										.HasDownArrow(false)
										.ContentPadding(FMargin(3, 3))
										.ButtonContent()
										[
											SNew(STextBlock)
											.TextStyle(FEditorStyle::Get(), "Sequencer.BreadcrumbText")
											.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.11"))
											.Text(FText::FromString(FString(TEXT("\xf07c"))) /*fa-folder-open*/)
										]
									]

									// Right Aligned Breadcrumbs
									+ SHorizontalBox::Slot()
									.HAlign(HAlign_Right)
									.VAlign(VAlign_Center)
									.AutoWidth()
									[
										SAssignNew(BreadcrumbTrail, SBreadcrumbTrail<FSequencerBreadcrumb>)
										.Visibility(this, &SSequencer::GetBreadcrumbTrailVisibility)
										.OnCrumbClicked(this, &SSequencer::OnCrumbClicked)
										.ButtonStyle(FEditorStyle::Get(), "FlatButton")
										.DelimiterImage(FEditorStyle::GetBrush("Sequencer.BreadcrumbIcon"))
										.TextStyle(FEditorStyle::Get(), "Sequencer.BreadcrumbText")
									]

									// Sequence Locking symbol
									+SHorizontalBox::Slot()
									.HAlign(HAlign_Right)
									.VAlign(VAlign_Center)
									.AutoWidth()
									.Padding(2, 0, 0, 0)
									[
										SNew(SCheckBox)
										.IsFocusable(false)		
										.IsChecked_Lambda([this] { return GetIsSequenceReadOnly() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; } )
										.OnCheckStateChanged(this, &SSequencer::OnSetSequenceReadOnly)
										.ToolTipText_Lambda([this] { return GetIsSequenceReadOnly() ? LOCTEXT("UnlockSequence", "Unlock the animation so that it is editable") : LOCTEXT("LockSequence", "Lock the animation so that it is not editable"); } )
										.ForegroundColor(FLinearColor::White)
										.CheckedImage(FEditorStyle::GetBrush("Sequencer.LockSequence"))
										.CheckedHoveredImage(FEditorStyle::GetBrush("Sequencer.LockSequence"))
										.CheckedPressedImage(FEditorStyle::GetBrush("Sequencer.LockSequence"))
										.UncheckedImage(FEditorStyle::GetBrush("Sequencer.UnlockSequence"))
										.UncheckedHoveredImage(FEditorStyle::GetBrush("Sequencer.UnlockSequence"))
										.UncheckedPressedImage(FEditorStyle::GetBrush("Sequencer.UnlockSequence"))
									]
								]
							]
						]
					]

					+ SGridPanel::Slot( Column0, Row1 )
					[
						SNew(SBorder)
						.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
						[
							SNew(SSpacer)
						]
					]

					// outliner search box
					+ SGridPanel::Slot( Column0, Row1, SGridPanel::Layer(10) )
					[
						SNew(SBorder)
						.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
						.Padding(FMargin(CommonPadding*2, CommonPadding))
						[
							SNew(SHorizontalBox)

							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							.Padding(FMargin(0.f, 0.f, CommonPadding, 0.f))
							[
								MakeAddButton()
							]
							
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							.Padding(FMargin(0.f, 0.f, CommonPadding, 0.f))
							[
								MakeFilterButton()
							]

							+ SHorizontalBox::Slot()
							.VAlign(VAlign_Center)
							[
								SAssignNew(SearchBox, SSearchBox)
								.HintText(LOCTEXT("SearchNodesHint", "Search Tracks"))
								.OnTextChanged( this, &SSequencer::OnOutlinerSearchChanged )
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							.HAlign(HAlign_Right)
							.Padding(FMargin(CommonPadding + 2.0, 0.f, 0.f, 0.f))
							[
								SNew(SBorder)
								.BorderImage(nullptr)
								[
									// Current Play Time 
									SAssignNew(PlayTimeDisplay, STemporarilyFocusedSpinBox<double>)
									.Style(&FEditorStyle::GetWidgetStyle<FSpinBoxStyle>("Sequencer.PlayTimeSpinBox"))
									.Value_Lambda([this]() -> double {
										return SequencerPtr.Pin()->GetLocalTime().Time.GetFrame().Value;
									})
									.OnValueChanged(this, &SSequencer::SetPlayTimeClampedByWorkingRange)
									.OnValueCommitted_Lambda([this](double InFrame, ETextCommit::Type) {
										SetPlayTimeClampedByWorkingRange(InFrame);

										// Refocus on the previously focused widget so that user can continue on after setting a time
										PlayTimeDisplay->Refocus();
									})
									.MinValue(TOptional<double>())
									.MaxValue(TOptional<double>())
									.TypeInterface(NumericTypeInterface)
									.Delta(this, &SSequencer::GetSpinboxDelta)
									.LinearDeltaSensitivity(25)
									.MinDesiredWidth(this, &SSequencer::GetPlayTimeMinDesiredWidth)
								]
							]
						]
					]

					// main sequencer area
					+ SGridPanel::Slot( Column0, Row2, SGridPanel::Layer(10) )
					.ColumnSpan(2)
					[
						SNew(SHorizontalBox)

						+ SHorizontalBox::Slot()
						[
							SNew( SOverlay )

							+ SOverlay::Slot()
							[
								SNew(SVerticalBox)

								+SVerticalBox::Slot()
								.AutoHeight()
								[
									SNew(SBorder)
									.Padding(FMargin(0.0f, 0.0f, 0.0f, CommonPadding))
									[
										SNew(SHorizontalBox)

										// outliner tree
										+ SHorizontalBox::Slot()
										.FillWidth( FillCoefficient_0 )
										[
											SNew(SBox)
											[
												PinnedTreeView.ToSharedRef()
											]
										]

										// track area
										+ SHorizontalBox::Slot()
										.FillWidth( FillCoefficient_1 )
										[
											SNew(SBox)
											.Padding(ResizeBarPadding)
											.Clipping(EWidgetClipping::ClipToBounds)
											[
												PinnedTrackArea.ToSharedRef()
											]
										]
									]

								]

								+SVerticalBox::Slot()
								[
									SNew(SScrollBorder, TreeView.ToSharedRef())
									[
										SNew(SHorizontalBox)

										// outliner tree
										+ SHorizontalBox::Slot()
										.FillWidth( FillCoefficient_0 )
										[
											SNew(SBox)
											[
												TreeView.ToSharedRef()
											]
										]

										// track area
										+ SHorizontalBox::Slot()
										.FillWidth( FillCoefficient_1 )
										[
											SNew(SBox)
											.Padding(ResizeBarPadding)
											.Clipping(EWidgetClipping::ClipToBounds)
											[
												TrackArea.ToSharedRef()
											]
										]
									]
								]
							]

							+ SOverlay::Slot()
							.HAlign( HAlign_Right )
							[
								ScrollBar
							]
						]
					]

					// playback buttons
					+ SGridPanel::Slot( Column0, Row4, SGridPanel::Layer(10) )
					[
						SNew(SBorder)
						.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
						//.BorderBackgroundColor(FLinearColor(.50f, .50f, .50f, 1.0f))
						.HAlign(HAlign_Center)
						[
							SequencerPtr.Pin()->MakeTransportControls(true)
						]
					]

					// Second column

					+ SGridPanel::Slot( Column1, Row1 )
					.Padding(ResizeBarPadding)
					.RowSpan(3)
					[
						SNew(SBorder)
						.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
						[
							SNew(SSpacer)
						]
					]

					+ SGridPanel::Slot( Column1, Row1, SGridPanel::Layer(10) )
					.Padding(ResizeBarPadding)
					[
						SNew( SBorder )
						.BorderImage( FEditorStyle::GetBrush("ToolPanel.GroupBorder") )
						.BorderBackgroundColor( FLinearColor(.50f, .50f, .50f, 1.0f ) )
						.Padding(0)
						.Clipping(EWidgetClipping::ClipToBounds)
						[
							TopTimeSlider.ToSharedRef()
						]
					]

					// Overlay that draws the tick lines
					+ SGridPanel::Slot( Column1, Row2, SGridPanel::Layer(10) )
					.Padding(ResizeBarPadding)
					[
						SNew( SSequencerSectionOverlay, TimeSliderControllerRef )
						.Visibility( EVisibility::HitTestInvisible )
						.DisplayScrubPosition( false )
						.DisplayTickLines( true )
						.Clipping(EWidgetClipping::ClipToBounds)
					]

					// Overlay that draws the scrub position
					+ SGridPanel::Slot( Column1, Row2, SGridPanel::Layer(20) )
					.Padding(ResizeBarPadding)
					[
						SNew( SSequencerSectionOverlay, TimeSliderControllerRef )
						.Visibility( EVisibility::HitTestInvisible )
						.DisplayScrubPosition( true )
						.DisplayTickLines( false )
						.DisplayMarkedFrames( true )
						.PaintPlaybackRangeArgs(this, &SSequencer::GetSectionPlaybackRangeArgs)
						.Clipping(EWidgetClipping::ClipToBounds)
					]

					+ SGridPanel::Slot(Column1, Row2, SGridPanel::Layer(30))
						.Padding(ResizeBarPadding)
						.HAlign(HAlign_Left)
						.VAlign(VAlign_Top)
					[
						// Transform box
						SAssignNew(TransformBox, SSequencerTransformBox, SequencerPtr.Pin().ToSharedRef(), *Settings, NumericTypeInterface.ToSharedRef())
					]

					+ SGridPanel::Slot(Column1, Row2, SGridPanel::Layer(40))
					.Padding(ResizeBarPadding)
					[
						SAssignNew(TickResolutionOverlay, SSequencerTimePanel, SequencerPtr)
					]

					+ SGridPanel::Slot(Column1, Row2, SGridPanel::Layer(50))
						.Padding(ResizeBarPadding)
						.HAlign(HAlign_Left)
						.VAlign(VAlign_Top)
					[
						// Stretch box
						SAssignNew(StretchBox, SSequencerStretchBox, SequencerPtr.Pin().ToSharedRef(), *Settings, NumericTypeInterface.ToSharedRef())
					]

					// debug vis
					+ SGridPanel::Slot( Column1, Row3, SGridPanel::Layer(10) )
					.Padding(ResizeBarPadding)
					[
						SNew(SSequencerDebugVisualizer, InSequencer)
						.ViewRange(FAnimatedRange::WrapAttribute(InArgs._ViewRange))
						.Visibility(this, &SSequencer::GetDebugVisualizerVisibility)
					]

					// play range sliders
					+ SGridPanel::Slot( Column1, Row4, SGridPanel::Layer(10) )
					.Padding(ResizeBarPadding)
					[
						SNew( SBorder )
						.BorderImage( FEditorStyle::GetBrush("ToolPanel.GroupBorder") )
						.BorderBackgroundColor( FLinearColor(.50f, .50f, .50f, 1.0f ) )
						.Clipping(EWidgetClipping::ClipToBounds)
						.Padding(0)
						[
							SNew( SOverlay )

							+ SOverlay::Slot()
							[
								BottomTimeSlider
							]

							+ SOverlay::Slot()
							[
								BottomTimeRange
							]
						]
					]
				]

				+ SOverlay::Slot()
				[
					// track area virtual splitter overlay
					SNew(SSequencerSplitterOverlay)
					.Style(FEditorStyle::Get(), "Sequencer.AnimationOutliner.Splitter")
					.Visibility(EVisibility::SelfHitTestInvisible)

					+ SSplitter::Slot()
					.Value(FillCoefficient_0)
					.OnSlotResized(SSplitter::FOnSlotResized::CreateSP(this, &SSequencer::OnColumnFillCoefficientChanged, 0))
					[
						SNew(SSpacer)
					]

					+ SSplitter::Slot()
					.Value(FillCoefficient_1)
					.OnSlotResized(SSplitter::FOnSlotResized::CreateSP(this, &SSequencer::OnColumnFillCoefficientChanged, 1))
					[
						SNew(SSpacer)
					]
				]
			]
		]
	];

	HideTickResolutionOverlay();

	InSequencer->GetSelection().GetOnKeySelectionChanged().AddSP(this, &SSequencer::HandleKeySelectionChanged);
	InSequencer->GetSelection().GetOnSectionSelectionChanged().AddSP(this, &SSequencer::HandleSectionSelectionChanged);
	InSequencer->GetSelection().GetOnOutlinerNodeSelectionChanged().AddSP(this, &SSequencer::HandleOutlinerNodeSelectionChanged);

	ResetBreadcrumbs();
}
PRAGMA_ENABLE_OPTIMIZATION

void SSequencer::BindCommands(TSharedRef<FUICommandList> SequencerCommandBindings)
{
	auto CanPasteFromHistory = [this]{
		if (!HasFocusedDescendants() && !HasKeyboardFocus())
		{
			return false;
		}

		return SequencerPtr.Pin()->GetClipboardStack().Num() != 0;
	};
	
	SequencerCommandBindings->MapAction(
		FGenericCommands::Get().Paste,
		FExecuteAction::CreateSP(this, &SSequencer::OnPaste),
		FCanExecuteAction::CreateSP(this, &SSequencer::CanPaste)
	);

	SequencerCommandBindings->MapAction(
		FSequencerCommands::Get().PasteFromHistory,
		FExecuteAction::CreateSP(this, &SSequencer::PasteFromHistory),
		FCanExecuteAction::CreateLambda(CanPasteFromHistory)
	);

	SequencerCommandBindings->MapAction(
		FSequencerCommands::Get().ToggleShowGotoBox,
		FExecuteAction::CreateLambda([this] { PlayTimeDisplay->Setup();  FSlateApplication::Get().SetKeyboardFocus(PlayTimeDisplay, EFocusCause::SetDirectly); })
	);

	SequencerCommandBindings->MapAction(
		FSequencerCommands::Get().ToggleShowTransformBox,
		FExecuteAction::CreateLambda([this] { TransformBox->ToggleVisibility(); })
	);

	// Allow jumping to the Sequencer tree search if you have Sequencer focused
	SequencerCommandBindings->MapAction(
		FSequencerCommands::Get().QuickTreeSearch,
		FExecuteAction::CreateLambda([this] { FSlateApplication::Get().SetKeyboardFocus(SearchBox, EFocusCause::SetDirectly); })
	);

	// And jump to the Curve Editor tree search if you have the Curve Editor focused
	SequencerPtr.Pin()->GetCurveEditor()->GetCommands()->MapAction(
		FSequencerCommands::Get().QuickTreeSearch,
		FExecuteAction::CreateLambda([this] { FSlateApplication::Get().SetKeyboardFocus(CurveEditorSearchBox, EFocusCause::SetDirectly); })
	);
	
	SequencerCommandBindings->MapAction(
		FSequencerCommands::Get().ToggleShowStretchBox,
		FExecuteAction::CreateLambda([this] { StretchBox->ToggleVisibility(); })
	);
}

void SSequencer::ShowTickResolutionOverlay()
{
	TickResolutionOverlay->SetVisibility(EVisibility::Visible);
}

void SSequencer::HideTickResolutionOverlay()
{
	TickResolutionOverlay->SetVisibility(EVisibility::Collapsed);
}

const ISequencerEditTool* SSequencer::GetEditTool() const
{
	return TrackArea->GetEditTool();
}


void SSequencer::NotifyPostChange(const FPropertyChangedEvent& PropertyChangedEvent, FEditPropertyChain* PropertyThatChanged)
{
	// @todo sequencer: is this still needed?
}


/* SSequencer implementation
 *****************************************************************************/

TSharedRef<INumericTypeInterface<double>> SSequencer::GetNumericTypeInterface() const
{
	return NumericTypeInterface.ToSharedRef();
}

void SSequencer::InitializeTrackFilters()
{
	// Add all built-in track filters here
	AllTrackFilters.Add(MakeShared<FSequencerTrackFilter_AudioTracks>());
	AllTrackFilters.Add(MakeShared<FSequencerTrackFilter_EventTracks>());
	AllTrackFilters.Add(MakeShared<FSequencerTrackFilter_LevelVisibilityTracks>());
	AllTrackFilters.Add(MakeShared<FSequencerTrackFilter_ParticleTracks>());

	AllTrackFilters.Add(MakeShared<FSequencerTrackFilter_CameraObjects>());
	AllTrackFilters.Add(MakeShared<FSequencerTrackFilter_LightObjects>());
	AllTrackFilters.Add(MakeShared<FSequencerTrackFilter_SkeletalMeshObjects>());

	// Add any global user-defined frontend filters
	for (TObjectIterator<USequencerTrackFilterExtension> ExtensionIt(RF_NoFlags); ExtensionIt; ++ExtensionIt)
	{
		if (USequencerTrackFilterExtension* PotentialExtension = *ExtensionIt)
		{
			if (PotentialExtension->HasAnyFlags(RF_ClassDefaultObject) && !PotentialExtension->GetClass()->HasAnyClassFlags(CLASS_Deprecated | CLASS_Abstract))
			{
				// Grab the filters
				TArray< TSharedRef<FSequencerTrackFilter> > ExtendedTrackFilters;
				PotentialExtension->AddTrackFilterExtensions(ExtendedTrackFilters);
				AllTrackFilters.Append(ExtendedTrackFilters);
			}
		}
	}

	// Sort by display name
	AllTrackFilters.Sort([](const TSharedRef<FSequencerTrackFilter>& LHS, const TSharedRef<FSequencerTrackFilter>& RHS) { return LHS->GetDisplayName().ToString() < RHS->GetDisplayName().ToString(); });
}

/* SSequencer callbacks
 *****************************************************************************/

void SSequencer::HandleKeySelectionChanged()
{
}


void SSequencer::HandleLabelBrowserSelectionChanged(FString NewLabel, ESelectInfo::Type SelectInfo)
{
	if (SelectInfo == ESelectInfo::Direct)
	{
		return;
	}

	if (NewLabel.IsEmpty())
	{
		SearchBox->SetText(FText::GetEmpty());
	}
	else
	{
		SearchBox->SetText(FText::FromString(NewLabel));
	}
}


EVisibility SSequencer::HandleLabelBrowserVisibility() const
{
	if (Settings->GetLabelBrowserVisible())
	{
		return EVisibility::Visible;
	}

	return EVisibility::Collapsed;
}


void SSequencer::HandleSectionSelectionChanged()
{
}


void SSequencer::HandleOutlinerNodeSelectionChanged()
{
	TSharedPtr<FSequencer> Sequencer = SequencerPtr.Pin();
	if (!Sequencer.IsValid())
	{
		return;
	}

	const TSet<TSharedRef<FSequencerDisplayNode>>& SelectedDisplayNodes = Sequencer->GetSelection().GetSelectedOutlinerNodes();

	TSharedPtr<FCurveEditor> CurveEditor = Sequencer->GetCurveEditor();
	if (CurveEditor && CurveEditorTree)
	{
		// If we're isolating to the selection and there is one, add the filter
		if (Settings->ShouldIsolateToCurveEditorSelection() && SelectedDisplayNodes.Num() != 0)
		{
			if (!SequencerSelectionCurveEditorFilter)
			{
				SequencerSelectionCurveEditorFilter = MakeShared<FSequencerSelectionCurveFilter>();
			}

			SequencerSelectionCurveEditorFilter->Update(Sequencer->GetSelection().GetSelectedOutlinerNodes());

			CurveEditor->GetTree()->AddFilter(SequencerSelectionCurveEditorFilter);
		}
		// If we're not isolating to the selection (or there is no selection) remove the filter
		else if (SequencerSelectionCurveEditorFilter)
		{
			CurveEditor->GetTree()->RemoveFilter(SequencerSelectionCurveEditorFilter);
			SequencerSelectionCurveEditorFilter = nullptr;
		}

		if (Settings->ShouldSyncCurveEditorSelection())
		{
			TSharedRef<FSequencerNodeTree> NodeTree = Sequencer->GetNodeTree();

			// Clear the tree selection
			CurveEditorTree->ClearSelection();
			for (TSharedRef<FSequencerDisplayNode> Node : SelectedDisplayNodes)
			{
				FCurveEditorTreeItemID CurveEditorTreeItem = NodeTree->FindCurveEditorTreeItem(Node);
				if (CurveEditorTreeItem != FCurveEditorTreeItemID::Invalid())
				{
					CurveEditorTree->SetItemSelection(CurveEditorTreeItem, true);
				}
			}
		}
	}
}

TSharedRef<SWidget> SSequencer::MakeAddButton()
{
	return SNew(SComboButton)
	.OnGetMenuContent(this, &SSequencer::MakeAddMenu)
	.ButtonStyle(FEditorStyle::Get(), "FlatButton.Success")
	.ContentPadding(FMargin(2.0f, 1.0f))
	.HasDownArrow(false)
	.ButtonContent()
	[
		SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.AutoWidth()
		[
			SNew(STextBlock)
			.TextStyle(FEditorStyle::Get(), "NormalText.Important")
			.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.10"))
			.Text(FEditorFontGlyphs::Plus)
			.IsEnabled_Lambda([=]() { return !SequencerPtr.Pin()->IsReadOnly(); })
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(4, 0, 0, 0)
		[
			SNew(STextBlock)
			.TextStyle(FEditorStyle::Get(), "NormalText.Important")
			.Text(LOCTEXT("Track", "Track"))
			.IsEnabled_Lambda([=]() { return !SequencerPtr.Pin()->IsReadOnly(); })
		]

		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.AutoWidth()
		.Padding(4, 0, 0, 0)
		[
			SNew(STextBlock)
			.TextStyle(FEditorStyle::Get(), "NormalText.Important")
			.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.10"))
			.Text(FEditorFontGlyphs::Caret_Down)
			.IsEnabled_Lambda([=]() { return !SequencerPtr.Pin()->IsReadOnly(); })
		]
	];
}

TSharedRef<SWidget> SSequencer::MakeFilterButton()
{
	return SNew(SComboButton)
	.ComboButtonStyle(FEditorStyle::Get(), "GenericFilters.ComboButtonStyle")
	.ForegroundColor(FLinearColor::White)
	.ContentPadding(0)
	.ToolTipText(LOCTEXT("AddTrackFilterToolTip", "Add a track filter."))
	.OnGetMenuContent(this, &SSequencer::MakeFilterMenu)
	.HasDownArrow(true)
	.ContentPadding(FMargin(1, 0))
	.ButtonContent()
	[
		SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(STextBlock)
			.TextStyle(FEditorStyle::Get(), "GenericFilters.TextStyle")
			.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.9"))
			.Text(FEditorFontGlyphs::Filter)
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(2, 0, 0, 0)
		[
			SNew(STextBlock)
			.TextStyle(FEditorStyle::Get(), "GenericFilters.TextStyle")
			.Text(LOCTEXT("Filters", "Filters"))
		]
	];
}

TSharedRef<SWidget> SSequencer::MakeToolBar()
{
	ISequencerModule& SequencerModule = FModuleManager::GetModuleChecked<ISequencerModule>("Sequencer");
	TSharedPtr<FExtender> Extender = SequencerModule.GetToolBarExtensibilityManager()->GetAllExtenders();
	if (ToolbarExtender.IsValid())
	{
		Extender = FExtender::Combine({ Extender, ToolbarExtender });
	}

	FToolBarBuilder ToolBarBuilder( SequencerPtr.Pin()->GetCommandBindings(), FMultiBoxCustomization::None, Extender, Orient_Horizontal, true);

	ToolBarBuilder.BeginSection("Base Commands");
	{
		// General 
		if (SequencerPtr.Pin()->IsLevelEditorSequencer())
		{
			TAttribute<FSlateIcon> SaveIcon;
			SaveIcon.Bind(TAttribute<FSlateIcon>::FGetter::CreateLambda([&] {

				bool bAnyMovieSceneDirty = false;

				TArray<UMovieScene*> MovieScenesToSave;
				MovieSceneHelpers::GetDescendantMovieScenes(SequencerPtr.Pin()->GetRootMovieSceneSequence(), MovieScenesToSave);
				for (auto MovieSceneToSave : MovieScenesToSave)
				{
					UPackage* MovieScenePackageToSave = MovieSceneToSave->GetOuter()->GetOutermost();
					if (MovieScenePackageToSave->IsDirty())
					{
						bAnyMovieSceneDirty = true;
						break;
					}
				}

				if (bAnyMovieSceneDirty)
				{
					return FSlateIcon(FEditorStyle::GetStyleSetName(), "Sequencer.SaveAsterisk");
				}
				else
				{
					return FSlateIcon(FEditorStyle::GetStyleSetName(), "Sequencer.Save");
				}
			}));

			ToolBarBuilder.AddToolBarButton(
				FUIAction(FExecuteAction::CreateSP(this, &SSequencer::OnSaveMovieSceneClicked)),
				NAME_None,
				LOCTEXT("SaveDirtyPackages", "Save"),
				LOCTEXT("SaveDirtyPackagesTooltip", "Saves the current sequence and any subsequences"),
				SaveIcon
			);

			ToolBarBuilder.AddToolBarButton(
				FUIAction(FExecuteAction::CreateSP(this, &SSequencer::OnSaveMovieSceneAsClicked)),
				NAME_None,
				LOCTEXT("SaveAs", "Save As"),
				LOCTEXT("SaveAsTooltip", "Saves the current sequence under a different name"),
				FSlateIcon(FEditorStyle::GetStyleSetName(), "Sequencer.SaveAs")
			);

			//ToolBarBuilder.AddToolBarButton( FSequencerCommands::Get().DiscardChanges );
			ToolBarBuilder.AddToolBarButton( FSequencerCommands::Get().FindInContentBrowser );
			ToolBarBuilder.AddToolBarButton( FSequencerCommands::Get().CreateCamera );
			ToolBarBuilder.AddToolBarButton( FSequencerCommands::Get().RenderMovie );
			ToolBarBuilder.AddSeparator("Level Sequence Separator");
		}

		ToolBarBuilder.AddToolBarButton( FSequencerCommands::Get().RestoreAnimatedState );

		ToolBarBuilder.AddComboButton(
			FUIAction(),
			FOnGetContent::CreateSP(this, &SSequencer::MakeGeneralMenu),
			LOCTEXT("GeneralOptions", "General Options"),
			LOCTEXT("GeneralOptionsToolTip", "General Options"),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "Sequencer.GeneralOptions")
		);

		ToolBarBuilder.AddComboButton(
			FUIAction(),
			FOnGetContent::CreateSP(this, &SSequencer::MakePlaybackMenu),
			LOCTEXT("PlaybackOptions", "Playback Options"),
			LOCTEXT("PlaybackOptionsToolTip", "Playback Options"),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "Sequencer.PlaybackOptions")
		);

		ToolBarBuilder.AddComboButton(
			FUIAction(),
			FOnGetContent::CreateSP(this, &SSequencer::MakeSelectEditMenu),
			LOCTEXT("SelectEditOptions", "Select/Edit Options"),
			LOCTEXT("SelectEditOptionsToolTip", "Select/Edit Options"),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "Sequencer.SelectEditOptions")
		);

		ToolBarBuilder.AddSeparator();

		TAttribute<FSlateIcon> KeyGroupModeIcon;
		KeyGroupModeIcon.Bind(TAttribute<FSlateIcon>::FGetter::CreateLambda([&] {
			switch (SequencerPtr.Pin()->GetKeyGroupMode())
			{
			case EKeyGroupMode::KeyAll:
				return FSequencerCommands::Get().SetKeyAll->GetIcon();
			case EKeyGroupMode::KeyGroup:
				return FSequencerCommands::Get().SetKeyGroup->GetIcon();
			default: // EKeyGroupMode::KeyChanged
				return FSequencerCommands::Get().SetKeyChanged->GetIcon();
			}
		}));

		TAttribute<FText> KeyGroupModeToolTip;
		KeyGroupModeToolTip.Bind(TAttribute<FText>::FGetter::CreateLambda([&] {
			switch (SequencerPtr.Pin()->GetKeyGroupMode())
			{
			case EKeyGroupMode::KeyAll:
				return FSequencerCommands::Get().SetKeyAll->GetDescription();
			case EKeyGroupMode::KeyGroup:
				return FSequencerCommands::Get().SetKeyGroup->GetDescription();
			default: // EKeyGroupMode::KeyChanged
				return FSequencerCommands::Get().SetKeyChanged->GetDescription();
			}
		}));

		ToolBarBuilder.AddComboButton(
			FUIAction(),
			FOnGetContent::CreateSP(this, &SSequencer::MakeKeyGroupMenu),
			LOCTEXT("KeyGroup", "Key All"),
			KeyGroupModeToolTip,
			KeyGroupModeIcon);

		if (IVREditorModule::Get().IsVREditorModeActive() || (SequencerPtr.Pin()->IsLevelEditorSequencer() && ExactCast<ULevelSequence>(SequencerPtr.Pin()->GetFocusedMovieSceneSequence()) == nullptr))
		{
			TAttribute<FSlateIcon> AutoChangeModeIcon;
			AutoChangeModeIcon.Bind(TAttribute<FSlateIcon>::FGetter::CreateLambda( [&] {
				switch ( SequencerPtr.Pin()->GetAutoChangeMode() )
				{
				case EAutoChangeMode::AutoKey:
					return FSequencerCommands::Get().SetAutoKey->GetIcon();
				case EAutoChangeMode::AutoTrack:
					return FSequencerCommands::Get().SetAutoTrack->GetIcon();
				case EAutoChangeMode::All:
					return FSequencerCommands::Get().SetAutoChangeAll->GetIcon();
				default: // EAutoChangeMode::None
					return FSequencerCommands::Get().SetAutoChangeNone->GetIcon();
				}
			} ) );

			TAttribute<FText> AutoChangeModeToolTip;
			AutoChangeModeToolTip.Bind( TAttribute<FText>::FGetter::CreateLambda( [&] {
				switch ( SequencerPtr.Pin()->GetAutoChangeMode() )
				{
				case EAutoChangeMode::AutoKey:
					return FSequencerCommands::Get().SetAutoKey->GetDescription();
				case EAutoChangeMode::AutoTrack:
					return FSequencerCommands::Get().SetAutoTrack->GetDescription();
				case EAutoChangeMode::All:
					return FSequencerCommands::Get().SetAutoChangeAll->GetDescription();
				default: // EAutoChangeMode::None
					return FSequencerCommands::Get().SetAutoChangeNone->GetDescription();
				}
			} ) );
			
			ToolBarBuilder.AddComboButton(
				FUIAction(),
				FOnGetContent::CreateSP(this, &SSequencer::MakeAutoChangeMenu),
				LOCTEXT("AutoChangeMode", "Auto-Change Mode"),
				AutoChangeModeToolTip,
				AutoChangeModeIcon);
		}
		else
		{
			ToolBarBuilder.AddToolBarButton( FSequencerCommands::Get().ToggleAutoKeyEnabled );
		}

		if( SequencerPtr.Pin()->IsLevelEditorSequencer() )
		{
			TAttribute<FSlateIcon> AllowEditsModeIcon;
			AllowEditsModeIcon.Bind(TAttribute<FSlateIcon>::FGetter::CreateLambda( [&] {
				switch ( SequencerPtr.Pin()->GetAllowEditsMode() )
				{
				case EAllowEditsMode::AllEdits:
					return FSequencerCommands::Get().AllowAllEdits->GetIcon();
				case EAllowEditsMode::AllowSequencerEditsOnly:
					return FSequencerCommands::Get().AllowSequencerEditsOnly->GetIcon();
				default: // EAllowEditsMode::AllowLevelEditsOnly
					return FSequencerCommands::Get().AllowLevelEditsOnly->GetIcon();
				}
			} ) );

			TAttribute<FText> AllowEditsModeToolTip;
			AllowEditsModeToolTip.Bind( TAttribute<FText>::FGetter::CreateLambda( [&] {
				switch ( SequencerPtr.Pin()->GetAllowEditsMode() )
				{
				case EAllowEditsMode::AllEdits:
					return FSequencerCommands::Get().AllowAllEdits->GetDescription();
				case EAllowEditsMode::AllowSequencerEditsOnly:
					return FSequencerCommands::Get().AllowSequencerEditsOnly->GetDescription();
				default: // EAllowEditsMode::AllowLevelEditsOnly
					return FSequencerCommands::Get().AllowLevelEditsOnly->GetDescription();
				}
			} ) );

			ToolBarBuilder.AddComboButton(
				FUIAction(),
				FOnGetContent::CreateSP(this, &SSequencer::MakeAllowEditsMenu),
				LOCTEXT("AllowMode", "Allow Edits"),
				AllowEditsModeToolTip,
				AllowEditsModeIcon);
		}
	}
	ToolBarBuilder.EndSection();


	ToolBarBuilder.BeginSection("Snapping");
	{
		ToolBarBuilder.AddToolBarButton( FSequencerCommands::Get().ToggleIsSnapEnabled, NAME_None, TAttribute<FText>( FText::GetEmpty() ) );

		ToolBarBuilder.AddComboButton(
			FUIAction(),
			FOnGetContent::CreateSP( this, &SSequencer::MakeSnapMenu ),
			LOCTEXT( "SnapOptions", "Options" ),
			LOCTEXT( "SnapOptionsToolTip", "Snapping Options" ),
			TAttribute<FSlateIcon>(),
			true );

		ToolBarBuilder.AddSeparator();

		ToolBarBuilder.AddWidget(SNew(SSequencerPlayRateCombo, SequencerPtr.Pin(), SharedThis(this)));
	}
	ToolBarBuilder.EndSection();

	ToolBarBuilder.BeginSection("Curve Editor");
	{
		// Only add the button if we have a toolkit host to spawn tabs in
		if (SequencerPtr.Pin()->GetToolkitHost().IsValid())
		{
			ToolBarBuilder.AddToolBarButton( FSequencerCommands::Get().ToggleShowCurveEditor );
		}
	}
	ToolBarBuilder.EndSection();

	return ToolBarBuilder.MakeWidget();
}


void SSequencer::GetContextMenuContent(FMenuBuilder& MenuBuilder)
{
	// let toolkits populate the menu
	MenuBuilder.BeginSection("MainMenu");
	OnGetAddMenuContent.ExecuteIfBound(MenuBuilder, SequencerPtr.Pin().ToSharedRef());
	MenuBuilder.EndSection();

	// let track editors & object bindings populate the menu
	TSharedPtr<FSequencer> Sequencer = SequencerPtr.Pin();

	// Always create the section so that we afford extension
	MenuBuilder.BeginSection("ObjectBindings");
	if (Sequencer.IsValid())
	{
		Sequencer->BuildAddObjectBindingsMenu(MenuBuilder);
	}
	MenuBuilder.EndSection();

	// Always create the section so that we afford extension
	MenuBuilder.BeginSection("AddTracks");
	if (Sequencer.IsValid())
	{
		Sequencer->BuildAddTrackMenu(MenuBuilder);
	}
	MenuBuilder.EndSection();
}


TSharedRef<SWidget> SSequencer::MakeAddMenu()
{
	FMenuBuilder MenuBuilder(true, nullptr, AddMenuExtender);
	{
		GetContextMenuContent(MenuBuilder);
	}

	return MenuBuilder.MakeWidget();
}

TSharedRef<SWidget> SSequencer::MakeFilterMenu()
{
	FMenuBuilder MenuBuilder(false, nullptr, AddMenuExtender);

	// let track editors & object bindings populate the menu
	TSharedPtr<FSequencer> Sequencer = SequencerPtr.Pin();

	MenuBuilder.BeginSection("SequencerTracksResetFilters");
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("FilterListEnableAll", "Enable All"),
			LOCTEXT("FilterListEnableAllToolTip", "Selects all filters"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(this, &SSequencer::OnEnableAllFilters)));

		MenuBuilder.AddMenuEntry(
			LOCTEXT("FilterListResetFilters", "Reset Filters"),
			LOCTEXT("FilterListResetToolTip", "Resets current filter selection"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(this, &SSequencer::OnResetFilters))
		);
	}
	MenuBuilder.EndSection();

	UObject* PlaybackContext = Sequencer->GetPlaybackContext();
	UWorld* World = PlaybackContext ? PlaybackContext->GetWorld() : nullptr;

	if (World && World->GetLevels().Num() > 1)
	{
		MenuBuilder.BeginSection("TrackLevelFilters");
		MenuBuilder.AddSubMenu(LOCTEXT("LevelFilters", "Level Filters"), LOCTEXT("LevelFiltersToolTip", "Filter object tracks by level"), FNewMenuDelegate::CreateRaw(this, &SSequencer::FillLevelFilterMenu), false);
		MenuBuilder.EndSection();
	}

	MenuBuilder.BeginSection("TrackFilters");
	
	for (TSharedRef<FSequencerTrackFilter> TrackFilter : AllTrackFilters)
	{
		if (TrackFilter->SupportsSequence(Sequencer->GetFocusedMovieSceneSequence()))
		{
			MenuBuilder.AddMenuEntry(
				TrackFilter->GetDisplayName(),
				TrackFilter->GetToolTipText(),
				TrackFilter->GetIcon(),
				FUIAction(
				FExecuteAction::CreateSP(this, &SSequencer::OnTrackFilterClicked, TrackFilter),
				FCanExecuteAction(),
				FIsActionChecked::CreateSP(this, &SSequencer::IsTrackFilterActive, TrackFilter)),
				NAME_None,
				EUserInterfaceActionType::ToggleButton
			);
		}
	}

	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

void SSequencer::FillLevelFilterMenu(FMenuBuilder& InMenuBarBuilder)
{
	TSharedPtr<FSequencer> Sequencer = SequencerPtr.Pin();
	UObject* PlaybackContext = Sequencer->GetPlaybackContext();
	UWorld* World = PlaybackContext ? PlaybackContext->GetWorld() : nullptr;

	if (World)
	{
		const TArray<ULevel*> Levels = World->GetLevels();

		if (Levels.Num() > 0)
		{
			InMenuBarBuilder.BeginSection("SequencerTracksResetLevelFilters");

			InMenuBarBuilder.AddMenuEntry(
				LOCTEXT("EnableAllLevelFilters", "Enable All"),
				LOCTEXT("EnableAllLevelFiltersToolTip", "Enables all level filters"),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateSP(this, &SSequencer::OnEnableAllLevelFilters, true)));

			InMenuBarBuilder.AddMenuEntry(
				LOCTEXT("DisableAllLevelFilters", "Disable All"),
				LOCTEXT("DisableAllLevelFiltersToolTip", "Disable all level filters"),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateSP(this, &SSequencer::OnEnableAllLevelFilters, false)));

			InMenuBarBuilder.EndSection();

			InMenuBarBuilder.AddMenuSeparator();
		}

		for (ULevel* Level : Levels)
		{
			FString LevelName = FPackageName::GetShortName(Level->GetOutermost()->GetName());
			InMenuBarBuilder.AddMenuEntry(
				FText::FromString(LevelName),
				FText::FromString(Level->GetOutermost()->GetName()),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateSP(this, &SSequencer::OnTrackLevelFilterClicked, LevelName),
					FCanExecuteAction(),
					FIsActionChecked::CreateSP(this, &SSequencer::IsTrackLevelFilterActive, LevelName)),
				NAME_None,
				EUserInterfaceActionType::ToggleButton
			);
		}
	}
}

void SSequencer::OnResetFilters()
{
	TSharedPtr<FSequencer> Sequencer = SequencerPtr.Pin();
	Sequencer->GetNodeTree()->RemoveAllFilters();
}

void SSequencer::OnEnableAllFilters()
{
	TSharedPtr<FSequencer> Sequencer = SequencerPtr.Pin();

	for (TSharedRef<FSequencerTrackFilter> TrackFilter : AllTrackFilters)
	{
		if (TrackFilter->SupportsSequence(Sequencer->GetFocusedMovieSceneSequence()))
		{
			if (!Sequencer->GetNodeTree()->IsTrackFilterActive(TrackFilter))
			{
				Sequencer->GetNodeTree()->AddFilter(TrackFilter);
			}
		}
	}
}

void SSequencer::OnTrackFilterClicked(TSharedRef<FSequencerTrackFilter> TrackFilter)
{
	TSharedPtr<FSequencer> Sequencer = SequencerPtr.Pin();
	if (IsTrackFilterActive(TrackFilter))
	{
		Sequencer->GetNodeTree()->RemoveFilter(TrackFilter);
	}
	else
	{
		Sequencer->GetNodeTree()->AddFilter(TrackFilter);
	}
}

bool SSequencer::IsTrackFilterActive(TSharedRef<FSequencerTrackFilter> TrackFilter) const
{
	TSharedPtr<FSequencer> Sequencer = SequencerPtr.Pin();
	return Sequencer->GetNodeTree()->IsTrackFilterActive(TrackFilter);
}

void SSequencer::OnEnableAllLevelFilters(bool bEnableAll)
{
	TSharedPtr<FSequencer> Sequencer = SequencerPtr.Pin();
	UObject* PlaybackContext = Sequencer->GetPlaybackContext();
	UWorld* World = PlaybackContext ? PlaybackContext->GetWorld() : nullptr;

	if (World)
	{
		const TArray<ULevel*> Levels = World->GetLevels();

		for (ULevel* Level : Levels)
		{
			FString LevelName = FPackageName::GetShortName(Level->GetOutermost()->GetName());

			if (bEnableAll)
			{
				Sequencer->GetNodeTree()->AddLevelFilter(LevelName);
			}
			else
			{
				Sequencer->GetNodeTree()->RemoveLevelFilter(LevelName);
			}
		}
	}
}

void SSequencer::OnTrackLevelFilterClicked(const FString LevelName)
{
	TSharedPtr<FSequencer> Sequencer = SequencerPtr.Pin();
	if (IsTrackLevelFilterActive(LevelName))
	{
		Sequencer->GetNodeTree()->RemoveLevelFilter(LevelName);
	}
	else
	{
		Sequencer->GetNodeTree()->AddLevelFilter(LevelName);
	}
}

bool SSequencer::IsTrackLevelFilterActive(const FString LevelName) const
{
	TSharedPtr<FSequencer> Sequencer = SequencerPtr.Pin();
	return Sequencer->GetNodeTree()->IsTrackLevelFilterActive(LevelName);
}

TSharedRef<SWidget> SSequencer::MakeGeneralMenu()
{
	FMenuBuilder MenuBuilder( true, SequencerPtr.Pin()->GetCommandBindings() );
	TSharedPtr<FSequencer> Sequencer = SequencerPtr.Pin();

	// view options
	MenuBuilder.BeginSection( "ViewOptions", LOCTEXT( "ViewMenuHeader", "View" ) );
	{
		MenuBuilder.AddMenuEntry( FSequencerCommands::Get().ToggleLabelBrowser );
		MenuBuilder.AddMenuEntry( FSequencerCommands::Get().ToggleShowSelectedNodesOnly );
		MenuBuilder.AddMenuEntry( FSequencerCommands::Get().ToggleCombinedKeyframes );
		MenuBuilder.AddMenuEntry( FSequencerCommands::Get().ToggleChannelColors );
		MenuBuilder.AddMenuEntry( FSequencerCommands::Get().ToggleShowPreAndPostRoll );

		if (Sequencer->IsLevelEditorSequencer())
		{
			MenuBuilder.AddMenuEntry( FSequencerCommands::Get().FindInContentBrowser );
		}

		MenuBuilder.AddMenuEntry( FSequencerCommands::Get().ToggleExpandCollapseNodes );
		MenuBuilder.AddMenuEntry( FSequencerCommands::Get().ToggleExpandCollapseNodesAndDescendants );
		MenuBuilder.AddMenuEntry( FSequencerCommands::Get().ExpandAllNodesAndDescendants );
		MenuBuilder.AddMenuEntry( FSequencerCommands::Get().CollapseAllNodesAndDescendants );
		MenuBuilder.AddMenuEntry( FSequencerCommands::Get().SortAllNodesAndDescendants );
	}
	MenuBuilder.EndSection();

	MenuBuilder.AddMenuEntry(FSequencerCommands::Get().ToggleShowGotoBox);

	MenuBuilder.BeginSection( "Bindings", LOCTEXT( "BindingsMenuHeader", "Bindings" ) );

	if (SequencerPtr.Pin()->IsLevelEditorSequencer())
	{

		MenuBuilder.AddMenuEntry(FSequencerCommands::Get().FixActorReferences);
		MenuBuilder.AddMenuEntry(FSequencerCommands::Get().RebindPossessableReferences);
	}

	MenuBuilder.AddSubMenu(
		LOCTEXT("ExposeBindingLabel", "Exposed Binding Groups"),
		LOCTEXT("ExposeBindingTooltip", "Specifies options for exposing this binding to external systems as a persistent name."),
		FNewMenuDelegate::CreateSP(this, &SSequencer::PopulateExposeBindingsMenu)
	);

	MenuBuilder.EndSection();

	if ( SequencerPtr.Pin()->IsLevelEditorSequencer() )
	{
		MenuBuilder.AddMenuSeparator();
		
		MenuBuilder.AddMenuEntry( FSequencerCommands::Get().ImportFBX );
		MenuBuilder.AddMenuEntry( FSequencerCommands::Get().ExportFBX );
	}

	return MenuBuilder.MakeWidget();
}

void SSequencer::PopulateExposeBindingsMenu(FMenuBuilder& InMenuBuilder)
{
	InMenuBuilder.AddWidget(SNew(SExposedBindingsWidget, SequencerPtr), FText(), true);
}

void SSequencer::FillPlaybackSpeedMenu(FMenuBuilder& InMenuBarBuilder)
{
	const int32 NumPlaybackSpeeds = 7;
	float PlaybackSpeeds[NumPlaybackSpeeds] = { 0.1f, 0.25f, 0.5f, 1.0f, 2.0f, 5.0f, 10.0f };

	InMenuBarBuilder.BeginSection("PlaybackSpeed");
	for( uint32 PlaybackSpeedIndex = 1; PlaybackSpeedIndex < NumPlaybackSpeeds; ++PlaybackSpeedIndex )
	{
		float PlaybackSpeed = PlaybackSpeeds[PlaybackSpeedIndex];
		const FText MenuStr = FText::Format( LOCTEXT("PlaybackSpeedStr", "x{0}"), FText::AsNumber( PlaybackSpeed ) );
		InMenuBarBuilder.AddMenuEntry(MenuStr, FText(), FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda( [this, PlaybackSpeed]{ SequencerPtr.Pin()->SetPlaybackSpeed(PlaybackSpeed); }),
				FCanExecuteAction::CreateLambda([] { return true; }),
				FIsActionChecked::CreateLambda( [this, PlaybackSpeed]{ return SequencerPtr.Pin()->GetPlaybackSpeed() == PlaybackSpeed; })
				),
			NAME_None,
			EUserInterfaceActionType::RadioButton
			);
	}
	InMenuBarBuilder.EndSection();
}

void SSequencer::FillTimeDisplayFormatMenu(FMenuBuilder& MenuBuilder)
{
	TSharedPtr<FSequencer> Sequencer = SequencerPtr.Pin();
	bool bSupportsDropFormatDisplay = FTimecode::IsDropFormatTimecodeSupported(Sequencer->GetFocusedDisplayRate());

	const UEnum* FrameNumberDisplayEnum = StaticEnum<EFrameNumberDisplayFormats>();
	check(FrameNumberDisplayEnum);

	if (Settings)
	{
		for (int32 Index = 0; Index < FrameNumberDisplayEnum->NumEnums() - 1; Index++)
		{
			if (!FrameNumberDisplayEnum->HasMetaData(TEXT("Hidden"), Index))
			{
				EFrameNumberDisplayFormats Value = (EFrameNumberDisplayFormats)FrameNumberDisplayEnum->GetValueByIndex(Index);

				// Don't show Drop Frame Timecode when they're in a format that doesn't support it.
				if (Value == EFrameNumberDisplayFormats::DropFrameTimecode && !bSupportsDropFormatDisplay)
					continue;

				MenuBuilder.AddMenuEntry(
					FrameNumberDisplayEnum->GetDisplayNameTextByIndex(Index),
					FrameNumberDisplayEnum->GetToolTipTextByIndex(Index),
					FSlateIcon(),
					FUIAction(
						FExecuteAction::CreateUObject(Settings, &USequencerSettings::SetTimeDisplayFormat, Value),
						FCanExecuteAction(),
						FIsActionChecked::CreateLambda([=] { return Settings->GetTimeDisplayFormat() == Value; })
					),
					NAME_None,
					EUserInterfaceActionType::RadioButton
				);
			}
		}
	}
}

TSharedRef<SWidget> SSequencer::MakePlaybackMenu()
{
	FMenuBuilder MenuBuilder( true, SequencerPtr.Pin()->GetCommandBindings() );

	// playback range options
	MenuBuilder.BeginSection("PlaybackThisSequence", LOCTEXT("PlaybackThisSequenceHeader", "Playback - This Sequence"));
	{
		// Menu entry for the start position
		auto OnStartChanged = [=](double NewValue){

			// We clamp the new value when the value is set. We can't clamp in the UI because we need an unset Min/Max for linear scaling to work.
			double Min = -FLT_MAX;
			double Max = SequencerPtr.Pin()->GetPlaybackRange().GetUpperBoundValue().Value;

			NewValue = FMath::Clamp(NewValue, Min, Max);
			FFrameNumber ValueAsFrame = FFrameTime::FromDecimal(NewValue).GetFrame();

			FFrameNumber Upper = MovieScene::DiscreteExclusiveUpper(SequencerPtr.Pin()->GetPlaybackRange());

			TRange<FFrameNumber> NewRange = TRange<FFrameNumber>(FMath::Min(ValueAsFrame, Upper - 1), Upper);

			SequencerPtr.Pin()->SetPlaybackRange(NewRange);

			TRange<double> PlayRangeSeconds = SequencerPtr.Pin()->GetPlaybackRange() / SequencerPtr.Pin()->GetFocusedTickResolution();
			const double AdditionalRange = (PlayRangeSeconds.GetUpperBoundValue() - PlayRangeSeconds.GetLowerBoundValue()) * 0.1;

			TRange<double> NewClampRange = SequencerPtr.Pin()->GetClampRange();
			NewClampRange.SetLowerBoundValue(SequencerPtr.Pin()->GetPlaybackRange().GetLowerBoundValue() / SequencerPtr.Pin()->GetFocusedTickResolution() - AdditionalRange);
			if (SequencerPtr.Pin()->GetClampRange().GetLowerBoundValue() > NewClampRange.GetLowerBoundValue())
			{
				SequencerPtr.Pin()->SetClampRange(NewClampRange);
			}

			TRange<double> NewViewRange = SequencerPtr.Pin()->GetViewRange();
			NewViewRange.SetLowerBoundValue(SequencerPtr.Pin()->GetPlaybackRange().GetLowerBoundValue() / SequencerPtr.Pin()->GetFocusedTickResolution() - AdditionalRange);
			if (SequencerPtr.Pin()->GetViewRange().GetLowerBoundValue() > NewViewRange.GetLowerBoundValue())
			{
				SequencerPtr.Pin()->SetViewRange(NewViewRange);
			}
		};

		MenuBuilder.AddWidget(
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
				[
					SNew(SSpacer)
				]
			+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SSpinBox<double>)
						.TypeInterface(NumericTypeInterface)
						.IsEnabled_Lambda([=]() {
							return !SequencerPtr.Pin()->IsPlaybackRangeLocked();
						})
						.Style(&FEditorStyle::GetWidgetStyle<FSpinBoxStyle>("Sequencer.HyperlinkSpinBox"))
						.OnValueCommitted_Lambda([=](double Value, ETextCommit::Type){ OnStartChanged(Value); })
						.OnValueChanged_Lambda([=](double Value) { OnStartChanged(Value); })
						.OnBeginSliderMovement(OnPlaybackRangeBeginDrag)
						.OnEndSliderMovement_Lambda([=](double Value){ OnStartChanged(Value); OnPlaybackRangeEndDrag.ExecuteIfBound(); })
						.MinValue(TOptional<double>())
						.MaxValue(TOptional<double>())
						.Value_Lambda([=]() -> double {
							return SequencerPtr.Pin()->GetPlaybackRange().GetLowerBoundValue().Value;
						})
						.Delta(this, &SSequencer::GetSpinboxDelta)
						.LinearDeltaSensitivity(25)
			],
			LOCTEXT("PlaybackStartLabel", "Start"));

		// Menu entry for the end position
		auto OnEndChanged = [=](double NewValue) {

			// We clamp the new value when the value is set. We can't clamp in the UI because we need an unset Min/Max for linear scaling to work.
			double Min = SequencerPtr.Pin()->GetPlaybackRange().GetLowerBoundValue().Value;
			double Max = FLT_MAX;

			NewValue = FMath::Clamp(NewValue, Min, Max);
			FFrameNumber ValueAsFrame = FFrameTime::FromDecimal(NewValue).GetFrame();

			FFrameNumber Lower = MovieScene::DiscreteInclusiveLower(SequencerPtr.Pin()->GetPlaybackRange());
			SequencerPtr.Pin()->SetPlaybackRange(TRange<FFrameNumber>(Lower, FMath::Max(ValueAsFrame, Lower)));

			TRange<double> PlayRangeSeconds = SequencerPtr.Pin()->GetPlaybackRange() / SequencerPtr.Pin()->GetFocusedTickResolution();
			const double AdditionalRange = (PlayRangeSeconds.GetUpperBoundValue() - PlayRangeSeconds.GetLowerBoundValue()) * 0.1;

			TRange<double> NewClampRange = SequencerPtr.Pin()->GetClampRange();
			NewClampRange.SetUpperBoundValue(SequencerPtr.Pin()->GetPlaybackRange().GetUpperBoundValue() / SequencerPtr.Pin()->GetFocusedTickResolution() + AdditionalRange);
			if (SequencerPtr.Pin()->GetClampRange().GetUpperBoundValue() < NewClampRange.GetUpperBoundValue())
			{
				SequencerPtr.Pin()->SetClampRange(NewClampRange);
			}

			TRange<double> NewViewRange = SequencerPtr.Pin()->GetViewRange();
			NewViewRange.SetUpperBoundValue(SequencerPtr.Pin()->GetPlaybackRange().GetUpperBoundValue() / SequencerPtr.Pin()->GetFocusedTickResolution() + AdditionalRange);
			if (SequencerPtr.Pin()->GetViewRange().GetUpperBoundValue() < NewViewRange.GetUpperBoundValue())
			{
				SequencerPtr.Pin()->SetViewRange(NewViewRange);
			}
		};

		MenuBuilder.AddWidget(
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
				[
					SNew(SSpacer)
				]
			+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SSpinBox<double>)
						.TypeInterface(NumericTypeInterface)
						.IsEnabled_Lambda([=]() {
					 		return !SequencerPtr.Pin()->IsPlaybackRangeLocked();
						})
						.Style(&FEditorStyle::GetWidgetStyle<FSpinBoxStyle>("Sequencer.HyperlinkSpinBox"))
						.OnValueCommitted_Lambda([=](double Value, ETextCommit::Type){ OnEndChanged(Value); })
						.OnValueChanged_Lambda([=](double Value) { OnEndChanged(Value); })
						.OnBeginSliderMovement(OnPlaybackRangeBeginDrag)
						.OnEndSliderMovement_Lambda([=](double Value){ OnEndChanged(Value); OnPlaybackRangeEndDrag.ExecuteIfBound(); })
						.MinValue(TOptional<double>())
						.MaxValue(TOptional<double>())
						.Value_Lambda([=]() -> double {
					 		return SequencerPtr.Pin()->GetPlaybackRange().GetUpperBoundValue().Value;
						})
						.Delta(this, &SSequencer::GetSpinboxDelta)
						.LinearDeltaSensitivity(25)
				],
			LOCTEXT("PlaybackStartEnd", "End"));


		MenuBuilder.AddSubMenu(LOCTEXT("PlaybackSpeedHeader", "Playback Speed"), FText::GetEmpty(), FNewMenuDelegate::CreateRaw(this, &SSequencer::FillPlaybackSpeedMenu));

		MenuBuilder.AddMenuEntry( FSequencerCommands::Get().TogglePlaybackRangeLocked );

		if (SequencerPtr.Pin()->IsLevelEditorSequencer())
		{
			MenuBuilder.AddMenuEntry( FSequencerCommands::Get().ToggleRerunConstructionScripts );
		}
	}
	MenuBuilder.EndSection();

	MenuBuilder.BeginSection( "PlaybackAllSequences", LOCTEXT( "PlaybackRangeAllSequencesHeader", "Playback Range - All Sequences" ) );
	{
		if (SequencerPtr.Pin()->IsLevelEditorSequencer())
		{
			MenuBuilder.AddMenuEntry( FSequencerCommands::Get().ToggleEvaluateSubSequencesInIsolation );
		}

		MenuBuilder.AddMenuEntry( FSequencerCommands::Get().ToggleKeepCursorInPlaybackRangeWhileScrubbing );
		MenuBuilder.AddMenuEntry( FSequencerCommands::Get().ToggleKeepCursorInPlaybackRange );

		if (!SequencerPtr.Pin()->IsLevelEditorSequencer())
		{
			MenuBuilder.AddMenuEntry(FSequencerCommands::Get().ToggleKeepPlaybackRangeInSectionBounds);
		}
		
		MenuBuilder.AddMenuEntry(FSequencerCommands::Get().ToggleLinkCurveEditorTimeRange);

		// Menu entry for zero padding
		auto OnZeroPadChanged = [=](uint8 NewValue){
			Settings->SetZeroPadFrames(NewValue);
		};

		MenuBuilder.AddWidget(
			SNew(SHorizontalBox)	
			+ SHorizontalBox::Slot()
				[
					SNew(SSpacer)
				]
			+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SSpinBox<uint8>)
					.Style(&FEditorStyle::GetWidgetStyle<FSpinBoxStyle>("Sequencer.HyperlinkSpinBox"))
					.OnValueCommitted_Lambda([=](uint8 Value, ETextCommit::Type){ OnZeroPadChanged(Value); })
					.OnValueChanged_Lambda(OnZeroPadChanged)
					.MinValue(0)
					.MaxValue(8)
					.Value_Lambda([=]() -> uint8 {
						return Settings->GetZeroPadFrames();
					})
				],
			LOCTEXT("ZeroPaddingText", "Zero Pad Frame Numbers"));
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

TSharedRef<SWidget> SSequencer::MakeSelectEditMenu()
{
	FMenuBuilder MenuBuilder( true, SequencerPtr.Pin()->GetCommandBindings() );
	TSharedPtr<FSequencer> Sequencer = SequencerPtr.Pin();

	MenuBuilder.AddMenuEntry(FSequencerCommands::Get().ToggleShowTransformBox);
	MenuBuilder.AddMenuEntry(FSequencerCommands::Get().ToggleShowStretchBox);

	if (SequencerPtr.Pin()->IsLevelEditorSequencer())
	{
		MenuBuilder.AddMenuEntry(FSequencerCommands::Get().BakeTransform);
		MenuBuilder.AddMenuEntry(FSequencerCommands::Get().SyncSectionsUsingSourceTimecode);
	}

	// selection range options
	MenuBuilder.BeginSection("SelectionRange", LOCTEXT("SelectionRangeHeader", "Selection Range"));
	{
		MenuBuilder.AddMenuEntry(FSequencerCommands::Get().SetSelectionRangeStart);
		MenuBuilder.AddMenuEntry(FSequencerCommands::Get().SetSelectionRangeEnd);
		MenuBuilder.AddMenuEntry(FSequencerCommands::Get().ResetSelectionRange);
		MenuBuilder.AddMenuEntry(FSequencerCommands::Get().SelectKeysInSelectionRange);
		MenuBuilder.AddMenuEntry(FSequencerCommands::Get().SelectSectionsInSelectionRange);
		MenuBuilder.AddMenuEntry(FSequencerCommands::Get().SelectAllInSelectionRange);
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}


TSharedRef<SWidget> SSequencer::MakeSnapMenu()
{
	FMenuBuilder MenuBuilder( false, SequencerPtr.Pin()->GetCommandBindings() );

	MenuBuilder.BeginSection("FramesRanges", LOCTEXT("SnappingMenuFrameRangesHeader", "Frame Ranges") );
	{
		MenuBuilder.AddMenuEntry( FSequencerCommands::Get().ToggleAutoScroll );
		MenuBuilder.AddMenuEntry( FSequencerCommands::Get().ToggleShowRangeSlider );
	}
	MenuBuilder.EndSection();

	MenuBuilder.BeginSection( "KeySnapping", LOCTEXT( "SnappingMenuKeyHeader", "Key Snapping" ) );
	{
		MenuBuilder.AddMenuEntry( FSequencerCommands::Get().ToggleSnapKeyTimesToInterval );
		MenuBuilder.AddMenuEntry( FSequencerCommands::Get().ToggleSnapKeyTimesToKeys );
	}
	MenuBuilder.EndSection();

	MenuBuilder.BeginSection("SectionSnapping", LOCTEXT("SnappingMenuSectionHeader", "Section Snapping"));
	{
		MenuBuilder.AddMenuEntry(FSequencerCommands::Get().ToggleSnapSectionTimesToInterval);
		MenuBuilder.AddMenuEntry(FSequencerCommands::Get().ToggleSnapSectionTimesToSections);
	}
	MenuBuilder.EndSection();

	MenuBuilder.BeginSection("KeyAndSectionSnapping", LOCTEXT("SnappingMenuKeysAndSectionHeader", "Keys and Sections Snapping"));
	{
		MenuBuilder.AddMenuEntry(FSequencerCommands::Get().ToggleSnapKeysAndSectionsToPlayRange);
	}
	MenuBuilder.EndSection();

	MenuBuilder.BeginSection( "PlayTimeSnapping", LOCTEXT( "SnappingMenuPlayTimeHeader", "Play Time Snapping" ) );
	{
		MenuBuilder.AddMenuEntry( FSequencerCommands::Get().ToggleSnapPlayTimeToInterval );
		MenuBuilder.AddMenuEntry( FSequencerCommands::Get().ToggleSnapPlayTimeToKeys );
		MenuBuilder.AddMenuEntry( FSequencerCommands::Get().ToggleSnapPlayTimeToPressedKey );
		MenuBuilder.AddMenuEntry( FSequencerCommands::Get().ToggleSnapPlayTimeToDraggedKey );
	}
	MenuBuilder.EndSection();

	MenuBuilder.BeginSection( "CurveSnapping", LOCTEXT( "SnappingMenuCurveHeader", "Curve Snapping" ) );
	{
		MenuBuilder.AddMenuEntry( FSequencerCommands::Get().ToggleSnapCurveValueToInterval );
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}


TSharedRef<SWidget> SSequencer::MakeAutoChangeMenu()
{
	FMenuBuilder MenuBuilder(false, SequencerPtr.Pin()->GetCommandBindings());

	MenuBuilder.AddMenuEntry(FSequencerCommands::Get().SetAutoKey);

	if (SequencerPtr.Pin()->IsLevelEditorSequencer())
	{
		MenuBuilder.AddMenuEntry(FSequencerCommands::Get().SetAutoTrack);
	}

	if (IVREditorModule::Get().IsVREditorModeActive() || (SequencerPtr.Pin()->IsLevelEditorSequencer() && ExactCast<ULevelSequence>(SequencerPtr.Pin()->GetFocusedMovieSceneSequence()) == nullptr))
	{
		MenuBuilder.AddMenuEntry(FSequencerCommands::Get().SetAutoChangeAll);
	}

	MenuBuilder.AddMenuEntry(FSequencerCommands::Get().SetAutoChangeNone);

	return MenuBuilder.MakeWidget();
}

TSharedRef<SWidget> SSequencer::MakeAllowEditsMenu()
{
	FMenuBuilder MenuBuilder(false, SequencerPtr.Pin()->GetCommandBindings());

	MenuBuilder.AddMenuEntry(FSequencerCommands::Get().AllowAllEdits);
	MenuBuilder.AddMenuEntry(FSequencerCommands::Get().AllowSequencerEditsOnly);
	MenuBuilder.AddMenuEntry(FSequencerCommands::Get().AllowLevelEditsOnly);

	return MenuBuilder.MakeWidget();

}

TSharedRef<SWidget> SSequencer::MakeKeyGroupMenu()
{
	FMenuBuilder MenuBuilder(false, SequencerPtr.Pin()->GetCommandBindings());

	if (SequencerPtr.Pin()->IsLevelEditorSequencer())
	{
		MenuBuilder.AddMenuEntry(FSequencerCommands::Get().SetKeyAll);
		MenuBuilder.AddMenuEntry(FSequencerCommands::Get().SetKeyGroup);
		MenuBuilder.AddMenuEntry(FSequencerCommands::Get().SetKeyChanged);
	}

	// Interpolation
	MenuBuilder.BeginSection("SequencerInterpolation", LOCTEXT("KeyInterpolationMenu", "Default Key Interpolation"));
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("SetKeyInterpolationAuto", "Cubic (Auto)"),
			LOCTEXT("SetKeyInterpolationAutoTooltip", "Set key interpolation to auto"),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "Sequencer.IconKeyAuto"),
			FUIAction(
				FExecuteAction::CreateLambda([this] { SequencerPtr.Pin()->SetKeyInterpolation(EMovieSceneKeyInterpolation::Auto); }),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([this] { return SequencerPtr.Pin()->GetKeyInterpolation() == EMovieSceneKeyInterpolation::Auto; })),
			NAME_None,
			EUserInterfaceActionType::ToggleButton
		);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("SetKeyInterpolationUser", "Cubic (User)"),
			LOCTEXT("SetKeyInterpolationUserTooltip", "Set key interpolation to user"),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "Sequencer.IconKeyUser"),
			FUIAction(
				FExecuteAction::CreateLambda([this] { SequencerPtr.Pin()->SetKeyInterpolation(EMovieSceneKeyInterpolation::User); }),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([this] { return SequencerPtr.Pin()->GetKeyInterpolation() == EMovieSceneKeyInterpolation::User; })),
			NAME_None,
			EUserInterfaceActionType::ToggleButton
		);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("SetKeyInterpolationBreak", "Cubic (Break)"),
			LOCTEXT("SetKeyInterpolationBreakTooltip", "Set key interpolation to break"),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "Sequencer.IconKeyBreak"),
			FUIAction(
				FExecuteAction::CreateLambda([this] { SequencerPtr.Pin()->SetKeyInterpolation(EMovieSceneKeyInterpolation::Break); }),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([this] { return SequencerPtr.Pin()->GetKeyInterpolation() == EMovieSceneKeyInterpolation::Break; })),
			NAME_None,
			EUserInterfaceActionType::ToggleButton
		);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("SetKeyInterpolationLinear", "Linear"),
			LOCTEXT("SetKeyInterpolationLinearTooltip", "Set key interpolation to linear"),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "Sequencer.IconKeyLinear"),
			FUIAction(
				FExecuteAction::CreateLambda([this] { SequencerPtr.Pin()->SetKeyInterpolation(EMovieSceneKeyInterpolation::Linear); }),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([this] { return SequencerPtr.Pin()->GetKeyInterpolation() == EMovieSceneKeyInterpolation::Linear; })),
			NAME_None,
			EUserInterfaceActionType::ToggleButton
		);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("SetKeyInterpolationConstant", "Constant"),
			LOCTEXT("SetKeyInterpolationConstantTooltip", "Set key interpolation to constant"),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "Sequencer.IconKeyConstant"),
			FUIAction(
				FExecuteAction::CreateLambda([this] { SequencerPtr.Pin()->SetKeyInterpolation(EMovieSceneKeyInterpolation::Constant); }),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([this] { return SequencerPtr.Pin()->GetKeyInterpolation() == EMovieSceneKeyInterpolation::Constant; })),
			NAME_None,
			EUserInterfaceActionType::ToggleButton
		);
	}
	MenuBuilder.EndSection(); // SequencerInterpolation

	return MenuBuilder.MakeWidget();

}

TSharedRef<SWidget> SSequencer::MakeTimeRange(const TSharedRef<SWidget>& InnerContent, bool bShowWorkingRange, bool bShowViewRange, bool bShowPlaybackRange)
{
	ISequencerWidgetsModule& SequencerWidgets = FModuleManager::Get().LoadModuleChecked<ISequencerWidgetsModule>( "SequencerWidgets" );

	EShowRange ShowRange = EShowRange::None;
	if (bShowWorkingRange)
	{
		ShowRange |= EShowRange::WorkingRange;
	}
	if (bShowViewRange)
	{
		ShowRange |= EShowRange::ViewRange;
	}
	if (bShowPlaybackRange)
	{
		ShowRange |= EShowRange::PlaybackRange;
	}

	FTimeRangeArgs Args(
		ShowRange,
		TimeSliderController.ToSharedRef(),
		EVisibility::Visible,
		NumericTypeInterface.ToSharedRef()
		);
	return SequencerWidgets.CreateTimeRange(Args, InnerContent);
}

TSharedPtr<ITimeSlider> SSequencer::GetTopTimeSliderWidget() const
{
	return TopTimeSlider;
}

SSequencer::~SSequencer()
{
	USelection::SelectionChangedEvent.RemoveAll(this);


	TSharedPtr<FSequencer> Sequencer = SequencerPtr.Pin();
	if(Sequencer)
	{
		FTabId TabId = FTabId(SSequencer::CurveEditorTabName);
		TSharedPtr<SDockTab> CurveEditorTab = Sequencer->GetToolkitHost()->GetTabManager()->FindExistingLiveTab(TabId);
		if (CurveEditorTab)
		{
			CurveEditorTab->RequestCloseTab();
		}
	}
}


void SSequencer::RegisterActiveTimerForPlayback()
{
	if (!bIsActiveTimerRegistered)
	{
		bIsActiveTimerRegistered = true;
		RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateSP(this, &SSequencer::EnsureSlateTickDuringPlayback));
	}
}


EActiveTimerReturnType SSequencer::EnsureSlateTickDuringPlayback(double InCurrentTime, float InDeltaTime)
{
	if (SequencerPtr.IsValid())
	{
		auto PlaybackStatus = SequencerPtr.Pin()->GetPlaybackStatus();
		if (PlaybackStatus == EMovieScenePlayerStatus::Playing || PlaybackStatus == EMovieScenePlayerStatus::Recording || PlaybackStatus == EMovieScenePlayerStatus::Scrubbing)
		{
			return EActiveTimerReturnType::Continue;
		}
	}

	bIsActiveTimerRegistered = false;
	return EActiveTimerReturnType::Stop;
}


void RestoreSelectionState(const TArray<TSharedRef<FSequencerDisplayNode>>& DisplayNodes, TSet<FString>& SelectedPathNames, FSequencerSelection& SequencerSelection)
{
	for (TSharedRef<FSequencerDisplayNode> DisplayNode : DisplayNodes)
	{
		if (SelectedPathNames.Contains(DisplayNode->GetPathName()))
		{
			SequencerSelection.AddToSelection(DisplayNode);
		}

		RestoreSelectionState(DisplayNode->GetChildNodes(), SelectedPathNames, SequencerSelection);
	}
}

void SSequencer::UpdateLayoutTree()
{
	TrackArea->Empty();
	PinnedTrackArea->Empty();

	TSharedPtr<FSequencer> Sequencer = SequencerPtr.Pin();
	if ( Sequencer.IsValid() )
	{
		// Cache the selected path names so selection can be restored after the update.
		TSet<FString> SelectedPathNames;
		// Cache selected keys
		TSet<FSequencerSelectedKey> SelectedKeys = Sequencer->GetSelection().GetSelectedKeys();
		TSet<TWeakObjectPtr<UMovieSceneSection> > SelectedSections = Sequencer->GetSelection().GetSelectedSections();

		for (TSharedRef<const FSequencerDisplayNode> SelectedDisplayNode : Sequencer->GetSelection().GetSelectedOutlinerNodes().Array())
		{
			FString PathName = SelectedDisplayNode->GetPathName();
			if ( FName(*PathName).IsNone() == false )
			{
				SelectedPathNames.Add(PathName);
			}
		}

		// Add any additional paths that have been added by the user for nodes that may not exist yet but we want them to be selected
		// after the node tree is updated and we restore selections.
		SelectedPathNames.Append(AdditionalSelectionsToAdd);

		// Suspend broadcasting selection changes because we don't want unnecessary rebuilds.
		Sequencer->GetSelection().SuspendBroadcast();

		// Update the node tree
		Sequencer->GetNodeTree()->Update();

		// Restore the selection state. This is still needed to apply the AdditionalSelectionsToAdd hack.
		RestoreSelectionState(Sequencer->GetNodeTree()->GetRootNodes(), SelectedPathNames, SequencerPtr.Pin()->GetSelection());	// Update to actor selection.

		// This must come after the selection state has been restored so that the tree and curve editor are populated with the correctly selected nodes
		TreeView->Refresh();
		
		// If we've manually specified an additional selection to add it's because the item was newly created.
		// Now that the treeview has been refreshed and selection restored, we'll try to focus the first item
		// so that the view scrolls down when things are added to the bottom.
		if (AdditionalSelectionsToAdd.Num() > 0)
		{
			FString NodePath = AdditionalSelectionsToAdd[0];

			for (TSharedRef<FSequencerDisplayNode> Node : Sequencer->GetNodeTree()->GetAllNodes())
			{
				if (Node->GetPathName() == NodePath)
				{
					auto Parent = Node->GetParent();
					while (Parent.IsValid())
					{
						TreeView->SetItemExpansion(Parent->AsShared(), true);
						PinnedTreeView->SetItemExpansion(Parent->AsShared(), true);
						Parent = Parent->GetParent();
					}

					TreeView->RequestScrollIntoView(Node);
					break;
				}
			}
		}

		AdditionalSelectionsToAdd.Empty();

		if (Sequencer->GetFocusedMovieSceneSequence())
		{
			bool bAnyChanged = false;

			TSharedPtr<FSequencerNodeTree> NodeTree = Sequencer->GetNodeTree();
			const bool bHasSoloNodes = NodeTree->HasSoloNodes();
			for (TSharedRef<FSequencerDisplayNode> Node : NodeTree->GetAllNodes())
			{
				if (Node->GetType() == ESequencerNode::Track)
				{
					UMovieSceneTrack* Track = static_cast<FSequencerTrackNode&>(Node.Get()).GetTrack();
					bool bDisableEval = NodeTree->IsNodeMute(&Node.Get()) || (bHasSoloNodes && !NodeTree->IsNodeSolo(&Node.Get()));
					if (bDisableEval != Track->IsEvalDisabled())
					{
						Track->Modify();
						Track->SetEvalDisabled(bDisableEval);
						bAnyChanged = true;
					}
					
				}
			}
			if (bAnyChanged)
			{
				Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::TrackValueChanged);
			}
		}

		// Continue broadcasting selection changes
		Sequencer->GetSelection().ResumeBroadcast();
	}
}

void SSequencer::UpdateBreadcrumbs()
{
	TSharedPtr<FSequencer> Sequencer = SequencerPtr.Pin();
	if (!Sequencer.IsValid())
	{
		return;
	}

	FMovieSceneSequenceID FocusedID = Sequencer->GetFocusedTemplateID();

	if (BreadcrumbTrail->PeekCrumb().BreadcrumbType == FSequencerBreadcrumb::ShotType)
	{
		BreadcrumbTrail->PopCrumb();
	}

	if( BreadcrumbTrail->PeekCrumb().BreadcrumbType == FSequencerBreadcrumb::MovieSceneType && BreadcrumbTrail->PeekCrumb().SequenceID != FocusedID )
	{
		TWeakObjectPtr<UMovieSceneSubSection> SubSection = Sequencer->FindSubSection(FocusedID);
		TAttribute<FText> CrumbNameAttribute = MakeAttributeSP(this, &SSequencer::GetBreadcrumbTextForSection, SubSection);

		// The current breadcrumb is not a moviescene so we need to make a new breadcrumb in order return to the parent moviescene later
		BreadcrumbTrail->PushCrumb( CrumbNameAttribute, FSequencerBreadcrumb( FocusedID, CrumbNameAttribute.Get()) );
	}
}


void SSequencer::ResetBreadcrumbs()
{
	BreadcrumbTrail->ClearCrumbs();

	TAttribute<FText> CrumbNameAttribute = MakeAttributeSP(this, &SSequencer::GetBreadcrumbTextForSequence, MakeWeakObjectPtr(SequencerPtr.Pin()->GetRootMovieSceneSequence()), true);
	BreadcrumbTrail->PushCrumb(CrumbNameAttribute, FSequencerBreadcrumb(MovieSceneSequenceID::Root, CrumbNameAttribute.Get()));
}

void SSequencer::PopBreadcrumb()
{
	BreadcrumbTrail->PopCrumb();
}

void SSequencer::OnOutlinerSearchChanged( const FText& Filter )
{
	TSharedPtr<FSequencer> Sequencer = SequencerPtr.Pin();
	if ( Sequencer.IsValid())
	{
		const FString FilterString = Filter.ToString();

		Sequencer->GetNodeTree()->FilterNodes( FilterString );

		TreeView->Refresh();

		if ( FilterString.StartsWith( TEXT( "label:" ) ) )
		{
			LabelBrowser->SetSelectedLabel(FilterString);
		}
		else
		{
			LabelBrowser->SetSelectedLabel( FString() );
		}
	}
}


void SSequencer::OnDragEnter( const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent )
{
	// @todo sequencer: Add drop validity cue
}


void SSequencer::OnDragLeave( const FDragDropEvent& DragDropEvent )
{
	// @todo sequencer: Clear drop validity cue
}


FReply SSequencer::OnDragOver( const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent )
{
	bool bIsDragSupported = false;

	TSharedPtr<FDragDropOperation> Operation = DragDropEvent.GetOperation();
	if (Operation.IsValid() && (
		Operation->IsOfType<FAssetDragDropOp>() ||
		Operation->IsOfType<FClassDragDropOp>() ||
		Operation->IsOfType<FActorDragDropGraphEdOp>() ) )
	{
		bIsDragSupported = true;
	}

	return bIsDragSupported ? FReply::Handled() : FReply::Unhandled();
}


FReply SSequencer::OnDrop( const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent )
{
	bool bWasDropHandled = false;

	// @todo sequencer: Get rid of hard-code assumptions about dealing with ACTORS at this level?

	// @todo sequencer: We may not want any actor-specific code here actually.  We need systems to be able to
	// register with sequencer to support dropping assets/classes/actors, or OTHER types!

	// @todo sequencer: Handle drag and drop from other FDragDropOperations, including unloaded classes/asset and external drags!

	// @todo sequencer: Consider allowing drops into the level viewport to add to the MovieScene as well.
	//		- Basically, when Sequencer is open it would take over drops into the level and auto-add puppets for these instead of regular actors
	//		- This would let people drag smoothly and precisely into the view to drop assets/classes into the scene

	TSharedPtr<FDragDropOperation> Operation = DragDropEvent.GetOperation();

	if (Operation.IsValid() )
	{
		if ( Operation->IsOfType<FAssetDragDropOp>() )
		{
			const auto& DragDropOp = StaticCastSharedPtr<FAssetDragDropOp>( Operation );

			OnAssetsDropped( *DragDropOp );
			bWasDropHandled = true;
		}
		else if( Operation->IsOfType<FClassDragDropOp>() )
		{
			const auto& DragDropOp = StaticCastSharedPtr<FClassDragDropOp>( Operation );

			OnClassesDropped( *DragDropOp );
			bWasDropHandled = true;
		}
		else if( Operation->IsOfType<FActorDragDropGraphEdOp>() )
		{
			const auto& DragDropOp = StaticCastSharedPtr<FActorDragDropGraphEdOp>( Operation );

			OnActorsDropped( *DragDropOp );
			bWasDropHandled = true;
		}
	}

	return bWasDropHandled ? FReply::Handled() : FReply::Unhandled();
}


FReply SSequencer::OnKeyDown( const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent ) 
{
	// A toolkit tab is active, so direct all command processing to it
	TSharedPtr<FSequencer> SequencerPin = SequencerPtr.Pin();
	if ( SequencerPin.IsValid() && SequencerPin->GetCommandBindings()->ProcessCommandBindings( InKeyEvent ) )
	{
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

void SSequencer::OnFocusChanging( const FWeakWidgetPath& PreviousFocusPath, const FWidgetPath& NewWidgetPath, const FFocusEvent& InFocusEvent )
{
	if (NewWidgetPath.ContainsWidget(AsShared()))
	{
		OnReceivedFocus.ExecuteIfBound();
	}
}

void SSequencer::OnAssetsDropped( const FAssetDragDropOp& DragDropOp )
{
	FSequencer& SequencerRef = *SequencerPtr.Pin();

	bool bObjectAdded = false;
	TArray< UObject* > DroppedObjects;
	bool bAllAssetsWereLoaded = true;
	bool bNeedsLoad = false;

	for (const FAssetData& AssetData : DragDropOp.GetAssets())
	{
		if (!AssetData.IsAssetLoaded())
		{
			bNeedsLoad = true;
			break;
		}
	}

	if (bNeedsLoad)
	{
		GWarn->BeginSlowTask(LOCTEXT("OnDrop_FullyLoadPackage", "Fully Loading Package For Drop"), true, false);
	}

	for (const FAssetData& AssetData : DragDropOp.GetAssets())
	{
		UObject* Object = AssetData.GetAsset();

		if ( Object != nullptr )
		{
			DroppedObjects.Add( Object );
		}
		else
		{
			bAllAssetsWereLoaded = false;
		}
	}

	if (bNeedsLoad)
	{
		GWarn->EndSlowTask();
	}

	const TSet< TSharedRef<FSequencerDisplayNode> >& SelectedNodes = SequencerPtr.Pin()->GetSelection().GetSelectedOutlinerNodes();
	FGuid TargetObjectGuid;
	// if exactly one object node is selected, we have a target object guid
	TSharedPtr<const FSequencerDisplayNode> DisplayNode;
	if (SelectedNodes.Num() == 1)
	{
		for (TSharedRef<const FSequencerDisplayNode> SelectedNode : SelectedNodes )
		{
			DisplayNode = SelectedNode;
		}
		if (DisplayNode.IsValid() && DisplayNode->GetType() == ESequencerNode::Object)
		{
			TSharedPtr<const FSequencerObjectBindingNode> ObjectBindingNode = StaticCastSharedPtr<const FSequencerObjectBindingNode>(DisplayNode);
			TargetObjectGuid = ObjectBindingNode->GetObjectBinding();
		}
	}

	for( auto CurObjectIter = DroppedObjects.CreateConstIterator(); CurObjectIter; ++CurObjectIter )
	{
		UObject* CurObject = *CurObjectIter;

		if (!SequencerRef.OnHandleAssetDropped(CurObject, TargetObjectGuid))
		{
			// Doesn't make sense to drop a level sequence asset into sequencer as a spawnable actor
			if (CurObject->IsA<ULevelSequence>())
			{
				UE_LOG(LogSequencer, Warning, TEXT("Can't add '%s' as a spawnable"), *CurObject->GetName());
				continue;
			}

			FGuid NewGuid = SequencerRef.MakeNewSpawnable( *CurObject, DragDropOp.GetActorFactory() );

			UMovieScene* MovieScene = SequencerRef.GetFocusedMovieSceneSequence()->GetMovieScene();
			if (MovieScene)
			{
				FMovieSceneSpawnable* Spawnable = MovieScene->FindSpawnable(NewGuid);

				if (Spawnable && Spawnable->GetObjectTemplate()->IsA<ACameraActor>())
				{
					SequencerRef.NewCameraAdded(NewGuid);
				}
			}
		}
		bObjectAdded = true;
	}

	if( bObjectAdded )
	{
		// Update the sequencers view of the movie scene data when any object is added
		SequencerRef.NotifyMovieSceneDataChanged( EMovieSceneDataChangeType::MovieSceneStructureItemAdded );

		// Update the tree and synchronize selection
		UpdateLayoutTree();

		SequencerRef.SynchronizeSequencerSelectionWithExternalSelection();
	}
}


void SSequencer::OnClassesDropped( const FClassDragDropOp& DragDropOp )
{
	FSequencer& SequencerRef = *SequencerPtr.Pin();

	for( auto ClassIter = DragDropOp.ClassesToDrop.CreateConstIterator(); ClassIter; ++ClassIter )
	{
		UClass* Class = ( *ClassIter ).Get();
		if( Class != nullptr )
		{
			UObject* Object = Class->GetDefaultObject();

			FGuid NewGuid = SequencerRef.MakeNewSpawnable( *Object );
		}
	}
}

void SSequencer::OnActorsDropped( FActorDragDropGraphEdOp& DragDropOp )
{
	SequencerPtr.Pin()->OnActorsDropped( DragDropOp.Actors );
}

void SSequencer::OnCrumbClicked(const FSequencerBreadcrumb& Item)
{
	if (Item.BreadcrumbType != FSequencerBreadcrumb::ShotType)
	{
		if( SequencerPtr.Pin()->GetFocusedTemplateID() == Item.SequenceID ) 
		{
			// then do zooming
		}
		else
		{
			SequencerPtr.Pin()->PopToSequenceInstance( Item.SequenceID );
		}
	}
}

void SSequencer::OnBreadcrumbPickerContentClicked(const FSequencerBreadcrumb& Breadcrumb)
{
	while (BreadcrumbTrail->NumCrumbs() > 1 && BreadcrumbTrail->PeekCrumb().SequenceID != Breadcrumb.SequenceID)
	{
		BreadcrumbTrail->PopCrumb();
	}
	OnCrumbClicked(Breadcrumb);
}

TSharedRef<SWidget> SSequencer::GetBreadcrumbPickerContent()
{
	TArray<FSequencerBreadcrumb> CrumbData;
	BreadcrumbTrail->GetAllCrumbData(CrumbData);

	FMenuBuilder MenuBuilder(true, nullptr);

	MenuBuilder.BeginSection("SequencerBreadcrumbPicker");

	for(FSequencerBreadcrumb& Breadcrumb : CrumbData)
	{
		MenuBuilder.AddMenuEntry(
			Breadcrumb.BreadcrumbName,
			FText::GetEmpty(),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda(([this, Breadcrumb]() { return this->OnBreadcrumbPickerContentClicked(Breadcrumb); } )))
		);
	}
	
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

FText SSequencer::GetRootAnimationName() const
{
	return SequencerPtr.Pin()->GetRootMovieSceneSequence()->GetDisplayName();
}


TSharedPtr<SSequencerTreeView> SSequencer::GetTreeView() const
{
	return TreeView;
}


void SSequencer::OnSaveMovieSceneClicked()
{
	SequencerPtr.Pin()->SaveCurrentMovieScene();
}


void SSequencer::OnSaveMovieSceneAsClicked()
{
	SequencerPtr.Pin()->SaveCurrentMovieSceneAs();
}


void SSequencer::StepToNextKey()
{
	StepToKey(true, false);
}


void SSequencer::StepToPreviousKey()
{
	StepToKey(false, false);
}


void SSequencer::StepToNextCameraKey()
{
	StepToKey(true, true);
}


void SSequencer::StepToPreviousCameraKey()
{
	StepToKey(false, true);
}


void SSequencer::StepToKey(bool bStepToNextKey, bool bCameraOnly)
{
	TSharedPtr<FSequencer> Sequencer = SequencerPtr.Pin();
	if ( Sequencer.IsValid() )
	{
		TSet< TSharedRef<FSequencerDisplayNode> > Nodes;

		if ( bCameraOnly )
		{
			TSet<TSharedRef<FSequencerDisplayNode>> RootNodes( Sequencer->GetNodeTree()->GetRootNodes() );

			TSet<TWeakObjectPtr<AActor> > LockedActors;
			for (FLevelEditorViewportClient* LevelVC : GEditor->GetLevelViewportClients())
			{
				if ( LevelVC && LevelVC->IsPerspective() && LevelVC->GetViewMode() != VMI_Unknown )
				{
					TWeakObjectPtr<AActor> ActorLock = LevelVC->GetActiveActorLock();
					if ( ActorLock.IsValid() )
					{
						LockedActors.Add( ActorLock );
					}
				}
			}

			for ( auto RootNode : RootNodes )
			{
				TSharedRef<FSequencerObjectBindingNode> ObjectBindingNode = StaticCastSharedRef<FSequencerObjectBindingNode>( RootNode );

				for (TWeakObjectPtr<>& Object : Sequencer->FindObjectsInCurrentSequence(ObjectBindingNode->GetObjectBinding()))
				{
					AActor* RuntimeActor = Cast<AActor>( Object.Get() );
					if ( RuntimeActor != nullptr && LockedActors.Contains( RuntimeActor ) )
					{
						Nodes.Add( RootNode );
					}
				}
			}
		}
		else
		{
			const TSet< TSharedRef<FSequencerDisplayNode> >& SelectedNodes = Sequencer->GetSelection().GetSelectedOutlinerNodes();
			Nodes = SelectedNodes;

			if ( Nodes.Num() == 0 )
			{
				TSet<TSharedRef<FSequencerDisplayNode>> RootNodes( Sequencer->GetNodeTree()->GetRootNodes() );
				for ( auto RootNode : RootNodes )
				{
					Nodes.Add( RootNode );

					SequencerHelpers::GetDescendantNodes( RootNode, Nodes );
				}
			}
		}

		if ( Nodes.Num() > 0 )
		{
			FFrameTime ClosestKeyDistance = FFrameTime(TNumericLimits<int32>::Max(), 0.99999f);
			FFrameTime CurrentTime = Sequencer->GetLocalTime().Time;
			TOptional<FFrameTime> NextTime;

			TOptional<FFrameNumber> StepToTime;

			auto It = Nodes.CreateConstIterator();
			bool bExpand = !( *It ).Get().IsExpanded();

			for ( auto Node : Nodes )
			{
				TArray<FFrameNumber> AllTimes;

				TSet<TSharedPtr<IKeyArea>> KeyAreas;
				SequencerHelpers::GetAllKeyAreas( Node, KeyAreas );
				for ( TSharedPtr<IKeyArea> KeyArea : KeyAreas )
				{
					KeyArea->GetKeyTimes(AllTimes, KeyArea->GetOwningSection()->GetRange());
				}

				TSet<TWeakObjectPtr<UMovieSceneSection> > Sections;
				SequencerHelpers::GetAllSections( Node, Sections );

				for ( TWeakObjectPtr<UMovieSceneSection> Section : Sections )
				{
					if (Section.IsValid())
					{
						if (Section->HasStartFrame())
						{
							AllTimes.Add(Section->GetInclusiveStartFrame());
						}
						if (Section->HasEndFrame())
						{
							AllTimes.Add(Section->GetExclusiveEndFrame());
						}
					}
				}

				for (FFrameNumber Time : AllTimes)
				{
					if ( bStepToNextKey )
					{
						if ( Time > CurrentTime && Time - CurrentTime < ClosestKeyDistance )
						{
							StepToTime = Time;
							ClosestKeyDistance = Time - CurrentTime;
						}
					}
					else
					{
						if ( Time < CurrentTime && CurrentTime - Time < ClosestKeyDistance )
						{
							StepToTime = Time;
							ClosestKeyDistance = CurrentTime - Time;
						}
					}
				}
			}

			if ( StepToTime.IsSet() )
			{
				Sequencer->SetLocalTime( StepToTime.GetValue() );
			}
		}
	}
}


FText SSequencer::GetBreadcrumbTextForSection(TWeakObjectPtr<UMovieSceneSubSection> SubSection) const
{
	UMovieSceneSubSection* SubSectionPtr = SubSection.Get();
	return SubSectionPtr ? GetBreadcrumbTextForSequence(SubSectionPtr->GetSequence(), SubSectionPtr->IsActive()) : FText();
}


FText SSequencer::GetBreadcrumbTextForSequence(TWeakObjectPtr<UMovieSceneSequence> Sequence, bool bIsActive) const
{
	UMovieSceneSequence* SequencePtr = Sequence.Get();

	bool bIsDirty = SequencePtr->GetMovieScene()->GetOuter()->GetOutermost()->IsDirty();

	if (bIsActive)
	{
		if (bIsDirty)
		{
			return FText::Format(LOCTEXT("DirtySequenceBreadcrumbFormat", "{0}*"), SequencePtr->GetDisplayName());
		}
		else
		{
			return SequencePtr->GetDisplayName();
		}
	}
	else
	{
		if (bIsDirty)
		{
			return FText::Format(LOCTEXT("DirtyInactiveSequenceBreadcrumbFormat", "{0}* [{1}]"),
				SequencePtr->GetDisplayName(),
				LOCTEXT("InactiveSequenceBreadcrumb", "Inactive"));

		}
		else
		{
			return FText::Format(LOCTEXT("InactiveSequenceBreadcrumbFormat", "{0} [{1}]"),
				SequencePtr->GetDisplayName(),
				LOCTEXT("InactiveSequenceBreadcrumb", "Inactive"));
		}
	}
}


EVisibility SSequencer::GetBreadcrumbTrailVisibility() const
{
	return SequencerPtr.Pin()->IsLevelEditorSequencer() ? EVisibility::Visible : EVisibility::Collapsed;
}


EVisibility SSequencer::GetBottomTimeSliderVisibility() const
{
	return Settings->GetShowRangeSlider() ? EVisibility::Hidden : EVisibility::Visible;
}


EVisibility SSequencer::GetTimeRangeVisibility() const
{
	return Settings->GetShowRangeSlider() ? EVisibility::Visible : EVisibility::Hidden;
}

EFrameNumberDisplayFormats SSequencer::GetTimeDisplayFormat() const
{
	return Settings->GetTimeDisplayFormat();
}


void SSequencer::OnColumnFillCoefficientChanged(float FillCoefficient, int32 ColumnIndex)
{
	ColumnFillCoefficients[ColumnIndex] = FillCoefficient;
}

void SSequencer::OnCurveEditorVisibilityChanged(bool bShouldBeVisible)
{
	TSharedPtr<FSequencer> Sequencer = SequencerPtr.Pin();
	FTabId TabId = FTabId(SSequencer::CurveEditorTabName);

	if (bShouldBeVisible)
	{
		// Request the Tab Manager invoke the tab. This will spawn the tab if needed, otherwise pull it to focus. This assumes
		// that the Toolkit Host's Tab Manager has already registered a tab with a NullWidget for content.
		TSharedRef<SDockTab> CurveEditorTab = Sequencer->GetToolkitHost()->GetTabManager()->InvokeTab(TabId);
		CurveEditorTab->SetContent(CurveEditorPanel.ToSharedRef());
		SequencerPtr.Pin()->GetCurveEditor()->ZoomToFit();
	}
	else
	{
		TSharedPtr<SDockTab> ExistingTab = Sequencer->GetToolkitHost()->GetTabManager()->FindExistingLiveTab(TabId);
		if (ExistingTab)
		{
			ExistingTab->RequestCloseTab();
		}
	}

	TreeView->UpdateTrackArea();
}


void SSequencer::OnTimeSnapIntervalChanged(float InInterval)
{
	// @todo: sequencer-timecode: Address dealing with different time intervals
	// TSharedPtr<FSequencer> Sequencer = SequencerPtr.Pin();
	// if ( Sequencer.IsValid() )
	// {
	// 	UMovieScene* MovieScene = Sequencer->GetFocusedMovieSceneSequence()->GetMovieScene();
	// 	if (!FMath::IsNearlyEqual(MovieScene->GetFixedFrameInterval(), InInterval))
	// 	{
	// 		FScopedTransaction SetFixedFrameIntervalTransaction( NSLOCTEXT( "Sequencer", "SetFixedFrameInterval", "Set scene fixed frame interval" ) );
	// 		MovieScene->Modify();
	// 		MovieScene->SetFixedFrameInterval( InInterval );

	// 		// Update the current time to the new interval
	// 		float NewTime = SequencerHelpers::SnapTimeToInterval(Sequencer->GetLocalTime(), InInterval);
	// 		Sequencer->SetLocalTime(NewTime);
	// 	}
	// }
}


FPaintPlaybackRangeArgs SSequencer::GetSectionPlaybackRangeArgs() const
{
	if (GetBottomTimeSliderVisibility() == EVisibility::Visible)
	{
		static FPaintPlaybackRangeArgs Args(FEditorStyle::GetBrush("Sequencer.Timeline.PlayRange_L"), FEditorStyle::GetBrush("Sequencer.Timeline.PlayRange_R"), 6.f);
		return Args;
	}
	else
	{
		static FPaintPlaybackRangeArgs Args(FEditorStyle::GetBrush("Sequencer.Timeline.PlayRange_Bottom_L"), FEditorStyle::GetBrush("Sequencer.Timeline.PlayRange_Bottom_R"), 6.f);
		return Args;
	}
}


FVirtualTrackArea SSequencer::GetVirtualTrackArea(const SSequencerTrackArea* InTrackArea) const
{
	const SSequencerTrackArea* TargetTrackArea = TrackArea.Get();
	TSharedPtr<SSequencerTreeView> TargetTreeView = TreeView;
	
	if (InTrackArea != nullptr)
	{
		TargetTrackArea = InTrackArea;
		TargetTreeView = TargetTrackArea->GetTreeView().Pin();
	}

	return FVirtualTrackArea(*SequencerPtr.Pin(), *TargetTreeView.Get(), TargetTrackArea->GetCachedGeometry());
}

FPasteContextMenuArgs SSequencer::GeneratePasteArgs(FFrameNumber PasteAtTime, TSharedPtr<FMovieSceneClipboard> Clipboard)
{
	TSharedPtr<FSequencer> Sequencer = SequencerPtr.Pin();
	if (Settings->GetIsSnapEnabled())
	{
		FFrameRate TickResolution = Sequencer->GetFocusedTickResolution();
		FFrameRate DisplayRate    = Sequencer->GetFocusedDisplayRate();

		PasteAtTime = ConvertFrameTime(PasteAtTime, TickResolution, DisplayRate).RoundToFrame();
		PasteAtTime = ConvertFrameTime(PasteAtTime, DisplayRate, TickResolution).FrameNumber;
	}

	// Open a paste menu at the current mouse position
	FSlateApplication& Application = FSlateApplication::Get();
	FVector2D LocalMousePosition = TrackArea->GetCachedGeometry().AbsoluteToLocal(Application.GetCursorPos());

	FVirtualTrackArea VirtualTrackArea = GetVirtualTrackArea();

	// Paste into the currently selected sections, or hit test the mouse position as a last resort
	TArray<TSharedRef<FSequencerDisplayNode>> PasteIntoNodes;
	{
		TSet<TWeakObjectPtr<UMovieSceneSection>> Sections = Sequencer->GetSelection().GetSelectedSections();
		for (const FSequencerSelectedKey& Key : Sequencer->GetSelection().GetSelectedKeys())
		{
			Sections.Add(Key.Section);
		}

		for (TWeakObjectPtr<UMovieSceneSection> WeakSection : Sections)
		{
			if (TOptional<FSectionHandle> Handle = Sequencer->GetNodeTree()->GetSectionHandle(WeakSection.Get()))
			{
				PasteIntoNodes.Add(Handle->GetTrackNode());
			}
		}
	}

	if (PasteIntoNodes.Num() == 0)
	{
		TSharedPtr<FSequencerDisplayNode> Node = VirtualTrackArea.HitTestNode(LocalMousePosition.Y);
		if (Node.IsValid())
		{
			PasteIntoNodes.Add(Node.ToSharedRef());
		}
	}

	return FPasteContextMenuArgs::PasteInto(MoveTemp(PasteIntoNodes), PasteAtTime, Clipboard);
}

void SSequencer::OnPaste()
{
	TSharedPtr<FSequencer> Sequencer = SequencerPtr.Pin();
	TSet<TSharedRef<FSequencerDisplayNode>> SelectedNodes = Sequencer->GetSelection().GetSelectedOutlinerNodes();
	if (SelectedNodes.Num() == 0)
	{
		if (OpenPasteMenu())
		{
			return;
		}
	}

	DoPaste();
}

bool SSequencer::CanPaste()
{
	TSharedPtr<FSequencer> Sequencer = SequencerPtr.Pin();
	
	FString TextToImport;
	FPlatformApplicationMisc::ClipboardPaste(TextToImport);

	// Attempts to deserialize the text into object bindings/tracks that Sequencer understands.
	if (Sequencer->CanPaste(TextToImport))
	{
		TArray<UMovieSceneCopyableTrack*> ImportedTracks;
		TArray<UMovieSceneSection*> ImportedSections;
		TArray<UMovieSceneCopyableBinding*> ImportedObjects;
		Sequencer->ImportTracksFromText(TextToImport, ImportedTracks);
		Sequencer->ImportSectionsFromText(TextToImport, ImportedSections);
		Sequencer->ImportObjectBindingsFromText(TextToImport, ImportedObjects);

		// If we couldn't deserialize any tracks or objects then the data isn't valid for sequencer,
		// and we'll block a paste attempt.
		if (ImportedTracks.Num() == 0 && ImportedSections.Num() == 0 && ImportedObjects.Num() == 0)
		{
			return false;
		}

		// Otherwise, as long as they have one or the other, there is something to paste.
		return true;
	}

	return SequencerPtr.Pin()->GetClipboardStack().Num() != 0;
}

void SSequencer::DoPaste()
{
	TSharedPtr<FSequencer> Sequencer = SequencerPtr.Pin();

	Sequencer->DoPaste();
}

bool SSequencer::OpenPasteMenu()
{
	TSharedPtr<FPasteContextMenu> ContextMenu;

	TSharedPtr<FSequencer> Sequencer = SequencerPtr.Pin();
	if (Sequencer->GetClipboardStack().Num() != 0)
	{
		FPasteContextMenuArgs Args = GeneratePasteArgs(Sequencer->GetLocalTime().Time.FrameNumber, Sequencer->GetClipboardStack().Last());
		ContextMenu = FPasteContextMenu::CreateMenu(*Sequencer, Args);
	}

	if (!ContextMenu.IsValid() || !ContextMenu->IsValidPaste())
	{
		return false;
	}
	else if (ContextMenu->AutoPaste())
	{
		return true;
	}

	const bool bShouldCloseWindowAfterMenuSelection = true;
	FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, SequencerPtr.Pin()->GetCommandBindings());

	ContextMenu->PopulateMenu(MenuBuilder);

	FWidgetPath Path;
	FSlateApplication::Get().FindPathToWidget(AsShared(), Path);
	
	FSlateApplication::Get().PushMenu(
		AsShared(),
		Path,
		MenuBuilder.MakeWidget(),
		FSlateApplication::Get().GetCursorPos(),
		FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu)
		);

	return true;
}

void SSequencer::PasteFromHistory()
{
	TSharedPtr<FSequencer> Sequencer = SequencerPtr.Pin();
	if (Sequencer->GetClipboardStack().Num() == 0)
	{
		return;
	}

	FPasteContextMenuArgs Args = GeneratePasteArgs(Sequencer->GetLocalTime().Time.FrameNumber);
	TSharedPtr<FPasteFromHistoryContextMenu> ContextMenu = FPasteFromHistoryContextMenu::CreateMenu(*Sequencer, Args);

	if (ContextMenu.IsValid())
	{
		const bool bShouldCloseWindowAfterMenuSelection = true;
		FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, Sequencer->GetCommandBindings());

		ContextMenu->PopulateMenu(MenuBuilder);

		FWidgetPath Path;
		FSlateApplication::Get().FindPathToWidget(AsShared(), Path);
		
		FSlateApplication::Get().PushMenu(
			AsShared(),
			Path,
			MenuBuilder.MakeWidget(),
			FSlateApplication::Get().GetCursorPos(),
			FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu)
			);
	}
}

EVisibility SSequencer::GetDebugVisualizerVisibility() const
{
	return Settings->ShouldShowDebugVisualization() ? EVisibility::Visible : EVisibility::Collapsed;
}

void SSequencer::BuildCustomContextMenuForGuid(FMenuBuilder& MenuBuilder, FGuid ObjectBinding)
{
	OnBuildCustomContextMenuForGuid.ExecuteIfBound(MenuBuilder, ObjectBinding);
}

double SSequencer::GetSpinboxDelta() const
{
	TSharedPtr<FSequencer> Sequencer = SequencerPtr.Pin();
	return Sequencer->GetDisplayRateDeltaFrameCount();
}

float SSequencer::GetPlayTimeMinDesiredWidth() const
{
	TRange<double> ViewRange = SequencerPtr.Pin()->GetViewRange();

	FString LowerBoundStr = NumericTypeInterface->ToString(ViewRange.GetLowerBoundValue());
	FString UpperBoundStr = NumericTypeInterface->ToString(ViewRange.GetUpperBoundValue());

	const FSlateFontInfo NormalFont = FCoreStyle::Get().GetFontStyle(TEXT("NormalFont"));
	
	const TSharedRef< FSlateFontMeasure > FontMeasureService = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();

	FVector2D LowerTextSize = FontMeasureService->Measure(LowerBoundStr, NormalFont);
	FVector2D UpperTextSize = FontMeasureService->Measure(UpperBoundStr, NormalFont);

	return FMath::Max(LowerTextSize.X, UpperTextSize.X);
}


bool SSequencer::GetIsSequenceReadOnly() const
{
	TSharedPtr<FSequencer> Sequencer = SequencerPtr.Pin();
	return Sequencer->GetFocusedMovieSceneSequence() ? Sequencer->GetFocusedMovieSceneSequence()->GetMovieScene()->IsReadOnly() : false;
}

void SSequencer::OnSetSequenceReadOnly(ECheckBoxState CheckBoxState)
{
	TSharedPtr<FSequencer> Sequencer = SequencerPtr.Pin();
	
	bool bReadOnly = CheckBoxState == ECheckBoxState::Checked;

	if (Sequencer->GetFocusedMovieSceneSequence())
	{
		UMovieScene* MovieScene = Sequencer->GetFocusedMovieSceneSequence()->GetMovieScene();
		const FScopedTransaction Transaction(CheckBoxState == ECheckBoxState::Checked ? LOCTEXT("LockMovieScene", "Lock Movie Scene") : LOCTEXT("UnlockMovieScene", "Unlock Movie Scene") );

		MovieScene->Modify();
		MovieScene->SetReadOnly(bReadOnly);

		TArray<UMovieScene*> DescendantMovieScenes;
		MovieSceneHelpers::GetDescendantMovieScenes(Sequencer->GetFocusedMovieSceneSequence(), DescendantMovieScenes);

		for (UMovieScene* DescendantMovieScene : DescendantMovieScenes)
		{
			if (DescendantMovieScene && bReadOnly != DescendantMovieScene->IsReadOnly())
			{
				DescendantMovieScene->Modify();
				DescendantMovieScene->SetReadOnly(bReadOnly);
			}
		}

		Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::Unknown);
	}
}

void SSequencer::SetPlayTimeClampedByWorkingRange(double Frame)
{
	if (SequencerPtr.IsValid())
	{
		TSharedPtr<FSequencer> Sequencer = SequencerPtr.Pin();
		// Some of our spin boxes need to use an unbounded min/max so that they can drag linearly instead of based on the current value.
		// We clamp the value here by the working range to emulate the behavior of the Cinematic Level Viewport
		FFrameRate   PlayRate = Sequencer->GetLocalTime().Rate;
		UMovieScene* MovieScene = Sequencer->GetFocusedMovieSceneSequence()->GetMovieScene();
		double StartInSeconds = MovieScene->GetEditorData().WorkStart;
		double EndInSeconds = MovieScene->GetEditorData().WorkEnd;

		Frame = FMath::Clamp(Frame, (double)(StartInSeconds*PlayRate).GetFrame().Value, (double)(EndInSeconds*PlayRate).GetFrame().Value);

		Sequencer->SetLocalTime(FFrameTime::FromDecimal(Frame));
	}
}

#undef LOCTEXT_NAMESPACE

