// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SplineComponentVisualizer.h"
#include "CoreMinimal.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Commands/InputChord.h"
#include "Framework/Commands/Commands.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "EditorStyleSet.h"
#include "UnrealWidget.h"
#include "Editor.h"
#include "EditorViewportClient.h"
#include "EditorViewportCommands.h"
#include "LevelEditorActions.h"
#include "Components/SplineComponent.h"
#include "ScopedTransaction.h"
#include "ActorEditorUtils.h"
#include "WorldCollision.h"

IMPLEMENT_HIT_PROXY(HSplineVisProxy, HComponentVisProxy);
IMPLEMENT_HIT_PROXY(HSplineKeyProxy, HSplineVisProxy);
IMPLEMENT_HIT_PROXY(HSplineSegmentProxy, HSplineVisProxy);
IMPLEMENT_HIT_PROXY(HSplineTangentHandleProxy, HSplineVisProxy);

#define LOCTEXT_NAMESPACE "SplineComponentVisualizer"
DEFINE_LOG_CATEGORY_STATIC(LogSplineComponentVisualizer, Log, All)

#define VISUALIZE_SPLINE_UPVECTORS 0

/** Define commands for the spline component visualizer */
class FSplineComponentVisualizerCommands : public TCommands<FSplineComponentVisualizerCommands>
{
public:
	FSplineComponentVisualizerCommands() : TCommands <FSplineComponentVisualizerCommands>
	(
		"SplineComponentVisualizer",	// Context name for fast lookup
		LOCTEXT("SplineComponentVisualizer", "Spline Component Visualizer"),	// Localized context name for displaying
		NAME_None,	// Parent
		FEditorStyle::GetStyleSetName()
	)
	{
	}

	virtual void RegisterCommands() override
	{
		UI_COMMAND(DeleteKey, "Delete Spline Point", "Delete the currently selected spline point.", EUserInterfaceActionType::Button, FInputChord(EKeys::Delete));
		UI_COMMAND(DuplicateKey, "Duplicate Spline Point", "Duplicate the currently selected spline point.", EUserInterfaceActionType::Button, FInputChord());
		UI_COMMAND(AddKey, "Add Spline Point Here", "Add a new spline point at the cursor location.", EUserInterfaceActionType::Button, FInputChord());
		UI_COMMAND(SelectAll, "Select All Spline Points", "Select all spline points.", EUserInterfaceActionType::Button, FInputChord());
		UI_COMMAND(ResetToUnclampedTangent, "Unclamped Tangent", "Reset the tangent for this spline point to its default unclamped value.", EUserInterfaceActionType::Button, FInputChord());
		UI_COMMAND(ResetToClampedTangent, "Clamped Tangent", "Reset the tangent for this spline point to its default clamped value.", EUserInterfaceActionType::Button, FInputChord());
		UI_COMMAND(SetKeyToCurve, "Curve", "Set spline point to Curve type", EUserInterfaceActionType::RadioButton, FInputChord());
		UI_COMMAND(SetKeyToLinear, "Linear", "Set spline point to Linear type", EUserInterfaceActionType::RadioButton, FInputChord());
		UI_COMMAND(SetKeyToConstant, "Constant", "Set spline point to Constant type", EUserInterfaceActionType::RadioButton, FInputChord());
		UI_COMMAND(SnapToNearestSplinePoint, "Snap to Nearest Spline Point", "Snap to nearest spline point.", EUserInterfaceActionType::Button, FInputChord());
		UI_COMMAND(AlignToNearestSplinePoint, "Align to Nearest Spline Point", "Align to nearest spline point.", EUserInterfaceActionType::Button, FInputChord());
		UI_COMMAND(SnapAllToSelectedX, "Snap All To Selected X", "Snap all spline points to selected spline point X.", EUserInterfaceActionType::Button, FInputChord());
		UI_COMMAND(SnapAllToSelectedY, "Snap All To Selected Y", "Snap all spline points to selected spline point Y.", EUserInterfaceActionType::Button, FInputChord());
		UI_COMMAND(SnapAllToSelectedZ, "Snap All To Selected Z", "Snap all spline points to selected spline point Z.", EUserInterfaceActionType::Button, FInputChord());
		UI_COMMAND(SetLockedAxisNone, "None", "New spline point axis is not fixed.", EUserInterfaceActionType::RadioButton, FInputChord());
		UI_COMMAND(SetLockedAxisX, "X", "Fix X axis when adding new spline points.", EUserInterfaceActionType::RadioButton, FInputChord());
		UI_COMMAND(SetLockedAxisY, "Y", "Fix Y axis when adding new spline points.", EUserInterfaceActionType::RadioButton, FInputChord());
		UI_COMMAND(SetLockedAxisZ, "Z", "Fix Z axis when adding new spline points.", EUserInterfaceActionType::RadioButton, FInputChord());
		UI_COMMAND(VisualizeRollAndScale, "Visualize Roll and Scale", "Whether the visualization should show roll and scale on this spline.", EUserInterfaceActionType::ToggleButton, FInputChord());
		UI_COMMAND(DiscontinuousSpline, "Allow Discontinuous Splines", "Whether the visualization allows Arrive and Leave tangents to be set separately.", EUserInterfaceActionType::ToggleButton, FInputChord());
		UI_COMMAND(ResetToDefault, "Reset to Default", "Reset this spline to its archetype default.", EUserInterfaceActionType::Button, FInputChord());
	}

public:
	/** Delete key */
	TSharedPtr<FUICommandInfo> DeleteKey;

	/** Duplicate key */
	TSharedPtr<FUICommandInfo> DuplicateKey;

	/** Add key */
	TSharedPtr<FUICommandInfo> AddKey;

	/** Select all */
	TSharedPtr<FUICommandInfo> SelectAll;

	/** Reset to unclamped tangent */
	TSharedPtr<FUICommandInfo> ResetToUnclampedTangent;

	/** Reset to clamped tangent */
	TSharedPtr<FUICommandInfo> ResetToClampedTangent;

	/** Set spline key to Curve type */
	TSharedPtr<FUICommandInfo> SetKeyToCurve;

	/** Set spline key to Linear type */
	TSharedPtr<FUICommandInfo> SetKeyToLinear;

	/** Set spline key to Constant type */
	TSharedPtr<FUICommandInfo> SetKeyToConstant;

	/** Snap to nearest spline point on another spline component */
	TSharedPtr<FUICommandInfo> SnapToNearestSplinePoint;

	/** Align to nearest spline point on another spline component */
	TSharedPtr<FUICommandInfo> AlignToNearestSplinePoint;

	/** Snap all spline points to selected point X */
	TSharedPtr<FUICommandInfo> SnapAllToSelectedX;

	/** Snap all spline points to selected point Y */
	TSharedPtr<FUICommandInfo> SnapAllToSelectedY;

	/** Snap all spline points to selected point Z */
	TSharedPtr<FUICommandInfo> SnapAllToSelectedZ;

	/** No axis is locked when adding new spline points */
	TSharedPtr<FUICommandInfo> SetLockedAxisNone;

	/** Lock X axis when adding new spline points */
	TSharedPtr<FUICommandInfo> SetLockedAxisX;

	/** Lock Y axis when adding new spline points */
	TSharedPtr<FUICommandInfo> SetLockedAxisY;

	/** Lock Z axis when adding new spline points */
	TSharedPtr<FUICommandInfo> SetLockedAxisZ;

	/** Whether the visualization should show roll and scale */
	TSharedPtr<FUICommandInfo> VisualizeRollAndScale;

	/** Whether we allow separate Arrive / Leave tangents, resulting in a discontinuous spline */
	TSharedPtr<FUICommandInfo> DiscontinuousSpline;

	/** Reset this spline to its default */
	TSharedPtr<FUICommandInfo> ResetToDefault;
};

FSplineComponentVisualizer::FSplineComponentVisualizer()
	: FComponentVisualizer()
	, LastKeyIndexSelected(INDEX_NONE)
	, SelectedSegmentIndex(INDEX_NONE)
	, SelectedTangentHandle(INDEX_NONE)
	, SelectedTangentHandleType(ESelectedTangentHandle::None)
	, bAllowDuplication(true)
	, AddKeyLockedAxis(EAxis::None)
{
	FSplineComponentVisualizerCommands::Register();

	SplineComponentVisualizerActions = MakeShareable(new FUICommandList);

	SplineCurvesProperty = FindField<UProperty>(USplineComponent::StaticClass(), GET_MEMBER_NAME_CHECKED(USplineComponent, SplineCurves));
}

