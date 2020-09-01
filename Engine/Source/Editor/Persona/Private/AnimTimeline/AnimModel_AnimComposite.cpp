// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimModel_AnimComposite.h"
#include "Animation/AnimComposite.h"
#include "AnimTimelineTrack.h"
#include "AnimTimelineTrack_CompositePanel.h"
#include "Animation/DebugSkelMeshComponent.h"
#include "AnimPreviewInstance.h"
#include "IPersonaPreviewScene.h"

#define LOCTEXT_NAMESPACE "FAnimModel_AnimComposite"

FAnimModel_AnimComposite::FAnimModel_AnimComposite(const TSharedRef<IPersonaPreviewScene>& InPreviewScene, const TSharedRef<IEditableSkeleton>& InEditableSkeleton, const TSharedRef<FUICommandList>& InCommandList, UAnimComposite* InAnimComposite)
	: FAnimModel_AnimSequenceBase(InPreviewScene, InEditableSkeleton, InCommandList, InAnimComposite)
	, AnimComposite(InAnimComposite)
{
	SnapTypes.Add(FAnimModel::FSnapType::CompositeSegment.Type, FAnimModel::FSnapType::CompositeSegment);
}

void FAnimModel_AnimComposite::RefreshTracks()
{
	ClearTrackSelection();

	// Clear all tracks
	RootTracks.Empty();

	// Add the composite root track
	if(!CompositeRoot.IsValid())
	{
		CompositeRoot = MakeShared<FAnimTimelineTrack>(LOCTEXT("CompositeTitle", "Composite"), LOCTEXT("CompositeTooltip", "Composite animation track"), SharedThis(this), true);
	}

	CompositeRoot->ClearChildren();
	RootTracks.Add(CompositeRoot.ToSharedRef());

	TSharedRef<FAnimTimelineTrack_CompositePanel> CompositePanel = MakeShared<FAnimTimelineTrack_CompositePanel>(SharedThis(this));
	CompositeRoot->AddChild(CompositePanel);

	// Add notifies
	RefreshNotifyTracks();

	// Add curves
	RefreshCurveTracks();

	// Snaps
	RefreshSnapTimes();

	// Tell the UI to refresh
	OnTracksChangedDelegate.Broadcast();

	UpdateRange();
}

void FAnimModel_AnimComposite::RefreshSnapTimes()
{
	FAnimModel_AnimSequenceBase::RefreshSnapTimes();
	
	for(const FAnimSegment& Segment : AnimComposite->AnimationTrack.AnimSegments)
	{
		SnapTimes.Add(FSnapTime(FSnapType::CompositeSegment.Type, (double)Segment.StartPos));
		SnapTimes.Add(FSnapTime(FSnapType::CompositeSegment.Type, (double)(Segment.StartPos + Segment.AnimEndTime)));
	}
}

UAnimSequenceBase* FAnimModel_AnimComposite::GetAnimSequenceBase() const 
{
	return AnimComposite;
}

void FAnimModel_AnimComposite::RecalculateSequenceLength()
{
	// Remove Gaps and update Montage Sequence Length
	if(AnimComposite)
	{
		AnimComposite->InvalidateRecursiveAsset();

		float NewSequenceLength = CalculateSequenceLengthOfEditorObject();
		if (NewSequenceLength != AnimComposite->SequenceLength)
		{
			ClampToEndTime(NewSequenceLength);

			AnimComposite->SetSequenceLength(NewSequenceLength);

			// Reset view if we changed length (note: has to be done after ->SetSequenceLength)!
			UpdateRange();

			UAnimPreviewInstance* PreviewInstance = (GetPreviewScene()->GetPreviewMeshComponent()) ? GetPreviewScene()->GetPreviewMeshComponent()->PreviewInstance : nullptr;
			if (PreviewInstance)
			{
				// Re-set the position, so instance is clamped properly
				PreviewInstance->SetPosition(PreviewInstance->GetCurrentTime(), false); 
			}
		}
	}

	FAnimModel::RecalculateSequenceLength();
}

float FAnimModel_AnimComposite::CalculateSequenceLengthOfEditorObject() const
{
	return AnimComposite->AnimationTrack.GetLength();
}

#undef LOCTEXT_NAMESPACE