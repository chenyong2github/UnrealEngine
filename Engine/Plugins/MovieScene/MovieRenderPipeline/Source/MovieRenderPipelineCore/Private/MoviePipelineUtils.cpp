// Copyright Epic Games, Inc. All Rights Reserved.

#include "MoviePipelineUtils.h"
#include "Modules/ModuleManager.h"
#include "AssetRegistryModule.h"
#include "MoviePipelineSetting.h"
#include "ARFilter.h"
#include "AssetData.h"
#include "UObject/UObjectIterator.h"
#include "HAL/IConsoleManager.h"
#include "MoviePipelineAntiAliasingSetting.h"
#include "Tracks/MovieSceneSubTrack.h"
#include "Tracks/MovieSceneCameraCutTrack.h"
#include "Tracks/MovieSceneCinematicShotTrack.h"
#include "Sections/MovieSceneCinematicShotSection.h"
#include "Sections/MovieSceneCameraCutSection.h"
#include "MovieSceneSequence.h"
#include "MovieRenderPipelineCoreModule.h"
#include "Misc/Paths.h"
#include "Misc/FrameRate.h"
#include "MoviePipelineQueue.h"

namespace UE
{
	namespace MovieRenderPipeline
	{
		TArray<UClass*> FindMoviePipelineSettingClasses()
		{
			FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

			TArray<FAssetData> ClassList;

			FARFilter Filter;
			Filter.ClassNames.Add(UMoviePipelineSetting::StaticClass()->GetFName());

			// Include any Blueprint based objects as well, this includes things like Blutilities, UMG, and GameplayAbility objects
			Filter.bRecursiveClasses = true;
			AssetRegistryModule.Get().GetAssets(Filter, ClassList);

			TArray<UClass*> Classes;

			for (const FAssetData& Data : ClassList)
			{
				UClass* Class = Data.GetClass();
				if (Class)
				{
					Classes.Add(Class);
				}
			}

			for (TObjectIterator<UClass> ClassIterator; ClassIterator; ++ClassIterator)
			{
				if (ClassIterator->IsChildOf(UMoviePipelineSetting::StaticClass()) && !ClassIterator->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
				{
					Classes.Add(*ClassIterator);
				}
			}

			return Classes;
		}
	
		/**
		* Returns the anti-aliasing setting that we should use. This defaults to the project setting, and then
		* uses the one specified by the setting if overriden.
		*/
		EAntiAliasingMethod GetEffectiveAntiAliasingMethod(const UMoviePipelineAntiAliasingSetting* InSetting)
		{
			EAntiAliasingMethod AntiAliasingMethod = EAntiAliasingMethod::AAM_None;

			IConsoleVariable* AntiAliasingCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.DefaultFeature.AntiAliasing"));
			if (AntiAliasingCVar)
			{
				int32 Value = AntiAliasingCVar->GetInt();
				if (Value >= 0 && Value < AAM_MAX)
				{
					AntiAliasingMethod = (EAntiAliasingMethod)Value;
				}
			}

			if (InSetting)
			{
				if (InSetting->bOverrideAntiAliasing)
				{
					AntiAliasingMethod = InSetting->AntiAliasingMethod;
				}
			}

			return AntiAliasingMethod;
		}
	}
}

namespace MoviePipeline
{
	void GetPassCompositeData(FMoviePipelineMergerOutputFrame* InMergedOutputFrame, TArray<MoviePipeline::FCompositePassInfo>& OutCompositedPasses)
	{
		for (TPair<FMoviePipelinePassIdentifier, TUniquePtr<FImagePixelData>>& RenderPassData : InMergedOutputFrame->ImageOutputData)
		{
			FImagePixelDataPayload* Payload = RenderPassData.Value->GetPayload<FImagePixelDataPayload>();
			if (Payload->bCompositeToFinalImage)
			{
				// Burn in data should always be 8 bit values, this is assumed later when we composite.
				check(RenderPassData.Value->GetType() == EImagePixelType::Color);

				FCompositePassInfo& Info = OutCompositedPasses.AddDefaulted_GetRef();
				Info.PassIdentifier = RenderPassData.Key;
				Info.PixelData = RenderPassData.Value->CopyImageData();
			}
		}
	}

