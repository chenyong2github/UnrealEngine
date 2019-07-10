// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "CurveEditor.h"
#include "Layout/Geometry.h"
#include "CurveEditorSnapMetrics.h"
#include "CurveEditorCommands.h"
#include "CurveEditorSettings.h"
#include "CurveDrawInfo.h"
#include "Framework/Commands/GenericCommands.h"
#include "Framework/Commands/UICommandList.h"
#include "Editor.h"
#include "ScopedTransaction.h"
#include "SCurveEditorView.h"
#include "SCurveEditorPanel.h"
#include "ICurveEditorExtension.h"
#include "ICurveEditorToolExtension.h"
#include "Tree/ICurveEditorTreeItem.h"
#include "Filters/CurveEditorFilterBase.h"
#include "Filters/SCurveEditorFilterPanel.h"
#include "Editor/EditorEngine.h"
#include "ITimeSlider.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "CurveEditor"

FCurveModelID FCurveModelID::Unique()
{
	static uint32 CurrentID = 1;

	FCurveModelID ID;
	ID.ID = CurrentID++;
	return ID;
}

FCurveEditor::FCurveEditor()
	: Bounds(new FStaticCurveEditorBounds)
	, bBoundTransformUpdatesSuppressed(false)
	, ActiveCurvesSerialNumber(0)
{
	Settings = GetMutableDefault<UCurveEditorSettings>();
	CommandList = MakeShared<FUICommandList>();

	OutputSnapEnabledAttribute = true;
	InputSnapEnabledAttribute  = true;
	OutputSnapIntervalAttribute = 0.1;
	InputSnapRateAttribute = FFrameRate(10, 1);

	GridLineLabelFormatXAttribute = LOCTEXT("GridXLabelFormat", "{0}s");
	GridLineLabelFormatYAttribute = LOCTEXT("GridYLabelFormat", "{0}");
}

void FCurveEditor::InitCurveEditor(const FCurveEditorInitParams& InInitParams)
{
	ICurveEditorModule& CurveEditorModule = FModuleManager::LoadModuleChecked<ICurveEditorModule>("CurveEditor");

	// Editor Extensions can be registered in the Curve Editor module. To allow users to derive from FCurveEditor
	// we have to manually reach out to the module and get a list of extensions to create an instance of them.
	// If none of your extensions are showing up, it's because you forgot to call this function after construction
	// We're not allowed to use SharedThis(...) in a Constructor so it must exist as a separate function call.
	TArrayView<const FOnCreateCurveEditorExtension> Extensions = CurveEditorModule.GetEditorExtensions();
	for (int32 DelegateIndex = 0; DelegateIndex < Extensions.Num(); ++DelegateIndex)
	{
		check(Extensions[DelegateIndex].IsBound());

		// We call a delegate and have the delegate create the instance to cover cross-module
		TSharedRef<ICurveEditorExtension> NewExtension = Extensions[DelegateIndex].Execute(SharedThis(this));
		EditorExtensions.Add(NewExtension);
	}

	TArrayView<const FOnCreateCurveEditorToolExtension> Tools = CurveEditorModule.GetToolExtensions();
	for (int32 DelegateIndex = 0; DelegateIndex < Tools.Num(); ++DelegateIndex)
	{
		check(Tools[DelegateIndex].IsBound());

		// We call a delegate and have the delegate create the instance to cover cross-module
		AddTool(Tools[DelegateIndex].Execute(SharedThis(this)));
	}

	// Listen to global undo so we can fix up our selection state for keys that no longer exist.
	GEditor->RegisterForUndo(this);
}

void FCurveEditor::SetPanel(TSharedPtr<SCurveEditorPanel> InPanel)
{
	WeakPanel = InPanel;
}

TSharedPtr<SCurveEditorPanel> FCurveEditor::GetPanel() const
{
	return WeakPanel.Pin();
}

FCurveModel* FCurveEditor::FindCurve(FCurveModelID CurveID) const
{
	const TUniquePtr<FCurveModel>* Ptr = CurveData.Find(CurveID);
	return Ptr ? Ptr->Get() : nullptr;
}

const TMap<FCurveModelID, TUniquePtr<FCurveModel>>& FCurveEditor::GetCurves() const
{
	return CurveData;
}

FCurveEditorToolID FCurveEditor::AddTool(TUniquePtr<ICurveEditorToolExtension>&& InTool)
{
	FCurveEditorToolID NewID = FCurveEditorToolID::Unique();
	ToolExtensions.Add(NewID, MoveTemp(InTool));
	ToolExtensions[NewID]->SetToolID(NewID);
	return NewID;
}

FCurveModelID FCurveEditor::AddCurve(TUniquePtr<FCurveModel>&& InCurve)
{
	FCurveModelID NewID = FCurveModelID::Unique();
	CurveData.Add(NewID, MoveTemp(InCurve));

	++ActiveCurvesSerialNumber;

	return NewID;
}

FCurveModelID FCurveEditor::AddCurveForTreeItem(TUniquePtr<FCurveModel>&& InCurve, FCurveEditorTreeItemID TreeItemID)
{
	FCurveModelID NewID = FCurveModelID::Unique();

	CurveData.Add(NewID, MoveTemp(InCurve));
	TreeIDByCurveID.Add(NewID, TreeItemID);

	++ActiveCurvesSerialNumber;

	return NewID;
}

void FCurveEditor::RemoveCurve(FCurveModelID InCurveID)
{
	TSharedPtr<SCurveEditorPanel> Panel = WeakPanel.Pin();
	if (Panel.IsValid())
	{
		Panel->RemoveCurveFromViews(InCurveID);
	}

	CurveData.Remove(InCurveID);
	Selection.Remove(InCurveID);
	PinnedCurves.Remove(InCurveID);

	++ActiveCurvesSerialNumber;
}

bool FCurveEditor::IsCurvePinned(FCurveModelID InCurveID) const
{
	return PinnedCurves.Contains(InCurveID);
}

void FCurveEditor::PinCurve(FCurveModelID InCurveID)
{
	PinnedCurves.Add(InCurveID);
	++ActiveCurvesSerialNumber;
}

void FCurveEditor::UnpinCurve(FCurveModelID InCurveID)
{
	PinnedCurves.Remove(InCurveID);
	++ActiveCurvesSerialNumber;
}

