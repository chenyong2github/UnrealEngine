// Copyright Epic Games, Inc. All Rights Reserved.

#include "TrailHierarchy.h"
#include "MotionTrailEditorMode.h"

#include "CanvasItem.h"

void FTrailHierarchyRenderer::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
{
	const int32 NumEvalTimes = int32(OwningHierarchy->GetViewRange().Size<double>() / OwningHierarchy->GetEditorMode()->GetTrailOptions()->SecondsPerSegment);
	const int32 NumLinesReserveSize = int32(NumEvalTimes * OwningHierarchy->GetAllTrails().Num() * 1.5);
	PDI->AddReserveLines(SDPG_Foreground, NumLinesReserveSize);

	// <parent, current>
	TQueue<FGuid> BFSQueue;
	BFSQueue.Enqueue(OwningHierarchy->GetRootTrailGuid());
	while (!BFSQueue.IsEmpty())
	{
		FGuid CurGuid;
		BFSQueue.Dequeue(CurGuid);

		FTrajectoryDrawInfo* CurDrawInfo = OwningHierarchy->GetAllTrails()[CurGuid]->GetDrawInfo();
		if (CurDrawInfo && CurDrawInfo->IsVisible())
		{
			FTrajectoryDrawInfo::FDisplayContext DisplayContext = {
				CurGuid,
				FTrailScreenSpaceTransform(View, Viewport),
				OwningHierarchy->GetEditorMode()->GetTrailOptions()->SecondsPerTick,
				OwningHierarchy->GetViewRange(),
				OwningHierarchy
			};

			TArray<FVector> PointsToDraw = CurDrawInfo->GetTrajectoryPointsForDisplay(DisplayContext);

			if (PointsToDraw.Num() <= 1)
			{
				continue;
			}

			FVector LastPoint = PointsToDraw[0];
			for (int32 Idx = 1; Idx < PointsToDraw.Num(); Idx++)
			{
				const FVector CurPoint = PointsToDraw[Idx];
				PDI->DrawLine(LastPoint, CurPoint, CurDrawInfo->GetColor(), SDPG_Foreground);
				LastPoint = CurPoint;
			}
		}

		for (const FGuid& ChildGuid : OwningHierarchy->GetHierarchy()[CurGuid].Children)
		{
			BFSQueue.Enqueue(ChildGuid);
		}
	}
}

void FTrailHierarchyRenderer::DrawHUD(FEditorViewportClient* ViewportClient, FViewport* Viewport, const FSceneView* View, FCanvas* Canvas)
{
	TQueue<FGuid> BFSQueue;
	BFSQueue.Enqueue(OwningHierarchy->GetRootTrailGuid());
	while (!BFSQueue.IsEmpty())
	{
		FGuid CurGuid;
		BFSQueue.Dequeue(CurGuid);

		FTrajectoryDrawInfo* CurDrawInfo = OwningHierarchy->GetAllTrails()[CurGuid]->GetDrawInfo();
		if (CurDrawInfo && CurDrawInfo->IsVisible())
		{
			FTrajectoryDrawInfo::FDisplayContext DisplayContext = {
				CurGuid,
				FTrailScreenSpaceTransform(View, Viewport, ViewportClient->GetDPIScale()),
				OwningHierarchy->GetEditorMode()->GetTrailOptions()->bLockTicksToFrames ? OwningHierarchy->GetSecondsPerFrame() : OwningHierarchy->GetEditorMode()->GetTrailOptions()->SecondsPerTick,
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
				Canvas->DrawItem(LineItem);
			}
		}

		for (const FGuid& ChildGuid : OwningHierarchy->GetHierarchy()[CurGuid].Children)
		{
			BFSQueue.Enqueue(ChildGuid);
		}
	}
}

void FTrailHierarchy::Update()
{
	//bHasDeadTrails = false;

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

		for (const FGuid& ChildGuid : Hierarchy[CurGuid].Children)
		{
			if (!Visited.Contains(ChildGuid))
			{
				BFSQueue.Enqueue(ChildGuid);
			}

			Visited.FindOrAdd(ChildGuid).Add(CurGuid, CurCacheState);
		}
	}

	for (const FGuid& TrailGuid : DeadTrails)
	{
		RemoveTrail(TrailGuid);
	}
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