	void GetOutputStateFormatArgs(FMoviePipelineFormatArgs& InOutFinalFormatArgs, const FString FrameNumber, const FString FrameNumberShot, const FString FrameNumberRel, const FString FrameNumberShotRel, const FString CameraName, const FString ShotName)
	{
		InOutFinalFormatArgs.FilenameArguments.Add(TEXT("frame_number"), FrameNumber);
		InOutFinalFormatArgs.FilenameArguments.Add(TEXT("frame_number_shot"), FrameNumberShot);
		InOutFinalFormatArgs.FilenameArguments.Add(TEXT("frame_number_rel"), FrameNumberRel);
		InOutFinalFormatArgs.FilenameArguments.Add(TEXT("frame_number_shot_rel"), FrameNumberShotRel);
		InOutFinalFormatArgs.FilenameArguments.Add(TEXT("camera_name"), CameraName);
		InOutFinalFormatArgs.FilenameArguments.Add(TEXT("shot_name"), ShotName);

		InOutFinalFormatArgs.FileMetadata.Add(TEXT("unreal/sequenceFrameNumber"), FrameNumber);
		InOutFinalFormatArgs.FileMetadata.Add(TEXT("unreal/shotFrameNumber"), FrameNumberShot);
		InOutFinalFormatArgs.FileMetadata.Add(TEXT("unreal/sequenceFrameNumberRelative"), FrameNumberRel);
		InOutFinalFormatArgs.FileMetadata.Add(TEXT("unreal/shotFrameNumberRelative"), FrameNumberShotRel);
		InOutFinalFormatArgs.FileMetadata.Add(TEXT("unreal/cameraName"), CameraName);
		InOutFinalFormatArgs.FileMetadata.Add(TEXT("unreal/shotName"), ShotName);
	}

}

//namespace UE
//{
namespace MoviePipeline
{
	void GatherLeafNodesRecursive(TSharedPtr<FCameraCutSubSectionHierarchyNode> InNode, TArray<TSharedPtr<FCameraCutSubSectionHierarchyNode>>& OutLeaves)
	{
		for (TSharedPtr<FCameraCutSubSectionHierarchyNode> Child : InNode->GetChildren())
		{
			GatherLeafNodesRecursive(Child, OutLeaves);
		}

		if (InNode->GetChildren().Num() == 0)
		{
			OutLeaves.Add(InNode);
		}
	}

	void BuildCompleteSequenceHierarchyRecursive(UMovieSceneSequence* InSequence, TSharedPtr<FCameraCutSubSectionHierarchyNode> InNode)
	{
		if (UMovieSceneCameraCutTrack* CameraCutTrack = Cast<UMovieSceneCameraCutTrack>(InSequence->GetMovieScene()->GetCameraCutTrack()))
		{
			// We create leaf nodes for each section. This kind of makes duplicate leafs but since this is separate from the evaluation tree
			// and we only use it to save/restore state at the end, its ok.
			for (UMovieSceneSection* Section : CameraCutTrack->GetAllSections())
			{
				TSharedPtr<FCameraCutSubSectionHierarchyNode> Node = MakeShared<FCameraCutSubSectionHierarchyNode>();
				Node->MovieScene = TWeakObjectPtr<UMovieScene>(InSequence->GetMovieScene());
				Node->CameraCutSection = TWeakObjectPtr<UMovieSceneCameraCutSection>(Cast<UMovieSceneCameraCutSection>(Section));
				InNode->AddChild(Node);
			}
		}

		// The evaluation tree only contains the active bits of the hierarchy (which is what we want). However we disable non-active sections while
		// soloing, and we can't restore them because they weren't part of the shot hierarchy. To resolve this, we build a complete copy of the
		// original for restoration at the end. We'll build our own tree, kept separate from the per-shot ones.
		for (UMovieSceneTrack* Track : InSequence->GetMovieScene()->GetMasterTracks())
		{
			UMovieSceneSubTrack* SubTrack = Cast<UMovieSceneSubTrack>(Track);
			if (!SubTrack)
			{
				continue;
			}

			for (UMovieSceneSection* Section : SubTrack->GetAllSections())
			{
				UMovieSceneSubSection* SubSection = Cast<UMovieSceneSubSection>(Section);
				if (!SubSection)
				{
					continue;
				}

				TSharedPtr<FCameraCutSubSectionHierarchyNode> Node = MakeShared<FCameraCutSubSectionHierarchyNode>();
				Node->MovieScene = TWeakObjectPtr<UMovieScene>(InSequence->GetMovieScene());
				Node->Section = TWeakObjectPtr<UMovieSceneSubSection>(SubSection);
				InNode->AddChild(Node);

				if (SubSection->GetSequence())
				{
					BuildCompleteSequenceHierarchyRecursive(SubSection->GetSequence(), Node);
				}
			}
		}

		// Cache any sections that we will auto-expand later.
		for (UMovieSceneSection* Section : InSequence->GetMovieScene()->GetAllSections())
		{
			if (Section->GetSupportsInfiniteRange())
			{
				InNode->AdditionalSectionsToExpand.Add(MakeTuple(Section, Section->GetRange()));
			}
		}
	}