void FSplineComponentVisualizer::OnRegister()
{
	const auto& Commands = FSplineComponentVisualizerCommands::Get();

	SplineComponentVisualizerActions->MapAction(
		Commands.DeleteKey,
		FExecuteAction::CreateSP(this, &FSplineComponentVisualizer::OnDeleteKey),
		FCanExecuteAction::CreateSP(this, &FSplineComponentVisualizer::CanDeleteKey));

	SplineComponentVisualizerActions->MapAction(
		Commands.DuplicateKey,
		FExecuteAction::CreateSP(this, &FSplineComponentVisualizer::OnDuplicateKey),
		FCanExecuteAction::CreateSP(this, &FSplineComponentVisualizer::IsKeySelectionValid));

	SplineComponentVisualizerActions->MapAction(
		Commands.AddKey,
		FExecuteAction::CreateSP(this, &FSplineComponentVisualizer::OnAddKeyToSegment),
		FCanExecuteAction::CreateSP(this, &FSplineComponentVisualizer::CanAddKeyToSegment));

	SplineComponentVisualizerActions->MapAction(
		Commands.SelectAll,
		FExecuteAction::CreateSP(this, &FSplineComponentVisualizer::OnSelectAllSplinePoints),
		FCanExecuteAction::CreateSP(this, &FSplineComponentVisualizer::CanSelectAllSplinePoints));

	SplineComponentVisualizerActions->MapAction(
		Commands.ResetToUnclampedTangent,
		FExecuteAction::CreateSP(this, &FSplineComponentVisualizer::OnResetToAutomaticTangent, CIM_CurveAuto),
		FCanExecuteAction::CreateSP(this, &FSplineComponentVisualizer::CanResetToAutomaticTangent, CIM_CurveAuto));

	SplineComponentVisualizerActions->MapAction(
		Commands.ResetToClampedTangent,
		FExecuteAction::CreateSP(this, &FSplineComponentVisualizer::OnResetToAutomaticTangent, CIM_CurveAutoClamped),
		FCanExecuteAction::CreateSP(this, &FSplineComponentVisualizer::CanResetToAutomaticTangent, CIM_CurveAutoClamped));

	SplineComponentVisualizerActions->MapAction(
		Commands.SetKeyToCurve,
		FExecuteAction::CreateSP(this, &FSplineComponentVisualizer::OnSetKeyType, CIM_CurveAuto),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &FSplineComponentVisualizer::IsKeyTypeSet, CIM_CurveAuto));

	SplineComponentVisualizerActions->MapAction(
		Commands.SetKeyToLinear,
		FExecuteAction::CreateSP(this, &FSplineComponentVisualizer::OnSetKeyType, CIM_Linear),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &FSplineComponentVisualizer::IsKeyTypeSet, CIM_Linear));

	SplineComponentVisualizerActions->MapAction(
		Commands.SetKeyToConstant,
		FExecuteAction::CreateSP(this, &FSplineComponentVisualizer::OnSetKeyType, CIM_Constant),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &FSplineComponentVisualizer::IsKeyTypeSet, CIM_Constant));

	SplineComponentVisualizerActions->MapAction(
		Commands.SnapToNearestSplinePoint,
		FExecuteAction::CreateSP(this, &FSplineComponentVisualizer::OnSnapToNearestSplinePoint, false),
		FCanExecuteAction::CreateSP(this, &FSplineComponentVisualizer::CanSnapToNearestSplinePoint));

	SplineComponentVisualizerActions->MapAction(
		Commands.AlignToNearestSplinePoint,
		FExecuteAction::CreateSP(this, &FSplineComponentVisualizer::OnSnapToNearestSplinePoint, true),
		FCanExecuteAction::CreateSP(this, &FSplineComponentVisualizer::CanSnapToNearestSplinePoint));

	SplineComponentVisualizerActions->MapAction(
		Commands.SnapAllToSelectedX,
		FExecuteAction::CreateSP(this, &FSplineComponentVisualizer::OnSnapAll, EAxis::X),
		FCanExecuteAction::CreateSP(this, &FSplineComponentVisualizer::CanSnapAll));

	SplineComponentVisualizerActions->MapAction(
		Commands.SnapAllToSelectedY,
		FExecuteAction::CreateSP(this, &FSplineComponentVisualizer::OnSnapAll, EAxis::Y),
		FCanExecuteAction::CreateSP(this, &FSplineComponentVisualizer::CanSnapAll));

	SplineComponentVisualizerActions->MapAction(
		Commands.SnapAllToSelectedZ,
		FExecuteAction::CreateSP(this, &FSplineComponentVisualizer::OnSnapAll, EAxis::Z),
		FCanExecuteAction::CreateSP(this, &FSplineComponentVisualizer::CanSnapAll));

	SplineComponentVisualizerActions->MapAction(
		Commands.SetLockedAxisNone,
		FExecuteAction::CreateSP(this, &FSplineComponentVisualizer::OnLockAxis, EAxis::None),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &FSplineComponentVisualizer::IsLockAxisSet, EAxis::None));

	SplineComponentVisualizerActions->MapAction(
		Commands.SetLockedAxisX,
		FExecuteAction::CreateSP(this, &FSplineComponentVisualizer::OnLockAxis, EAxis::X),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &FSplineComponentVisualizer::IsLockAxisSet, EAxis::X));

	SplineComponentVisualizerActions->MapAction(
		Commands.SetLockedAxisY,
		FExecuteAction::CreateSP(this, &FSplineComponentVisualizer::OnLockAxis, EAxis::Y),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &FSplineComponentVisualizer::IsLockAxisSet, EAxis::Y));

	SplineComponentVisualizerActions->MapAction(
		Commands.SetLockedAxisZ,
		FExecuteAction::CreateSP(this, &FSplineComponentVisualizer::OnLockAxis, EAxis::Z),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &FSplineComponentVisualizer::IsLockAxisSet, EAxis::Z));


	SplineComponentVisualizerActions->MapAction(
		Commands.VisualizeRollAndScale,
		FExecuteAction::CreateSP(this, &FSplineComponentVisualizer::OnSetVisualizeRollAndScale),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &FSplineComponentVisualizer::IsVisualizingRollAndScale));

	SplineComponentVisualizerActions->MapAction(
		Commands.DiscontinuousSpline,
		FExecuteAction::CreateSP(this, &FSplineComponentVisualizer::OnSetDiscontinuousSpline),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &FSplineComponentVisualizer::IsDiscontinuousSpline));

	SplineComponentVisualizerActions->MapAction(
		Commands.ResetToDefault,
		FExecuteAction::CreateSP(this, &FSplineComponentVisualizer::OnResetToDefault),
		FCanExecuteAction::CreateSP(this, &FSplineComponentVisualizer::CanResetToDefault));

	bool bAlign = false;
	bool bUseLineTrace = false;
	bool bUseBounds = false;
	bool bUsePivot = false;
	SplineComponentVisualizerActions->MapAction(
		FLevelEditorCommands::Get().SnapToFloor,
		FExecuteAction::CreateStatic(&FLevelEditorActionCallbacks::SnapToFloor_Clicked, bAlign, bUseLineTrace, bUseBounds, bUsePivot),
		FCanExecuteAction::CreateStatic(&FLevelEditorActionCallbacks::ActorSelected_CanExecute)
		);

	bAlign = true;
	bUseLineTrace = false;
	bUseBounds = false;
	bUsePivot = false;
	SplineComponentVisualizerActions->MapAction(
		//Commands.AlignToFloor,
		FLevelEditorCommands::Get().AlignToFloor,
		FExecuteAction::CreateStatic(&FLevelEditorActionCallbacks::SnapToFloor_Clicked, bAlign, bUseLineTrace, bUseBounds, bUsePivot),
		FCanExecuteAction::CreateStatic(&FLevelEditorActionCallbacks::ActorSelected_CanExecute)
		);
}

FSplineComponentVisualizer::~FSplineComponentVisualizer()
{
	FSplineComponentVisualizerCommands::Unregister();
}

static float GetDashSize(const FSceneView* View, const FVector& Start, const FVector& End, float Scale)
{
	const float StartW = View->WorldToScreen(Start).W;
	const float EndW = View->WorldToScreen(End).W;

	const float WLimit = 10.0f;
	if (StartW > WLimit || EndW > WLimit)
	{
		return FMath::Max(StartW, EndW) * Scale;
	}

	return 0.0f;
}

void FSplineComponentVisualizer::DrawVisualization(const UActorComponent* Component, const FSceneView* View, FPrimitiveDrawInterface* PDI)
{
	if (const USplineComponent* SplineComp = Cast<const USplineComponent>(Component))
	{
		const FInterpCurveVector& SplineInfo = SplineComp->GetSplinePointsPosition();
		const USplineComponent* EditedSplineComp = GetEditedSplineComponent();

		const USplineComponent* Archetype = CastChecked<USplineComponent>(SplineComp->GetArchetype());
		const bool bIsSplineEditable = !SplineComp->bModifiedByConstructionScript; // bSplineHasBeenEdited || SplineInfo == Archetype->SplineCurves.Position || SplineComp->bInputSplinePointsToConstructionScript;

		const FColor ReadOnlyColor = FColor(255, 0, 255, 255);
		const FColor NormalColor = bIsSplineEditable ? FColor(SplineComp->EditorUnselectedSplineSegmentColor.ToFColor(true)) : ReadOnlyColor;
		const FColor SelectedColor = bIsSplineEditable ? FColor(SplineComp->EditorSelectedSplineSegmentColor.ToFColor(true)) : ReadOnlyColor;
		const float GrabHandleSize = 10.0f;
		const float TangentHandleSize = 8.0f;

		// Draw the tangent handles before anything else so they will not overdraw the rest of the spline
		// Only draw tangent handle when single key is selected
		if (SplineComp == EditedSplineComp)
		{
			for (int32 SelectedKey : SelectedKeys)
			{
				if (SplineInfo.Points[SelectedKey].IsCurveKey())
				{
					const FVector Location = SplineComp->GetLocationAtSplinePoint(SelectedKey, ESplineCoordinateSpace::World);
					const FVector LeaveTangent = SplineComp->GetLeaveTangentAtSplinePoint(SelectedKey, ESplineCoordinateSpace::World);
					const FVector ArriveTangent = SplineComp->bAllowDiscontinuousSpline ?
						SplineComp->GetArriveTangentAtSplinePoint(SelectedKey, ESplineCoordinateSpace::World) : LeaveTangent;

					PDI->SetHitProxy(NULL);

					PDI->DrawLine(Location, Location + LeaveTangent, NormalColor, SDPG_Foreground);
					PDI->DrawLine(Location, Location - ArriveTangent, NormalColor, SDPG_Foreground);

					if (bIsSplineEditable)
					{
						PDI->SetHitProxy(new HSplineTangentHandleProxy(Component, SelectedKey, false));
					}
					PDI->DrawPoint(Location + LeaveTangent, NormalColor, TangentHandleSize, SDPG_Foreground);

					if (bIsSplineEditable)
					{
						PDI->SetHitProxy(new HSplineTangentHandleProxy(Component, SelectedKey, true));
					}
					PDI->DrawPoint(Location - ArriveTangent, NormalColor, TangentHandleSize, SDPG_Foreground);

					PDI->SetHitProxy(NULL);
				}
			}
		}

		const bool bShouldVisualizeScale = SplineComp->bShouldVisualizeScale;
		const float DefaultScale = SplineComp->ScaleVisualizationWidth;

		FVector OldKeyPos(0);
		FVector OldKeyRightVector(0);
		FVector OldKeyScale(0);

		const int32 NumPoints = SplineInfo.Points.Num();
		const int32 NumSegments = SplineInfo.bIsLooped ? NumPoints : NumPoints - 1;
		for (int32 KeyIdx = 0; KeyIdx < NumSegments + 1; KeyIdx++)
		{
			const FVector NewKeyPos = SplineComp->GetLocationAtSplinePoint(KeyIdx, ESplineCoordinateSpace::World);
			const FVector NewKeyRightVector = SplineComp->GetRightVectorAtSplinePoint(KeyIdx, ESplineCoordinateSpace::World);
			const FVector NewKeyUpVector = SplineComp->GetUpVectorAtSplinePoint(KeyIdx, ESplineCoordinateSpace::World);
			const FVector NewKeyScale = SplineComp->GetScaleAtSplinePoint(KeyIdx) * DefaultScale;

			const FColor KeyColor = (SplineComp == EditedSplineComp && SelectedKeys.Contains(KeyIdx)) ? SelectedColor : NormalColor;

			// Draw the keypoint and up/right vectors
			if (KeyIdx < NumPoints)
			{
				if (bShouldVisualizeScale)
				{
					PDI->SetHitProxy(NULL);

					PDI->DrawLine(NewKeyPos, NewKeyPos - NewKeyRightVector * NewKeyScale.Y, KeyColor, SDPG_Foreground);
					PDI->DrawLine(NewKeyPos, NewKeyPos + NewKeyRightVector * NewKeyScale.Y, KeyColor, SDPG_Foreground);
					PDI->DrawLine(NewKeyPos, NewKeyPos + NewKeyUpVector * NewKeyScale.Z, KeyColor, SDPG_Foreground);

					const int32 ArcPoints = 20;
					FVector OldArcPos = NewKeyPos + NewKeyRightVector * NewKeyScale.Y;
					for (int32 ArcIndex = 1; ArcIndex <= ArcPoints; ArcIndex++)
					{
						float Sin;
						float Cos;
						FMath::SinCos(&Sin, &Cos, ArcIndex * PI / ArcPoints);
						const FVector NewArcPos = NewKeyPos + Cos * NewKeyRightVector * NewKeyScale.Y + Sin * NewKeyUpVector * NewKeyScale.Z;
						PDI->DrawLine(OldArcPos, NewArcPos, KeyColor, SDPG_Foreground);
						OldArcPos = NewArcPos;
					}
				}

				if (bIsSplineEditable)
				{
					PDI->SetHitProxy(new HSplineKeyProxy(Component, KeyIdx));
				}
				PDI->DrawPoint(NewKeyPos, KeyColor, GrabHandleSize, SDPG_Foreground);
				PDI->SetHitProxy(NULL);
			}

			// If not the first keypoint, draw a line to the previous keypoint.
			if (KeyIdx > 0)
			{
				const FColor LineColor = (SplineComp == EditedSplineComp && SelectedKeys.Contains(KeyIdx - 1)) ? SelectedColor : NormalColor;
				if (bIsSplineEditable)
				{
					PDI->SetHitProxy(new HSplineSegmentProxy(Component, KeyIdx - 1));
				}

				// For constant interpolation - don't draw ticks - just draw dotted line.
				if (SplineInfo.Points[KeyIdx - 1].InterpMode == CIM_Constant)
				{
					const float DashSize = GetDashSize(View, OldKeyPos, NewKeyPos, 0.03f);
					if (DashSize > 0.0f)
					{
						DrawDashedLine(PDI, OldKeyPos, NewKeyPos, LineColor, DashSize, SDPG_World);
					}
				}
				else
				{
					// Find position on first keyframe.
					FVector OldPos = OldKeyPos;
					FVector OldRightVector = OldKeyRightVector;
					FVector OldScale = OldKeyScale;

					// Then draw a line for each substep.
					const int32 NumSteps = 20;

					for (int32 StepIdx = 1; StepIdx <= NumSteps; StepIdx++)
					{
						const float Key = (KeyIdx - 1) + (StepIdx / static_cast<float>(NumSteps));
						const FVector NewPos = SplineComp->GetLocationAtSplineInputKey(Key, ESplineCoordinateSpace::World);
						const FVector NewRightVector = SplineComp->GetRightVectorAtSplineInputKey(Key, ESplineCoordinateSpace::World);
						const FVector NewScale = SplineComp->GetScaleAtSplineInputKey(Key) * DefaultScale;

						PDI->DrawLine(OldPos, NewPos, LineColor, SDPG_Foreground);
						if (bShouldVisualizeScale)
						{
							PDI->DrawLine(OldPos - OldRightVector * OldScale.Y, NewPos - NewRightVector * NewScale.Y, LineColor, SDPG_Foreground);
							PDI->DrawLine(OldPos + OldRightVector * OldScale.Y, NewPos + NewRightVector * NewScale.Y, LineColor, SDPG_Foreground);

							#if VISUALIZE_SPLINE_UPVECTORS
							const FVector NewUpVector = SplineComp->GetUpVectorAtSplineInputKey(Key, ESplineCoordinateSpace::World);
							PDI->DrawLine(NewPos, NewPos + NewUpVector * SplineComp->ScaleVisualizationWidth * 0.5f, LineColor, SDPG_Foreground);
							PDI->DrawLine(NewPos, NewPos + NewRightVector * SplineComp->ScaleVisualizationWidth * 0.5f, LineColor, SDPG_Foreground);
							#endif
						}

						OldPos = NewPos;
						OldRightVector = NewRightVector;
						OldScale = NewScale;
					}
				}

				PDI->SetHitProxy(NULL);
			}

			OldKeyPos = NewKeyPos;
			OldKeyRightVector = NewKeyRightVector;
			OldKeyScale = NewKeyScale;
		}
	}
}

