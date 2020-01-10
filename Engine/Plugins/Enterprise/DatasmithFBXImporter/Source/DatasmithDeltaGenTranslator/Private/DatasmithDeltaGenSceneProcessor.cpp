// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithDeltaGenSceneProcessor.h"

#include "DatasmithDeltaGenImportData.h"
#include "DatasmithDeltaGenLog.h"
#include "DatasmithFBXScene.h"

using TimelineToAnimations = TMap<FDeltaGenTmlDataTimeline*, FDeltaGenTmlDataTimelineAnimation*>;

namespace FDeltaGenProcessorImpl
{
	// Moves tracks of TrackTypes to new animations within each Timeline, all tied to the Dummy node's name
	void MoveTracksToDummyAnimation(TSharedPtr<FDatasmithFBXSceneNode>& Dummy,
									EDeltaGenTmlDataAnimationTrackType TrackTypes,
									const FVector4& TransOffset,
									TimelineToAnimations* FoundAnimations,
									TMap<FDeltaGenTmlDataTimeline *, TArray<FDeltaGenTmlDataTimelineAnimation>> &NewAnimationsPerTimeline)
	{
		for (TPair<FDeltaGenTmlDataTimeline*, FDeltaGenTmlDataTimelineAnimation*>& AnimationInTimeline : *FoundAnimations)
		{
			FDeltaGenTmlDataTimeline* Timeline = AnimationInTimeline.Key;
			FDeltaGenTmlDataTimelineAnimation* Animation = AnimationInTimeline.Value;

			TArray<FDeltaGenTmlDataAnimationTrack>& Tracks = Animation->Tracks;
			TArray<FDeltaGenTmlDataAnimationTrack> NewTracks;
			NewTracks.Reserve(Tracks.Num());

			for (int32 TrackIndex = Tracks.Num() - 1; TrackIndex >= 0; --TrackIndex)
			{
				FDeltaGenTmlDataAnimationTrack& ThisTrack = Tracks[TrackIndex];

				if (EnumHasAnyFlags(ThisTrack.Type, TrackTypes))
				{
					if (EnumHasAnyFlags(ThisTrack.Type, EDeltaGenTmlDataAnimationTrackType::Translation))
					{
						for (FVector4& Value : ThisTrack.Values)
						{
							Value += TransOffset;
						}
					}

					NewTracks.Add(ThisTrack);
					Tracks.RemoveAt(TrackIndex);
				}
			}

			// Move tracks to the dummy
			if (NewTracks.Num() > 0)
			{
				TArray<FDeltaGenTmlDataTimelineAnimation>& NewAnimations = NewAnimationsPerTimeline.FindOrAdd(Timeline);

				FDeltaGenTmlDataTimelineAnimation* NewAnimation = new(NewAnimations) FDeltaGenTmlDataTimelineAnimation;
				NewAnimation->TargetNode = *Dummy->Name;
				NewAnimation->Tracks = MoveTemp(NewTracks);

				Dummy->MarkMovableNode();
			}
		}
	}

	void DecomposeRotationPivotsForNode(TSharedPtr<FDatasmithFBXSceneNode> Node,
									    TMap<FString, TimelineToAnimations>& NodeNamesToAnimations,
										TMap<FDeltaGenTmlDataTimeline*, TArray<FDeltaGenTmlDataTimelineAnimation>>& NewAnimationsPerTimeline)
	{
		if (!Node.IsValid() || Node->RotationPivot.IsNearlyZero())
		{
			return;
		}

		TSharedPtr<FDatasmithFBXSceneNode> NodeParent = Node->Parent.Pin();
		if (!NodeParent.IsValid())
		{
			return;
		}

		FVector RotPivot = Node->RotationPivot;
		FVector NodeLocation = Node->LocalTransform.GetTranslation();
		FQuat NodeRotation = Node->LocalTransform.GetRotation();

		Node->RotationPivot.Set(0.0f, 0.0f, 0.0f);
		Node->LocalTransform.SetTranslation(-RotPivot);
		Node->LocalTransform.SetRotation(FQuat::Identity);

		TSharedPtr<FDatasmithFBXSceneNode> Dummy = MakeShared<FDatasmithFBXSceneNode>();
		Dummy->Name = Node->Name + TEXT("_RotationPivot");
		Dummy->OriginalName = Dummy->Name;
		Dummy->SplitNodeID = Node->SplitNodeID;
		Dummy->LocalTransform.SetTranslation(NodeLocation + RotPivot);
		Dummy->LocalTransform.SetRotation(NodeRotation);

		const FVector4 TransOffset = RotPivot + Node->RotationOffset + Node->ScalingOffset;

		if (TimelineToAnimations* FoundAnimations = NodeNamesToAnimations.Find(Node->OriginalName))
		{
			MoveTracksToDummyAnimation(Dummy,
									   EDeltaGenTmlDataAnimationTrackType::Rotation | EDeltaGenTmlDataAnimationTrackType::RotationDeltaGenEuler | EDeltaGenTmlDataAnimationTrackType::Translation,
									   TransOffset,
									   FoundAnimations,
									   NewAnimationsPerTimeline);
		}

		// Fix hierarchy (place Dummy between Node and Parent)
		Dummy->AddChild(Node);
		NodeParent->Children.Remove(Node);
		NodeParent->AddChild(Dummy);
	}