	void SaveOrRestoreSubSectionHierarchy(TSharedPtr<FCameraCutSubSectionHierarchyNode> InLeaf, const bool bInSave)
	{
		TSharedPtr<FCameraCutSubSectionHierarchyNode> Node = InLeaf;
		while (Node)
		{
			if (Node->MovieScene.IsValid())
			{
				if (bInSave)
				{
					Node->OriginalMovieScenePlaybackRange = Node->MovieScene->GetPlaybackRange();
#if WITH_EDITOR
					Node->bOriginalMovieSceneReadOnly = Node->MovieScene->IsReadOnly();
					Node->bOriginalMovieScenePlaybackRangeLocked = Node->MovieScene->IsPlaybackRangeLocked();
#endif
					if (UPackage* OwningPackage = Node->MovieScene->GetTypedOuter<UPackage>())
					{
						Node->bOriginalMovieScenePackageDirty = OwningPackage->IsDirty();
					}

					Node->EvaluationType = Node->MovieScene->GetEvaluationType();
				}

				// Unlock the movie scene so we can make changes to sections below, it'll get re-locked later if needed.
				// This has to be done each iteration of the loop because we iterate through leaves, so the first leaf 
				// can end up re-locking a MovieScene higher up in the hierarchy, and then when subsequent leaves try
				// to restore their hierarchy the now-locked MovieScene prevents full restoration.
#if WITH_EDITOR
				if (!bInSave)
				{
					Node->MovieScene->SetReadOnly(false);
				}
#endif
			}

			if (Node->Section.IsValid())
			{
				if (bInSave)
				{
					Node->bOriginalShotSectionIsLocked = Node->Section->IsLocked();
					Node->OriginalShotSectionRange = Node->Section->GetRange();
					Node->bOriginalShotSectionIsActive = Node->Section->IsActive();
				}
				else
				{
					Node->Section->SetRange(Node->OriginalShotSectionRange);
					Node->Section->SetIsActive(Node->bOriginalShotSectionIsActive);
					Node->Section->SetIsLocked(Node->bOriginalShotSectionIsLocked);
					Node->Section->MarkAsChanged();
				}
			}

			if (Node->CameraCutSection.IsValid())
			{
				if (bInSave)
				{
					Node->OriginalCameraCutSectionRange = Node->CameraCutSection->GetRange();
					Node->bOriginalCameraCutIsActive = Node->CameraCutSection->IsActive();
				}
				else
				{
					Node->CameraCutSection->SetRange(Node->OriginalCameraCutSectionRange);
					Node->CameraCutSection->SetIsActive(Node->bOriginalCameraCutIsActive);

					Node->CameraCutSection->MarkAsChanged();
				}
			}

			if(!bInSave)
			{
				// These are restored, but they're not saved using this function. This is because they're cached earlier
				for (const TTuple<UMovieSceneSection*, TRange<FFrameNumber>>& Pair : Node->AdditionalSectionsToExpand)
				{
					Pair.Key->SetRange(Pair.Value);
					Pair.Key->MarkAsChanged();
				}
			}

			if (Node->MovieScene.IsValid())
			{
				// Has to come last otherwise calls to MarkAsChanged from children re-dirty the package.
				if (!bInSave)
				{
					Node->MovieScene->SetPlaybackRange(Node->OriginalMovieScenePlaybackRange);
					Node->MovieScene->SetEvaluationType(Node->EvaluationType);
#if WITH_EDITOR
					Node->MovieScene->SetReadOnly(Node->bOriginalMovieSceneReadOnly);
					Node->MovieScene->SetPlaybackRangeLocked(Node->bOriginalMovieScenePlaybackRangeLocked);
#endif
					Node->MovieScene->MarkAsChanged();

					if (UPackage* OwningPackage = Node->MovieScene->GetTypedOuter<UPackage>())
					{
						OwningPackage->SetDirtyFlag(Node->bOriginalMovieScenePackageDirty);
					}
				}
			}

			Node = Node->GetParent();
		}
	}