void FSplineComponentVisualizer::ChangeSelectionState(int32 Index, bool bIsCtrlHeld)
{
	if (Index == INDEX_NONE)
	{
		SelectedKeys.Empty();
		LastKeyIndexSelected = INDEX_NONE;
	}
	else if (!bIsCtrlHeld)
	{
		SelectedKeys.Empty();
		SelectedKeys.Add(Index);
		LastKeyIndexSelected = Index;
	}
	else
	{
		// Add or remove from selection if Ctrl is held
		if (SelectedKeys.Contains(Index))
		{
			// If already in selection, toggle it off
			SelectedKeys.Remove(Index);

			if (LastKeyIndexSelected == Index)
			{
				if (SelectedKeys.Num() == 0)
				{
					// Last key selected: clear last key index selected
					LastKeyIndexSelected = INDEX_NONE;
				}
				else
				{
					// Arbitarily set last key index selected to first member of the set (so that it is valid)
					LastKeyIndexSelected = *SelectedKeys.CreateConstIterator();
				}
			}
		}
		else
		{
			// Add to selection
			SelectedKeys.Add(Index);
			LastKeyIndexSelected = Index;
		}
	}
}

bool FSplineComponentVisualizer::VisProxyHandleClick(FEditorViewportClient* InViewportClient, HComponentVisProxy* VisProxy, const FViewportClick& Click)
{
	if(VisProxy && VisProxy->Component.IsValid())
	{
		const USplineComponent* SplineComp = CastChecked<const USplineComponent>(VisProxy->Component.Get());

		SplineCompPropName = GetComponentPropertyName(SplineComp);
		if(SplineCompPropName.IsValid())
		{
			AActor* OldSplineOwningActor = SplineOwningActor.Get();
			SplineOwningActor = SplineComp->GetOwner();

			if (OldSplineOwningActor != SplineOwningActor)
			{
				// Reset selection state if we are selecting a different actor to the one previously selected
				ChangeSelectionState(INDEX_NONE, false);
				SelectedSegmentIndex = INDEX_NONE;
				SelectedTangentHandle = INDEX_NONE;
				SelectedTangentHandleType = ESelectedTangentHandle::None;
			}

			if (VisProxy->IsA(HSplineKeyProxy::StaticGetType()))
			{
				// Control point clicked

				// temporarily disable

				HSplineKeyProxy* KeyProxy = (HSplineKeyProxy*)VisProxy;

				// Modify the selection state, unless right-clicking on an already selected key
				if (Click.GetKey() != EKeys::RightMouseButton || !SelectedKeys.Contains(KeyProxy->KeyIndex))
				{
					ChangeSelectionState(KeyProxy->KeyIndex, InViewportClient->IsCtrlPressed());
				}
				SelectedSegmentIndex = INDEX_NONE;
				SelectedTangentHandle = INDEX_NONE;
				SelectedTangentHandleType = ESelectedTangentHandle::None;

				if (LastKeyIndexSelected == INDEX_NONE)
				{
					SplineOwningActor = nullptr;
					return false;
				}

				CachedRotation = SplineComp->GetQuaternionAtSplinePoint(LastKeyIndexSelected, ESplineCoordinateSpace::World);

				return true;
			}
			else if (VisProxy->IsA(HSplineSegmentProxy::StaticGetType()))
			{
				// Spline segment clicked

				// Divide segment into subsegments and test each subsegment against ray representing click position and camera direction.
				// Closest encounter with the spline determines the spline position.
				const int32 NumSubdivisions = 16;

				HSplineSegmentProxy* SegmentProxy = (HSplineSegmentProxy*)VisProxy;
				ChangeSelectionState(SegmentProxy->SegmentIndex, InViewportClient->IsCtrlPressed());
				SelectedSegmentIndex = SegmentProxy->SegmentIndex;
				SelectedTangentHandle = INDEX_NONE;
				SelectedTangentHandleType = ESelectedTangentHandle::None;

				if (LastKeyIndexSelected == INDEX_NONE)
				{
					SplineOwningActor = nullptr;
					return false;
				}

				CachedRotation = SplineComp->GetQuaternionAtSplinePoint(LastKeyIndexSelected, ESplineCoordinateSpace::World);

				float SubsegmentStartKey = static_cast<float>(SelectedSegmentIndex);
				FVector SubsegmentStart = SplineComp->GetLocationAtSplineInputKey(SubsegmentStartKey, ESplineCoordinateSpace::World);

				float ClosestDistance = TNumericLimits<float>::Max();
				FVector BestLocation = SubsegmentStart;

				for (int32 Step = 1; Step < NumSubdivisions; Step++)
				{
					const float SubsegmentEndKey = SelectedSegmentIndex + Step / static_cast<float>(NumSubdivisions);
					const FVector SubsegmentEnd = SplineComp->GetLocationAtSplineInputKey(SubsegmentEndKey, ESplineCoordinateSpace::World);

					FVector SplineClosest;
					FVector RayClosest;
					FMath::SegmentDistToSegmentSafe(SubsegmentStart, SubsegmentEnd, Click.GetOrigin(), Click.GetOrigin() + Click.GetDirection() * 50000.0f, SplineClosest, RayClosest);

					const float Distance = FVector::DistSquared(SplineClosest, RayClosest);
					if (Distance < ClosestDistance)
					{
						ClosestDistance = Distance;
						BestLocation = SplineClosest;
					}

					SubsegmentStartKey = SubsegmentEndKey;
					SubsegmentStart = SubsegmentEnd;
				}

				SelectedSplinePosition = BestLocation;

				return true;
			}
			else if (VisProxy->IsA(HSplineTangentHandleProxy::StaticGetType()))
			{
				// Tangent handle clicked

				HSplineTangentHandleProxy* KeyProxy = (HSplineTangentHandleProxy*)VisProxy;

				// Note: don't change key selection when a tangent handle is clicked
				SelectedSegmentIndex = INDEX_NONE;
				SelectedTangentHandle = KeyProxy->KeyIndex;
				SelectedTangentHandleType = KeyProxy->bArriveTangent ? ESelectedTangentHandle::Arrive : ESelectedTangentHandle::Leave;

				CachedRotation = SplineComp->GetQuaternionAtSplinePoint(SelectedTangentHandle, ESplineCoordinateSpace::World);

				return true;
			}
		}
		else
		{
			SplineOwningActor = nullptr;
		}
	}

	return false;
}

USplineComponent* FSplineComponentVisualizer::GetEditedSplineComponent() const
{
	return Cast<USplineComponent>(GetComponentFromPropertyName(SplineOwningActor.Get(), SplineCompPropName));
}


bool FSplineComponentVisualizer::GetWidgetLocation(const FEditorViewportClient* ViewportClient, FVector& OutLocation) const
{
	USplineComponent* SplineComp = GetEditedSplineComponent();
	if (SplineComp != nullptr)
	{
		const FInterpCurveVector& Position = SplineComp->GetSplinePointsPosition();

		if (SelectedTangentHandle != INDEX_NONE)
		{
			// If tangent handle index is set, use that
			check(SelectedTangentHandle < Position.Points.Num());
			const auto& Point = Position.Points[SelectedTangentHandle];

			check(SelectedTangentHandleType != ESelectedTangentHandle::None);
			if (SelectedTangentHandleType == ESelectedTangentHandle::Leave)
			{
				OutLocation = SplineComp->GetComponentTransform().TransformPosition(Point.OutVal + Point.LeaveTangent);
			}
			else if (SelectedTangentHandleType == ESelectedTangentHandle::Arrive)
			{
				OutLocation = SplineComp->GetComponentTransform().TransformPosition(Point.OutVal - Point.ArriveTangent);
			}

			return true;
		}
		else if (LastKeyIndexSelected != INDEX_NONE)
		{
			// Otherwise use the last key index set
			check(LastKeyIndexSelected < Position.Points.Num());
			check(SelectedKeys.Contains(LastKeyIndexSelected));
			const auto& Point = Position.Points[LastKeyIndexSelected];
			OutLocation = SplineComp->GetComponentTransform().TransformPosition(Point.OutVal);
			return true;
		}
	}

	return false;
}


