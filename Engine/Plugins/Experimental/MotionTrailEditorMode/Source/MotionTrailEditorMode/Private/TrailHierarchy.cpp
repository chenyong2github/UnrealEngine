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
	
	const int32 NumEvalTimes = int32(OwningHierarchy->GetViewRange().Size<double>() / OwningHierarchy->GetEditorMode()->GetTrailOptions()->SecondsPerSegment);
	const int32 NumLinesReserveSize = int32(NumEvalTimes * OwningHierarchy->GetAllTrails().Num() * 1.3);
	PDI->AddReserveLines(SDPG_Foreground, NumLinesReserveSize);

	// <parent, current>
	TQueue<FGuid> BFSQueue;
	BFSQueue.Enqueue(OwningHierarchy->GetRootTrailGuid());
	while (!BFSQueue.IsEmpty())
	{
		FGuid CurGuid;
		BFSQueue.Dequeue(CurGuid);

		FTrajectoryDrawInfo* CurDrawInfo = OwningHierarchy->GetAllTrails()[CurGuid]->GetDrawInfo();
		if (CurDrawInfo && CurDrawInfo->IsVisible() && !OwningHierarchy->GetSelectionMask().Contains(CurGuid))
		{
			FTrajectoryDrawInfo::FDisplayContext DisplayContext = {
				CurGuid,
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

		for (const FGuid& ChildGuid : OwningHierarchy->GetHierarchy()[CurGuid].Children)
		{
			BFSQueue.Enqueue(ChildGuid);
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

	TQueue<FGuid> BFSQueue;
	BFSQueue.Enqueue(OwningHierarchy->GetRootTrailGuid());
	while (!BFSQueue.IsEmpty())
	{
		FGuid CurGuid;
		BFSQueue.Dequeue(CurGuid);

		FTrajectoryDrawInfo* CurDrawInfo = OwningHierarchy->GetAllTrails()[CurGuid]->GetDrawInfo();
		if (CurDrawInfo && CurDrawInfo->IsVisible() && !OwningHierarchy->GetSelectionMask().Contains(CurGuid))
		{
			FTrajectoryDrawInfo::FDisplayContext DisplayContext = {
				CurGuid,
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

		for (const FGuid& ChildGuid : OwningHierarchy->GetHierarchy()[CurGuid].Children)
		{
			BFSQueue.Enqueue(ChildGuid);
		}
	}

	const FTimespan DrawHUDTimespan = FDateTime::Now() - DrawHUDStartTime;
	OwningHierarchy->GetTimingStats().Add("FTrailHierarchyRenderer::DrawHUD", DrawHUDTimespan);
}

void FTrailHierarchy::Update()
{
	const FDateTime UpdateStartTime = FDateTime::Now();

	SelectionMask.Reset();
	TArray<double> EvalTimesArr;
	const double Spacing = WeakEditorMode->GetTrailOptions()->SecondsPerSegment;
	for (double SecondsItr = ViewRange.GetLowerBoundValue(); SecondsItr < ViewRange.GetUpperBoundValue() + Spacing; SecondsItr += Spacing)
	{
		EvalTimesArr.Add(SecondsItr);
	}

	FTrailEvaluateTimes EvalTimes = FTrailEvaluateTimes(EvalTimesArr, Spacing);

	TArray<FGuid> DeadTrails;

	// <parent, current>
	TMap<FGuid, TMap<FGuid, ETrailCacheState>> Visited;
	TQueue<FGuid> BFSQueue;
	BFSQueue.Enqueue(RootTrailGuid);
	Visited.Add(RootTrailGuid);
	while (!BFSQueue.IsEmpty())
	{
		FGuid CurGuid;
		BFSQueue.Dequeue(CurGuid);

		FTrail::FSceneContext SceneContext = {
			CurGuid,
			EvalTimes,
			this,
			Visited[CurGuid]
		};

		ETrailCacheState CurCacheState = AllTrails[CurGuid]->UpdateTrail(SceneContext);
		if (CurCacheState == ETrailCacheState::Dead)
		{
			DeadTrails.Add(CurGuid);
		}

		if (CurCacheState != ETrailCacheState::NotUpdated)
		{
			for (const FGuid& ChildGuid : Hierarchy[CurGuid].Children)
			{
				if (!Visited.Contains(ChildGuid))
				{
					BFSQueue.Enqueue(ChildGuid);
				}

				Visited.FindOrAdd(ChildGuid).Add(CurGuid, CurCacheState);
			}
		}
		else
		{
			TArray<FGuid> Children = GetAllChildren(CurGuid);
			SelectionMask.Add(CurGuid);
			SelectionMask.Append(Children);
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
	TMap<FString, FInteractiveTrailTool*> ToolsForTrail = AllTrails[Key]->GetTools();
	for (TPair<FString, FInteractiveTrailTool*>& NameToolPair : ToolsForTrail)
	{
		WeakEditorMode->RemoveTrailTool(NameToolPair.Key, NameToolPair.Value);
	}

	FTrailHierarchyNode& TrailNode = Hierarchy[Key];
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

} // namespace MovieScene
} // namespace UE