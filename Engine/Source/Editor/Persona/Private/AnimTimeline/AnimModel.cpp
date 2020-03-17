// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimModel.h"
#include "IPersonaPreviewScene.h"
#include "Animation/DebugSkelMeshComponent.h"
#include "AnimPreviewInstance.h"
#include "Preferences/PersonaOptions.h"
#include "Animation/EditorAnimBaseObj.h"
#include "AnimTimelineTrack.h"
#include "Animation/AnimSequence.h"

#define LOCTEXT_NAMESPACE "FAnimModel"

const FAnimModel::FSnapType FAnimModel::FSnapType::CompositeSegment("CompositeSegment", LOCTEXT("CompositeSegmentSnapName", "Composite Segments"), true);

FAnimModel::FAnimModel(const TSharedRef<IPersonaPreviewScene>& InPreviewScene, const TSharedRef<IEditableSkeleton>& InEditableSkeleton, const TSharedRef<FUICommandList>& InCommandList)
	: WeakPreviewScene(InPreviewScene)
	, WeakEditableSkeleton(InEditableSkeleton)
	, WeakCommandList(InCommandList)
	, bIsSelecting(false)
{
}

void FAnimModel::Initialize()
{

}

FAnimatedRange FAnimModel::GetViewRange() const
{
	return ViewRange;
}

FAnimatedRange FAnimModel::GetWorkingRange() const
{
	return WorkingRange;
}

double FAnimModel::GetFrameRate() const
{
	if(UAnimSequence* AnimSequence = Cast<UAnimSequence>(GetAnimSequenceBase()))
	{
		return (double)AnimSequence->GetFrameRate();
	}
	else
	{
		return 30.0;
	}
}

int32 FAnimModel::GetTickResolution() const
{
	return FMath::RoundToInt((double)GetDefault<UPersonaOptions>()->TimelineScrubSnapValue * GetFrameRate());
}

TRange<FFrameNumber> FAnimModel::GetPlaybackRange() const
{
	const int32 Resolution = GetTickResolution();
	return TRange<FFrameNumber>(FFrameNumber(FMath::RoundToInt(PlaybackRange.GetLowerBoundValue() * (double)Resolution)), FFrameNumber(FMath::RoundToInt(PlaybackRange.GetUpperBoundValue() * (double)Resolution)));
}

FFrameNumber FAnimModel::GetScrubPosition() const
{
	if(WeakPreviewScene.IsValid())
	{
		UDebugSkelMeshComponent* PreviewMeshComponent = WeakPreviewScene.Pin()->GetPreviewMeshComponent();
		if(PreviewMeshComponent && PreviewMeshComponent->IsPreviewOn())
		{
			return FFrameNumber(FMath::RoundToInt(PreviewMeshComponent->PreviewInstance->GetCurrentTime() * (double)GetTickResolution()));
		}
	}

	return FFrameNumber(0);
}

void FAnimModel::SetScrubPosition(FFrameTime NewScrubPostion) const
{
	if(WeakPreviewScene.IsValid())
	{
		UDebugSkelMeshComponent* PreviewMeshComponent = WeakPreviewScene.Pin()->GetPreviewMeshComponent();
		if(PreviewMeshComponent && PreviewMeshComponent->IsPreviewOn())
		{
			PreviewMeshComponent->PreviewInstance->SetPosition(NewScrubPostion.AsDecimal() / (double)GetTickResolution());
		}
	}
}

void FAnimModel::HandleViewRangeChanged(TRange<double> InRange, EViewRangeInterpolation InInterpolation)
{
	SetViewRange(InRange);
}

void FAnimModel::SetViewRange(TRange<double> InRange)
{
	ViewRange = InRange;

	if(WorkingRange.HasLowerBound() && WorkingRange.HasUpperBound())
	{
		WorkingRange = TRange<double>::Hull(WorkingRange, ViewRange);
	}
	else
	{
		WorkingRange = ViewRange;
	}
}

void FAnimModel::HandleWorkingRangeChanged(TRange<double> InRange)
{
	WorkingRange = InRange;
}

bool FAnimModel::IsTrackSelected(const TSharedRef<FAnimTimelineTrack>& InTrack) const
{ 
	return SelectedTracks.Find(InTrack) != nullptr;
}

void FAnimModel::ClearTrackSelection()
{
	SelectedTracks.Empty();
}