bool FSplineComponentVisualizer::GetCustomInputCoordinateSystem(const FEditorViewportClient* ViewportClient, FMatrix& OutMatrix) const
{
	if (ViewportClient->GetWidgetCoordSystemSpace() == COORD_Local || ViewportClient->GetWidgetMode() == FWidget::WM_Rotate)
	{
		USplineComponent* SplineComp = GetEditedSplineComponent();
		if (SplineComp != nullptr)
		{
			OutMatrix = FRotationMatrix::Make(CachedRotation);
			return true;
		}
	}

	return false;
}


bool FSplineComponentVisualizer::IsVisualizingArchetype() const
{
	USplineComponent* SplineComp = GetEditedSplineComponent();
	return (SplineComp && SplineComp->GetOwner() && FActorEditorUtils::IsAPreviewOrInactiveActor(SplineComp->GetOwner()));
}


bool FSplineComponentVisualizer::HandleInputDelta(FEditorViewportClient* ViewportClient, FViewport* Viewport, FVector& DeltaTranslate, FRotator& DeltaRotate, FVector& DeltaScale)
{
	USplineComponent* SplineComp = GetEditedSplineComponent();
	if (SplineComp != nullptr)
	{
		if (SelectedTangentHandle != INDEX_NONE)
		{
			return TransformSelectedTangent(DeltaTranslate);
		}
		else
		{
			bool bDuplicateKey = ViewportClient->IsAltPressed() && bAllowDuplication;
			return TransformSelectedKeys(DeltaTranslate, DeltaRotate, DeltaScale, bDuplicateKey);
		}
	}

	return false;
}

bool FSplineComponentVisualizer::TransformSelectedTangent(const FVector& DeltaTranslate)
{
	check(SelectedTangentHandle != INDEX_NONE);

	USplineComponent* SplineComp = GetEditedSplineComponent();
	if (SplineComp != nullptr)
	{
		FInterpCurveVector& SplinePosition = SplineComp->GetSplinePointsPosition();

		const int32 NumPoints = SplinePosition.Points.Num();

		check(SelectedTangentHandle < NumPoints);
		check(SelectedTangentHandleType != ESelectedTangentHandle::None);

		if (!DeltaTranslate.IsZero())
		{
			SplineComp->Modify();

			FInterpCurvePoint<FVector>& EditedPoint = SplinePosition.Points[SelectedTangentHandle];
			if (SplineComp->bAllowDiscontinuousSpline)
			{
				if (SelectedTangentHandleType == ESelectedTangentHandle::Leave)
				{
					EditedPoint.LeaveTangent += SplineComp->GetComponentTransform().InverseTransformVector(DeltaTranslate);
				}
				else
				{
					EditedPoint.ArriveTangent += SplineComp->GetComponentTransform().InverseTransformVector(-DeltaTranslate);
				}
			}
			else
			{
				const FVector Delta = (SelectedTangentHandleType == ESelectedTangentHandle::Leave) ? DeltaTranslate : -DeltaTranslate;
				const FVector Tangent = EditedPoint.LeaveTangent + SplineComp->GetComponentTransform().InverseTransformVector(Delta);

				EditedPoint.LeaveTangent = Tangent;
				EditedPoint.ArriveTangent = Tangent;
			}

			EditedPoint.InterpMode = CIM_CurveUser;
		}

		SplineComp->UpdateSpline();
		SplineComp->bSplineHasBeenEdited = true;

		NotifyPropertyModified(SplineComp, SplineCurvesProperty);

		return true;
	}

	return false;
}

bool FSplineComponentVisualizer::TransformSelectedKeys(const FVector& DeltaTranslate, const FRotator& DeltaRotate, const FVector& DeltaScale, bool bDuplicateKey)
{
	USplineComponent* SplineComp = GetEditedSplineComponent();
	if (SplineComp != nullptr)
	{
		FInterpCurveVector& SplinePosition = SplineComp->GetSplinePointsPosition();
		FInterpCurveQuat& SplineRotation = SplineComp->GetSplinePointsRotation();
		FInterpCurveVector& SplineScale = SplineComp->GetSplinePointsScale();

		const int32 NumPoints = SplinePosition.Points.Num();

		check(LastKeyIndexSelected != INDEX_NONE);
		check(LastKeyIndexSelected < NumPoints);
		check(SelectedKeys.Num() > 0);

		SplineComp->Modify();

		if (bDuplicateKey)
		{
			DuplicateKey();

			// Don't duplicate again until we release LMB
			bAllowDuplication = false;
		}

		for (int32 SelectedKeyIndex : SelectedKeys)
		{
			FInterpCurvePoint<FVector>& EditedPoint = SplinePosition.Points[SelectedKeyIndex];
			FInterpCurvePoint<FQuat>& EditedRotPoint = SplineRotation.Points[SelectedKeyIndex];
			FInterpCurvePoint<FVector>& EditedScalePoint = SplineScale.Points[SelectedKeyIndex];


			if (!DeltaTranslate.IsZero())
			{
				// Find key position in world space
				const FVector CurrentWorldPos = SplineComp->GetComponentTransform().TransformPosition(EditedPoint.OutVal);
				// Move in world space
				const FVector NewWorldPos = CurrentWorldPos + DeltaTranslate;

				// Convert back to local space
				EditedPoint.OutVal = SplineComp->GetComponentTransform().InverseTransformPosition(NewWorldPos);
			}

			if (!DeltaRotate.IsZero())
			{
				// Set point tangent as user controlled
				EditedPoint.InterpMode = CIM_CurveUser;

				// Rotate tangent according to delta rotation
				FVector NewTangent = SplineComp->GetComponentTransform().GetRotation().RotateVector(EditedPoint.LeaveTangent); // convert local-space tangent vector to world-space
				NewTangent = DeltaRotate.RotateVector(NewTangent); // apply world-space delta rotation to world-space tangent
				NewTangent = SplineComp->GetComponentTransform().GetRotation().Inverse().RotateVector(NewTangent); // convert world-space tangent vector back into local-space
				EditedPoint.LeaveTangent = NewTangent;
				EditedPoint.ArriveTangent = NewTangent;

				// Rotate spline rotation according to delta rotation
				FQuat NewRot = SplineComp->GetComponentTransform().GetRotation() * EditedRotPoint.OutVal; // convert local-space rotation to world-space
				NewRot = DeltaRotate.Quaternion() * NewRot; // apply world-space rotation
				NewRot = SplineComp->GetComponentTransform().GetRotation().Inverse() * NewRot; // convert world-space rotation to local-space
				EditedRotPoint.OutVal = NewRot;
			}

			if (DeltaScale.X != 0.0f)
			{
				// Set point tangent as user controlled
				EditedPoint.InterpMode = CIM_CurveUser;

				const FVector NewTangent = EditedPoint.LeaveTangent * (1.0f + DeltaScale.X);
				EditedPoint.LeaveTangent = NewTangent;
				EditedPoint.ArriveTangent = NewTangent;
			}

			if (DeltaScale.Y != 0.0f)
			{
				// Scale in Y adjusts the scale spline
				EditedScalePoint.OutVal.Y *= (1.0f + DeltaScale.Y);
			}

			if (DeltaScale.Z != 0.0f)
			{
				// Scale in Z adjusts the scale spline
				EditedScalePoint.OutVal.Z *= (1.0f + DeltaScale.Z);
			}
		}

		SplineComp->UpdateSpline();
		SplineComp->bSplineHasBeenEdited = true;

		NotifyPropertyModified(SplineComp, SplineCurvesProperty);

		if (!DeltaRotate.IsZero())
		{
			CachedRotation = SplineComp->GetQuaternionAtSplinePoint(LastKeyIndexSelected, ESplineCoordinateSpace::World);
		}

		GEditor->RedrawLevelEditingViewports(true);

		return true;
	}

	return false;
}

bool FSplineComponentVisualizer::HandleInputKey(FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event)
{
	bool bHandled = false;

	if (Key == EKeys::LeftMouseButton && Event == IE_Released)
	{
		USplineComponent* SplineComp = GetEditedSplineComponent();
		if (SplineComp != nullptr)
		{
			// Recache widget rotation
			int32 Index = SelectedTangentHandle;
			if (Index == INDEX_NONE)
			{
				// If not set, fall back to last key index selected
				Index = LastKeyIndexSelected;
			}

			CachedRotation = SplineComp->GetQuaternionAtSplinePoint(Index, ESplineCoordinateSpace::World);
		}

		// Reset duplication flag on LMB release
		bAllowDuplication = true;
	}

	if (Event == IE_Pressed)
	{
		bHandled = SplineComponentVisualizerActions->ProcessCommandBindings(Key, FSlateApplication::Get().GetModifierKeys(), false);
	}

	return bHandled;
}

bool FSplineComponentVisualizer::HandleModifiedClick(FEditorViewportClient* InViewportClient, HHitProxy* HitProxy, const FViewportClick& Click)
{
	/*
	if (Click.IsControlDown())
	{
		// Add points on Ctrl-Click if the last spline point is selected.

		USplineComponent* SplineComp = GetEditedSplineComponent();
		if (SplineComp != nullptr)
		{
			FInterpCurveVector& SplinePosition = SplineComp->GetSplinePointsPosition();
			int32 NumPoints = SplinePosition.Points.Num();

			// to do add end point
			if (SelectedKeys.Num() == 1 && !SplineComp->IsClosedLoop())
			{
				check(LastKeyIndexSelected != INDEX_NONE);
				check(SelectedKeys.Contains(LastKeyIndexSelected));

				if (LastKeyIndexSelected == 0)
				{
					int32 KeyIdx = LastKeyIndexSelected;

					FInterpCurvePoint<FVector>& EditedPoint = SplinePosition.Points[LastKeyIndexSelected];

					FHitResult Hit(1.0f);
					FCollisionQueryParams Params(SCENE_QUERY_STAT(MoveSplineKeyToTrace), true);

					// Find key position in world space
					const FVector CurrentWorldPos = SplineComp->GetComponentTransform().TransformPosition(EditedPoint.OutVal);

					FVector DeltaTranslate = FVector::ZeroVector;

					if (SplineComp->GetWorld()->LineTraceSingleByChannel(Hit, Click.GetOrigin(), Click.GetOrigin() + Click.GetDirection() * WORLD_MAX, ECC_WorldStatic, Params))
					{
						DeltaTranslate = Hit.Location - CurrentWorldPos;
					}
					else
					{
						FVector ArriveTangent = SplineComp->GetComponentTransform().GetRotation().RotateVector(EditedPoint.ArriveTangent); // convert local-space tangent vector to world-space
						DeltaTranslate = ArriveTangent.GetSafeNormal() * ArriveTangent.Size() * 0.5;
						DeltaTranslate = ArriveTangent.GetSafeNormal() * ArriveTangent.Size() * 0.5;
					}

					OnAddKey();
					TransformSelectedKeys(DeltaTranslate);

					return true;
				}
			}
		}
	}
	*/
	return false;
}


