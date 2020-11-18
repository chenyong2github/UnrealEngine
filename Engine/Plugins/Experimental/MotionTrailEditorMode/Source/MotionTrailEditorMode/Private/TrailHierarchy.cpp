// Copyright Epic Games, Inc. All Rights Reserved.

#include "TrailHierarchy.h"
#include "MotionTrailEditorMode.h"
#include "TrajectoryDrawInfo.h"
#include "Containers/Queue.h"
#include "CanvasItem.h"
#include "CanvasTypes.h"

namespace UE
{
namespace MotionTrailEditor
{

void FTrailHierarchyRenderer::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
{
	const FDateTime RenderStartTime = FDateTime::Now();
	
	const int32 NumEvalTimes = int32(OwningHierarchy->GetViewRange().Size<double>() / OwningHierarchy->GetSecondsPerSegment());
	const int32 NumLinesReserveSize = int32(NumEvalTimes * OwningHierarchy->GetAllTrails().Num() * 1.3);
	PDI->AddReserveLines(SDPG_Foreground, NumLinesReserveSize);

	// <parent, current>
	for (const TPair<FGuid, TUniquePtr<FTrail>>& GuidTrailPair : OwningHierarchy->GetAllTrails())
	{
		FTrajectoryDrawInfo* CurDrawInfo = GuidTrailPair.Value->GetDrawInfo();
		if (CurDrawInfo && OwningHierarchy->GetVisibilityManager().IsTrailVisible(GuidTrailPair.Key))
		{
			FTrajectoryDrawInfo::FDisplayContext DisplayContext = {
				GuidTrailPair.Key,
				FTrailScreenSpaceTransform(View, Viewport),
				OwningHierarchy->GetEditorMode()->GetTrailOptions()->SecondsPerTick,
				OwningHierarchy->GetViewRange(),
				OwningHierarchy
			};

			TArray<FVector> PointsToDraw = CurDrawInfo->GetTrajectoryPointsForDisplay(DisplayContext);

			if (PointsToDraw.Num() > 1)
			{
				FVector LastPoint = PointsToDraw[0];
				for (int32 Idx = 1; Idx < PointsToDraw.Num(); Idx++)
				{
					const FVector CurPoint = PointsToDraw[Idx];
					PDI->DrawLine(LastPoint, CurPoint, CurDrawInfo->GetColor(), SDPG_Foreground, OwningHierarchy->GetEditorMode()->GetTrailOptions()->TrailThickness);
					LastPoint = CurPoint;
				}
			}
		}
	}

	const FTimespan RenderTimespan = FDateTime::Now() - RenderStartTime;
	OwningHierarchy->GetTimingStats().Add("FTrailHierarchyRenderer::Render", RenderTimespan);
}

void FTrailHierarchyRenderer::DrawHUD(FEditorViewportClient* ViewportClient, FViewport* Viewport, const FSceneView* View, FCanvas* Canvas)
{
	// TODO: cache tick locations and tangents in 3D space
	// TODO: Fix tick spacing issue where in some cases ticks are skipped or shown too far apart
	const FDateTime DrawHUDStartTime = FDateTime::Now();

	const double SecondsPerTick = OwningHierarchy->GetEditorMode()->GetTrailOptions()->bLockTicksToFrames ? OwningHierarchy->GetSecondsPerFrame() : OwningHierarchy->GetEditorMode()->GetTrailOptions()->SecondsPerTick;
	const int32 PredictedNumTicks = int32((OwningHierarchy->GetViewRange().Size<double>() / SecondsPerTick) * OwningHierarchy->GetAllTrails().Num() * 1.3); // Multiply by 1.3 to be safe
	Canvas->GetBatchedElements(FCanvas::EElementType::ET_Line)->AddReserveLines(PredictedNumTicks);

	for(const TPair<FGuid, TUniquePtr<FTrail>>& GuidTrailPair : OwningHierarchy->GetAllTrails())
	{
		FTrajectoryDrawInfo* CurDrawInfo = GuidTrailPair.Value->GetDrawInfo();
		if (CurDrawInfo && OwningHierarchy->GetVisibilityManager().IsTrailVisible(GuidTrailPair.Key))
		{
			FTrajectoryDrawInfo::FDisplayContext DisplayContext = {
				GuidTrailPair.Key,
				FTrailScreenSpaceTransform(View, Viewport, ViewportClient->GetDPIScale()),
				SecondsPerTick,
				OwningHierarchy->GetViewRange(),
				OwningHierarchy
			};

			TArray<FVector2D> Ticks, TickNormals;
			CurDrawInfo->GetTickPointsForDisplay(DisplayContext, Ticks, TickNormals);

			for (int32 Idx = 0; Idx < Ticks.Num(); Idx++)
			{
				const FVector2D StartPoint = Ticks[Idx] - TickNormals[Idx] * OwningHierarchy->GetEditorMode()->GetTrailOptions()->TickSize;
				const FVector2D EndPoint = Ticks[Idx] + TickNormals[Idx] * OwningHierarchy->GetEditorMode()->GetTrailOptions()->TickSize;
				FCanvasLineItem LineItem = FCanvasLineItem(StartPoint, EndPoint);
				LineItem.SetColor(CurDrawInfo->GetColor());
				Canvas->DrawItem(LineItem);
			}
		}
	}

	const FTimespan DrawHUDTimespan = FDateTime::Now() - DrawHUDStartTime;
	OwningHierarchy->GetTimingStats().Add("FTrailHierarchyRenderer::DrawHUD", DrawHUDTimespan);
}

void FTrailHierarchy::Update()
{
	const FDateTime UpdateStartTime = FDateTime::Now();

	// Build up minimal hierarchy to update
	TMap<FGuid, FTrailHierarchyNode> HierarchyToUpdate;
	HierarchyToUpdate.Add(RootTrailGuid);
	for (const TPair<FGuid, TUniquePtr<FTrail>>& GuidTrailPair : AllTrails)
	{
		if (!VisibilityManager.IsTrailVisible(GuidTrailPair.Key))
		{
			continue;
		}

		FGuid CurGuid = GuidTrailPair.Key;
		checkf(Hierarchy.FindChecked(CurGuid).Parents.Num() == 1, TEXT("Only single parents currently supported"));
		FGuid ParentGuid = Hierarchy.FindChecked(CurGuid).Parents[0];
		while (!HierarchyToUpdate.Contains(ParentGuid))
		{
			FTrailHierarchyNode& CurNode = HierarchyToUpdate.FindOrAdd(CurGuid);
			FTrailHierarchyNode& ParentNode = HierarchyToUpdate.FindOrAdd(ParentGuid);

			CurNode.Parents.Add(ParentGuid);
			ParentNode.Children.Add(CurGuid);

			CurGuid = ParentGuid;
			checkf(Hierarchy.FindChecked(CurGuid).Parents.Num() == 1, TEXT("Only single parents currently supported"));
			ParentGuid = Hierarchy.FindChecked(CurGuid).Parents[0];
		}

		{
			FTrailHierarchyNode& CurNode = HierarchyToUpdate.FindOrAdd(CurGuid);
			FTrailHierarchyNode& ParentNode = HierarchyToUpdate.FindOrAdd(ParentGuid);

			if (!CurNode.Parents.Contains(ParentGuid))
			{
				CurNode.Parents.Add(ParentGuid);
			}
			if (!ParentNode.Children.Contains(CurGuid))
			{
				ParentNode.Children.Add(CurGuid);
			}
		}
	}
	
	// Generate times to evaluate
	const double Spacing = GetSecondsPerSegment();
	TArray<double> EvalTimesArr;
	EvalTimesArr.Reserve(int32(ViewRange.Size<double>() / Spacing) + 1);
	for (double SecondsItr = ViewRange.GetLowerBoundValue(); SecondsItr < ViewRange.GetUpperBoundValue() + Spacing; SecondsItr += Spacing)
	{
		EvalTimesArr.Add(SecondsItr);
	}

	FTrailEvaluateTimes EvalTimes = FTrailEvaluateTimes(EvalTimesArr, Spacing);

	if (LastSecondsPerSegment != Spacing)
	{
		AllTrails.FindChecked(RootTrailGuid)->ForceEvaluateNextTick();
		LastSecondsPerSegment = Spacing;
	}

	VisibilityManager.InactiveMask.Reset();
	TArray<FGuid> DeadTrails;

	// Run BFS on the hierarchy to update every trail
	// <parent, current>
	TQueue<FGuid> BFSQueue;
	BFSQueue.Enqueue(RootTrailGuid);
	while (!BFSQueue.IsEmpty())
	{
		FGuid CurGuid;
		BFSQueue.Dequeue(CurGuid);

		// If tracked parent states different from actual parents, then update tracked parent states with the new parents
		TArray<FGuid> AccumulatedParentGuids;
		AccumulatedParentStates.GetParentStates(CurGuid).GetKeys(AccumulatedParentGuids);
		if (AccumulatedParentGuids != Hierarchy[CurGuid].Parents)
		{
			AccumulatedParentStates.OnParentsChanged(CurGuid, Hierarchy);
		}

		FTrail::FSceneContext SceneContext = {
			CurGuid,
			EvalTimes,
			this,
			AccumulatedParentStates.GetParentStates(CurGuid)
		};

		if (!AllTrails.Contains(CurGuid))
		{
			continue;
		}

		// Update the trail
		ETrailCacheState CurCacheState = AllTrails[CurGuid]->UpdateTrail(SceneContext);
		AccumulatedParentStates.ResetParentStates(CurGuid);
		if (CurCacheState == ETrailCacheState::Dead)
		{
			DeadTrails.Add(CurGuid);
		}

		// Add children if trail cache has been updated, otherwise mask out all the children
		if (CurCacheState != ETrailCacheState::NotUpdated)
		{
			for (const FGuid& ChildGuid : Hierarchy[CurGuid].Children)
			{
				AccumulatedParentStates.AccumulateParentState(ChildGuid, CurGuid, CurCacheState);
			}

			for (const FGuid& ChildGuid : HierarchyToUpdate[CurGuid].Children)
			{
				BFSQueue.Enqueue(ChildGuid);
			}
		}
		else
		{
			TArray<FGuid> Children = GetAllChildren(CurGuid);
			VisibilityManager.InactiveMask.Add(CurGuid);
			VisibilityManager.InactiveMask.Append(Children);
		}
	}

	for (const FGuid& TrailGuid : DeadTrails)
	{
		RemoveTrail(TrailGuid);
	}

	const FTimespan UpdateTimespan = FDateTime::Now() - UpdateStartTime;
	TimingStats.Add("FTrailHierarchy::Update", UpdateTimespan);
}

void FTrailHierarchy::AddTrail(const FGuid& Key, const FTrailHierarchyNode& Node, TUniquePtr<FTrail>&& TrailPtr)
{
	TMap<FString, FInteractiveTrailTool*> ToolsForTrail = TrailPtr->GetTools();
	for (TPair<FString, FInteractiveTrailTool*>& NameToolPair : ToolsForTrail)
	{
		WeakEditorMode->AddTrailTool(NameToolPair.Key, NameToolPair.Value);
	}

	AllTrails.Add(Key, MoveTemp(TrailPtr));
	Hierarchy.Add(Key, Node);
}

void FTrailHierarchy::RemoveTrail(const FGuid& Key)
{
	if (TUniquePtr<FTrail>* Trail = AllTrails.Find(Key))
	{
		TMap<FString, FInteractiveTrailTool*> ToolsForTrail = (*Trail)->GetTools();
		for (TPair<FString, FInteractiveTrailTool*>& NameToolPair : ToolsForTrail)
		{
			WeakEditorMode->RemoveTrailTool(NameToolPair.Key, NameToolPair.Value);
		}
	}

	FTrailHierarchyNode TrailNode = Hierarchy.FindRef(Key);
	for (const FGuid& ParentGuid : TrailNode.Parents)
	{
		Hierarchy[ParentGuid].Children.Remove(Key);
	}

	for (const FGuid& ChildGuid : TrailNode.Children)
	{
		Hierarchy[ChildGuid].Parents.Remove(Key);
	}

	AllTrails.Remove(Key);
	Hierarchy.Remove(Key);
}

TArray<FGuid> FTrailHierarchy::GetAllChildren(const FGuid& TrailGuid)
{
	TArray<FGuid> Children = Hierarchy.FindChecked(TrailGuid).Children;
	for (int32 Index = 0; Index < Children.Num(); ++Index)
	{
		FGuid Child = Children[Index];
		Children.Append(Hierarchy.FindChecked(Child).Children);
	}

	return Children;
}

FTrailHierarchy::FAccumulatedParentStates::FAccumulatedParentStates(const TMap<FGuid, FTrailHierarchyNode>& InHierarchy)
{
	for (const TPair<FGuid, FTrailHierarchyNode>& GuidNodePair : InHierarchy)
	{
		for (const FGuid ParentGuid : GuidNodePair.Value.Parents)
		{
			ParentStates.FindOrAdd(GuidNodePair.Key).Add(ParentGuid, ETrailCacheState::UpToDate);
		}
	}
}

void FTrailHierarchy::FAccumulatedParentStates::OnParentsChanged(const FGuid& Guid, const TMap<FGuid, FTrailHierarchyNode>& InHierarchy)
{
	TMap<FGuid, ETrailCacheState> OldParentState = ParentStates.FindOrAdd(Guid);
	ParentStates[Guid].Reset();
	for (const FGuid& Parent : InHierarchy[Guid].Parents)
	{
		ParentStates[Guid].Add(Parent, OldParentState.FindOrAdd(Parent, ETrailCacheState::Stale));
	}
}

void FTrailHierarchy::FAccumulatedParentStates::AccumulateParentState(const FGuid& Guid, const FGuid& ParentGuid, ETrailCacheState ParentState)
{
	ParentStates.FindOrAdd(Guid).FindOrAdd(ParentGuid, ETrailCacheState::UpToDate);
	ParentStates[Guid][ParentGuid] = FMath::Min(ParentStates[Guid][ParentGuid], ParentState);
}

void FTrailHierarchy::FAccumulatedParentStates::ResetParentStates(const FGuid& Guid)
{
	for (TPair<FGuid, ETrailCacheState>& GuidStatePair : ParentStates[Guid])
	{
		GuidStatePair.Value = ETrailCacheState::UpToDate;
	}
}

} // namespace MovieScene
} // namespace UE