const SCurveEditorView* FCurveEditor::FindFirstInteractiveView(FCurveModelID InCurveID) const
{
	for (auto ViewIt = GetPanel()->FindViews(InCurveID); ViewIt; ++ViewIt)
	{
		if (ViewIt.Value()->IsInteractive())
		{
			return &ViewIt.Value().Get();
		}
	}
	return nullptr;
}

FCurveEditorTreeItem& FCurveEditor::GetTreeItem(FCurveEditorTreeItemID ItemID)
{
	return Tree.GetItem(ItemID);
}

const FCurveEditorTreeItem& FCurveEditor::GetTreeItem(FCurveEditorTreeItemID ItemID) const
{
	return Tree.GetItem(ItemID);
}

const TArray<FCurveEditorTreeItemID>& FCurveEditor::GetRootTreeItems() const
{
	return Tree.GetRootItems();
}

FCurveEditorTreeItem* FCurveEditor::AddTreeItem(FCurveEditorTreeItemID ParentID)
{
	return Tree.AddItem(ParentID);
}

void FCurveEditor::RemoveTreeItem(FCurveEditorTreeItemID ItemID)
{
	FCurveEditorTreeItem* Item = Tree.FindItem(ItemID);
	if (!Item)
	{
		return;
	}

	Tree.RemoveItem(ItemID, this);
	++ActiveCurvesSerialNumber;
}

ECurveEditorTreeSelectionState FCurveEditor::GetTreeSelectionState(FCurveEditorTreeItemID InTreeItemID) const
{
	return Tree.GetSelectionState(InTreeItemID);
}

const TMap<FCurveEditorTreeItemID, ECurveEditorTreeSelectionState>& FCurveEditor::GetTreeSelection() const
{
	return Tree.GetSelection();
}

void FCurveEditor::SetBounds(TUniquePtr<ICurveEditorBounds>&& InBounds)
{
	check(InBounds.IsValid());
	Bounds = MoveTemp(InBounds);
}

bool FCurveEditor::ShouldAutoFrame() const
{
	return Settings->GetAutoFrameCurveEditor();
}

void FCurveEditor::BindCommands()
{
	UCurveEditorSettings* CurveSettings = Settings;

	CommandList->MapAction(FGenericCommands::Get().Undo,   FExecuteAction::CreateLambda([]{ GEditor->UndoTransaction(); }));
	CommandList->MapAction(FGenericCommands::Get().Redo,   FExecuteAction::CreateLambda([]{ GEditor->RedoTransaction(); }));
	CommandList->MapAction(FGenericCommands::Get().Delete, FExecuteAction::CreateSP(this, &FCurveEditor::DeleteSelection));

	CommandList->MapAction(FCurveEditorCommands::Get().ZoomToFit, FExecuteAction::CreateSP(this, &FCurveEditor::ZoomToFit, EAxisList::All));

	CommandList->MapAction(FCurveEditorCommands::Get().StepToNextKey, FExecuteAction::CreateSP(this, &FCurveEditor::StepToNextKey));
	CommandList->MapAction(FCurveEditorCommands::Get().StepToPreviousKey, FExecuteAction::CreateSP(this, &FCurveEditor::StepToPreviousKey));
	CommandList->MapAction(FCurveEditorCommands::Get().StepForward, FExecuteAction::CreateSP(this, &FCurveEditor::StepForward), EUIActionRepeatMode::RepeatEnabled);
	CommandList->MapAction(FCurveEditorCommands::Get().StepBackward, FExecuteAction::CreateSP(this, &FCurveEditor::StepBackward), EUIActionRepeatMode::RepeatEnabled);
	CommandList->MapAction(FCurveEditorCommands::Get().JumpToStart, FExecuteAction::CreateSP(this, &FCurveEditor::JumpToStart));
	CommandList->MapAction(FCurveEditorCommands::Get().JumpToEnd, FExecuteAction::CreateSP(this, &FCurveEditor::JumpToEnd));

	{
		FExecuteAction   ToggleInputSnapping     = FExecuteAction::CreateSP(this,   &FCurveEditor::ToggleInputSnapping);
		FIsActionChecked IsInputSnappingEnabled  = FIsActionChecked::CreateSP(this, &FCurveEditor::IsInputSnappingEnabled);
		FExecuteAction   ToggleOutputSnapping    = FExecuteAction::CreateSP(this,   &FCurveEditor::ToggleOutputSnapping);
		FIsActionChecked IsOutputSnappingEnabled = FIsActionChecked::CreateSP(this, &FCurveEditor::IsOutputSnappingEnabled);

		CommandList->MapAction(FCurveEditorCommands::Get().ToggleInputSnapping, ToggleInputSnapping, FCanExecuteAction(), IsInputSnappingEnabled);
		CommandList->MapAction(FCurveEditorCommands::Get().ToggleOutputSnapping, ToggleOutputSnapping, FCanExecuteAction(), IsOutputSnappingEnabled);
	}

	// Flatten and Straighten Tangents
	{
		CommandList->MapAction(FCurveEditorCommands::Get().FlattenTangents, FExecuteAction::CreateSP(this, &FCurveEditor::FlattenSelection), FCanExecuteAction::CreateSP(this, &FCurveEditor::CanFlattenOrStraightenSelection) );
		CommandList->MapAction(FCurveEditorCommands::Get().StraightenTangents, FExecuteAction::CreateSP(this, &FCurveEditor::StraightenSelection), FCanExecuteAction::CreateSP(this, &FCurveEditor::CanFlattenOrStraightenSelection) );
	}


	// Tangent Visibility
	{
		FExecuteAction SetAllTangents          = FExecuteAction::CreateUObject(Settings, &UCurveEditorSettings::SetTangentVisibility, ECurveEditorTangentVisibility::AllTangents);
		FExecuteAction SetSelectedKeyTangents  = FExecuteAction::CreateUObject(Settings, &UCurveEditorSettings::SetTangentVisibility, ECurveEditorTangentVisibility::SelectedKeys);
		FExecuteAction SetNoTangents           = FExecuteAction::CreateUObject(Settings, &UCurveEditorSettings::SetTangentVisibility, ECurveEditorTangentVisibility::NoTangents);

		FIsActionChecked IsAllTangents         = FIsActionChecked::CreateLambda( [CurveSettings]{ return CurveSettings->GetTangentVisibility() == ECurveEditorTangentVisibility::AllTangents; } );
		FIsActionChecked IsSelectedKeyTangents = FIsActionChecked::CreateLambda( [CurveSettings]{ return CurveSettings->GetTangentVisibility() == ECurveEditorTangentVisibility::SelectedKeys; } );
		FIsActionChecked IsNoTangents          = FIsActionChecked::CreateLambda( [CurveSettings]{ return CurveSettings->GetTangentVisibility() == ECurveEditorTangentVisibility::NoTangents; } );

		CommandList->MapAction(FCurveEditorCommands::Get().SetAllTangentsVisibility, SetAllTangents, FCanExecuteAction(), IsAllTangents);
		CommandList->MapAction(FCurveEditorCommands::Get().SetSelectedKeysTangentVisibility, SetSelectedKeyTangents, FCanExecuteAction(), IsSelectedKeyTangents);
		CommandList->MapAction(FCurveEditorCommands::Get().SetNoTangentsVisibility, SetNoTangents, FCanExecuteAction(), IsNoTangents);
	}

	CommandList->MapAction(FCurveEditorCommands::Get().ToggleAutoFrameCurveEditor,
		FExecuteAction::CreateLambda( [CurveSettings]{ CurveSettings->SetAutoFrameCurveEditor( !CurveSettings->GetAutoFrameCurveEditor() ); } ),
		FCanExecuteAction(),
		FIsActionChecked::CreateLambda( [CurveSettings]{ return CurveSettings->GetAutoFrameCurveEditor(); } )
	);

	CommandList->MapAction(FCurveEditorCommands::Get().ToggleShowCurveEditorCurveToolTips,
		FExecuteAction::CreateLambda( [CurveSettings]{ CurveSettings->SetShowCurveEditorCurveToolTips( !CurveSettings->GetShowCurveEditorCurveToolTips() ); } ),
		FCanExecuteAction(),
		FIsActionChecked::CreateLambda( [CurveSettings]{ return CurveSettings->GetShowCurveEditorCurveToolTips(); } ) );

	// Deactivate Current Tool
	CommandList->MapAction(FCurveEditorCommands::Get().DeactivateCurrentTool,
		FExecuteAction::CreateSP(this, &FCurveEditor::MakeToolActive, FCurveEditorToolID::Unset()));
		
	// Bind commands for Editor Extensions
	for (TSharedRef<ICurveEditorExtension> Extension : EditorExtensions)
	{
		Extension->BindCommands(CommandList.ToSharedRef());
	}

	// Bind Commands for Tool Extensions
	for (TPair<FCurveEditorToolID, TUniquePtr<ICurveEditorToolExtension>>& Pair : ToolExtensions)
	{
		Pair.Value->BindCommands(CommandList.ToSharedRef());
	}
}