	void SetSubSectionHierarchyActive(TSharedPtr<FCameraCutSubSectionHierarchyNode> InRoot, bool bInActive)
	{
		TSharedPtr<FCameraCutSubSectionHierarchyNode> Node = InRoot;
		while (Node)
		{
			if (Node->MovieScene.IsValid())
			{
#if WITH_EDITOR
				Node->MovieScene->SetReadOnly(false);
				Node->MovieScene->SetPlaybackRangeLocked(false);
#endif
				Node->MovieScene->SetEvaluationType(EMovieSceneEvaluationType::WithSubFrames);
			}

			if (Node->Section.IsValid())
			{
				Node->Section->SetIsLocked(false);
			}

			if (Node->CameraCutSection.IsValid())
			{
				Node->CameraCutSection->SetIsActive(bInActive);
				Node->CameraCutSection->MarkAsChanged();

				UE_LOG(LogMovieRenderPipeline, Verbose, TEXT("Disabled CameraCutSection: %s while soloing shot."), *GetNameSafe(Node->CameraCutSection.Get()));
			}

			if (Node->Section.IsValid())
			{
				Node->Section->SetIsActive(bInActive);
				Node->Section->MarkAsChanged();

				UE_LOG(LogMovieRenderPipeline, Verbose, TEXT("Disabled SubSequenceSection: %s while soloing shot."), *GetNameSafe(Node->Section.Get()));
			}

			Node = Node->GetParent();
		}
	}

