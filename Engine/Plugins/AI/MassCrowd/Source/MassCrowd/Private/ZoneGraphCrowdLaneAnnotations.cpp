// Copyright Epic Games, Inc. All Rights Reserved.

#include "ZoneGraphCrowdLaneAnnotations.h"
#include "ZoneGraphAnnotationSubsystem.h"
#include "ZoneGraphRenderingUtilities.h"
#include "ZoneGraphSubsystem.h"
#include "ZoneGraphTypes.h"
#include "ZoneGraphQuery.h"
#include "ZoneGraphSettings.h"
#include "MassNavigationTypes.h"
#include "VisualLogger/VisualLogger.h"

#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
#include "Engine/Canvas.h"
#endif // !UE_BUILD_SHIPPING && !UE_BUILD_TEST

void UZoneGraphCrowdLaneAnnotations::PostSubsystemsInitialized()
{
	Super::PostSubsystemsInitialized();

	CrowdSubsystem = UWorld::GetSubsystem<UMassCrowdSubsystem>(GetWorld());
	checkf(CrowdSubsystem, TEXT("Expecting MassCrowdSubsystem to be present."));
}

FZoneGraphTagMask UZoneGraphCrowdLaneAnnotations::GetAnnotationTags() const
{
	FZoneGraphTagMask AllTags;
	AllTags.Add(CloseLaneTag);
	AllTags.Add(WaitingLaneTag);

	return AllTags;
}

void UZoneGraphCrowdLaneAnnotations::HandleEvents(TConstArrayView<const UScriptStruct*> AllEventStructs, const FInstancedStructStream& Events)
{
	Events.ForEach([this](const FConstStructView View)
	{
		if (const FZoneGraphCrowdLaneStateChangeEvent* const StateChangeEvent = View.GetPtr<FZoneGraphCrowdLaneStateChangeEvent>())
		{
			StateChangeEvents.Add(*StateChangeEvent);
		}
	});
}

void UZoneGraphCrowdLaneAnnotations::TickAnnotation(const float DeltaTime, FZoneGraphAnnotationTagContainer& AnnotationTagContainer)
{
	if (!CloseLaneTag.IsValid())
	{
		return;
	}

	FZoneGraphTagMask AllTags;
	AllTags.Add(CloseLaneTag);
	AllTags.Add(WaitingLaneTag);

	// Process events
	for (const FZoneGraphCrowdLaneStateChangeEvent& Event : StateChangeEvents)
	{
		if (Event.Lane.IsValid())
		{
			TArrayView<FZoneGraphTagMask> LaneTags = AnnotationTagContainer.GetMutableAnnotationTagsForData(Event.Lane.DataHandle);
			FZoneGraphTagMask& LaneTagMask = LaneTags[Event.Lane.Index];

			LaneTagMask.Remove(AllTags);

			if (Event.State == ECrowdLaneState::Closed)
			{
				const FCrowdWaitAreaData* WaitArea = CrowdSubsystem->GetCrowdWaitingAreaData(Event.Lane);

				if (WaitArea && !WaitArea->IsFull())
				{
					LaneTagMask.Add(WaitingLaneTag);
				}
				else
				{
					LaneTagMask.Add(CloseLaneTag);
				}
			}
		}
		else
		{
			UE_VLOG_UELOG(this, LogMassNavigation, Warning, TEXT("Trying to set lane state %s on an invalid lane %s\n"), *UEnum::GetValueAsString(Event.State), *Event.Lane.ToString());
		}
	}
	StateChangeEvents.Reset();

#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
	MarkRenderStateDirty();
#endif
}

#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
void UZoneGraphCrowdLaneAnnotations::DebugDraw(FZoneGraphAnnotationSceneProxy* DebugProxy)
{
	UZoneGraphSubsystem* ZoneGraph = UWorld::GetSubsystem<UZoneGraphSubsystem>(GetWorld());
	UZoneGraphAnnotationSubsystem* ZoneGraphAnnotationSubsystem = UWorld::GetSubsystem<UZoneGraphAnnotationSubsystem>(GetWorld());
	if (!ZoneGraph || !ZoneGraphAnnotationSubsystem)
	{
		return;
	}

	const UZoneGraphSettings* ZoneGraphSettings = GetDefault<UZoneGraphSettings>();
	check(ZoneGraphSettings);

	static const FVector ZOffset(0, 0, 35.0f);
	static const FLinearColor WaitingColor(FColor(255, 196, 0));
	static const FLinearColor ClosedColor(FColor(255, 61, 0));

	FZoneGraphTagMask AllTags;
	AllTags.Add(CloseLaneTag);
	AllTags.Add(WaitingLaneTag);

	for (const FRegisteredCrowdLaneData& RegisteredLaneData : CrowdSubsystem->RegisteredLaneData)
	{
		const FZoneGraphStorage* ZoneStorage = ZoneGraph->GetZoneGraphStorage(RegisteredLaneData.DataHandle);
		if (ZoneStorage == nullptr)
		{
			continue;
		}

		for (int32 LaneIndex = 0; LaneIndex < RegisteredLaneData.CrowdLaneDataArray.Num(); LaneIndex++)
		{
			const FZoneGraphCrowdLaneData& LaneData = RegisteredLaneData.CrowdLaneDataArray[LaneIndex];
			if (LaneData.GetState() == ECrowdLaneState::Closed)
			{
				const FZoneGraphLaneHandle LaneHandle(LaneIndex, RegisteredLaneData.DataHandle);

				FLinearColor Color = ClosedColor;

				const FCrowdWaitAreaData* WaitArea = CrowdSubsystem->GetCrowdWaitingAreaData(LaneHandle);
				if (WaitArea && !WaitArea->IsFull())
				{
					Color = WaitingColor;
				}

				UE::ZoneGraph::RenderingUtilities::AppendLane(DebugProxy, *ZoneStorage, LaneHandle, Color.ToFColor(/*sRGB*/true), 4.0f, ZOffset);
			}
		}

		auto AppendCircleXY = [DebugProxy](const FVector& Center, const float Radius, const FColor Color, const float LineThickness)
		{
			static int32 NumDivs = 16;

			FVector PrevPoint;
			for (int32 Index = 0; Index <= NumDivs; Index++)
			{
				const float Angle = (float)Index / (float)NumDivs * PI * 2.0f;
				float DirX, DirY;
				FMath::SinCos(&DirX, &DirY, Angle);
				const FVector Dir(DirX, DirY, 0.0f);
				const FVector Point = Center + Dir * Radius;
				if (Index > 0)
				{
					DebugProxy->Lines.Emplace(PrevPoint, Point, Color, LineThickness);
				}
				PrevPoint = Point;
			}
		};

		const FColor SlotColor = FColor::Orange;
		for (const FCrowdWaitAreaData& WaitArea : RegisteredLaneData.WaitAreas)
		{
			for (const FCrowdWaitSlot& Slot : WaitArea.Slots)
			{
				AppendCircleXY(Slot.Position + ZOffset, Slot.Radius, SlotColor, 1.0f);
				DebugProxy->Lines.Emplace(Slot.Position + ZOffset, Slot.Position + Slot.Forward * Slot.Radius + ZOffset, SlotColor, 4.0f);
			}
		}
	}
}