bool FSplineComponentVisualizer::HandleBoxSelect(const FBox& InBox, FEditorViewportClient* InViewportClient, FViewport* InViewport) 
{
	USplineComponent* SplineComp = GetEditedSplineComponent();
	if (SplineComp != nullptr)
	{
		bool bSelectionChanged = false;

		const FInterpCurveVector& SplineInfo = SplineComp->GetSplinePointsPosition();
		int32 NumPoints = SplineInfo.Points.Num();

		// Spline control point selection always uses transparent box selection.
		for (int32 KeyIdx = 0; KeyIdx < NumPoints; KeyIdx++)
		{
			const FVector Pos = SplineComp->GetLocationAtSplinePoint(KeyIdx, ESplineCoordinateSpace::World);

			if (InBox.IsInside(Pos))
			{
				ChangeSelectionState(KeyIdx, true);
				bSelectionChanged = true;
			}
		}

		if (bSelectionChanged)
		{
			SelectedSegmentIndex = INDEX_NONE;
			SelectedTangentHandle = INDEX_NONE;
			SelectedTangentHandleType = ESelectedTangentHandle::None;
		}
	}

	return true;
}

bool FSplineComponentVisualizer::HandleFrustumSelect(const FConvexVolume& InFrustum, FEditorViewportClient* InViewportClient, FViewport* InViewport) 
{
	USplineComponent* SplineComp = GetEditedSplineComponent();
	if (SplineComp != nullptr)
	{
		bool bSelectionChanged = false;

		const FInterpCurveVector& SplineInfo = SplineComp->GetSplinePointsPosition();
		int32 NumPoints = SplineInfo.Points.Num();

		// Spline control point selection always uses transparent box selection.
		for (int32 KeyIdx = 0; KeyIdx < NumPoints; KeyIdx++)
		{
			const FVector Pos = SplineComp->GetLocationAtSplinePoint(KeyIdx, ESplineCoordinateSpace::World);

			if (InFrustum.IntersectPoint(Pos))
			{
				ChangeSelectionState(KeyIdx, true);
				bSelectionChanged = true;
			}
		}

		if (bSelectionChanged)
		{
			SelectedSegmentIndex = INDEX_NONE;
			SelectedTangentHandle = INDEX_NONE;
			SelectedTangentHandleType = ESelectedTangentHandle::None;
		}
	}

	return true;
}

bool FSplineComponentVisualizer::HasFocusOnSelectionBoundingBox(FBox& OutBoundingBox)
{
	OutBoundingBox.Init();

	if (SelectedKeys.Num() > 0)
	{
		USplineComponent* SplineComp = GetEditedSplineComponent();
		if (SplineComp != nullptr)
		{
			// Spline control point selection always uses transparent box selection.
			for (int32 KeyIdx : SelectedKeys)
			{
				const FVector Pos = SplineComp->GetLocationAtSplinePoint(KeyIdx, ESplineCoordinateSpace::World);

				OutBoundingBox += Pos;
			}

			OutBoundingBox.ExpandBy(50.f);
			return true;
		}
	}

	return false;
}

bool FSplineComponentVisualizer::HandleSnapTo(const bool bInAlign, const bool bInUseLineTrace, const bool bInUseBounds, const bool bInUsePivot, AActor* InDestination)
{
	// Does not handle Snap/Align Pivot, Snap/Align Bottom Control Points or Snap/Align to Actor.
	if (bInUsePivot || bInUseBounds || InDestination)
	{
		return false;
	}

	// Note: value of bInUseLineTrace is ignored as we always line trace from control points.

	USplineComponent* SplineComp = GetEditedSplineComponent();

	if (SplineComp != nullptr)
	{
		if (SelectedKeys.Num() > 0)
		{
			check(LastKeyIndexSelected != INDEX_NONE);
			check(SelectedKeys.Contains(LastKeyIndexSelected));

			SplineComp->Modify();

			FInterpCurveVector& SplinePosition = SplineComp->GetSplinePointsPosition();
			FInterpCurveQuat& SplineRotation = SplineComp->GetSplinePointsRotation();
			int32 NumPoints = SplinePosition.Points.Num();

			bool bMovedKey = false;

			// Spline control point selection always uses transparent box selection.
			for (int32 KeyIdx : SelectedKeys)
			{
				check(KeyIdx < NumPoints);

				FVector Direction = FVector(0.f, 0.f, -1.f);

				FInterpCurvePoint<FVector>& EditedPoint = SplinePosition.Points[KeyIdx];
				FInterpCurvePoint<FQuat>& EditedRotPoint = SplineRotation.Points[KeyIdx];

				FHitResult Hit(1.0f);
				FCollisionQueryParams Params(SCENE_QUERY_STAT(MoveSplineKeyToTrace), true);

				// Find key position in world space
				const FVector CurrentWorldPos = SplineComp->GetComponentTransform().TransformPosition(EditedPoint.OutVal);

				if (SplineComp->GetWorld()->LineTraceSingleByChannel(Hit, CurrentWorldPos, CurrentWorldPos + Direction * WORLD_MAX, ECC_WorldStatic, Params))
				{
					// Convert back to local space
					EditedPoint.OutVal = SplineComp->GetComponentTransform().InverseTransformPosition(Hit.Location);

					if (bInAlign)
					{		
						// Set point tangent as user controlled
						EditedPoint.InterpMode = CIM_CurveUser;

						// Get delta rotation between up vector and hit normal
						FVector WorldUpVector = SplineComp->GetUpVectorAtSplineInputKey(KeyIdx, ESplineCoordinateSpace::World);
						FQuat DeltaRotate = FQuat::FindBetweenNormals(WorldUpVector, Hit.Normal);

						// Rotate tangent according to delta rotation
						FVector NewTangent = SplineComp->GetComponentTransform().GetRotation().RotateVector(EditedPoint.LeaveTangent); // convert local-space tangent vector to world-space
						NewTangent = DeltaRotate.RotateVector(NewTangent); // apply world-space delta rotation to world-space tangent
						NewTangent = SplineComp->GetComponentTransform().GetRotation().Inverse().RotateVector(NewTangent); // convert world-space tangent vector back into local-space
						EditedPoint.LeaveTangent = NewTangent;
						EditedPoint.ArriveTangent = NewTangent;

						// Rotate spline rotation according to delta rotation
						FQuat NewRot = SplineComp->GetComponentTransform().GetRotation() * EditedRotPoint.OutVal; // convert local-space rotation to world-space
						NewRot = DeltaRotate * NewRot; // apply world-space rotation
						NewRot = SplineComp->GetComponentTransform().GetRotation().Inverse() * NewRot; // convert world-space rotation to local-space
						EditedRotPoint.OutVal = NewRot;
					}

					bMovedKey = true;
				}
			}

			if (bMovedKey)
			{
				SplineComp->UpdateSpline();
				SplineComp->bSplineHasBeenEdited = true;

				NotifyPropertyModified(SplineComp, SplineCurvesProperty);
				
				if (bInAlign)
				{
					CachedRotation = SplineComp->GetQuaternionAtSplinePoint(LastKeyIndexSelected, ESplineCoordinateSpace::World);
				}

				GEditor->RedrawLevelEditingViewports(true);
			}

			return true;
		}
	}

	return false;
}

void FSplineComponentVisualizer::OnSnapToNearestSplinePoint(bool bAlign)
{
	const FScopedTransaction Transaction(LOCTEXT("SnapToNearestSplinePoint", "Snap To Nearest Spline Point"));

	USplineComponent* SplineComp = GetEditedSplineComponent();
	check(SplineComp != nullptr);
	check(LastKeyIndexSelected != INDEX_NONE);
	check(SelectedKeys.Num() == 1);
	check(SelectedKeys.Contains(LastKeyIndexSelected));

	SplineComp->Modify();
	if (AActor* Owner = SplineComp->GetOwner())
	{
		Owner->Modify();
	}

	FInterpCurvePoint<FVector>& EditedPosition = SplineComp->GetSplinePointsPosition().Points[LastKeyIndexSelected];
	FInterpCurvePoint<FQuat>& EditedRotation = SplineComp->GetSplinePointsRotation().Points[LastKeyIndexSelected];
	FInterpCurvePoint<FVector>& EditedScale = SplineComp->GetSplinePointsScale().Points[LastKeyIndexSelected];

	const FVector WorldPos = SplineComp->GetComponentTransform().TransformPosition(EditedPosition.OutVal); // convert local-space position to world-space

	float NearestDistanceSquared = 0.0f;
	USplineComponent* NearestSplineComp = nullptr;
	int32 NearestKeyIndex = INDEX_NONE;

	static const float SnapTol = 5000.0f;
	float SnapTolSquared = SnapTol * SnapTol;

	// Search all spline components for nearest point.
	// Only test points in splines whose bounding box contains this point.
	for (TObjectIterator<USplineComponent> SplineIt; SplineIt; ++SplineIt)
	{
		USplineComponent* TestComponent = *SplineIt;

		// Ignore current spline, those that are being destroyed, those with empty bbox.
		if (TestComponent && TestComponent != SplineComp &&
			!TestComponent->IsBeingDestroyed() && 
			!FMath::IsNearlyZero(TestComponent->Bounds.SphereRadius))
		{
			FBox TestComponentBoundingBox = TestComponent->Bounds.GetBox().ExpandBy(FVector(SnapTol, SnapTol, SnapTol));

			if (TestComponentBoundingBox.IsInsideOrOn(WorldPos))
			{
				const FInterpCurveVector& SplineInfo = TestComponent->GetSplinePointsPosition();
				const int32 NumPoints = SplineInfo.Points.Num();
				for (int32 KeyIdx = 0; KeyIdx < NumPoints; KeyIdx++)
				{
					const FVector TestKeyWorldPos = TestComponent->GetLocationAtSplinePoint(KeyIdx, ESplineCoordinateSpace::World);
					float TestDistanceSquared = FVector::DistSquared(TestKeyWorldPos, WorldPos);

					if (TestDistanceSquared < SnapTolSquared && (NearestKeyIndex == INDEX_NONE || TestDistanceSquared < NearestDistanceSquared))
					{
						NearestDistanceSquared = TestDistanceSquared;
						NearestSplineComp = TestComponent;
						NearestKeyIndex = KeyIdx;
					}
				}
			}
		}
	}

	if (!NearestSplineComp || NearestKeyIndex == INDEX_NONE)
	{
		UE_LOG(LogSplineComponentVisualizer, Warning, TEXT("No nearest spline point found."));
		return;
	}

	const FInterpCurvePoint<FVector>& NearestPosition = NearestSplineComp->GetSplinePointsPosition().Points[NearestKeyIndex];
	const FInterpCurvePoint<FQuat>& NearestRotation = NearestSplineComp->GetSplinePointsRotation().Points[NearestKeyIndex];
	const FInterpCurvePoint<FVector>& NearestScale = NearestSplineComp->GetSplinePointsScale().Points[NearestKeyIndex];

	// Copy position
	const FVector NewWorldPos = NearestSplineComp->GetComponentTransform().TransformPosition(NearestPosition.OutVal); // convert local-space position to world-space
	EditedPosition.OutVal = SplineComp->GetComponentTransform().InverseTransformPosition(NewWorldPos); // convert world-space position to local-space

	if (bAlign)
	{
		// Copy tangents
		EditedPosition.InterpMode = CIM_CurveUser;
		const FVector NewArriveTangent = NearestSplineComp->GetComponentTransform().GetRotation().RotateVector(NearestPosition.ArriveTangent); // convert local-space tangent vectors to world-space
		const FVector NewLeaveTangent = NearestSplineComp->GetComponentTransform().GetRotation().RotateVector(NearestPosition.LeaveTangent); 

		const FVector ArriveTangent = SplineComp->GetComponentTransform().GetRotation().RotateVector(EditedPosition.ArriveTangent); // convert local-space tangent vectors to world-space
		const FVector LeaveTangent = SplineComp->GetComponentTransform().GetRotation().RotateVector(EditedPosition.LeaveTangent); 

		// Swap the tangents if they are not pointing in the same general direction
		float CurrentAngle = FMath::Acos(FVector::DotProduct(ArriveTangent, NewArriveTangent) / (ArriveTangent.Size() * NewArriveTangent.Size()));
		if (CurrentAngle > HALF_PI)
		{
			EditedPosition.ArriveTangent = SplineComp->GetComponentTransform().GetRotation().Inverse().RotateVector(NewLeaveTangent * -1.0f); // convert world-space tangent vectors back into local-space
			EditedPosition.LeaveTangent = SplineComp->GetComponentTransform().GetRotation().Inverse().RotateVector(NewArriveTangent * -1.0f);
		}
		else
		{
			EditedPosition.ArriveTangent = SplineComp->GetComponentTransform().GetRotation().Inverse().RotateVector(NewArriveTangent); // convert world-space tangent vectors back into local-space
			EditedPosition.LeaveTangent = SplineComp->GetComponentTransform().GetRotation().Inverse().RotateVector(NewLeaveTangent);
		}
  
		// Copy rotation
		FQuat NewRot = NearestSplineComp->GetComponentTransform().GetRotation() * NearestRotation.OutVal; // convert local-space rotation to world-space
		EditedRotation.OutVal = SplineComp->GetComponentTransform().GetRotation().Inverse() * NewRot; // convert world-space rotation to local-space
	}
  
	// Copy scale - X is not used so ignore it
	const FVector NearestSplineCompScale = NearestSplineComp->GetComponentTransform().GetScale3D();
	const FVector SplineCompScale = SplineComp->GetComponentTransform().GetScale3D();
	float NewScaleY = NearestSplineCompScale.Y * NearestScale.OutVal.Y; // convert local-space scale to world-space
	float NewScaleZ = NearestSplineCompScale.Z * NearestScale.OutVal.Z;
	EditedScale.OutVal.Y = FMath::IsNearlyZero(SplineCompScale.Y) ? NewScaleY : NewScaleY / SplineCompScale.Y; // convert world-space scale to local-space
	EditedScale.OutVal.Z = FMath::IsNearlyZero(SplineCompScale.Z) ? NewScaleZ : NewScaleZ / SplineCompScale.Z; 

	// Copy metadata
	if (USplineMetadata* SplineMetadata = SplineComp->GetSplinePointsMetadata())
	{ 
		if (const USplineMetadata* NearestSplineMetadata = NearestSplineComp->GetSplinePointsMetadata())
		{ 
			SplineMetadata->CopyPoint(NearestSplineMetadata, NearestKeyIndex, LastKeyIndexSelected);
		}
	}

	SplineComp->UpdateSpline();
	SplineComp->bSplineHasBeenEdited = true;

	NotifyPropertyModified(SplineComp, SplineCurvesProperty);
				
	if (bAlign)
	{
		CachedRotation = SplineComp->GetQuaternionAtSplinePoint(LastKeyIndexSelected, ESplineCoordinateSpace::World);
	}

	GEditor->RedrawLevelEditingViewports(true);
}