FCurveEditorSnapMetrics FCurveEditor::GetSnapMetrics() const
{
	FCurveEditorSnapMetrics Metrics;

	Metrics.bSnapOutputValues  = OutputSnapEnabledAttribute.Get();
	Metrics.OutputSnapInterval = OutputSnapIntervalAttribute.Get();
	Metrics.bSnapInputValues   = InputSnapEnabledAttribute.Get();
	Metrics.InputSnapRate      = InputSnapRateAttribute.Get();

	return Metrics;
}

void FCurveEditor::ZoomToFit(EAxisList::Type Axes)
{
	// If they have keys selected, we fit the specific keys.
	if (Selection.Count() > 0)
	{
		ZoomToFitSelection(Axes);
	}
	else
	{
		TMap<FCurveModelID, FKeyHandleSet> AllCurves;
		for (FCurveModelID ID : GetEditedCurves())
		{
			AllCurves.Add(ID);
		}
		ZoomToFitInternal(Axes, AllCurves);
	}
}

void FCurveEditor::ZoomToFitCurves(TArrayView<const FCurveModelID> CurveModelIDs, EAxisList::Type Axes)
{
	TMap<FCurveModelID, FKeyHandleSet> AllCurves;
	for (FCurveModelID ID : CurveModelIDs)
	{
		AllCurves.Add(ID);
	}
	ZoomToFitInternal(Axes, AllCurves);
}

void FCurveEditor::ZoomToFitSelection(EAxisList::Type Axes)
{
	ZoomToFitInternal(Axes, Selection.GetAll());
}