void FAnimModel::SetTrackSelected(const TSharedRef<FAnimTimelineTrack>& InTrack, bool bIsSelected)
{
	if(bIsSelected)
	{
		SelectedTracks.Add(InTrack);
	}
	else
	{
		SelectedTracks.Remove(InTrack);
	}
}

void FAnimModel::AddReferencedObjects(FReferenceCollector& Collector)
{
	EditorObjectTracker.AddReferencedObjects(Collector);
}

void FAnimModel::SelectObjects(const TArray<UObject*>& Objects)
{
	if(!bIsSelecting)
	{
		TGuardValue<bool> GuardValue(bIsSelecting, true);
		OnSelectObjects.ExecuteIfBound(Objects);

		OnHandleObjectsSelectedDelegate.Broadcast(Objects);
	}
}

UObject* FAnimModel::ShowInDetailsView(UClass* EdClass)
{
	UObject* Obj = EditorObjectTracker.GetEditorObjectForClass(EdClass);
	if(Obj != nullptr)
	{
		if(Obj->IsA(UEditorAnimBaseObj::StaticClass()))
		{
			if(!bIsSelecting)
			{
				TGuardValue<bool> GuardValue(bIsSelecting, true);

				ClearTrackSelection();

				UEditorAnimBaseObj *EdObj = Cast<UEditorAnimBaseObj>(Obj);
				InitDetailsViewEditorObject(EdObj);

				TArray<UObject*> Objects;
				Objects.Add(EdObj);
				OnSelectObjects.ExecuteIfBound(Objects);

				OnHandleObjectsSelectedDelegate.Broadcast(Objects);
			}
		}
	}
	return Obj;
}

void FAnimModel::ClearDetailsView()
{
	if(!bIsSelecting)
	{
		TGuardValue<bool> GuardValue(bIsSelecting, true);

		TArray<UObject*> Objects;
		OnSelectObjects.ExecuteIfBound(Objects);
		OnHandleObjectsSelectedDelegate.Broadcast(Objects);
	}
}

float FAnimModel::CalculateSequenceLengthOfEditorObject() const
{
	if(UAnimSequenceBase* AnimSequenceBase = GetAnimSequenceBase())
	{
		return AnimSequenceBase->SequenceLength;
	}

	return 0.0f;
}

void FAnimModel::RecalculateSequenceLength()
{
	if(UAnimSequenceBase* AnimSequenceBase = GetAnimSequenceBase())
	{
		AnimSequenceBase->ClampNotifiesAtEndOfSequence();
	}
}

void FAnimModel::SetEditableTime(int32 TimeIndex, double Time, bool bIsDragging)
{
	EditableTimes[TimeIndex] = FMath::Clamp(Time, 0.0, (double)CalculateSequenceLengthOfEditorObject());

	OnSetEditableTime(TimeIndex, EditableTimes[TimeIndex], bIsDragging);
}

bool FAnimModel::Snap(float& InOutTime, float InSnapMargin) const
{
	double DoubleTime = (double)InOutTime;
	bool bResult = Snap(DoubleTime, (double)InSnapMargin);
	InOutTime = DoubleTime;
	return bResult;
}

bool FAnimModel::Snap(double& InOutTime, double InSnapMargin) const
{
	InSnapMargin = FMath::Max(InSnapMargin, (double)KINDA_SMALL_NUMBER);

	// Find the closest in-range enabled snap time
	const FSnapTime* ClosestSnapTime = nullptr;
	double ClosestDelta = DBL_MAX;
	for(const FSnapTime& SnapTime : SnapTimes)
	{
		double Delta = FMath::Abs(SnapTime.Time - InOutTime);
		if(Delta < InSnapMargin && Delta < ClosestDelta)
		{
			if(const FSnapType* SnapType = SnapTypes.Find(SnapTime.Type))
			{
				if(SnapType->bEnabled)
				{
					ClosestDelta = Delta;
					ClosestSnapTime = &SnapTime;
				}
			}
		}
	}

	if(ClosestSnapTime != nullptr)
	{
		InOutTime = ClosestSnapTime->Time;
		return true;
	}

	return false;
}

void FAnimModel::BuildContextMenu(FMenuBuilder& InMenuBuilder)
{
	// Let each selected item contribute to the context menu
	TSet<FName> ExistingMenuTypes;
	for(const TSharedRef<FAnimTimelineTrack>& SelectedItem : SelectedTracks)
	{
		SelectedItem->AddToContextMenu(InMenuBuilder, ExistingMenuTypes);
	}
}

#undef LOCTEXT_NAMESPACE