bool FSplineComponentVisualizer::CanSnapToNearestSplinePoint() const
{
	USplineComponent* SplineComp = GetEditedSplineComponent();
	return (SplineComp != nullptr &&
		SelectedKeys.Num() == 1 &&
		LastKeyIndexSelected != INDEX_NONE);
}

void FSplineComponentVisualizer::OnSnapAll(EAxis::Type InAxis)
{
	const FScopedTransaction Transaction(LOCTEXT("SnapAllToSelectedAxis", "Snap All To Selected Axis"));
	USplineComponent* SplineComp = GetEditedSplineComponent();
	check(SplineComp != nullptr);
	check(LastKeyIndexSelected != INDEX_NONE);
	check(SelectedKeys.Num() == 1);
	check(SelectedKeys.Contains(LastKeyIndexSelected));
	check(InAxis == EAxis::X || InAxis == EAxis::Y || InAxis == EAxis::Z);

	SplineComp->Modify();
	if (AActor* Owner = SplineComp->GetOwner())
	{
		Owner->Modify();
	}

	FInterpCurveVector& SplinePositions = SplineComp->GetSplinePointsPosition();
	FInterpCurveQuat& SplineRotations = SplineComp->GetSplinePointsRotation();

	const FVector WorldPos = SplineComp->GetComponentTransform().TransformPosition(SplinePositions.Points[LastKeyIndexSelected].OutVal); 

	FVector NewUpVector;
	float WorldSnapAxisValue = 0.0f;
	if (InAxis == EAxis::X)
	{
		WorldSnapAxisValue = WorldPos.X;
		NewUpVector = FVector::ForwardVector;
	}
	else if (InAxis == EAxis::Y)
	{
		WorldSnapAxisValue = WorldPos.Y;
		NewUpVector = FVector::RightVector;
	}
	else
	{
		WorldSnapAxisValue = WorldPos.Z;
		NewUpVector = FVector::UpVector;
	}
		
	int32 NumPoints = SplinePositions.Points.Num();

	for (int32 KeyIdx = 0; KeyIdx < NumPoints; KeyIdx++)
	{
		FInterpCurvePoint<FVector>& EditedPosition = SplinePositions.Points[KeyIdx];
		FInterpCurvePoint<FQuat>& EditedRotation = SplineRotations.Points[KeyIdx];

		// Copy position
		FVector NewWorldPos = SplineComp->GetComponentTransform().TransformPosition(EditedPosition.OutVal); // convert local-space position to world-space
		if (InAxis == EAxis::X)
		{
			NewWorldPos.X = WorldSnapAxisValue;
		}
		else if (InAxis == EAxis::Y)
		{
			NewWorldPos.Y = WorldSnapAxisValue;
		}
		else
		{
			NewWorldPos.Z = WorldSnapAxisValue;
		}

		EditedPosition.OutVal = SplineComp->GetComponentTransform().InverseTransformPosition(NewWorldPos); // convert world-space position to local-space

		// Set point tangent as user controlled
		EditedPosition.InterpMode = CIM_CurveUser;

		// Get delta rotation between current up vector and new up vector
		FVector WorldUpVector = SplineComp->GetUpVectorAtSplineInputKey(KeyIdx, ESplineCoordinateSpace::World);
		FQuat DeltaRotate = FQuat::FindBetweenNormals(WorldUpVector, NewUpVector);

		// Rotate tangent according to delta rotation
		FVector NewTangent = SplineComp->GetComponentTransform().GetRotation().RotateVector(EditedPosition.LeaveTangent); // convert local-space tangent vector to world-space
		NewTangent = DeltaRotate.RotateVector(NewTangent); // apply world-space delta rotation to world-space tangent
		NewTangent = SplineComp->GetComponentTransform().GetRotation().Inverse().RotateVector(NewTangent); // convert world-space tangent vector back into local-space
		EditedPosition.LeaveTangent = NewTangent;
		EditedPosition.ArriveTangent = NewTangent;

		// Rotate spline rotation according to delta rotation
		FQuat NewRot = SplineComp->GetComponentTransform().GetRotation() * EditedRotation.OutVal; // convert local-space rotation to world-space
		NewRot = DeltaRotate * NewRot; // apply world-space rotation
		NewRot = SplineComp->GetComponentTransform().GetRotation().Inverse() * NewRot; // convert world-space rotation to local-space
		EditedRotation.OutVal = NewRot;
	}
  
	SplineComp->UpdateSpline();
	SplineComp->bSplineHasBeenEdited = true;

	NotifyPropertyModified(SplineComp, SplineCurvesProperty);

	CachedRotation = SplineComp->GetQuaternionAtSplinePoint(LastKeyIndexSelected, ESplineCoordinateSpace::World);

	GEditor->RedrawLevelEditingViewports(true);
}

bool FSplineComponentVisualizer::CanSnapAll() const
{
	USplineComponent* SplineComp = GetEditedSplineComponent();
	return (SplineComp != nullptr &&
			SelectedKeys.Num() == 1 &&
			LastKeyIndexSelected != INDEX_NONE);
}


void FSplineComponentVisualizer::EndEditing()
{
	SplineOwningActor = NULL;
	SplineCompPropName.Clear();
	ChangeSelectionState(INDEX_NONE, false);
	SelectedSegmentIndex = INDEX_NONE;
	SelectedTangentHandle = INDEX_NONE;
	SelectedTangentHandleType = ESelectedTangentHandle::None;
}


void FSplineComponentVisualizer::OnDuplicateKey()
{
	const FScopedTransaction Transaction(LOCTEXT("DuplicateSplinePoint", "Duplicate Spline Point"));
	
	USplineComponent* SplineComp = GetEditedSplineComponent();
	DuplicateKey();

	SplineComp->UpdateSpline();
	SplineComp->bSplineHasBeenEdited = true;

	NotifyPropertyModified(SplineComp, SplineCurvesProperty);
}


void FSplineComponentVisualizer::DuplicateKey()
{
	USplineComponent* SplineComp = GetEditedSplineComponent();
	check(SplineComp != nullptr);
	check(LastKeyIndexSelected != INDEX_NONE);
	check(SelectedKeys.Num() > 0);
	check(SelectedKeys.Contains(LastKeyIndexSelected));

	SplineComp->Modify();
	if (AActor* Owner = SplineComp->GetOwner())
	{
		Owner->Modify();
	}

	// Get a sorted list of all the selected indices, highest to lowest
	TArray<int32> SelectedKeysSorted;
	for (int32 SelectedKeyIndex : SelectedKeys)
	{
		SelectedKeysSorted.Add(SelectedKeyIndex);
	}
	SelectedKeysSorted.Sort([](int32 A, int32 B) { return A > B; });

	// Insert duplicates into the list, highest index first, so that the lower indices remain the same
	FInterpCurveVector& SplinePosition = SplineComp->GetSplinePointsPosition();
	FInterpCurveQuat& SplineRotation = SplineComp->GetSplinePointsRotation();
	FInterpCurveVector& SplineScale = SplineComp->GetSplinePointsScale();
	USplineMetadata* SplineMetadata = SplineComp->GetSplinePointsMetadata();

	for (int32 SelectedKeyIndex : SelectedKeysSorted)
	{
		// Insert duplicates into arrays.
		// It's necessary to take a copy because copying existing array items by reference isn't allowed (the array may reallocate)
		SplinePosition.Points.Insert(FInterpCurvePoint<FVector>(SplinePosition.Points[SelectedKeyIndex]), SelectedKeyIndex);
		SplineRotation.Points.Insert(FInterpCurvePoint<FQuat>(SplineRotation.Points[SelectedKeyIndex]), SelectedKeyIndex);
		SplineScale.Points.Insert(FInterpCurvePoint<FVector>(SplineScale.Points[SelectedKeyIndex]), SelectedKeyIndex);

		if (SplineMetadata)
		{
			SplineMetadata->DuplicatePoint(SelectedKeyIndex);
		}

		// Adjust input keys of subsequent points
		for (int Index = SelectedKeyIndex + 1; Index < SplinePosition.Points.Num(); Index++)
		{
			SplinePosition.Points[Index].InVal += 1.0f;
			SplineRotation.Points[Index].InVal += 1.0f;
			SplineScale.Points[Index].InVal += 1.0f;
		}
	}

	// Repopulate the selected keys
	SelectedKeys.Empty();
	int32 Offset = SelectedKeysSorted.Num();
	for (int32 SelectedKeyIndex : SelectedKeysSorted)
	{
		SelectedKeys.Add(SelectedKeyIndex + Offset);

		if (LastKeyIndexSelected == SelectedKeyIndex)
		{
			LastKeyIndexSelected += Offset;
		}

		Offset--;
	}

	// Unset tangent handle selection
	SelectedTangentHandle = INDEX_NONE;
	SelectedTangentHandleType = ESelectedTangentHandle::None;

	GEditor->RedrawLevelEditingViewports(true);
}