void FCurveEditor::ZoomToFitInternal(EAxisList::Type Axes, const TMap<FCurveModelID, FKeyHandleSet>& CurveKeySet)
{
	TArray<FKeyPosition> KeyPositionsScratch;

	double InputMin = TNumericLimits<double>::Max(), InputMax = TNumericLimits<double>::Lowest();

	TMap<TSharedRef<SCurveEditorView>, TTuple<double, double>> ViewToOutputBounds;

	for (const TTuple<FCurveModelID, FKeyHandleSet>& Pair : CurveKeySet)
	{
		FCurveModelID CurveID = Pair.Key;
		const FCurveModel* Curve = FindCurve(CurveID);
		if (!Curve)
		{
			continue;
		}

		double OutputMin = TNumericLimits<double>::Max(), OutputMax = TNumericLimits<double>::Lowest();

		int32 NumKeys = Pair.Value.AsArray().Num();
		if (NumKeys == 0)
		{
			double LocalMin = 0.0, LocalMax = 1.0;

			// Zoom to the entire curve range if no specific keys are specified
			if (Curve->GetNumKeys())
			{
				// Only zoom time range if there are keys on the curve (otherwise where do we zoom *to* on an infinite timeline?)
				Curve->GetTimeRange(LocalMin, LocalMax);
				InputMin = FMath::Min(InputMin, LocalMin);
				InputMax = FMath::Max(InputMax, LocalMax);
			}

			// Most curve types we know about support default values, so we can zoom to that even if there are no keys
			Curve->GetValueRange(LocalMin, LocalMax);
			OutputMin = FMath::Min(OutputMin, LocalMin);
			OutputMax = FMath::Max(OutputMax, LocalMax);
		}
		else
		{
			// Zoom to the min/max of the specified key set
			KeyPositionsScratch.SetNum(NumKeys, false);
			Curve->GetKeyPositions(Pair.Value.AsArray(), KeyPositionsScratch);
			for (const FKeyPosition& Key : KeyPositionsScratch)
			{
				InputMin  = FMath::Min(InputMin, Key.InputValue);
				InputMax  = FMath::Max(InputMax, Key.InputValue);
				OutputMin = FMath::Min(OutputMin, Key.OutputValue);
				OutputMax = FMath::Max(OutputMax, Key.OutputValue);
			}
		}

		if (Axes & EAxisList::Y)
		{
			// Store the min max for each view
			for (auto ViewIt = GetPanel()->FindViews(CurveID); ViewIt; ++ViewIt)
			{
				TTuple<double, double>* ViewBounds = ViewToOutputBounds.Find(ViewIt.Value());
				if (ViewBounds)
				{
					ViewBounds->Get<0>() = FMath::Min(ViewBounds->Get<0>(), OutputMin);
					ViewBounds->Get<1>() = FMath::Max(ViewBounds->Get<1>(), OutputMax);
				}
				else
				{
					ViewToOutputBounds.Add(ViewIt.Value(), MakeTuple(OutputMin, OutputMax));
				}
			}
		}
	}

	FCurveEditorSnapMetrics SnapMetrics = GetSnapMetrics();

	if (Axes & EAxisList::X && InputMin != TNumericLimits<double>::Max() && InputMax != TNumericLimits<double>::Lowest())
	{
		// If zooming to the same (or invalid) min/max, keep the same zoom scale and center within the timeline
		if (InputMin >= InputMax)
		{
			double CurrentInputMin = 0.0, CurrentInputMax = 1.0;
			Bounds->GetInputBounds(CurrentInputMin, CurrentInputMax);

			const double HalfInputScale = (CurrentInputMax - CurrentInputMin)*0.5;
			InputMin -= HalfInputScale;
			InputMax += HalfInputScale;
		}
		else
		{
			const double MinInputZoom = SnapMetrics.bSnapInputValues ? SnapMetrics.InputSnapRate.AsInterval() : 0.00001;
			const double InputPadding = FMath::Max((InputMax - InputMin) * 0.1, MinInputZoom);
			InputMax = FMath::Max(InputMin + MinInputZoom, InputMax);

			InputMin -= InputPadding;
			InputMax += InputPadding;
		}

		Bounds->SetInputBounds(InputMin, InputMax);
	}

	// Perform per-view output zoom for any computed ranges
	for (const TTuple<TSharedRef<SCurveEditorView>, TTuple<double, double>>& ViewAndBounds : ViewToOutputBounds)
	{
		TSharedRef<SCurveEditorView> View = ViewAndBounds.Key;
		double OutputMin = ViewAndBounds.Value.Get<0>();
		double OutputMax = ViewAndBounds.Value.Get<1>();

		// If zooming to the same (or invalid) min/max, keep the same zoom scale and center within the timeline
		if (OutputMin >= OutputMax)
		{
			const double HalfOutputScale = (View->GetOutputMax() - View->GetOutputMin())*0.5;
			OutputMin -= HalfOutputScale;
			OutputMax += HalfOutputScale;
		}
		else
		{
			const double MinOutputZoom = SnapMetrics.bSnapOutputValues ? SnapMetrics.OutputSnapInterval : 0.00001;
			const double OutputPadding = FMath::Max((OutputMax - OutputMin) * 0.05, MinOutputZoom);

			
			OutputMin -= OutputPadding;
			OutputMax = FMath::Max(OutputMin + MinOutputZoom, OutputMax) + OutputPadding;
		}

		View->SetOutputBounds(OutputMin, OutputMax);
	}
}

void FCurveEditor::StepToNextKey()
{
	if (!WeakTimeSliderController.IsValid())
	{
		return;
	}

	FFrameRate TickResolution = WeakTimeSliderController.Pin()->GetTickResolution();

	double CurrentTime = WeakTimeSliderController.Pin()->GetScrubPosition() / TickResolution;

	TOptional<double> NextTime;

	for (const TTuple<FCurveModelID, TUniquePtr<FCurveModel>>& Pair : CurveData)
	{
		FCurveModel* CurveModel = Pair.Value.Get();

		if (CurveModel)
		{
			TArray<FKeyHandle> KeyHandles;
			double MaxTime = NextTime.IsSet() ? NextTime.GetValue() : TNumericLimits<double>::Max();
			CurveModel->GetKeys(*this, CurrentTime, MaxTime, TNumericLimits<double>::Lowest(), TNumericLimits<double>::Max(), KeyHandles);

			TArray<FKeyPosition> KeyPositions;
			KeyPositions.SetNum(KeyHandles.Num());
			CurveModel->GetKeyPositions(TArrayView<FKeyHandle>(KeyHandles), KeyPositions);

			for (FKeyPosition KeyPosition : KeyPositions)
			{
				if (KeyPosition.InputValue > CurrentTime)
				{
					if (!NextTime.IsSet() || KeyPosition.InputValue < NextTime.GetValue())
					{
						NextTime = KeyPosition.InputValue;
					}
				}
			}
		}
	}

	if (NextTime.IsSet())
	{
		WeakTimeSliderController.Pin()->SetScrubPosition(NextTime.GetValue() * TickResolution);
	}
}

void FCurveEditor::StepToPreviousKey()
{
	if (!WeakTimeSliderController.IsValid())
	{
		return;
	}

	FFrameRate TickResolution = WeakTimeSliderController.Pin()->GetTickResolution();

	double CurrentTime = WeakTimeSliderController.Pin()->GetScrubPosition() / TickResolution;

	TOptional<double> PreviousTime;

	for (const TTuple<FCurveModelID, TUniquePtr<FCurveModel>>& Pair : CurveData)
	{
		FCurveModel* CurveModel = Pair.Value.Get();

		if (CurveModel)
		{
			TArray<FKeyHandle> KeyHandles;
			double MinTime = PreviousTime.IsSet() ? PreviousTime.GetValue() : TNumericLimits<double>::Lowest();
			CurveModel->GetKeys(*this, MinTime, CurrentTime, TNumericLimits<double>::Lowest(), TNumericLimits<double>::Max(), KeyHandles);

			TArray<FKeyPosition> KeyPositions;
			KeyPositions.SetNum(KeyHandles.Num());
			CurveModel->GetKeyPositions(TArrayView<FKeyHandle>(KeyHandles), KeyPositions);

			for (FKeyPosition KeyPosition : KeyPositions)
			{
				if (KeyPosition.InputValue < CurrentTime)
				{
					if (!PreviousTime.IsSet() || KeyPosition.InputValue > PreviousTime.GetValue())
					{
						PreviousTime = KeyPosition.InputValue;
					}
				}
			}
		}
	}

	if (PreviousTime.IsSet())
	{
		WeakTimeSliderController.Pin()->SetScrubPosition(PreviousTime.GetValue() * TickResolution);
	}
}