	void CheckPartialSectionEvaluationAndWarn(const FFrameNumber& LeftDeltaTicks, TSharedPtr<FCameraCutSubSectionHierarchyNode> Node, UMoviePipelineExecutorShot* InShot, const FFrameRate& InMasterDisplayRate)
	{
		// For the given movie scene, we want to produce a warning if there is no data to evaluate once we've expanded for 
		// handle frames or temporal sub-sampling. To do our best guess at which tracks are relevant, we can look at the
		// the range between (-HandleFrames+TemporalFrames, PlaybackRangeStart]. We are inclusive for PlaybackRangeStart
		// so that we detect the most common case - all tracks starting on frame 0. We are exclusive of the lower bound
		// so that we don't detect sections that have been correctly expanded. We also produce warnings where we find
		// Shots/Camera cut sections that don't land on whole frames and we warn for those too, as it's often undesired
		// and offsets the output frame number (when rounded back to whole numbers) for output
		if (LeftDeltaTicks > 0 && Node->MovieScene.IsValid())
		{
			FFrameNumber LowerCheckBound = InShot->ShotInfo.TotalOutputRangeMaster.GetLowerBoundValue() - LeftDeltaTicks;
			FFrameNumber UpperCheckBound = InShot->ShotInfo.TotalOutputRangeMaster.GetLowerBoundValue();

			TRange<FFrameNumber> CheckRange = TRange<FFrameNumber>(TRangeBound<FFrameNumber>::Exclusive(LowerCheckBound), TRangeBound<FFrameNumber>::Inclusive(UpperCheckBound));

			for (const UMovieSceneSection* Section : Node->MovieScene->GetAllSections())
			{
				if (!Section)
				{
					continue;
				}

				// If the section can be made infinite, it will automatically get expanded when the shot is activated, so no need to warn.
				if (Section->GetSupportsInfiniteRange())
				{
					continue;
				}

				// camera cut and sub-sections also will get the expansion manually, no need to warn
				if (Section == Node->Section || Section == Node->CameraCutSection)
				{
					continue;
				}

				if (Section->GetRange().HasLowerBound())
				{
					const bool bOverlaps = CheckRange.Contains(Section->GetRange().GetLowerBoundValue());
					if (bOverlaps)
					{
						FString SectionName = Section->GetName();
						FString BindingName = TEXT("None");

						// Try to find which binding it belongs to as the names will be more useful than section types.
						for (const FMovieSceneBinding& Binding : Node->MovieScene->GetBindings())
						{
							for (const UMovieSceneTrack* BindingTrack : Binding.GetTracks())
							{
								if (BindingTrack->HasSection(*Section))
								{
									BindingName = Binding.GetName();
									break;
								}
							}
						}

						// Convert ticks back to frames for human consumption
						FFrameNumber LowerCheckBoundFrame = FFrameRate::TransformTime(LowerCheckBound, InShot->ShotInfo.CachedTickResolution, InMasterDisplayRate).FloorToFrame();
						FFrameNumber UpperCheckBoundFrame = FFrameRate::TransformTime(UpperCheckBound, InShot->ShotInfo.CachedTickResolution, InMasterDisplayRate).FloorToFrame();

						UE_LOG(LogMovieRenderPipeline, Warning, TEXT("[%s %s] Due to Temporal sub-sampling or handle frames, evaluation will occur outside of shot boundaries (from frame %d to %d). Section %s (Binding: %s) starts during this time period and cannot be auto-expanded. Please extend this section to start on frame %d. (All times listed are relative to the master sequence)"),
							*InShot->OuterName, *InShot->InnerName, LowerCheckBoundFrame.Value, UpperCheckBoundFrame.Value,
							*SectionName, *BindingName, LowerCheckBoundFrame.Value);
					}
				}
			}
		}
	}

	void CacheCompleteSequenceHierarchy(UMovieSceneSequence* InSequence, TSharedPtr<FCameraCutSubSectionHierarchyNode> InRootNode)
	{
		// The evaluation tree only contains the active bits of the hierarchy (which is what we want). However we disable non-active sections while
		// soloing, and we can't restore them because they weren't part of the shot hierarchy. To resolve this, we build a complete copy of the
		// original for restoration at the end. We'll build our own tree, kept separate from the per-shot ones.
		BuildCompleteSequenceHierarchyRecursive(InSequence, InRootNode);

		TArray<TSharedPtr<FCameraCutSubSectionHierarchyNode>> LeafNodes;
		GatherLeafNodesRecursive(InRootNode, LeafNodes);

		// Now cache the values - playback ranges, section sizes, active states, etc.
		for (TSharedPtr<FCameraCutSubSectionHierarchyNode> Leaf : LeafNodes)
		{
			const bool bInSave = true;

			// This function only takes leaves. Technically we'll end up re-caching the parents multiple times,
			// but the values don't change so it's a non-issue.
			SaveOrRestoreSubSectionHierarchy(Leaf, bInSave);
		}
	}

	void RestoreCompleteSequenceHierarchy(UMovieSceneSequence* InSequence, TSharedPtr<FCameraCutSubSectionHierarchyNode> InRootNode)
	{
		TArray<TSharedPtr<FCameraCutSubSectionHierarchyNode>> LeafNodes;
		GatherLeafNodesRecursive(InRootNode, LeafNodes);
		for (TSharedPtr<FCameraCutSubSectionHierarchyNode> Leaf : LeafNodes)
		{
			const bool bInSave = false;
			SaveOrRestoreSubSectionHierarchy(Leaf, bInSave);
		}
	}