bool FSplineComponentVisualizer::CanAddKeyToSegment() const
{
	USplineComponent* SplineComp = GetEditedSplineComponent();
	if (SplineComp == nullptr)
	{
		return false;
	}

	const int32 NumPoints = SplineComp->SplineCurves.Position.Points.Num();
	const int32 NumSegments = SplineComp->IsClosedLoop() ? NumPoints : NumPoints - 1;

	return (SelectedSegmentIndex != INDEX_NONE && SelectedSegmentIndex < NumSegments);
}

void FSplineComponentVisualizer::OnAddKeyToSegment()
{
	const FScopedTransaction Transaction(LOCTEXT("AddSplinePoint", "Add Spline Point"));
	USplineComponent* SplineComp = GetEditedSplineComponent();
	check(SplineComp != nullptr);
	check(LastKeyIndexSelected != INDEX_NONE);
	check(SelectedKeys.Num() > 0);
	check(SelectedKeys.Contains(LastKeyIndexSelected));
	check(SelectedTangentHandle == INDEX_NONE);
	check(SelectedTangentHandleType == ESelectedTangentHandle::None);

	SplineComp->Modify();
	if (AActor* Owner = SplineComp->GetOwner())
	{
		Owner->Modify();
	}

	FInterpCurveVector& SplinePosition = SplineComp->GetSplinePointsPosition();
	FInterpCurveQuat& SplineRotation = SplineComp->GetSplinePointsRotation();
	FInterpCurveVector& SplineScale = SplineComp->GetSplinePointsScale();
	USplineMetadata* SplineMetadata = SplineComp->GetSplinePointsMetadata();
	
	FInterpCurvePoint<FVector> NewPoint(
		SelectedSegmentIndex,
		SplineComp->GetComponentTransform().InverseTransformPosition(SelectedSplinePosition),
		FVector::ZeroVector,
		FVector::ZeroVector,
		CIM_CurveAuto);

	FInterpCurvePoint<FQuat> NewRotPoint(
		SelectedSegmentIndex,
		FQuat::Identity,
		FQuat::Identity,
		FQuat::Identity,
		CIM_CurveAuto);

	FInterpCurvePoint<FVector> NewScalePoint(
		SelectedSegmentIndex,
		FVector(1.0f),
		FVector::ZeroVector,
		FVector::ZeroVector,
		CIM_CurveAuto);

	SplinePosition.Points.Insert(NewPoint, SelectedSegmentIndex + 1);
	SplineRotation.Points.Insert(NewRotPoint, SelectedSegmentIndex + 1);
	SplineScale.Points.Insert(NewScalePoint, SelectedSegmentIndex + 1);
	if (SplineMetadata)
	{
		SplineMetadata->InsertPoint(SelectedSegmentIndex, SelectedSegmentIndex + 1);
	}

	// Adjust input keys of subsequent points
	for (int Index = SelectedSegmentIndex + 1; Index < SplinePosition.Points.Num(); Index++)
	{
		SplinePosition.Points[Index].InVal += 1.0f;
		SplineRotation.Points[Index].InVal += 1.0f;
		SplineScale.Points[Index].InVal += 1.0f;
	}

	// Set selection to 'next' key
	ChangeSelectionState(SelectedSegmentIndex + 1, false);
	SelectedSegmentIndex = INDEX_NONE;

	SplineComp->UpdateSpline();
	SplineComp->bSplineHasBeenEdited = true;

	NotifyPropertyModified(SplineComp, SplineCurvesProperty);

	CachedRotation = SplineComp->GetQuaternionAtSplinePoint(LastKeyIndexSelected, ESplineCoordinateSpace::World);

	GEditor->RedrawLevelEditingViewports(true);
}


void FSplineComponentVisualizer::OnDeleteKey()
{
	const FScopedTransaction Transaction(LOCTEXT("DeleteSplinePoint", "Delete Spline Point"));
	USplineComponent* SplineComp = GetEditedSplineComponent();
	check(SplineComp != nullptr);
	check(LastKeyIndexSelected != INDEX_NONE);
	check(SelectedKeys.Num() > 0);
	check(SelectedKeys.Contains(LastKeyIndexSelected));

	SplineComp->Modify();
	if (AActor* Owner = SplineComp->GetOwner())
	{
		Owner->Modify();
	}

	// Get a sorted list of all the selected indices, highest to lowest
	TArray<int32> SelectedKeysSorted;
	for (int32 SelectedKeyIndex : SelectedKeys)
	{
		SelectedKeysSorted.Add(SelectedKeyIndex);
	}
	SelectedKeysSorted.Sort([](int32 A, int32 B) { return A > B; });

	// Delete selected keys from list, highest index first
	FInterpCurveVector& SplinePosition = SplineComp->GetSplinePointsPosition();
	FInterpCurveQuat& SplineRotation = SplineComp->GetSplinePointsRotation();
	FInterpCurveVector& SplineScale = SplineComp->GetSplinePointsScale();
	USplineMetadata* SplineMetadata = SplineComp->GetSplinePointsMetadata();
		
	for (int32 SelectedKeyIndex : SelectedKeysSorted)
	{
		if (SplineMetadata)
		{
			SplineMetadata->RemovePoint(SelectedKeyIndex);
		}
		
		SplinePosition.Points.RemoveAt(SelectedKeyIndex);
		SplineRotation.Points.RemoveAt(SelectedKeyIndex);
		SplineScale.Points.RemoveAt(SelectedKeyIndex);

		for (int Index = SelectedKeyIndex; Index < SplinePosition.Points.Num(); Index++)
		{
			SplinePosition.Points[Index].InVal -= 1.0f;
			SplineRotation.Points[Index].InVal -= 1.0f;
			SplineScale.Points[Index].InVal -= 1.0f;
		}
	}

	// Select first key
	ChangeSelectionState(0, false);
	SelectedSegmentIndex = INDEX_NONE;
	SelectedTangentHandle = INDEX_NONE;
	SelectedTangentHandleType = ESelectedTangentHandle::None;

	SplineComp->UpdateSpline();
	SplineComp->bSplineHasBeenEdited = true;

	NotifyPropertyModified(SplineComp, SplineCurvesProperty);

	CachedRotation = SplineComp->GetQuaternionAtSplinePoint(LastKeyIndexSelected, ESplineCoordinateSpace::World);

	GEditor->RedrawLevelEditingViewports(true);
}


bool FSplineComponentVisualizer::CanDeleteKey() const
{
	USplineComponent* SplineComp = GetEditedSplineComponent();
	return (SplineComp != nullptr &&
			SelectedKeys.Num() > 0 &&
			SelectedKeys.Num() != SplineComp->SplineCurves.Position.Points.Num() &&
			LastKeyIndexSelected != INDEX_NONE);
}


bool FSplineComponentVisualizer::IsKeySelectionValid() const
{
	USplineComponent* SplineComp = GetEditedSplineComponent();
	return (SplineComp != nullptr &&
			SelectedKeys.Num() > 0 &&
			LastKeyIndexSelected != INDEX_NONE);
}

void FSplineComponentVisualizer::OnLockAxis(EAxis::Type InAxis)
{
	AddKeyLockedAxis = InAxis;
}

bool FSplineComponentVisualizer::IsLockAxisSet(EAxis::Type Index) const
{
	return (Index == AddKeyLockedAxis);
}

void FSplineComponentVisualizer::OnResetToAutomaticTangent(EInterpCurveMode Mode)
{
	USplineComponent* SplineComp = GetEditedSplineComponent();
	if (SplineComp != nullptr)
	{
		const FScopedTransaction Transaction(LOCTEXT("ResetToAutomaticTangent", "Reset to Automatic Tangent"));

		SplineComp->Modify();
		if (AActor* Owner = SplineComp->GetOwner())
		{
			Owner->Modify();
		}

		for (int32 SelectedKeyIndex : SelectedKeys)
		{
			auto& Point = SplineComp->SplineCurves.Position.Points[SelectedKeyIndex];
			if (Point.IsCurveKey())
			{
				Point.InterpMode = Mode;
			}
		}

		SplineComp->UpdateSpline();
		SplineComp->bSplineHasBeenEdited = true;

		NotifyPropertyModified(SplineComp, SplineCurvesProperty);

		CachedRotation = SplineComp->GetQuaternionAtSplinePoint(LastKeyIndexSelected, ESplineCoordinateSpace::World);
	}
}


bool FSplineComponentVisualizer::CanResetToAutomaticTangent(EInterpCurveMode Mode) const
{
	USplineComponent* SplineComp = GetEditedSplineComponent();
	if (SplineComp != nullptr && LastKeyIndexSelected != INDEX_NONE)
	{
		for (int32 SelectedKeyIndex : SelectedKeys)
		{
			const auto& Point = SplineComp->SplineCurves.Position.Points[SelectedKeyIndex];
			if (Point.IsCurveKey() && Point.InterpMode != Mode)
			{
				return true;
			}
		}
	}

	return false;
}


void FSplineComponentVisualizer::OnSetKeyType(EInterpCurveMode Mode)
{
	USplineComponent* SplineComp = GetEditedSplineComponent();
	if (SplineComp != nullptr)
	{
		const FScopedTransaction Transaction(LOCTEXT("SetSplinePointType", "Set Spline Point Type"));

		SplineComp->Modify();
		if (AActor* Owner = SplineComp->GetOwner())
		{
			Owner->Modify();
		}

		for (int32 SelectedKeyIndex : SelectedKeys)
		{
			SplineComp->SplineCurves.Position.Points[SelectedKeyIndex].InterpMode = Mode;
		}

		SplineComp->UpdateSpline();
		SplineComp->bSplineHasBeenEdited = true;

		NotifyPropertyModified(SplineComp, SplineCurvesProperty);

		CachedRotation = SplineComp->GetQuaternionAtSplinePoint(LastKeyIndexSelected, ESplineCoordinateSpace::World);
	}
}


bool FSplineComponentVisualizer::IsKeyTypeSet(EInterpCurveMode Mode) const
{
	if (IsKeySelectionValid())
	{
		USplineComponent* SplineComp = GetEditedSplineComponent();
		check(SplineComp != nullptr);

		for (int32 SelectedKeyIndex : SelectedKeys)
		{
			const auto& SelectedPoint = SplineComp->SplineCurves.Position.Points[SelectedKeyIndex];
			if ((Mode == CIM_CurveAuto && SelectedPoint.IsCurveKey()) || SelectedPoint.InterpMode == Mode)
			{
				return true;
			}
		}
	}

	return false;
}