void FCurveEditor::StepForward()
{
	if (!WeakTimeSliderController.IsValid())
	{
		return;
	}

	FFrameRate TickResolution = WeakTimeSliderController.Pin()->GetTickResolution();
	FFrameRate DisplayRate = WeakTimeSliderController.Pin()->GetDisplayRate();

	FFrameTime OneFrame = FFrameRate::TransformTime(FFrameTime(1), DisplayRate, TickResolution);

	WeakTimeSliderController.Pin()->SetScrubPosition(WeakTimeSliderController.Pin()->GetScrubPosition() + OneFrame);
}

void FCurveEditor::StepBackward()
{
	FFrameRate TickResolution = WeakTimeSliderController.Pin()->GetTickResolution();
	FFrameRate DisplayRate = WeakTimeSliderController.Pin()->GetDisplayRate();

	FFrameTime OneFrame = FFrameRate::TransformTime(FFrameTime(1), DisplayRate, TickResolution);

	WeakTimeSliderController.Pin()->SetScrubPosition(WeakTimeSliderController.Pin()->GetScrubPosition() - OneFrame);
}

void FCurveEditor::JumpToStart()
{
	if (!WeakTimeSliderController.IsValid())
	{
		return;
	}

	WeakTimeSliderController.Pin()->SetScrubPosition(WeakTimeSliderController.Pin()->GetPlayRange().GetLowerBoundValue());
}

void FCurveEditor::JumpToEnd()
{
	if (!WeakTimeSliderController.IsValid())
	{
		return;
	}

	WeakTimeSliderController.Pin()->SetScrubPosition(WeakTimeSliderController.Pin()->GetPlayRange().GetUpperBoundValue());
}

bool FCurveEditor::IsInputSnappingEnabled() const
{
	return InputSnapEnabledAttribute.Get();
}

void FCurveEditor::ToggleInputSnapping()
{
	bool NewValue = !InputSnapEnabledAttribute.Get();

	if (!InputSnapEnabledAttribute.IsBound())
	{
		InputSnapEnabledAttribute = NewValue;
	}
	else
	{
		OnInputSnapEnabledChanged.ExecuteIfBound(NewValue);
	}
}

bool FCurveEditor::IsOutputSnappingEnabled() const
{
	return OutputSnapEnabledAttribute.Get();
}

void FCurveEditor::ToggleOutputSnapping()
{
	bool NewValue = !OutputSnapEnabledAttribute.Get();

	if (!OutputSnapEnabledAttribute.IsBound())
	{
		OutputSnapEnabledAttribute = NewValue;
	}
	else
	{
		OnOutputSnapEnabledChanged.ExecuteIfBound(NewValue);
	}
}

FCurveEditorScreenSpaceH FCurveEditor::GetPanelInputSpace() const
{
	const float PanelWidth = FMath::Max(1.f, WeakPanel.Pin()->GetViewContainerGeometry().GetLocalSize().X);

	double InputMin = 0.0, InputMax = 1.0;
	Bounds->GetInputBounds(InputMin, InputMax);

	InputMax = FMath::Max(InputMax, InputMin + 1e-10);
	return FCurveEditorScreenSpaceH(PanelWidth, InputMin, InputMax);
}

void FCurveEditor::ConstructXGridLines(TArray<float>& MajorGridLines, TArray<float>& MinorGridLines, TArray<FText>& MajorGridLabels) const
{
	FCurveEditorScreenSpaceH InputSpace = GetPanelInputSpace();

	double MajorGridStep  = 0.0;
	int32  MinorDivisions = 0;
	if (GetSnapMetrics().InputSnapRate.ComputeGridSpacing(InputSpace.PixelsPerInput(), MajorGridStep, MinorDivisions))
	{
		FText GridLineLabelFormatX = GridLineLabelFormatXAttribute.Get();
		const double FirstMajorLine = FMath::FloorToDouble(InputSpace.GetInputMin() / MajorGridStep) * MajorGridStep;
		const double LastMajorLine  = FMath::CeilToDouble(InputSpace.GetInputMax() / MajorGridStep) * MajorGridStep;

		for (double CurrentMajorLine = FirstMajorLine; CurrentMajorLine < LastMajorLine; CurrentMajorLine += MajorGridStep)
		{
			MajorGridLines.Add( (CurrentMajorLine - InputSpace.GetInputMin()) * InputSpace.PixelsPerInput() );
			MajorGridLabels.Add( FText::Format(GridLineLabelFormatX, FText::AsNumber(CurrentMajorLine)) );

			for (int32 Step = 1; Step < MinorDivisions; ++Step)
			{
				float MinorLine = CurrentMajorLine + Step*MajorGridStep/MinorDivisions;
				MinorGridLines.Add( (MinorLine - InputSpace.GetInputMin()) * InputSpace.PixelsPerInput() );
			}
		}
	}
}

void FCurveEditor::DeleteSelection()
{
	FScopedTransaction Transaction(LOCTEXT("DeleteKeys", "Delete Keys"));

	for (const TTuple<FCurveModelID, FKeyHandleSet>& Pair : Selection.GetAll())
	{
		if (FCurveModel* Curve = FindCurve(Pair.Key))
		{
			Curve->Modify();
			Curve->RemoveKeys(Pair.Value.AsArray());
		}
	}

	Selection.Clear();
}