	TTuple<FString, FString> GetNameForShot(const FMovieSceneSequenceHierarchy& InHierarchy, UMovieSceneSequence* InRootSequence, TSharedPtr<FCameraCutSubSectionHierarchyNode> InSubSectionHierarchy)
	{
		FString InnerName;
		FString OuterName;

		// The inner name is the camera cut (if available) otherwise it falls back to the name of the moviescene.
		if (InSubSectionHierarchy->CameraCutSection.IsValid())
		{
			const FMovieSceneObjectBindingID& CameraObjectBindingId = InSubSectionHierarchy->CameraCutSection->GetCameraBindingID();
			if (CameraObjectBindingId.IsValid())
			{
				// Look up the correct sequence from the compiled hierarchy, as bindings can exist in other movie scenes.
				FMovieSceneSequenceID ParentId = InSubSectionHierarchy->NodeID;
				UMovieSceneSequence* OwningSequence = InRootSequence;
				if (ParentId != MovieSceneSequenceID::Invalid)
				{
					FMovieSceneSequenceID ResolvedSequenceId = CameraObjectBindingId.ResolveSequenceID(ParentId, &InHierarchy);
					if (InHierarchy.FindSubSequence(ResolvedSequenceId))
					{
						OwningSequence = InHierarchy.FindSubSequence(ResolvedSequenceId);
					}
				}

				if (OwningSequence && OwningSequence->GetMovieScene())
				{
					const FMovieSceneBinding* Binding = OwningSequence->GetMovieScene()->FindBinding(CameraObjectBindingId.GetGuid());
					if (Binding)
					{
						if (const FMovieSceneSpawnable* Spawnable = OwningSequence->GetMovieScene()->FindSpawnable(Binding->GetObjectGuid()))
						{
							InnerName = Spawnable->GetName();
						}
						else if (const FMovieScenePossessable* Posssessable = OwningSequence->GetMovieScene()->FindPossessable(Binding->GetObjectGuid()))
						{
							InnerName = Posssessable->GetName();
						}
						else
						{
							InnerName = Binding->GetName();
						}
					}
				}
			}
		}
		else if (InSubSectionHierarchy->MovieScene.IsValid())
		{
			InnerName = FPaths::GetBaseFilename(InSubSectionHierarchy->MovieScene->GetPathName());
		}

		// The outer name is a little more complicated. We don't use Outers here because each subscene is outered to its own package.
		TArray<FString> Names;
		TSharedPtr<FCameraCutSubSectionHierarchyNode> CurNode = InSubSectionHierarchy;
		while (CurNode)
		{
			// Camera Cut owned by Track, Track owned by MovieScene, MovieScene by ULevelSequence, Level Sequence by Package
			if (UMovieSceneCinematicShotSection* AsShot = Cast<UMovieSceneCinematicShotSection>(CurNode->Section))
			{
				Names.Add(AsShot->GetShotDisplayName());
			}
			else if (UMovieSceneSubSection* AsSubSequence = Cast<UMovieSceneSubSection>(CurNode->Section))
			{
				// Sub-sequences don't have renameable sections, we just have to use the target sequence name
				if (AsSubSequence->GetSequence())
				{
					Names.Add(FPaths::GetBaseFilename(AsSubSequence->GetSequence()->GetPathName()));

				}
			}

			CurNode = CurNode->GetParent();
		}

		// We built these inner to outer so we need to reverse them
		TStringBuilder<64> StringBuilder;
		for (int32 Index = Names.Num() - 1; Index >= 0; Index--)
		{
			StringBuilder.Append(*Names[Index]);
			if (Index != 0)
			{
				// Separate them by dots, but skip the dot on the last one.
				StringBuilder.Append(TEXT("."));
			}
		}

		// If you don't have any shots, then StringBuilder will be empty.
		if (StringBuilder.Len() == 0)
		{
			StringBuilder.Append(TEXT("no shot"));
		}
		OuterName = StringBuilder.ToString();

		return TTuple<FString, FString>(InnerName, OuterName);
	}