void FSplineComponentVisualizer::OnSetVisualizeRollAndScale()
{
	USplineComponent* SplineComp = GetEditedSplineComponent();
	check(SplineComp != nullptr);

	SplineComp->Modify();
	if (AActor* Owner = SplineComp->GetOwner())
	{
		Owner->Modify();
	}

	SplineComp->bShouldVisualizeScale = !SplineComp->bShouldVisualizeScale;

	NotifyPropertyModified(SplineComp, FindField<UProperty>(USplineComponent::StaticClass(), GET_MEMBER_NAME_CHECKED(USplineComponent, bShouldVisualizeScale)));

	GEditor->RedrawLevelEditingViewports(true);
}


bool FSplineComponentVisualizer::IsVisualizingRollAndScale() const
{
	USplineComponent* SplineComp = GetEditedSplineComponent();
	
	return SplineComp ? SplineComp->bShouldVisualizeScale : false;
}


void FSplineComponentVisualizer::OnSetDiscontinuousSpline()
{
	USplineComponent* SplineComp = GetEditedSplineComponent();
	check(SplineComp != nullptr);

	SplineComp->Modify();
	if (AActor* Owner = SplineComp->GetOwner())
	{
		Owner->Modify();
	}

	SplineComp->bAllowDiscontinuousSpline = !SplineComp->bAllowDiscontinuousSpline;

	// If not allowed discontinuous splines, set all ArriveTangents to match LeaveTangents
	if (!SplineComp->bAllowDiscontinuousSpline)
	{
		for (int Index = 0; Index < SplineComp->SplineCurves.Position.Points.Num(); Index++)
		{
			SplineComp->SplineCurves.Position.Points[Index].ArriveTangent = SplineComp->SplineCurves.Position.Points[Index].LeaveTangent;
		}
	}

	TArray<UProperty*> Properties;
	Properties.Add(SplineCurvesProperty);
	Properties.Add(FindField<UProperty>(USplineComponent::StaticClass(), GET_MEMBER_NAME_CHECKED(USplineComponent, bAllowDiscontinuousSpline)));
	NotifyPropertiesModified(SplineComp, Properties);

	GEditor->RedrawLevelEditingViewports(true);
}


bool FSplineComponentVisualizer::IsDiscontinuousSpline() const
{
	USplineComponent* SplineComp = GetEditedSplineComponent();

	return SplineComp ? SplineComp->bAllowDiscontinuousSpline : false;
}


void FSplineComponentVisualizer::OnResetToDefault()
{
	USplineComponent* SplineComp = GetEditedSplineComponent();
	check(SplineComp != nullptr);

	const FScopedTransaction Transaction(LOCTEXT("ResetToDefault", "Reset to Default"));

	SplineComp->Modify();
	if (SplineOwningActor.IsValid())
	{
		SplineOwningActor.Get()->Modify();
	}

	SplineComp->bSplineHasBeenEdited = false;

	// Select first key
	ChangeSelectionState(0, false);
	SelectedSegmentIndex = INDEX_NONE;
	SelectedTangentHandle = INDEX_NONE;
	SelectedTangentHandleType = ESelectedTangentHandle::None;

	if (SplineOwningActor.IsValid())
	{
		SplineOwningActor.Get()->PostEditMove(false);
	}

	GEditor->RedrawLevelEditingViewports(true);
}


bool FSplineComponentVisualizer::CanResetToDefault() const
{
	USplineComponent* SplineComp = GetEditedSplineComponent();
	if(SplineComp != nullptr)
    {
        return SplineComp->SplineCurves != CastChecked<USplineComponent>(SplineComp->GetArchetype())->SplineCurves;
    }
    else
    {
        return false;
    }
}

void FSplineComponentVisualizer::OnSelectAllSplinePoints()
{
	USplineComponent* SplineComp = GetEditedSplineComponent();
	if (SplineComp != nullptr)
	{
		const FScopedTransaction Transaction(LOCTEXT("SelectAllSplinePoints", "Select All Spline Points"));

		bool bSelectionChanged = false;

		const FInterpCurveVector& SplineInfo = SplineComp->GetSplinePointsPosition();
		int32 NumPoints = SplineInfo.Points.Num();

		SelectedKeys.Empty();

		// Spline control point selection always uses transparent box selection.
		for (int32 KeyIdx = 0; KeyIdx < NumPoints; KeyIdx++)
		{
			SelectedKeys.Add(KeyIdx);
		}

		LastKeyIndexSelected = NumPoints - 1;
		SelectedSegmentIndex = INDEX_NONE;
		SelectedTangentHandle = INDEX_NONE;
		SelectedTangentHandleType = ESelectedTangentHandle::None;
	}
}

bool FSplineComponentVisualizer::CanSelectAllSplinePoints() const
{
	USplineComponent* SplineComp = GetEditedSplineComponent();
	return (SplineComp != nullptr);
}

TSharedPtr<SWidget> FSplineComponentVisualizer::GenerateContextMenu() const
{
	FMenuBuilder MenuBuilder(true, SplineComponentVisualizerActions);
	{
		MenuBuilder.BeginSection("SplinePointEdit", LOCTEXT("SplinePoint", "Spline Point"));
		{
			if (SelectedSegmentIndex != INDEX_NONE)
			{
				MenuBuilder.AddMenuEntry(FSplineComponentVisualizerCommands::Get().AddKey);
			}
			else if (LastKeyIndexSelected != INDEX_NONE)
			{
				MenuBuilder.AddMenuEntry(FSplineComponentVisualizerCommands::Get().DeleteKey);
				MenuBuilder.AddMenuEntry(FSplineComponentVisualizerCommands::Get().DuplicateKey);
				MenuBuilder.AddMenuEntry(FSplineComponentVisualizerCommands::Get().SelectAll);

				MenuBuilder.AddSubMenu(
					LOCTEXT("SplinePointType", "Spline Point Type"),
					LOCTEXT("KeyTypeTooltip", "Define the type of the spline point."),
					FNewMenuDelegate::CreateSP(this, &FSplineComponentVisualizer::GenerateSplinePointTypeSubMenu));

				// Only add the Automatic Tangents submenu if any of the keys is a curve type
				USplineComponent* SplineComp = GetEditedSplineComponent();
				if (SplineComp != nullptr)
				{
					for (int32 SelectedKeyIndex : SelectedKeys)
					{
						const auto& Point = SplineComp->SplineCurves.Position.Points[SelectedKeyIndex];
						if (Point.IsCurveKey())
						{
							MenuBuilder.AddSubMenu(
								LOCTEXT("ResetToAutomaticTangent", "Reset to Automatic Tangent"),
								LOCTEXT("ResetToAutomaticTangentTooltip", "Reset the spline point tangent to an automatically generated value."),
								FNewMenuDelegate::CreateSP(this, &FSplineComponentVisualizer::GenerateTangentTypeSubMenu));
							break;
						}
					}
				}
			}
		}
		MenuBuilder.EndSection();

		MenuBuilder.BeginSection("Transform");
		{
			MenuBuilder.AddMenuEntry(FEditorViewportCommands::Get().FocusViewportToSelection);

			MenuBuilder.AddSubMenu(
				LOCTEXT("SnapAlign", "Snap/Align"),
				LOCTEXT("KeyTypeTooltip", "Snap align options."),
				FNewMenuDelegate::CreateSP(this, &FSplineComponentVisualizer::GenerateSnapAlignSubMenu));

			/* temporarily disabled
			MenuBuilder.AddSubMenu(
				LOCTEXT("LockAxis", "Lock Axis"),
				LOCTEXT("KeyTypeTooltip", "Axis to lock when adding new spline points."),
				FNewMenuDelegate::CreateSP(this, &FSplineComponentVisualizer::GenerateLockAxisSubMenu));
				*/
		}
		MenuBuilder.EndSection();

		MenuBuilder.BeginSection("Spline", LOCTEXT("Spline", "Spline"));
		{
			MenuBuilder.AddMenuEntry(FSplineComponentVisualizerCommands::Get().ResetToDefault);
		}
		MenuBuilder.EndSection();

		MenuBuilder.BeginSection("Visualization", LOCTEXT("Visualization", "Visualization"));
		{
			MenuBuilder.AddMenuEntry(FSplineComponentVisualizerCommands::Get().VisualizeRollAndScale);
			MenuBuilder.AddMenuEntry(FSplineComponentVisualizerCommands::Get().DiscontinuousSpline);
		}
		MenuBuilder.EndSection();
	}

	TSharedPtr<SWidget> MenuWidget = MenuBuilder.MakeWidget();
	return MenuWidget;
}

void FSplineComponentVisualizer::GenerateSplinePointTypeSubMenu(FMenuBuilder& MenuBuilder) const
{
	MenuBuilder.AddMenuEntry(FSplineComponentVisualizerCommands::Get().SetKeyToCurve);
	MenuBuilder.AddMenuEntry(FSplineComponentVisualizerCommands::Get().SetKeyToLinear);
	MenuBuilder.AddMenuEntry(FSplineComponentVisualizerCommands::Get().SetKeyToConstant);
}

void FSplineComponentVisualizer::GenerateTangentTypeSubMenu(FMenuBuilder& MenuBuilder) const
{
	MenuBuilder.AddMenuEntry(FSplineComponentVisualizerCommands::Get().ResetToUnclampedTangent);
	MenuBuilder.AddMenuEntry(FSplineComponentVisualizerCommands::Get().ResetToClampedTangent);
}

void FSplineComponentVisualizer::GenerateSnapAlignSubMenu(FMenuBuilder& MenuBuilder) const
{
	MenuBuilder.AddMenuEntry(FLevelEditorCommands::Get().SnapToFloor);
	MenuBuilder.AddMenuEntry(FLevelEditorCommands::Get().AlignToFloor);
	MenuBuilder.AddMenuEntry(FSplineComponentVisualizerCommands::Get().SnapToNearestSplinePoint);
	MenuBuilder.AddMenuEntry(FSplineComponentVisualizerCommands::Get().AlignToNearestSplinePoint);
	MenuBuilder.AddMenuEntry(FSplineComponentVisualizerCommands::Get().SnapAllToSelectedX);
	MenuBuilder.AddMenuEntry(FSplineComponentVisualizerCommands::Get().SnapAllToSelectedY);
	MenuBuilder.AddMenuEntry(FSplineComponentVisualizerCommands::Get().SnapAllToSelectedZ);
}

void FSplineComponentVisualizer::GenerateLockAxisSubMenu(FMenuBuilder& MenuBuilder) const
{
	MenuBuilder.AddMenuEntry(FSplineComponentVisualizerCommands::Get().SetLockedAxisNone);
	MenuBuilder.AddMenuEntry(FSplineComponentVisualizerCommands::Get().SetLockedAxisX);
	MenuBuilder.AddMenuEntry(FSplineComponentVisualizerCommands::Get().SetLockedAxisY);
	MenuBuilder.AddMenuEntry(FSplineComponentVisualizerCommands::Get().SetLockedAxisZ);
}



#undef LOCTEXT_NAMESPACE