void FCurveEditor::FlattenSelection()
{
	FScopedTransaction Transaction(LOCTEXT("FlattenTangents", "Flatten Tangents"));
	bool bFoundAnyTangents = false;

	TArray<FKeyHandle> KeyHandles;
	TArray<FKeyAttributes> AllKeyPositions;
	//Since we don't have access here to the Section to get Tick Resolution if we flatten a weighted tangent we
	//do so by converting it to non-weighted and then back again.
	TArray<FKeyHandle>  KeyHandlesWeighted;
	TArray<FKeyAttributes> KeyAttributesWeighted;
	for (const TTuple<FCurveModelID, FKeyHandleSet>& Pair : Selection.GetAll())
	{
		if (FCurveModel* Curve = FindCurve(Pair.Key))
		{
			KeyHandles.Reset(Pair.Value.Num());
			KeyHandles.Append(Pair.Value.AsArray().GetData(), Pair.Value.Num());

			AllKeyPositions.SetNum(KeyHandles.Num());
			Curve->GetKeyAttributes(KeyHandles, AllKeyPositions);

			KeyHandlesWeighted.Reset(Pair.Value.Num());
			KeyHandlesWeighted.Append(Pair.Value.AsArray().GetData(), Pair.Value.Num());

			KeyAttributesWeighted.SetNum(KeyHandlesWeighted.Num());
			Curve->GetKeyAttributes(KeyHandlesWeighted, KeyAttributesWeighted);


			// Straighten tangents, ignoring any keys that we can't set tangents on
			for (int32 Index = AllKeyPositions.Num()-1 ; Index >= 0; --Index)
			{
				FKeyAttributes& Attributes = AllKeyPositions[Index];
				if (Attributes.HasTangentMode() && (Attributes.HasArriveTangent() || Attributes.HasLeaveTangent()))
				{
					Attributes.SetArriveTangent(0.f).SetLeaveTangent(0.f);
					if (Attributes.GetTangentMode() == RCTM_Auto)
					{
						Attributes.SetTangentMode(RCTM_User);
					}
					//if any weighted convert and convert back to both (which is what only support other modes are not really used).,
					if (Attributes.GetTangentWeightMode() == RCTWM_WeightedBoth || Attributes.GetTangentWeightMode() == RCTWM_WeightedArrive
						|| Attributes.GetTangentWeightMode() == RCTWM_WeightedLeave)
					{
						Attributes.SetTangentWeightMode(RCTWM_WeightedNone);
						FKeyAttributes& WeightedAttributes = KeyAttributesWeighted[Index];
						WeightedAttributes.UnsetArriveTangent();
						WeightedAttributes.UnsetLeaveTangent();
						WeightedAttributes.UnsetArriveTangentWeight();
						WeightedAttributes.UnsetLeaveTangentWeight();
						WeightedAttributes.SetTangentWeightMode(RCTWM_WeightedBoth);

					}
					else
					{
						KeyAttributesWeighted.RemoveAtSwap(Index, 1, false);
						KeyHandlesWeighted.RemoveAtSwap(Index, 1, false);
					}
				}
				else
				{
					AllKeyPositions.RemoveAtSwap(Index, 1, false);
					KeyHandles.RemoveAtSwap(Index, 1, false);
					KeyAttributesWeighted.RemoveAtSwap(Index, 1, false);
					KeyHandlesWeighted.RemoveAtSwap(Index, 1, false);
				}
			}

			if (AllKeyPositions.Num() > 0)
			{
				Curve->Modify();
				Curve->SetKeyAttributes(KeyHandles, AllKeyPositions);
				if (KeyAttributesWeighted.Num() > 0)
				{
					Curve->SetKeyAttributes(KeyHandlesWeighted, KeyAttributesWeighted);
				}
				bFoundAnyTangents = true;
			}
		}
	}

	if (!bFoundAnyTangents)
	{
		Transaction.Cancel();
	}
}

void FCurveEditor::StraightenSelection()
{
	FScopedTransaction Transaction(LOCTEXT("StraightenTangents", "Straighten Tangents"));
	bool bFoundAnyTangents = false;

	TArray<FKeyHandle> KeyHandles;
	TArray<FKeyAttributes> AllKeyPositions;

	for (const TTuple<FCurveModelID, FKeyHandleSet>& Pair : Selection.GetAll())
	{
		if (FCurveModel* Curve = FindCurve(Pair.Key))
		{
			KeyHandles.Reset(Pair.Value.Num());
			KeyHandles.Append(Pair.Value.AsArray().GetData(), Pair.Value.Num());

			AllKeyPositions.SetNum(KeyHandles.Num());
			Curve->GetKeyAttributes(KeyHandles, AllKeyPositions);

			// Straighten tangents, ignoring any keys that we can't set tangents on
			for (int32 Index = AllKeyPositions.Num()-1 ; Index >= 0; --Index)
			{
				FKeyAttributes& Attributes = AllKeyPositions[Index];
				if (Attributes.HasTangentMode() && Attributes.HasArriveTangent() && Attributes.HasLeaveTangent())
				{
					float NewTangent = (Attributes.GetLeaveTangent() + Attributes.GetArriveTangent()) * 0.5f;
					Attributes.SetArriveTangent(NewTangent).SetLeaveTangent(NewTangent);
					if (Attributes.GetTangentMode() == RCTM_Auto)
					{
						Attributes.SetTangentMode(RCTM_User);
					}
				}
				else
				{
					AllKeyPositions.RemoveAtSwap(Index, 1, false);
					KeyHandles.RemoveAtSwap(Index, 1, false);
				}
			}

			if (AllKeyPositions.Num() > 0)
			{
				Curve->Modify();
				Curve->SetKeyAttributes(KeyHandles, AllKeyPositions);
				bFoundAnyTangents = true;
			}
		}
	}

	if (!bFoundAnyTangents)
	{
		Transaction.Cancel();
	}
}

bool FCurveEditor::CanFlattenOrStraightenSelection() const
{
	return Selection.Count() > 0;
}

bool FCurveEditor::IsToolActive(const FCurveEditorToolID InToolID) const
{
	if (ActiveTool.IsSet())
	{
		return ActiveTool == InToolID;
	}

	return false;
}

void FCurveEditor::MakeToolActive(const FCurveEditorToolID InToolID)
{
	if (ActiveTool.IsSet())
	{
		// Early out in the event that they're trying to switch to the same tool. This avoids
		// unwanted activation/deactivation calls.
		if (ActiveTool == InToolID)
		{
			return;
		}

		// Deactivate the current tool before we activate the new one.
		ToolExtensions[ActiveTool.GetValue()]->OnToolDeactivated();
	}

	ActiveTool.Reset();

	// Notify anyone listening that we've switched tools (possibly to an inactive one)
	OnActiveToolChangedDelegate.Broadcast(InToolID);

	if (InToolID != FCurveEditorToolID::Unset())
	{
		ActiveTool = InToolID;
		ToolExtensions[ActiveTool.GetValue()]->OnToolActivated();
	}
}