	TRange<FFrameNumber> GetCameraWarmUpRangeFromSequence(UMovieSceneSequence* InSequence, FFrameNumber InSectionEnd, FMovieSceneTimeTransform InInnerToOuterTransform)
	{
		if (!ensureAlways(InSequence && InSequence->GetMovieScene()))
		{
			return TRange<FFrameNumber>::Empty();
		}

		UMovieSceneCameraCutTrack* CamTrack = Cast<UMovieSceneCameraCutTrack>(InSequence->GetMovieScene()->GetCameraCutTrack());
		if (CamTrack && CamTrack->GetAllSections().Num() > 0)
		{
			UMovieSceneCameraCutSection* ValidCameraCutSection = CastChecked<UMovieSceneCameraCutSection>(CamTrack->GetAllSections()[0]);
			if (ValidCameraCutSection->GetRange().HasLowerBound())
			{
				FFrameNumber CameraCutSectionStart = (ValidCameraCutSection->GetRange().GetLowerBoundValue() * InInnerToOuterTransform).FloorToFrame();

				return TRange<FFrameNumber>(CameraCutSectionStart, InSectionEnd);
			}
		}

		return TRange<FFrameNumber>::Empty();
	}

	void BuildSectionHierarchyRecursive(const FMovieSceneSequenceHierarchy& InHierarchy, UMovieSceneSequence* InRootSequence, const FMovieSceneSequenceID InSequenceId, const FMovieSceneSequenceID InChildId, TSharedPtr<FCameraCutSubSectionHierarchyNode> OutSubsectionHierarchy)
	{
		const FMovieSceneSequenceHierarchyNode* SequenceNode = InHierarchy.FindNode(InSequenceId);
		const UMovieSceneSequence* Sequence = (InSequenceId == MovieSceneSequenceID::Root) ? InRootSequence : InHierarchy.FindSubSequence(InSequenceId);
		const FMovieSceneSubSequenceData* ChildSubSectionData = InHierarchy.FindSubData(InChildId);

		if (!SequenceNode || !Sequence || !Sequence->GetMovieScene())
		{
			return;
		}


		if (ChildSubSectionData)
		{
			const TArray<UMovieSceneTrack*> MasterTracks = Sequence->GetMovieScene()->GetMasterTracks();
			for (const UMovieSceneTrack* Track : MasterTracks)
			{
				// SubTracks encompass both Cinematic Shot sections and Sub-Sequences
				if (Track && Track->IsA<UMovieSceneSubTrack>())
				{
					const UMovieSceneSubTrack* SubTrack = CastChecked<UMovieSceneSubTrack>(Track);
					for (UMovieSceneSection* Section : SubTrack->GetAllSections())
					{
						UMovieSceneSubSection* SubSection = Cast<UMovieSceneSubSection>(Section);
						if (SubSection)
						{
							const bool bMatch = SubSection->GetSignature() == ChildSubSectionData->GetSubSectionSignature();
							if (bMatch)
							{
								// This sub-section is the owner of our child. Push it into our tree.
								OutSubsectionHierarchy->Section = TWeakObjectPtr<UMovieSceneSubSection>(SubSection);
								break;
							}
						}

					}
				}
			}
		}

		OutSubsectionHierarchy->MovieScene = TWeakObjectPtr<UMovieScene>(Sequence->GetMovieScene());
		OutSubsectionHierarchy->NodeID = InSequenceId;

		// Only try assigning the parent and diving in if this node isn't already the root, roots have no parents.
		if (InSequenceId != MovieSceneSequenceID::Root)
		{
			TSharedPtr<FCameraCutSubSectionHierarchyNode> ParentNode = MakeShared<FCameraCutSubSectionHierarchyNode>();
			OutSubsectionHierarchy->SetParent(ParentNode);

			BuildSectionHierarchyRecursive(InHierarchy, InRootSequence, SequenceNode->ParentID, InSequenceId, ParentNode);
		}
	}


} // MoviePipeline
//} // UE

namespace UE
{
	namespace MoviePipeline
	{
		void ValidateOutputFormatString(FString& InOutFilenameFormatString, const bool bTestRenderPass, const bool bTestFrameNumber)
		{
			const FString FrameNumberIdentifiers[] = { TEXT("{frame_number}"), TEXT("{frame_number_shot}"), TEXT("{frame_number_rel}"), TEXT("{frame_number_shot_rel}") };

			// If there is more than one file being written for this frame, make sure they uniquely identify.
			if (bTestRenderPass)
			{
				if (!InOutFilenameFormatString.Contains(TEXT("{render_pass}"), ESearchCase::IgnoreCase))
				{
					UE_LOG(LogMovieRenderPipeline, Warning, TEXT("Multiple render passes exported but no {render_pass} format found. Automatically adding!"));

					// Search for a frame number in the output string
					int32 FrameNumberIndex = INDEX_NONE;
					for (const FString& Identifier : FrameNumberIdentifiers)
					{
						FrameNumberIndex = InOutFilenameFormatString.Find(Identifier, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
						if (FrameNumberIndex != INDEX_NONE)
						{
							break;
						}
					}

					if (FrameNumberIndex == INDEX_NONE)
					{
						// No frame number found, so just append render_pass
						InOutFilenameFormatString += TEXT("{render_pass}");
					}
					else
					{
						// If a frame number is found, we need to insert render_pass first before it, so various editing
						// software will still be able to identify if this is an image sequence
						InOutFilenameFormatString.InsertAt(FrameNumberIndex, TEXT("{render_pass}."));
					}
				}
			}

			if (bTestFrameNumber)
			{
				// Ensure there is a frame number in the output string somewhere to uniquely identify individual files in an image sequence.
				int32 FrameNumberIndex = INDEX_NONE;
				for (const FString& Identifier : FrameNumberIdentifiers)
				{
					FrameNumberIndex = InOutFilenameFormatString.Find(Identifier, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
					if (FrameNumberIndex != INDEX_NONE)
					{
						break;
					}
				}

				// We want to insert a {file_dup} before the frame number. This instructs the name resolver to put the (2) before
				// the frame number, so that they're still properly recognized as image sequences by other software. It will resolve
				// to "" if not needed.
				if (FrameNumberIndex == INDEX_NONE)
				{
					// Previously, the frame number identifier would be inserted so that files would not be overwritten. However, users prefer to have exact control over the filename.
					//InOutFilenameFormatString.Append(TEXT("{file_dup}.{frame_number}"));
					UE_LOG(LogMovieRenderPipeline, Warning, TEXT("Frame number identifier not found. Files may be overwritten."));
				}
				else
				{
					// The user had already specified a frame number identifier, so we need to insert the
					// file_dup tag before it.
					InOutFilenameFormatString.InsertAt(FrameNumberIndex, TEXT("{file_dup}"));
				}
			}

			if (!InOutFilenameFormatString.Contains(TEXT("{file_dup}"), ESearchCase::IgnoreCase))
			{
				InOutFilenameFormatString.Append(TEXT("{file_dup}"));
			}
		}

		void RemoveFrameNumberFormatStrings(FString& InOutFilenameFormatString, const bool bIncludeShots)
		{
			// Strip {frame_number} related separators from their file name, otherwise it will create one output file per frame.
			InOutFilenameFormatString.ReplaceInline(TEXT("{frame_number}"), TEXT(""));
			InOutFilenameFormatString.ReplaceInline(TEXT("{frame_number_rel}"), TEXT(""));

			if (bIncludeShots)
			{
				InOutFilenameFormatString.ReplaceInline(TEXT("{frame_number_shot}"), TEXT(""));
				InOutFilenameFormatString.ReplaceInline(TEXT("{frame_number_shot_rel}"), TEXT(""));
			}
		}
	}
}