	void DecomposeScalingPivotsForNode(TSharedPtr<FDatasmithFBXSceneNode> Node,
									   TMap<FString, TimelineToAnimations>& NodeNamesToAnimations,
									   TMap<FDeltaGenTmlDataTimeline*, TArray<FDeltaGenTmlDataTimelineAnimation>>& NewAnimationsPerTimeline)
	{
		if (!Node.IsValid() || Node->ScalingPivot.IsNearlyZero())
		{
			return;
		}

		TSharedPtr<FDatasmithFBXSceneNode> NodeParent = Node->Parent.Pin();
		if (!NodeParent.IsValid())
		{
			return;
		}

		FVector ScalingPivot = Node->ScalingPivot;
		FVector NodeLocation = Node->LocalTransform.GetTranslation();
		FVector NodeScaling = Node->LocalTransform.GetScale3D();

		Node->ScalingPivot.Set(0.0f, 0.0f, 0.0f);
		Node->LocalTransform.SetTranslation(-ScalingPivot);
		Node->LocalTransform.SetScale3D(FVector::OneVector);

		TSharedPtr<FDatasmithFBXSceneNode> Dummy = MakeShared<FDatasmithFBXSceneNode>();
		Dummy->Name = Node->Name + TEXT("_ScalingPivot");
		Dummy->OriginalName = Dummy->Name;
		Dummy->SplitNodeID = Node->SplitNodeID;
		Dummy->LocalTransform.SetTranslation(NodeLocation + ScalingPivot);
		Dummy->LocalTransform.SetScale3D(NodeScaling);

		const FVector4 TransOffset = ScalingPivot + Node->RotationOffset + Node->ScalingOffset;

		if (TimelineToAnimations* FoundAnimations = NodeNamesToAnimations.Find(Node->OriginalName))
		{
			MoveTracksToDummyAnimation(Dummy,
				EDeltaGenTmlDataAnimationTrackType::Scale | EDeltaGenTmlDataAnimationTrackType::Translation,
				TransOffset,
				FoundAnimations,
				NewAnimationsPerTimeline);
		}

		// Fix hierarchy (place Dummy between Node and Parent)
		Dummy->AddChild(Node);
		NodeParent->Children.Remove(Node);
		NodeParent->AddChild(Dummy);
	}
};

FDatasmithDeltaGenSceneProcessor::FDatasmithDeltaGenSceneProcessor(FDatasmithFBXScene* InScene)
	: FDatasmithFBXSceneProcessor(InScene)
{
}

void FDatasmithDeltaGenSceneProcessor::DecomposePivots(TArray<FDeltaGenTmlDataTimeline>& Timelines)
{
	// Cache node names to all the animations they have on all timelines
	TMap<FString, TimelineToAnimations> NodeNamesToAnimations;
	for (FDeltaGenTmlDataTimeline& Timeline : Timelines)
	{
		for (FDeltaGenTmlDataTimelineAnimation& Animation : Timeline.Animations)
		{
			TimelineToAnimations& Animations = NodeNamesToAnimations.FindOrAdd(Animation.TargetNode.ToString());
			Animations.Add(&Timeline, &Animation);
		}
	}

	// Iterate over this array so that we don't step into any newly generated dummy actors
	TMap<FDeltaGenTmlDataTimeline*, TArray<FDeltaGenTmlDataTimelineAnimation>> NewAnimationsPerTimeline;
	for (TSharedPtr<FDatasmithFBXSceneNode> Node : Scene->GetAllNodes())
	{
		FDeltaGenProcessorImpl::DecomposeRotationPivotsForNode(Node, NodeNamesToAnimations, NewAnimationsPerTimeline);
		FDeltaGenProcessorImpl::DecomposeScalingPivotsForNode(Node, NodeNamesToAnimations, NewAnimationsPerTimeline);
	}

	// Add the new animations only afterwards, as NodeNamesToAnimations stores raw pointers to animations within TArrays
	// that might reallocate somewhere else
	for (TTuple<FDeltaGenTmlDataTimeline*, TArray<FDeltaGenTmlDataTimelineAnimation>> Pair : NewAnimationsPerTimeline)
	{
		FDeltaGenTmlDataTimeline* Timeline = Pair.Key;
		const TArray<FDeltaGenTmlDataTimelineAnimation>& NewAnimations = Pair.Value;

		Timeline->Animations.Append(NewAnimations);
	}
}