ICurveEditorToolExtension* FCurveEditor::GetCurrentTool() const
{
	if (ActiveTool.IsSet())
	{
		return ToolExtensions[ActiveTool.GetValue()].Get();
	}

	// If there is no active tool we return nullptr.
	return nullptr;
}

TSet<FCurveModelID> FCurveEditor::GetEditedCurves() const
{
	TArray<FCurveModelID> AllCurves;
	GetCurves().GenerateKeyArray(AllCurves);
	return TSet<FCurveModelID>(AllCurves);
}

void FCurveEditor::SetBufferedCurves(const TSet<FCurveModelID>& InCurves)
{
	BufferedCurves.Empty();

	// We make a copy of the curve data and store it.
	for (FCurveModelID CurveID : InCurves)
	{
		FCurveModel* CurveModel = FindCurve(CurveID);
		check(CurveModel);


		// We loop through the key data on the curve and apply it to the copy, since there's no copy constructors for Curve Models (for good reason!)
		FBufferedCurve& BufferedCurve = BufferedCurves.Emplace_GetRef();
		
		int32 NumKeys = CurveModel->GetNumKeys();
		BufferedCurve.KeyPositions.SetNumUninitialized(NumKeys);
		BufferedCurve.KeyAttributes.SetNumUninitialized(NumKeys);

		// Copy all of the from the curve into our cached buffer
		TArray<FKeyHandle> KeyHandles;

		CurveModel->GetKeys(*this, TNumericLimits<double>::Lowest(), TNumericLimits<double>::Max(), TNumericLimits<double>::Lowest(), TNumericLimits<double>::Max(), KeyHandles);
		CurveModel->GetKeyPositions(KeyHandles, BufferedCurve.KeyPositions);
		CurveModel->GetKeyAttributes(KeyHandles, BufferedCurve.KeyAttributes);
		BufferedCurve.IntentionName = CurveModel->GetIntentionName();
	}
}


void FCurveEditor::ApplyBufferedCurveToTarget(const FBufferedCurve& BufferedCurve, FCurveModel* TargetCurve)
{
	check(TargetCurve);

	TArray<FKeyPosition> KeyPositions;
	TArray<FKeyAttributes> KeyAttributes;


	// Copy the data from the Buffered curve into the target curve. This just does wholesale replacement.
	TArray<FKeyHandle> TargetKeyHandles;
	TargetCurve->GetKeys(*this, TNumericLimits<double>::Lowest(), TNumericLimits<double>::Max(), TNumericLimits<double>::Lowest(), TNumericLimits<double>::Max(), TargetKeyHandles);

	// Clear our current keys from the target curve
	TargetCurve->RemoveKeys(TargetKeyHandles);

	// Now put our buffered keys into the target curve
	TargetCurve->AddKeys(BufferedCurve.KeyPositions, BufferedCurve.KeyAttributes);
}