void UZoneGraphCrowdLaneAnnotations::DebugDrawCanvas(UCanvas* Canvas, APlayerController*)
{
	if (!bEnableDebugDrawing)
	{
		return;
	}

	const FColor OldDrawColor = Canvas->DrawColor;
	const UFont* RenderFont = GEngine->GetSmallFont();

	Canvas->SetDrawColor(FColor::White);
	static const FVector ZOffset(0, 0, 35.0f);

	UZoneGraphSubsystem* ZoneGraph = UWorld::GetSubsystem<UZoneGraphSubsystem>(GetWorld());
	UZoneGraphAnnotationSubsystem* ZoneGraphAnnotationSubsystem = UWorld::GetSubsystem<UZoneGraphAnnotationSubsystem>(GetWorld());
	if (!ZoneGraph)
	{
		return;
	}

	for (const FRegisteredCrowdLaneData& RegisteredLaneData : CrowdSubsystem->RegisteredLaneData)
	{
		const FZoneGraphStorage* ZoneStorage = RegisteredLaneData.DataHandle.IsValid() ? ZoneGraph->GetZoneGraphStorage(RegisteredLaneData.DataHandle) : nullptr;
		if (ZoneStorage == nullptr)
		{
			continue;
		}

		for (int32 LaneIdx = 0; LaneIdx < ZoneStorage->Lanes.Num() && ZoneGraphAnnotationSubsystem; LaneIdx++)
		{
			FZoneGraphLaneLocation CenterLoc;
			UE::ZoneGraph::Query::CalculateLocationAlongLaneFromRatio(*ZoneStorage, LaneIdx, 0.5f, CenterLoc);
			const FVector ScreenLoc = Canvas->Project(CenterLoc.Position);

			const FZoneGraphTagMask Mask = ZoneGraphAnnotationSubsystem->GetAnnotationTags({LaneIdx, RegisteredLaneData.DataHandle});
			Canvas->DrawText(RenderFont, FString::Printf(TEXT("%s\n0x%08X"), *UE::ZoneGraph::Helpers::GetTagMaskString(Mask, TEXT(", ")), Mask.GetValue()), ScreenLoc.X, ScreenLoc.Y);
		}

		for (auto It = RegisteredLaneData.LaneToTrackingDataLookup.CreateConstIterator(); It; ++It)
		{
			const FCrowdTrackingLaneData& TrackingData = It->Value;
			if (TrackingData.NumEntitiesOnLane > 0)
			{
				const int32 LaneIndex = It->Key;
				FZoneGraphLaneLocation CenterLoc;
				UE::ZoneGraph::Query::CalculateLocationAlongLaneFromRatio(*ZoneStorage, LaneIndex, 0.5f, CenterLoc);
				const FVector ScreenLoc = Canvas->Project(CenterLoc.Position + ZOffset);
				Canvas->DrawText(RenderFont, FString::Printf(TEXT("Num: %d"), TrackingData.NumEntitiesOnLane), ScreenLoc.X, ScreenLoc.Y);
			}
		}

		for (const FCrowdWaitAreaData& WaitArea : RegisteredLaneData.WaitAreas)
		{
			for (const FCrowdWaitSlot& Slot : WaitArea.Slots)
			{
				if (Slot.bOccupied)
				{
					const FVector ScreenLoc = Canvas->Project(Slot.Position + ZOffset);
					Canvas->DrawText(RenderFont, TEXT("OCCUPIED"), ScreenLoc.X, ScreenLoc.Y);
				}
			}
		}
	}

	Canvas->SetDrawColor(OldDrawColor);
}

#endif // !UE_BUILD_SHIPPING && !UE_BUILD_TEST