bool FCurveEditor::ApplyBufferedCurves(const TSet<FCurveModelID>& InCurvesToApplyTo)
{
	FScopedTransaction Transaction(LOCTEXT("ApplyBufferedCurves", "Apply Buffered Curves"));

	// Each curve can specify an "Intention" name. This gives a little bit of context about how the curve is intended to be used,
	// without locking anyone into a specific set of intentions. When you go to apply the buffered curves, for each curve that you
	// want to apply it to, we can look in our stored curves to see if someone has the same intention. If there isn't a matching intention
	// then we skip and consider a fallback method (such as 1:1 copy). There is a lot of guessing still involved as there are complex
	// situations that users may try to use it in (such as buffering two sets of transform curves and applying it to two destination transform curves)
	// or trying to copy something with a name like "Focal Length" and pasting it onto a different track. We don't handle these cases for now,
	// but attempt to communicate it to the user via  toast notification when pasting fails for whatever reason.
	int32 NumCurvesMatchedByIntent = 0;
	int32 NumCurvesNoMatchedIntent = 0;
	bool bFoundAnyMatchedIntent = false;

	TMap<FString, int32> IntentMatchIndexes;

	for (const FCurveModelID CurveModelID : InCurvesToApplyTo)
	{
		FCurveModel* TargetCurve = FindCurve(CurveModelID);
		check(TargetCurve);

		// Figure out what our destination thinks it's supposed to be used for, ie "Location.X"
		FString TargetIntent = TargetCurve->GetIntentionName();
		if (TargetIntent.IsEmpty())
		{
			// We don't try to match curves with no intent as that's just chaos.
			NumCurvesNoMatchedIntent++;
			continue;
		}

		TargetCurve->Modify();

		// In an attempt to support buffering multiple curves with the same intention, we'll try to match them up in pairs. This means
		// for the first curve that we're trying to apply to, if the intention is "Location.X" we will search the buffered curves for a
		// "Location.X". Upon finding one, we store the index that it was found at, so the next time we try to find a curve with the same
		// intention, we look for the second "Location.X" and so forth. If we don't find a second "Location.X" in our buffered curves we'll
		// fall back to the first buffered one so you can 1:Many copy a curve.
		int32 BufferedCurveSearchIndexStart = 0;
		const int32* PreviouslyFoundIntent = IntentMatchIndexes.Find(TargetIntent);
		if (PreviouslyFoundIntent)
		{
			// Start our search on the next item in the array. If we don't find one, we'll fall back to the last one.
			BufferedCurveSearchIndexStart = IntentMatchIndexes[TargetIntent] + 1;
		}

		int32 MatchedBufferedCurveIndex = -1;
		for (int32 BufferedCurveIndex = BufferedCurveSearchIndexStart; BufferedCurveIndex < BufferedCurves.Num(); BufferedCurveIndex++)
		{
			if (BufferedCurves[BufferedCurveIndex].IntentionName == TargetIntent)
			{
				MatchedBufferedCurveIndex = BufferedCurveIndex;

				// Update our previously found intent to the latest one.
				IntentMatchIndexes.FindOrAdd(TargetIntent) = MatchedBufferedCurveIndex;
				break;
			}
		}

		// The Intent Match Indexes stores the latest index to find a valid curve, or the last one if no new valid one was found.
		// If there is an entry in the match indexes now, we can use that to figure out which buffered curve we'll pull from.
		// If we didn't find any more with the same intention, we fall back to the existing one (if it exists!)
		if (IntentMatchIndexes.Find(TargetIntent))
		{
			MatchedBufferedCurveIndex = IntentMatchIndexes[TargetIntent];
		}

		// Finally, we can try to use the matched curve if one was found.
		if (MatchedBufferedCurveIndex >= 0)
		{
			// We successfully matched, so count that one up!
			NumCurvesMatchedByIntent++;
			bFoundAnyMatchedIntent = true;

			const FBufferedCurve& BufferedCurve = BufferedCurves[MatchedBufferedCurveIndex];
			ApplyBufferedCurveToTarget(BufferedCurve, TargetCurve);

		}
		else
		{
			// We couldn't find a match despite our best efforts
			NumCurvesNoMatchedIntent++;
		}
	}

	// If we managed to match any by intent, we're going to early out and assume that's what their intent was.
	if (bFoundAnyMatchedIntent)
	{
		const FText NotificationText = FText::Format(LOCTEXT("MatchedBufferedCurvesByIntent", "Applied {0}/{1} buffered curves to {2}/{3} target curves."),
			FText::AsNumber(IntentMatchIndexes.Num()), FText::AsNumber(BufferedCurves.Num()),		// We used X of Y total buffered curves
			FText::AsNumber(NumCurvesMatchedByIntent), FText::AsNumber(InCurvesToApplyTo.Num()));	// To apply to Z of W target curves,

		FNotificationInfo Info(NotificationText);
		Info.ExpireDuration = 6.f;
		Info.bUseLargeFont = false;
		Info.bUseSuccessFailIcons = false;
		FSlateNotificationManager::Get().AddNotification(Info);

		if (NumCurvesNoMatchedIntent > 0)
		{
			const FText FailedNotificationText = FText::Format(LOCTEXT("NumCurvesNotMatchedByIntent", "Failed to find a buffered curve with the same intent for {0} target curves, skipping..."),
				FText::AsNumber(NumCurvesNoMatchedIntent));												// Leaving V many target curves unaffected due to no intent match.

			FNotificationInfo FailInfo(FailedNotificationText);
			FailInfo.ExpireDuration = 6.f;
			FailInfo.bUseLargeFont = false;
			FailInfo.bUseSuccessFailIcons = true;
			FSlateNotificationManager::Get().AddNotification(FailInfo);
		}

		// Early out
		return true;
	}

	// If we got this far, it means that the buffered curves have no recognizable relation to the target curves.
	// If the number of curves match, we'll just do a 1:1 mapping. This works for most cases where you're trying
	// to paste an unrelated curve onto another as it's likely that there's only one curve. We don't limit it to
	// one curve though, we'll just warn...
	if (InCurvesToApplyTo.Num() == BufferedCurves.Num())
	{
		// This will work great in the case there's only one curve. It'll guess if there's more than one, relying on
		// sets with no guaranteed order.
		TArray<FCurveModelID> CurvesToApplyTo = InCurvesToApplyTo.Array();
		
		for (int32 CurveIndex = 0; CurveIndex < InCurvesToApplyTo.Num(); CurveIndex++)
		{
			ApplyBufferedCurveToTarget(BufferedCurves[CurveIndex], FindCurve(CurvesToApplyTo[CurveIndex]));
		}

		FText NotificationText;
		if (InCurvesToApplyTo.Num() == 1)
		{
			NotificationText = LOCTEXT("MatchedBufferedCurvesBySolo", "Applied buffered curve to target curve with no intention matching.");
		}
		else
		{
			NotificationText = LOCTEXT("MatchedBufferedCurvesByIndex", "Applied buffered curves with no intention matching. Order not guranteed.");
		}

		FNotificationInfo Info(NotificationText);
		Info.ExpireDuration = 6.f;
		Info.bUseLargeFont = false;
		Info.bUseSuccessFailIcons = false;
		FSlateNotificationManager::Get().AddNotification(Info);

		// Early out
		return true;
	}

	// If we got this far, we have no idea what to do. They're trying to match a bunch of curves with no intention and different amounts. 
	// Warn of failure and give up.
	{
		const FText FailedNotificationText = LOCTEXT("NoBufferedCurvesMatched", "Failed to apply buffered curves, apply them one at a time instead.");

		FNotificationInfo FailInfo(FailedNotificationText);
		FailInfo.ExpireDuration = 6.f;
		FailInfo.bUseLargeFont = false;
		FailInfo.bUseSuccessFailIcons = true;
		FSlateNotificationManager::Get().AddNotification(FailInfo);
	}

	// No need to make a entry in the Undo/Redo buffer if it didn't apply anything.
	Transaction.Cancel();
	return false;
}

void FCurveEditor::PostUndo(bool bSuccess)
{
	// If you create keys and then undo them the selection set still thinks there's keys selected.
	// This presents issues with context menus and other things that are activated when there is a selection set.
	// To fix this, we have to loop through all of our curve models, and re-select only the key handles that were
	// previously selected that still exist. Ugly, but reasonably functional.
	TMap<FCurveModelID, FKeyHandleSet> SelectionSet = Selection.GetAll();
	for (const TPair<FCurveModelID, FKeyHandleSet>& Set : SelectionSet)
	{
		FCurveModel* CurveModel = FindCurve(Set.Key);

		// If the entire curve was removed, just dump that out of the selection set.
		if (!CurveModel)
		{
			Selection.Remove(Set.Key);
			continue;
		}

		// Get all of the key handles from this curve.
		TArray<FKeyHandle> KeyHandles;
		CurveModel->GetKeys(*this, TNumericLimits<double>::Lowest(), TNumericLimits<double>::Max(), TNumericLimits<double>::Lowest(), TNumericLimits<double>::Max(), KeyHandles);

		// The set handles will be mutated as we remove things so we need a copy that we can iterate through.
		TArrayView<const FKeyHandle> SelectedHandles = Set.Value.AsArray();
		TArray<FKeyHandle> NonMutableArray = TArray<FKeyHandle>(SelectedHandles.GetData(), SelectedHandles.Num());
		
		for (const FKeyHandle& Handle : NonMutableArray)
		{
			// Check to see if our curve model contains this handle still.
			if (!KeyHandles.Contains(Handle))
			{
				Selection.Remove(Set.Key, ECurvePointType::Key, Handle);
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
