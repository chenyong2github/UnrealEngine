// Copyright Epic Games, Inc. All Rights Reserved.

#include "MovieSceneToolHelpers.h"
#include "MovieSceneToolsModule.h"
#include "MovieScene.h"
#include "Layout/Margin.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Text/STextBlock.h"
#include "AssetData.h"
#include "Containers/ArrayView.h"
#include "ISequencer.h"
#include "Modules/ModuleManager.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Input/SComboBox.h"
#include "ScopedTransaction.h"
#include "EditorStyleSet.h"
#include "Compilation/MovieSceneCompiledDataManager.h"
#include "EditorDirectories.h"
#include "Sections/MovieSceneFloatSection.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "Tracks/MovieSceneCameraCutTrack.h"
#include "Sections/MovieScene3DTransformSection.h"
#include "Tracks/MovieScene3DTransformTrack.h"
#include "Sections/MovieSceneCinematicShotSection.h"
#include "LevelSequence.h"
#include "AssetRegistryModule.h"
#include "DesktopPlatformModule.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "MovieSceneTranslatorEDL.h"
#include "MessageLogModule.h"
#include "IMessageLogListing.h"
#include "FbxImporter.h"
#include "MatineeImportTools.h"
#include "MovieSceneToolsProjectSettings.h"
#include "MovieSceneToolsUserSettings.h"
#include "CineCameraActor.h"
#include "CineCameraComponent.h"
#include "Math/UnitConversion.h"
#include "PropertyEditorModule.h"
#include "IDetailsView.h"
#include "Widgets/Input/SButton.h"
#include "Editor.h"
#include "LevelEditorViewport.h"
#include "AssetToolsModule.h"
#include "Camera/CameraAnim.h"
#include "Matinee/InterpGroup.h"
#include "Matinee/InterpGroupInst.h"
#include "Matinee/InterpTrackMove.h"
#include "Matinee/InterpTrackMoveAxis.h"
#include "Matinee/InterpTrackInstMove.h"
#include "Channels/MovieSceneChannelProxy.h"
#include "Evaluation/MovieSceneEvaluationTrack.h"
#include "Evaluation/MovieSceneEvaluationTemplateInstance.h"
#include "IMovieScenePlayer.h"
#include "Tracks/MovieSceneCinematicShotTrack.h"
#include "Tracks/MovieSceneCameraCutTrack.h"
#include "Sections/MovieSceneCameraCutSection.h"
#include "Engine/LevelStreaming.h"
#include "FbxExporter.h"
#include "Serialization/ObjectWriter.h"
#include "Serialization/ObjectReader.h"
#include "AnimationRecorder.h"
#include "Components/SkeletalMeshComponent.h"
#include "ILiveLinkClient.h"
#include "LiveLinkPresetTypes.h"
#include "LiveLinkSourceSettings.h"
#include "Features/IModularFeatures.h"
#include "Tracks/MovieSceneSpawnTrack.h"
#include "Sections/MovieSceneSpawnSection.h"
#include "Exporters/AnimSeqExportOption.h"
#include "Widgets/Input/NumericTypeInterface.h"
#include "FrameNumberDetailsCustomization.h"
#include "PropertyEditorDelegates.h"
#include "INodeAndChannelMappings.h"
#include "Channels/MovieSceneIntegerChannel.h"
#include "Channels/MovieSceneByteChannel.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

/* FSkelMeshRecorder
 ***********/

void FSkelMeshRecorderState::Init(USkeletalMeshComponent* InComponent)
{
	SkelComp = InComponent;

	if (InComponent)
	{
		CachedSkelCompForcedLodModel = InComponent->GetForcedLOD();
		InComponent->SetForcedLOD(1);

		// turn off URO and make sure we always update even if out of view
		bCachedEnableUpdateRateOptimizations = InComponent->bEnableUpdateRateOptimizations;
		CachedVisibilityBasedAnimTickOption = InComponent->VisibilityBasedAnimTickOption;

		InComponent->bEnableUpdateRateOptimizations = false;
		InComponent->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	}
}

void FSkelMeshRecorderState::FinishRecording()
{
	if (SkelComp.IsValid())
	{
		// restore force lod setting
		SkelComp->SetForcedLOD(CachedSkelCompForcedLodModel);

		// restore update flags
		SkelComp->bEnableUpdateRateOptimizations = bCachedEnableUpdateRateOptimizations;
		SkelComp->VisibilityBasedAnimTickOption = CachedVisibilityBasedAnimTickOption;
	}
}


/* MovieSceneToolHelpers
 *****************************************************************************/

void MovieSceneToolHelpers::TrimSection(const TSet<TWeakObjectPtr<UMovieSceneSection>>& Sections, FQualifiedFrameTime Time, bool bTrimLeft, bool bDeleteKeys)
{
	for (auto Section : Sections)
	{
		if (Section.IsValid())
		{
			Section->TrimSection(Time, bTrimLeft, bDeleteKeys);
		}
	}
}


void MovieSceneToolHelpers::TrimOrExtendSection(UMovieSceneTrack* Track, TOptional<int32> SpecifiedRowIndex, FQualifiedFrameTime Time, bool bTrimOrExtendLeft, bool bDeleteKeys)
{
	Track->Modify();

	int32 StartRowIndex = SpecifiedRowIndex.IsSet() ? SpecifiedRowIndex.GetValue() : 0;
	int32 EndRowIndex = SpecifiedRowIndex.IsSet() ? SpecifiedRowIndex.GetValue() : Track->GetMaxRowIndex();

	for (int32 RowIndex = StartRowIndex; RowIndex <= EndRowIndex; ++RowIndex)
	{
		// First, trim all intersecting sections
		bool bAnyIntersects = false;
		for (UMovieSceneSection* Section : Track->GetAllSections())
		{
			if (Section->GetRowIndex() == RowIndex && Section->HasStartFrame() && Section->HasEndFrame() && Section->GetRange().Contains(Time.Time.GetFrame()))
			{
				Section->TrimSection(Time, bTrimOrExtendLeft, bDeleteKeys);
				bAnyIntersects = true;
			}
		}

		// If there aren't any intersects, extend the closest start/end
		if (!bAnyIntersects)
		{
			UMovieSceneSection* ClosestSection = nullptr;
			TOptional<FFrameNumber> MinDiff;

			for (UMovieSceneSection* Section : Track->GetAllSections())
			{
				if (Section->GetRowIndex() == RowIndex)
				{
					if (bTrimOrExtendLeft)
					{
						if (Section->HasStartFrame())
						{
							FFrameNumber StartFrame = Section->GetInclusiveStartFrame();
							if (StartFrame > Time.Time.GetFrame())
							{
								FFrameNumber Diff = StartFrame - Time.Time.GetFrame();
								if (!MinDiff.IsSet() || Diff < MinDiff.GetValue())
								{
									ClosestSection = Section;
									MinDiff = Diff;
								}
							}
						}
					}
					else
					{
						if (Section->HasEndFrame())
						{
							FFrameNumber EndFrame = Section->GetExclusiveEndFrame();
							if (EndFrame < Time.Time.GetFrame())
							{
								FFrameNumber Diff = Time.Time.GetFrame() - EndFrame;
								if (!MinDiff.IsSet() || Diff < MinDiff.GetValue())
								{
									ClosestSection = Section;
									MinDiff = Diff;
								}
							}
						}
					}
				}
			}

			if (ClosestSection)
			{
				ClosestSection->Modify();
				if (bTrimOrExtendLeft)
				{
					ClosestSection->SetStartFrame(Time.Time.GetFrame());
				}
				else
				{
					ClosestSection->SetEndFrame(Time.Time.GetFrame());
				}
			}
		}
	}
}


void MovieSceneToolHelpers::SplitSection(const TSet<TWeakObjectPtr<UMovieSceneSection>>& Sections, FQualifiedFrameTime Time, bool bDeleteKeys)
{
	for (auto Section : Sections)
	{
		if (Section.IsValid())
		{
			Section->SplitSection(Time, bDeleteKeys);
		}
	}
}

bool MovieSceneToolHelpers::ParseShotName(const FString& ShotName, FString& ShotPrefix, uint32& ShotNumber, uint32& TakeNumber)
{
	// Parse a shot name
	// 
	// sht010:
	//  ShotPrefix = sht
	//  ShotNumber = 10
	//  TakeNumber = 1 (default)
	// 
	// sp020_002
	//  ShotPrefix = sp
	//  ShotNumber = 20
	//  TakeNumber = 2
	//
	const UMovieSceneToolsProjectSettings* ProjectSettings = GetDefault<UMovieSceneToolsProjectSettings>();

	uint32 FirstShotNumberIndex = INDEX_NONE;
	uint32 LastShotNumberIndex = INDEX_NONE;
	bool bInShotNumber = false;

	uint32 FirstTakeNumberIndex = INDEX_NONE;
	uint32 LastTakeNumberIndex = INDEX_NONE;
	bool bInTakeNumber = false;

	bool bFoundTakeSeparator = false;
	TOptional<uint32> ParsedTakeNumber;
	TakeNumber = ProjectSettings->FirstTakeNumber;

	for (int32 CharIndex = 0; CharIndex < ShotName.Len(); ++CharIndex)
	{
		if (FChar::IsDigit(ShotName[CharIndex]))
		{
			// Find shot number indices
			if (FirstShotNumberIndex == INDEX_NONE)
			{
				bInShotNumber = true;
				FirstShotNumberIndex = CharIndex;
			}
			if (bInShotNumber)
			{
				LastShotNumberIndex = CharIndex;
			}

			if (FirstShotNumberIndex != INDEX_NONE && LastShotNumberIndex != INDEX_NONE)
			{
				if (bFoundTakeSeparator)
				{
					// Find take number indices
					if (FirstTakeNumberIndex == INDEX_NONE)
					{
						bInTakeNumber = true;
						FirstTakeNumberIndex = CharIndex;
					}
					if (bInTakeNumber)
					{
						LastTakeNumberIndex = CharIndex;
					}
				}
			}
		}

		if (FirstShotNumberIndex != INDEX_NONE && LastShotNumberIndex != INDEX_NONE)
		{
			if (ShotName[CharIndex] == ProjectSettings->TakeSeparator[0])
			{
				bFoundTakeSeparator = true;
			}
		}
	}

	if (FirstShotNumberIndex != INDEX_NONE)
	{
		ShotPrefix = ShotName.Left(FirstShotNumberIndex);
		ShotNumber = FCString::Atoi(*ShotName.Mid(FirstShotNumberIndex, LastShotNumberIndex-FirstShotNumberIndex+1));
	}

	if (FirstTakeNumberIndex != INDEX_NONE)
	{
		FString TakeStr = ShotName.Mid(FirstTakeNumberIndex, LastTakeNumberIndex-FirstTakeNumberIndex+1);
		if (TakeStr.IsNumeric())
		{
			ParsedTakeNumber = FCString::Atoi(*TakeStr);
		}
	}

	// If take number wasn't found, search backwards to find the first take separator and assume [shot prefix]_[take number]
	//
	if (!ParsedTakeNumber.IsSet())
	{
		int32 LastSlashPos = ShotName.Find(ProjectSettings->TakeSeparator, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
		if (LastSlashPos != INDEX_NONE)
		{
			ShotPrefix = ShotName.Left(LastSlashPos);
			ShotNumber = INDEX_NONE; // Nullify the shot number since we only have a shot prefix
			TakeNumber = FCString::Atoi(*ShotName.RightChop(LastSlashPos+1));
			return true;
		}
	}

	if (ParsedTakeNumber.IsSet())
	{
		TakeNumber = ParsedTakeNumber.GetValue();
	}

	return FirstShotNumberIndex != INDEX_NONE;
}


FString MovieSceneToolHelpers::ComposeShotName(const FString& ShotPrefix, uint32 ShotNumber, uint32 TakeNumber)
{
	const UMovieSceneToolsProjectSettings* ProjectSettings = GetDefault<UMovieSceneToolsProjectSettings>();

	FString ShotName = ShotPrefix;

	if (ShotNumber != INDEX_NONE)
	{
		ShotName += FString::Printf(TEXT("%0*d"), ProjectSettings->ShotNumDigits, ShotNumber);
	}

	if (TakeNumber != INDEX_NONE)
	{
		FString TakeFormat = TEXT("%0") + FString::Printf(TEXT("%d"), ProjectSettings->TakeNumDigits) + TEXT("d");
		
		ShotName += ProjectSettings->TakeSeparator;
		ShotName += FString::Printf(TEXT("%0*d"), ProjectSettings->TakeNumDigits, TakeNumber);
	}
	return ShotName;
}

bool IsPackageNameUnique(const TArray<FAssetData>& ObjectList, FString& NewPackageName)
{
	for (auto AssetObject : ObjectList)
	{
		if (AssetObject.PackageName.ToString() == NewPackageName)
		{
			return false;
		}
	}
	return true;
}

FString MovieSceneToolHelpers::GenerateNewShotPath(UMovieScene* SequenceMovieScene, FString& NewShotName)
{
	const UMovieSceneToolsProjectSettings* ProjectSettings = GetDefault<UMovieSceneToolsProjectSettings>();

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

	TArray<FAssetData> ObjectList;
	AssetRegistryModule.Get().GetAssetsByClass(ULevelSequence::StaticClass()->GetFName(), ObjectList);

	UObject* SequenceAsset = SequenceMovieScene->GetOuter();
	UPackage* SequencePackage = SequenceAsset->GetOutermost();
	FString SequencePackageName = SequencePackage->GetName(); // ie. /Game/cine/max/master
	int32 LastSlashPos = SequencePackageName.Find(TEXT("/"), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
	FString SequencePath = SequencePackageName.Left(LastSlashPos);

	FString NewShotPrefix;
	uint32 NewShotNumber = INDEX_NONE;
	uint32 NewTakeNumber = INDEX_NONE;
	ParseShotName(NewShotName, NewShotPrefix, NewShotNumber, NewTakeNumber);

	FString NewShotDirectory = ComposeShotName(NewShotPrefix, NewShotNumber, INDEX_NONE);
	FString NewShotPath = SequencePath;

	FString ShotDirectory = ProjectSettings->ShotDirectory;
	if (!ShotDirectory.IsEmpty())
	{
		NewShotPath /= ShotDirectory;
	}
	NewShotPath /= NewShotDirectory; // put this in the shot directory, ie. /Game/cine/max/shots/shot0010

	// Make sure this shot path is unique
	FString NewPackageName = NewShotPath;
	NewPackageName /= NewShotName; // ie. /Game/cine/max/shots/shot0010/shot0010_001
	if (!IsPackageNameUnique(ObjectList, NewPackageName))
	{
		while (1)
		{
			NewShotNumber += ProjectSettings->ShotIncrement;
			NewShotName = ComposeShotName(NewShotPrefix, NewShotNumber, NewTakeNumber);
			NewShotDirectory = ComposeShotName(NewShotPrefix, NewShotNumber, INDEX_NONE);
			NewShotPath = SequencePath;
			if (!ShotDirectory.IsEmpty())
			{
				NewShotPath /= ShotDirectory;
			}
			NewShotPath /= NewShotDirectory;

			NewPackageName = NewShotPath;
			NewPackageName /= NewShotName;
			if (IsPackageNameUnique(ObjectList, NewPackageName))
			{
				break;
			}
		}
	}

	return NewShotPath;
}


FString MovieSceneToolHelpers::GenerateNewShotName(const TArray<UMovieSceneSection*>& AllSections, FFrameNumber Time)
{
	const UMovieSceneToolsProjectSettings* ProjectSettings = GetDefault<UMovieSceneToolsProjectSettings>();

	UMovieSceneCinematicShotSection* BeforeShot = nullptr;
	UMovieSceneCinematicShotSection* NextShot = nullptr;

	FFrameNumber MinEndDiff = TNumericLimits<int32>::Max();
	FFrameNumber MinStartDiff = TNumericLimits<int32>::Max();

	for (auto Section : AllSections)
	{
		if (Section->HasEndFrame() && Section->GetExclusiveEndFrame() >= Time)
		{
			FFrameNumber EndDiff = Section->GetExclusiveEndFrame() - Time;
			if (MinEndDiff > EndDiff)
			{
				MinEndDiff = EndDiff;
				BeforeShot = Cast<UMovieSceneCinematicShotSection>(Section);
			}
		}
		if (Section->HasStartFrame() && Section->GetInclusiveStartFrame() <= Time)
		{
			FFrameNumber StartDiff = Time - Section->GetInclusiveStartFrame();
			if (MinStartDiff > StartDiff)
			{
				MinStartDiff = StartDiff;
				NextShot = Cast<UMovieSceneCinematicShotSection>(Section);
			}
		}
	}
	
	// There aren't any shots, let's create the first shot name
	if (BeforeShot == nullptr || NextShot == nullptr)
	{
		// Default case
	}
	// This is the last shot
	else if (BeforeShot == NextShot)
	{
		FString NextShotPrefix = ProjectSettings->ShotPrefix;
		uint32 NextShotNumber = ProjectSettings->FirstShotNumber;
		uint32 NextTakeNumber = ProjectSettings->FirstTakeNumber;

		if (ParseShotName(NextShot->GetShotDisplayName(), NextShotPrefix, NextShotNumber, NextTakeNumber))
		{
			uint32 NewShotNumber = NextShotNumber + ProjectSettings->ShotIncrement;
			return ComposeShotName(NextShotPrefix, NewShotNumber, ProjectSettings->FirstTakeNumber);
		}
	}
	// This is in between two shots
	else 
	{
		FString BeforeShotPrefix = ProjectSettings->ShotPrefix;
		uint32 BeforeShotNumber = ProjectSettings->FirstShotNumber;
		uint32 BeforeTakeNumber = ProjectSettings->FirstTakeNumber;

		FString NextShotPrefix = ProjectSettings->ShotPrefix;
		uint32 NextShotNumber = ProjectSettings->FirstShotNumber;
		uint32 NextTakeNumber = ProjectSettings->FirstTakeNumber;

		if (ParseShotName(BeforeShot->GetShotDisplayName(), BeforeShotPrefix, BeforeShotNumber, BeforeTakeNumber) &&
			ParseShotName(NextShot->GetShotDisplayName(), NextShotPrefix, NextShotNumber, NextTakeNumber))
		{
			if (BeforeShotNumber < NextShotNumber)
			{
				uint32 NewShotNumber = BeforeShotNumber + ( (NextShotNumber - BeforeShotNumber) / 2); // what if we can't find one? or conflicts with another?
				return ComposeShotName(BeforeShotPrefix, NewShotNumber, ProjectSettings->FirstTakeNumber);
			}
		}
	}

	// Default case
	return ComposeShotName(ProjectSettings->ShotPrefix, ProjectSettings->FirstShotNumber, ProjectSettings->FirstTakeNumber);
}

void MovieSceneToolHelpers::GatherTakes(const UMovieSceneSection* Section, TArray<FAssetData>& AssetData, uint32& OutCurrentTakeNumber)
{
	const UMovieSceneSubSection* SubSection = Cast<const UMovieSceneSubSection>(Section);
	
	if (SubSection->GetSequence() == nullptr)
	{
		return;
	}

	if (FMovieSceneToolsModule::Get().GatherTakes(Section, AssetData, OutCurrentTakeNumber))
	{
		return;
	}

	FAssetData ShotData(SubSection->GetSequence()->GetOuter());

	FString ShotPackagePath = ShotData.PackagePath.ToString();

	FString ShotPrefix;
	uint32 ShotNumber = INDEX_NONE;
	OutCurrentTakeNumber = INDEX_NONE;

	FString SubSectionName = SubSection->GetSequence()->GetName();
	if (SubSection->IsA<UMovieSceneCinematicShotSection>())
	{
		const UMovieSceneCinematicShotSection* ShotSection = Cast<UMovieSceneCinematicShotSection>(SubSection);
		SubSectionName = ShotSection->GetShotDisplayName();
	}

	if (ParseShotName(SubSectionName, ShotPrefix, ShotNumber, OutCurrentTakeNumber))
	{
		// Gather up all level sequence assets
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		TArray<FAssetData> ObjectList;
		AssetRegistryModule.Get().GetAssetsByClass(ULevelSequence::StaticClass()->GetFName(), ObjectList);

		for (auto AssetObject : ObjectList)
		{
			FString AssetPackagePath = AssetObject.PackagePath.ToString();

			if (AssetPackagePath == ShotPackagePath)
			{
				FString AssetShotPrefix;
				uint32 AssetShotNumber = INDEX_NONE;
				uint32 AssetTakeNumber = INDEX_NONE;

				if (ParseShotName(AssetObject.AssetName.ToString(), AssetShotPrefix, AssetShotNumber, AssetTakeNumber))
				{
					if (AssetShotPrefix == ShotPrefix && AssetShotNumber == ShotNumber)
					{
						AssetData.Add(AssetObject);
					}
				}
			}
		}
	}
}

bool MovieSceneToolHelpers::GetTakeNumber(const UMovieSceneSection* Section, FAssetData AssetData, uint32& OutTakeNumber)
{
	if (FMovieSceneToolsModule::Get().GetTakeNumber(Section, AssetData, OutTakeNumber))
	{
		return true;
	}

	const UMovieSceneSubSection* SubSection = Cast<const UMovieSceneSubSection>(Section);

	FAssetData ShotData(SubSection->GetSequence()->GetOuter());

	FString ShotPackagePath = ShotData.PackagePath.ToString();
	int32 ShotLastSlashPos = INDEX_NONE;
	ShotPackagePath.FindLastChar(TCHAR('/'), ShotLastSlashPos);
	ShotPackagePath.LeftInline(ShotLastSlashPos, false);

	FString ShotPrefix;
	uint32 ShotNumber = INDEX_NONE;
	uint32 TakeNumberDummy = INDEX_NONE;

	FString SubSectionName = SubSection->GetSequence()->GetName();
	if (SubSection->IsA<UMovieSceneCinematicShotSection>())
	{
		const UMovieSceneCinematicShotSection* ShotSection = Cast<UMovieSceneCinematicShotSection>(SubSection);
		SubSectionName = ShotSection->GetShotDisplayName();
	}

	if (ParseShotName(SubSectionName, ShotPrefix, ShotNumber, TakeNumberDummy))
	{
		// Gather up all level sequence assets
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		TArray<FAssetData> ObjectList;
		AssetRegistryModule.Get().GetAssetsByClass(ULevelSequence::StaticClass()->GetFName(), ObjectList);

		for (auto AssetObject : ObjectList)
		{
			if (AssetObject == AssetData)
			{
				FString AssetPackagePath = AssetObject.PackagePath.ToString();
				int32 AssetLastSlashPos = INDEX_NONE;
				AssetPackagePath.FindLastChar(TCHAR('/'), AssetLastSlashPos);
				AssetPackagePath.LeftInline(AssetLastSlashPos, false);

				if (AssetPackagePath == ShotPackagePath)
				{
					FString AssetShotPrefix;
					uint32 AssetShotNumber = INDEX_NONE;
					uint32 AssetTakeNumber = INDEX_NONE;

					if (ParseShotName(AssetObject.AssetName.ToString(), AssetShotPrefix, AssetShotNumber, AssetTakeNumber))
					{
						if (AssetShotPrefix == ShotPrefix && AssetShotNumber == ShotNumber)
						{
							OutTakeNumber = AssetTakeNumber;
							return true;
						}
					}
				}
			}
		}
	}

	return false;
}

bool MovieSceneToolHelpers::SetTakeNumber(const UMovieSceneSection* Section, uint32 InTakeNumber)
{
	return FMovieSceneToolsModule::Get().SetTakeNumber(Section, InTakeNumber);
}

int32 MovieSceneToolHelpers::FindAvailableRowIndex(UMovieSceneTrack* InTrack, UMovieSceneSection* InSection, const TArray<UMovieSceneSection*>& SectionsToDisregard)
{
	for (int32 RowIndex = 0; RowIndex <= InTrack->GetMaxRowIndex(); ++RowIndex)
	{
		bool bFoundIntersect = false;
		for (UMovieSceneSection* Section : InTrack->GetAllSections())
		{
			if (SectionsToDisregard.Contains(Section))
			{
				continue;
			}
	
			if (!Section->HasStartFrame() || !Section->HasEndFrame() || !InSection->HasStartFrame() || !InSection->HasEndFrame())
			{
				bFoundIntersect = true;
				break;
			}

			if (Section != InSection && Section->GetRowIndex() == RowIndex && Section->GetRange().Overlaps(InSection->GetRange()))
			{
				bFoundIntersect = true;
				break;
			}
		}
		if (!bFoundIntersect)
		{
			return RowIndex;
		}
	}

	return InTrack->GetMaxRowIndex() + 1;
}


bool MovieSceneToolHelpers::OverlapsSection(UMovieSceneTrack* InTrack, UMovieSceneSection* InSection, const TArray<UMovieSceneSection*>& SectionsToDisregard)
{
	for (UMovieSceneSection* Section : InTrack->GetAllSections())
	{
		if (SectionsToDisregard.Contains(Section))
		{
			continue;
		}
	
		if (!Section->HasStartFrame() || !Section->HasEndFrame() || !InSection->HasStartFrame() || !InSection->HasEndFrame())
		{
			return true;
		}

		if (Section != InSection && Section->GetRange().Overlaps(InSection->GetRange()))
		{
			return true;
		}
	}

	return false;
}

TSharedRef<SWidget> MovieSceneToolHelpers::MakeEnumComboBox(const UEnum* InEnum, TAttribute<int32> InCurrentValue, SEnumComboBox::FOnEnumSelectionChanged InOnSelectionChanged)
{
	return SNew(SEnumComboBox, InEnum)
		.CurrentValue(InCurrentValue)
		.ButtonStyle(FEditorStyle::Get(), "FlatButton.Light")
		.ContentPadding(FMargin(2, 0))
		.Font(FEditorStyle::GetFontStyle("Sequencer.AnimationOutliner.RegularFont"))
		.OnEnumSelectionChanged(InOnSelectionChanged);
}

bool MovieSceneToolHelpers::ShowImportEDLDialog(UMovieScene* InMovieScene, FFrameRate InFrameRate, FString InOpenDirectory)
{
	TArray<FString> OpenFilenames;
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	bool bOpen = false;
	if (DesktopPlatform)
	{
		FString ExtensionStr;
		ExtensionStr += TEXT("CMX 3600 EDL (*.edl)|*.edl|");

		bOpen = DesktopPlatform->OpenFileDialog(
			FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
			NSLOCTEXT("MovieSceneToolHelpers", "ImportEDL", "Import EDL from...").ToString(), 
			InOpenDirectory,
			TEXT(""), 
			*ExtensionStr,
			EFileDialogFlags::None,
			OpenFilenames
			);
	}
	if (!bOpen)
	{
		return false;
	}

	if (!OpenFilenames.Num())
	{
		return false;
	}

	const FScopedTransaction Transaction( NSLOCTEXT( "MovieSceneTools", "ImportEDLTransaction", "Import EDL" ) );

	return MovieSceneTranslatorEDL::ImportEDL(InMovieScene, InFrameRate, OpenFilenames[0]);
}

bool MovieSceneToolHelpers::ShowExportEDLDialog(const UMovieScene* InMovieScene, FFrameRate InFrameRate, FString InSaveDirectory, int32 InHandleFrames, FString InMovieExtension)
{
	TArray<FString> SaveFilenames;
	FString SequenceName = InMovieScene->GetOuter()->GetName();

	// Pop open a dialog to request the location of the edl
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	bool bSave = false;
	if (DesktopPlatform)
	{
		FString ExtensionStr;
		ExtensionStr += TEXT("CMX 3600 EDL (*.edl)|*.edl|");
		ExtensionStr += TEXT("RV (*.rv)|*.rv|");

		bSave = DesktopPlatform->SaveFileDialog(
			FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
			NSLOCTEXT("MovieSceneTools", "ExportEDL", "Export EDL to...").ToString(), 
			InSaveDirectory,
			SequenceName + TEXT(".edl"), 
			*ExtensionStr,
			EFileDialogFlags::None,
			SaveFilenames
			);
	}
	if (!bSave)
	{
		return false;
	}

	if (!SaveFilenames.Num())
	{
		return false;
	}

	if (MovieSceneTranslatorEDL::ExportEDL(InMovieScene, InFrameRate, SaveFilenames[0], InHandleFrames, InMovieExtension))
	{
		const FString AbsoluteFilename = FPaths::ConvertRelativePathToFull(SaveFilenames[0]);
		const FString SaveDirectory = FPaths::GetPath(AbsoluteFilename);

		FNotificationInfo NotificationInfo(NSLOCTEXT("MovieSceneTools", "EDLExportFinished", "EDL Export finished"));
		NotificationInfo.ExpireDuration = 5.f;
		NotificationInfo.Hyperlink = FSimpleDelegate::CreateStatic( [](FString InDirectory) { FPlatformProcess::ExploreFolder( *InDirectory ); }, SaveDirectory);
		NotificationInfo.HyperlinkText = NSLOCTEXT("MovieSceneTools", "OpenEDLExportFolder", "Open EDL Export Folder...");
		FSlateNotificationManager::Get().AddNotification(NotificationInfo);
		
		return true;
	}

	return false;
}

bool MovieSceneToolHelpers::MovieSceneTranslatorImport(FMovieSceneImporter* InImporter, UMovieScene* InMovieScene, FFrameRate InFrameRate, FString InOpenDirectory)
{
	TArray<FString> OpenFilenames;
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	bool bOpen = false;
	if (DesktopPlatform)
	{
		FString FileTypeDescription = InImporter->GetFileTypeDescription().ToString();
		FString DialogTitle = InImporter->GetDialogTitle().ToString();

		bOpen = DesktopPlatform->OpenFileDialog(
			FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
			DialogTitle,
			InOpenDirectory,
			TEXT(""),
			FileTypeDescription,
			EFileDialogFlags::None,
			OpenFilenames
		);
	}

	if (!bOpen || !OpenFilenames.Num())
	{
		return false;
	}

	FScopedTransaction Transaction(InImporter->GetTransactionDescription());

	TSharedRef<FMovieSceneTranslatorContext> ImportContext(new FMovieSceneTranslatorContext);
	ImportContext->Init();

	bool bSuccess = InImporter->Import(InMovieScene, InFrameRate, OpenFilenames[0], ImportContext);

	// Display any messages in context
	MovieSceneTranslatorLogMessages(InImporter, ImportContext, true);

	// Roll back transaction when import fails.
	if (!bSuccess)
	{
		Transaction.Cancel();
	}

	return bSuccess;
}

bool MovieSceneToolHelpers::MovieSceneTranslatorExport(FMovieSceneExporter* InExporter, const UMovieScene* InMovieScene, const FMovieSceneCaptureSettings& Settings)
{
	if (InExporter == nullptr || InMovieScene == nullptr)
	{
		return false;
	}

	FString SaveDirectory = FPaths::ConvertRelativePathToFull(Settings.OutputDirectory.Path);
	int32 HandleFrames = Settings.HandleFrames;
	// @todo: generate filename based on filename format, currently outputs {shot}.avi
	FString FilenameFormat = Settings.OutputFormat;
	FFrameRate FrameRate = Settings.GetFrameRate();
	uint32 ResX = Settings.Resolution.ResX;
	uint32 ResY = Settings.Resolution.ResY;
	FString MovieExtension = Settings.MovieExtension;

	TArray<FString> SaveFilenames;
	FString SequenceName = InMovieScene->GetOuter()->GetName();

	// Pop open a dialog to request the location of the edl
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	bool bSave = false;
	if (DesktopPlatform)
	{
		FString FileTypeDescription = InExporter->GetFileTypeDescription().ToString();
		FString DialogTitle = InExporter->GetDialogTitle().ToString();
		FString FileExtension = InExporter->GetDefaultFileExtension().ToString();

		bSave = DesktopPlatform->SaveFileDialog(
			FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
			DialogTitle,
			SaveDirectory,
			SequenceName + TEXT(".") + FileExtension,
			FileTypeDescription,
			EFileDialogFlags::None,
			SaveFilenames
		);
	}

	if (!bSave || !SaveFilenames.Num())
	{
		return false;
	}

	TSharedRef<FMovieSceneTranslatorContext> ExportContext(new FMovieSceneTranslatorContext);
	ExportContext->Init();

	bool bSuccess = InExporter->Export(InMovieScene, FilenameFormat, FrameRate, ResX, ResY, HandleFrames, SaveFilenames[0], ExportContext, MovieExtension);
	
	// Display any messages in context
	MovieSceneTranslatorLogMessages(InExporter, ExportContext, true);

	if (bSuccess)
	{
		const FString AbsoluteFilename = FPaths::ConvertRelativePathToFull(SaveFilenames[0]);
		const FString ActualSaveDirectory = FPaths::GetPath(AbsoluteFilename);

		FNotificationInfo NotificationInfo(InExporter->GetNotificationExportFinished());
		NotificationInfo.ExpireDuration = 5.f;
		NotificationInfo.Hyperlink = FSimpleDelegate::CreateStatic([](FString InDirectory) { FPlatformProcess::ExploreFolder(*InDirectory); }, ActualSaveDirectory);
		NotificationInfo.HyperlinkText = InExporter->GetNotificationHyperlinkText();
		FSlateNotificationManager::Get().AddNotification(NotificationInfo);
	}

	return bSuccess;
}

void MovieSceneToolHelpers::MovieSceneTranslatorLogMessages(FMovieSceneTranslator *InTranslator, TSharedRef<FMovieSceneTranslatorContext> InContext, bool bDisplayMessages)
{
	if (InTranslator == nullptr || InContext->GetMessages().Num() == 0)
	{
		return;
	}
	
	// Clear any old messages after an import or export
	const FName LogTitle = InTranslator->GetMessageLogWindowTitle();
	FMessageLogModule& MessageLogModule = FModuleManager::LoadModuleChecked<FMessageLogModule>("MessageLog");
	TSharedRef<IMessageLogListing> LogListing = MessageLogModule.GetLogListing(LogTitle);
	LogListing->SetLabel(InTranslator->GetMessageLogLabel());
	LogListing->ClearMessages();

	for (TSharedRef<FTokenizedMessage> Message : InContext->GetMessages())
	{
		LogListing->AddMessage(Message);
	}

	if (bDisplayMessages)
	{
		MessageLogModule.OpenMessageLog(LogTitle);
	}
}

void MovieSceneToolHelpers::MovieSceneTranslatorLogOutput(FMovieSceneTranslator *InTranslator, TSharedRef<FMovieSceneTranslatorContext> InContext)
{
	if (InTranslator == nullptr || InContext->GetMessages().Num() == 0)
	{
		return;
	}

	for (TSharedRef<FTokenizedMessage> Message : InContext->GetMessages())
	{
		if (Message->GetSeverity() == EMessageSeverity::Error)
		{
			UE_LOG(LogMovieScene, Error, TEXT("%s"), *Message->ToText().ToString());
		}
		else if (Message->GetSeverity() == EMessageSeverity::Warning)
		{
			UE_LOG(LogMovieScene, Warning, TEXT("%s"), *Message->ToText().ToString());

		}
	}
}

static FGuid GetHandleToObject(UObject* InObject, UMovieSceneSequence* InSequence, IMovieScenePlayer* Player, FMovieSceneSequenceIDRef TemplateID)
{
	UMovieScene* MovieScene = InSequence->GetMovieScene();

	// Attempt to resolve the object through the movie scene instance first, 
	FGuid PropertyOwnerGuid = FGuid();
	if (InObject != nullptr && !MovieScene->IsReadOnly())
	{
		FGuid ObjectGuid = Player->FindObjectId(*InObject, TemplateID);
		if (ObjectGuid.IsValid())
		{
			// Check here for spawnable otherwise spawnables get recreated as possessables, which doesn't make sense
			FMovieSceneSpawnable* Spawnable = MovieScene->FindSpawnable(ObjectGuid);
			if (Spawnable)
			{
				PropertyOwnerGuid = ObjectGuid;
			}
			else
			{
				FMovieScenePossessable* Possessable = MovieScene->FindPossessable(ObjectGuid);
				if (Possessable)
				{
					PropertyOwnerGuid = ObjectGuid;
				}
			}
		}
	}

	if (PropertyOwnerGuid.IsValid())
	{
		return PropertyOwnerGuid;
	}

	// Otherwise, create a possessable for this object. Note this will handle creating the parent possessables if this is a component.
	PropertyOwnerGuid = InSequence->CreatePossessable(InObject);
	
	return PropertyOwnerGuid;
}

bool ImportFBXProperty(FString NodeName, FString AnimatedPropertyName, FGuid ObjectBinding, UnFbx::FFbxCurvesAPI& CurveAPI, UMovieSceneSequence* InSequence, IMovieScenePlayer* Player, FMovieSceneSequenceIDRef TemplateID)
{
	UMovieScene* MovieScene = InSequence->GetMovieScene();

	const int32 ChannelIndex = 0;
	const int32 CompositeIndex = 0;
	FRichCurve Source;
	const bool bNegative = false;
	CurveAPI.GetCurveDataForSequencer(NodeName, AnimatedPropertyName, ChannelIndex, CompositeIndex, Source, bNegative);

	// First, see if any of the custom importers can import this named property
	if (FMovieSceneToolsModule::Get().ImportAnimatedProperty(AnimatedPropertyName, Source, ObjectBinding, MovieScene))
	{
		return true;
	}

	const UMovieSceneToolsProjectSettings* ProjectSettings = GetDefault<UMovieSceneToolsProjectSettings>();
	const UMovieSceneUserImportFBXSettings* ImportFBXSettings = GetDefault<UMovieSceneUserImportFBXSettings>();

	TArrayView<TWeakObjectPtr<>> BoundObjects = Player->FindBoundObjects(ObjectBinding, TemplateID);

	for (auto FbxSetting : ProjectSettings->FbxSettings)
	{
		if (FCString::Strcmp(*FbxSetting.FbxPropertyName.ToUpper(), *AnimatedPropertyName.ToUpper()) != 0)
		{
			continue;
		}

		for (TWeakObjectPtr<>& WeakObject : BoundObjects)
		{
			UObject* FoundObject = WeakObject.Get();

			if (!FoundObject)
			{
				continue;
			}
			
			UObject* PropertyOwner = FoundObject;
			if (!FbxSetting.PropertyPath.ComponentName.IsEmpty())
			{
				PropertyOwner = FindObjectFast<UObject>(FoundObject, *FbxSetting.PropertyPath.ComponentName);
			}

			if (!PropertyOwner)
			{
				continue;
			}
		
			FGuid PropertyOwnerGuid = GetHandleToObject(PropertyOwner, InSequence, Player, TemplateID);
			if (!PropertyOwnerGuid.IsValid())
			{
				continue;
			}

			if (!PropertyOwnerGuid.IsValid())
			{
				continue;
			}

			UMovieSceneFloatTrack* FloatTrack = MovieScene->FindTrack<UMovieSceneFloatTrack>(PropertyOwnerGuid, *FbxSetting.PropertyPath.PropertyName);
			if (!FloatTrack)
			{
				MovieScene->Modify();
				FloatTrack = MovieScene->AddTrack<UMovieSceneFloatTrack>(PropertyOwnerGuid);
				FloatTrack->SetPropertyNameAndPath(*FbxSetting.PropertyPath.PropertyName, *FbxSetting.PropertyPath.PropertyName);
			}

			if (FloatTrack)
			{
				FloatTrack->Modify();
				FloatTrack->RemoveAllAnimationData();

				FFrameRate FrameRate = FloatTrack->GetTypedOuter<UMovieScene>()->GetTickResolution();

				bool bSectionAdded = false;
				UMovieSceneFloatSection* FloatSection = Cast<UMovieSceneFloatSection>(FloatTrack->FindOrAddSection(0, bSectionAdded));
				if (!FloatSection)
				{
					continue;
				}

				FloatSection->Modify();

				if (bSectionAdded)
				{
					FloatSection->SetRange(TRange<FFrameNumber>::All());
				}

				FMovieSceneFloatChannel* Channel = FloatSection->GetChannelProxy().GetChannel<FMovieSceneFloatChannel>(0);
				TMovieSceneChannelData<FMovieSceneFloatValue> ChannelData = Channel->GetData();

				ChannelData.Reset();
				double DecimalRate = FrameRate.AsDecimal();

				for (auto SourceIt = Source.GetKeyHandleIterator(); SourceIt; ++SourceIt)
				{
					FRichCurveKey &Key = Source.GetKey(*SourceIt);
					float ArriveTangent = Key.ArriveTangent;
					FKeyHandle PrevKeyHandle = Source.GetPreviousKey(*SourceIt);
					if (Source.IsKeyHandleValid(PrevKeyHandle))
					{
						FRichCurveKey &PrevKey = Source.GetKey(PrevKeyHandle);
						ArriveTangent = ArriveTangent / (Key.Time - PrevKey.Time);

					}
					float LeaveTangent = Key.LeaveTangent;
					FKeyHandle NextKeyHandle = Source.GetNextKey(*SourceIt);
					if (Source.IsKeyHandleValid(NextKeyHandle))
					{
						FRichCurveKey &NextKey = Source.GetKey(NextKeyHandle);
						LeaveTangent = LeaveTangent / (NextKey.Time - Key.Time);
					}

					FFrameNumber KeyTime = (Key.Time * FrameRate).RoundToFrame();
					FMatineeImportTools::SetOrAddKey(ChannelData, KeyTime, Key.Value, ArriveTangent, LeaveTangent,
						MovieSceneToolHelpers::RichCurveInterpolationToMatineeInterpolation(Key.InterpMode, Key.TangentMode), FrameRate, Key.TangentWeightMode,
						Key.ArriveTangentWeight, Key.LeaveTangentWeight);

				}

				Channel->AutoSetTangents();

				if (ImportFBXSettings->bReduceKeys)
				{
					FKeyDataOptimizationParams Params;
					Params.Tolerance = ImportFBXSettings->ReduceKeysTolerance;
					Params.DisplayRate = FrameRate;
					Params.bAutoSetInterpolation = true; //we use this to perform the AutoSetTangents after the keys are reduced.
					Channel->Optimize(Params);
				}

				return true;
			}
		}
	}
	return false;
}


void MovieSceneToolHelpers::LockCameraActorToViewport(const TSharedPtr<ISequencer>& Sequencer, ACameraActor* CameraActor)
{
	Sequencer->SetPerspectiveViewportCameraCutEnabled(false);

	// Lock the viewport to this camera
	if (CameraActor && CameraActor->GetLevel())
	{
		GCurrentLevelEditingViewportClient->SetCinematicActorLock(nullptr);
		GCurrentLevelEditingViewportClient->SetActorLock(CameraActor);
		GCurrentLevelEditingViewportClient->bLockedCameraView = true;
		GCurrentLevelEditingViewportClient->UpdateViewForLockedActor();
		GCurrentLevelEditingViewportClient->Invalidate();
	}
}

void MovieSceneToolHelpers::CreateCameraCutSectionForCamera(UMovieScene* OwnerMovieScene, FGuid CameraGuid, FFrameNumber FrameNumber)
{
	// If there's a cinematic shot track, no need to set this camera to a shot
	UMovieSceneTrack* CinematicShotTrack = OwnerMovieScene->FindMasterTrack(UMovieSceneCinematicShotTrack::StaticClass());
	if (CinematicShotTrack)
	{
		return;
	}

	UMovieSceneTrack* CameraCutTrack = OwnerMovieScene->GetCameraCutTrack();

	// If there's a camera cut track with at least one section, no need to change the section
	if (CameraCutTrack && CameraCutTrack->GetAllSections().Num() > 0)
	{
		return;
	}

	if (!CameraCutTrack)
	{
		CameraCutTrack = OwnerMovieScene->AddCameraCutTrack(UMovieSceneCameraCutTrack::StaticClass());
	}

	if (CameraCutTrack)
	{
		UMovieSceneSection* Section = MovieSceneHelpers::FindSectionAtTime(CameraCutTrack->GetAllSections(), FrameNumber);
		UMovieSceneCameraCutSection* CameraCutSection = Cast<UMovieSceneCameraCutSection>(Section);

		if (CameraCutSection)
		{
			CameraCutSection->Modify();
			CameraCutSection->SetCameraGuid(CameraGuid);
		}
		else
		{
			CameraCutTrack->Modify();

			UMovieSceneCameraCutSection* NewSection = Cast<UMovieSceneCameraCutSection>(CameraCutTrack->CreateNewSection());
			NewSection->SetRange(OwnerMovieScene->GetPlaybackRange());
			NewSection->SetCameraGuid(CameraGuid);
			CameraCutTrack->AddSection(*NewSection);
		}
	}
}

void ImportTransformChannel(const FRichCurve& Source, FMovieSceneFloatChannel* Dest, FFrameRate DestFrameRate, bool bNegateTangents, bool bClearChannel,FFrameNumber StartFrame = 0, bool bNegateValue = false)
{
	// If there are no keys, don't clear the existing channel
	if (!Source.GetNumKeys())
	{
		return;
	}

	TMovieSceneChannelData<FMovieSceneFloatValue> ChannelData = Dest->GetData();

	if (bClearChannel)
	{
		ChannelData.Reset();
	}
	for (auto SourceIt = Source.GetKeyHandleIterator(); SourceIt; ++SourceIt)
	{
		const FRichCurveKey Key = Source.GetKey(*SourceIt);
		float ArriveTangent = Key.ArriveTangent;
		FKeyHandle PrevKeyHandle = Source.GetPreviousKey(*SourceIt);
		if (Source.IsKeyHandleValid(PrevKeyHandle))
		{
			const FRichCurveKey PrevKey = Source.GetKey(PrevKeyHandle);
			ArriveTangent = ArriveTangent / (Key.Time - PrevKey.Time);

		}
		float LeaveTangent = Key.LeaveTangent;
		FKeyHandle NextKeyHandle = Source.GetNextKey(*SourceIt);
		if (Source.IsKeyHandleValid(NextKeyHandle))
		{
			const FRichCurveKey NextKey = Source.GetKey(NextKeyHandle);
			LeaveTangent = LeaveTangent / (NextKey.Time - Key.Time);
		}

		if (bNegateTangents)
		{
			ArriveTangent = -ArriveTangent;
			LeaveTangent = -LeaveTangent;
		}

		FFrameNumber KeyTime = (Key.Time * DestFrameRate).RoundToFrame();
		float Value = !bNegateValue ? Key.Value : -Key.Value;
		FMatineeImportTools::SetOrAddKey(ChannelData, KeyTime + StartFrame, Value, ArriveTangent, LeaveTangent,
			MovieSceneToolHelpers::RichCurveInterpolationToMatineeInterpolation(Key.InterpMode, Key.TangentMode), DestFrameRate, Key.TangentWeightMode,
			Key.ArriveTangentWeight, Key.LeaveTangentWeight);

	}

	Dest->AutoSetTangents();

	const UMovieSceneUserImportFBXSettings* ImportFBXSettings = GetDefault<UMovieSceneUserImportFBXSettings>();
	if (ImportFBXSettings->bReduceKeys)
	{
		FKeyDataOptimizationParams Params;
		Params.Tolerance = ImportFBXSettings->ReduceKeysTolerance;
		Params.DisplayRate = DestFrameRate;
		Dest->Optimize(Params);
	}
}

void ImportTransformChannelToBool(const FRichCurve& Source, FMovieSceneBoolChannel* Dest, FFrameRate DestFrameRate, bool bClearChannel, FFrameNumber StartFrame)
{
	// If there are no keys, don't clear the existing channel
	if (!Source.GetNumKeys())
	{
		return;
	}

	TMovieSceneChannelData<bool> ChannelData = Dest->GetData();

	if (bClearChannel)
	{
		ChannelData.Reset();
	}
	for (auto SourceIt = Source.GetKeyHandleIterator(); SourceIt; ++SourceIt)
	{
		const FRichCurveKey Key = Source.GetKey(*SourceIt);
		bool bValue = Key.Value != 0.0f ? true : false;

		FFrameNumber KeyTime = (Key.Time * DestFrameRate).RoundToFrame();

		KeyTime += StartFrame;
		if (ChannelData.FindKey(KeyTime) == INDEX_NONE)
		{
			ChannelData.AddKey(KeyTime, bValue);
		} //todo need to do a set here?
	}
}

void ImportTransformChannelToEnum(const FRichCurve& Source, FMovieSceneByteChannel* Dest, FFrameRate DestFrameRate, bool bClearChannel, FFrameNumber StartFrame)
{
	// If there are no keys, don't clear the existing channel
	if (!Source.GetNumKeys())
	{
		return;
	}

	TMovieSceneChannelData<uint8> ChannelData = Dest->GetData();

	if (bClearChannel)
	{
		ChannelData.Reset();
	}
	for (auto SourceIt = Source.GetKeyHandleIterator(); SourceIt; ++SourceIt)
	{
		const FRichCurveKey Key = Source.GetKey(*SourceIt);
		uint8 Value = (uint8)Key.Value;

		FFrameNumber KeyTime = (Key.Time * DestFrameRate).RoundToFrame();

		KeyTime += StartFrame;
		if (ChannelData.FindKey(KeyTime) == INDEX_NONE)
		{
			ChannelData.AddKey(KeyTime, Value);
		} //todo need to do a set here?
	}
}


void ImportTransformChannelToInteger(const FRichCurve& Source, FMovieSceneIntegerChannel* Dest, FFrameRate DestFrameRate, bool bClearChannel, FFrameNumber StartFrame)
{
	// If there are no keys, don't clear the existing channel
	if (!Source.GetNumKeys())
	{
		return;
	}

	TMovieSceneChannelData<int32> ChannelData = Dest->GetData();

	if (bClearChannel)
	{
		ChannelData.Reset();
	}
	for (auto SourceIt = Source.GetKeyHandleIterator(); SourceIt; ++SourceIt)
	{
		const FRichCurveKey Key = Source.GetKey(*SourceIt);
		int32 Value = (int32)Key.Value;

		FFrameNumber KeyTime = (Key.Time * DestFrameRate).RoundToFrame();

		KeyTime += StartFrame;
		if (ChannelData.FindKey(KeyTime) == INDEX_NONE)
		{
			ChannelData.AddKey(KeyTime, Value);
		} //todo need to do a set here?
	}
}

void SetChannelValue(FMovieSceneFloatChannel* Channel, FMovieSceneBoolChannel *BoolChannel, FMovieSceneByteChannel *EnumChannel, FMovieSceneIntegerChannel* IntegerChannel,
	FFrameRate FrameRate, FFrameNumber StartFrame,
	FControlRigChannelEnum ChannelEnum, UMovieSceneUserImportFBXControlRigSettings* ImportFBXControlRigSettings,
	FTransform& DefaultTransform,
	FRichCurve& TranslationX, FRichCurve& TranslationY, FRichCurve& TranslationZ,
	FRichCurve& EulerRotationX, FRichCurve& EulerRotationY, FRichCurve& EulerRotationZ,
	FRichCurve& ScaleX, FRichCurve& ScaleY, FRichCurve& ScaleZ)
{
	FVector Location = DefaultTransform.GetLocation(), Rotation = DefaultTransform.GetRotation().Euler(), Scale3D = DefaultTransform.GetScale3D();

	for (FControlToTransformMappings& Mapping: ImportFBXControlRigSettings->ControlChannelMappings)
	{
		if (ChannelEnum == Mapping.ControlChannel)
		{
			bool bNegate = Mapping.bNegate;
			if (Mapping.FBXChannel == FTransformChannelEnum::TranslateX)
			{
				if (ChannelEnum == FControlRigChannelEnum::Bool && BoolChannel)
				{
					bool bDefault = Location.X == 0.0 ? false : true;
					BoolChannel->SetDefault(bDefault);
					ImportTransformChannelToBool(TranslationX, BoolChannel, FrameRate, false, StartFrame);
				}
				else if (ChannelEnum == FControlRigChannelEnum::Enum && EnumChannel)
				{
					uint8 bDefault = (uint8)bNegate ? -Location.X : Location.X;
					EnumChannel->SetDefault(bDefault);
					ImportTransformChannelToEnum(TranslationX, EnumChannel, FrameRate, false, StartFrame);
				}
				else if (ChannelEnum == FControlRigChannelEnum::Integer && IntegerChannel)
				{
					int32 bDefault = (int32)bNegate ? -Location.X : Location.X;
					IntegerChannel->SetDefault(bDefault);
					ImportTransformChannelToInteger(TranslationX, IntegerChannel, FrameRate, false, StartFrame);
				}
				else if (Channel)
				{
					float Default = bNegate ? -Location.X : Location.X;
					Channel->SetDefault(Default);
					ImportTransformChannel(TranslationX, Channel, FrameRate, false, false, StartFrame, bNegate);
				}
			}
			else if (Mapping.FBXChannel == FTransformChannelEnum::TranslateY)
			{
				if (ChannelEnum == FControlRigChannelEnum::Bool && BoolChannel)
				{
					bool bDefault = Location.Y == 0.0 ? false : true;
					BoolChannel->SetDefault(bDefault);
					ImportTransformChannelToBool(TranslationY, BoolChannel, FrameRate, false, StartFrame);
				}
				else if (ChannelEnum == FControlRigChannelEnum::Enum && EnumChannel)
				{
					uint8 bDefault = (uint8)bNegate ? -Location.Y : Location.Y;
					EnumChannel->SetDefault(bDefault);
					ImportTransformChannelToEnum(TranslationY, EnumChannel, FrameRate, false, StartFrame);
				}
				else if (ChannelEnum == FControlRigChannelEnum::Integer && IntegerChannel)
				{
					int32 bDefault = (int32)bNegate ? -Location.Y : Location.Y;
					IntegerChannel->SetDefault(bDefault);
					ImportTransformChannelToInteger(TranslationY, IntegerChannel, FrameRate, false, StartFrame);
				}
				else if (Channel)
				{
					bNegate = !bNegate;
					float Default = bNegate ? -Location.Y : Location.Y;
					Channel->SetDefault(Default);
					ImportTransformChannel(TranslationY, Channel, FrameRate, false, false, StartFrame, bNegate);
				}
			}
			else if (Mapping.FBXChannel == FTransformChannelEnum::TranslateZ)
			{
				if (ChannelEnum == FControlRigChannelEnum::Bool && BoolChannel)
				{
					bool bDefault = Location.Z == 0.0 ? false : true;
					BoolChannel->SetDefault(bDefault);
					ImportTransformChannelToBool(TranslationZ, BoolChannel, FrameRate, false, StartFrame);
				}
				else if (ChannelEnum == FControlRigChannelEnum::Enum && EnumChannel)
				{
					uint8 bDefault = (uint8)bNegate ? -Location.Z : Location.Z;
					EnumChannel->SetDefault(bDefault);
					ImportTransformChannelToEnum(TranslationZ, EnumChannel, FrameRate, false, StartFrame);
				}
				else if (ChannelEnum == FControlRigChannelEnum::Integer && IntegerChannel)
				{
					int32 bDefault = (int32)bNegate ? -Location.Z : Location.Z;
					IntegerChannel->SetDefault(bDefault);
					ImportTransformChannelToInteger(TranslationZ, IntegerChannel, FrameRate, false, StartFrame);
				}
				else if (Channel)
				{
					float Default = bNegate ? -Location.Z : Location.Z;
					Channel->SetDefault(Default);
					ImportTransformChannel(TranslationZ, Channel, FrameRate, false, false, StartFrame, bNegate);
				}
			}
			else if (Mapping.FBXChannel == FTransformChannelEnum::RotateX)
			{
				if (ChannelEnum == FControlRigChannelEnum::Bool && BoolChannel)
				{
					bool bDefault = Rotation.X == 0.0 ? false : true;
					BoolChannel->SetDefault(bDefault);
					ImportTransformChannelToBool(EulerRotationX, BoolChannel, FrameRate, false, StartFrame);
				}
				else if (ChannelEnum == FControlRigChannelEnum::Enum && EnumChannel)
				{
					uint8 bDefault = (uint8)bNegate ? -Rotation.X : Rotation.X;
					EnumChannel->SetDefault(bDefault);
					ImportTransformChannelToEnum(EulerRotationX, EnumChannel, FrameRate, false, StartFrame);
				}
				else if (ChannelEnum == FControlRigChannelEnum::Integer && IntegerChannel)
				{
					int32 bDefault = (int32)bNegate ? -Rotation.X : Rotation.X;
					IntegerChannel->SetDefault(bDefault);
					ImportTransformChannelToInteger(EulerRotationX, IntegerChannel, FrameRate, false, StartFrame);
				}
				else if (Channel)
				{
					float Default = bNegate ? -Rotation.X : Rotation.X;
					Channel->SetDefault(Default);
					ImportTransformChannel(EulerRotationX, Channel, FrameRate, false, false, StartFrame, bNegate);
				}
			}
			else if (Mapping.FBXChannel == FTransformChannelEnum::RotateY)
			{
				if (ChannelEnum == FControlRigChannelEnum::Bool && BoolChannel)
				{
					bool bDefault = Rotation.Y == 0.0 ? false : true;
					BoolChannel->SetDefault(bDefault);
					ImportTransformChannelToBool(EulerRotationY, BoolChannel, FrameRate, false, StartFrame);
				}
				else if (ChannelEnum == FControlRigChannelEnum::Enum && EnumChannel)
				{
					uint8 bDefault = (uint8)bNegate ? -Rotation.Y : Rotation.Y;
					EnumChannel->SetDefault(bDefault);
					ImportTransformChannelToEnum(EulerRotationY, EnumChannel, FrameRate, false, StartFrame);
				}
				else if (ChannelEnum == FControlRigChannelEnum::Integer && IntegerChannel)
				{
					int32 bDefault = (int32)bNegate ? -Rotation.Y : Rotation.Y;
					IntegerChannel->SetDefault(bDefault);
					ImportTransformChannelToInteger(EulerRotationY, IntegerChannel, FrameRate, false, StartFrame);
				}
				else if (Channel)
				{
					float Default = bNegate ? -Rotation.Y : Rotation.Y;
					Channel->SetDefault(Default);
					ImportTransformChannel(EulerRotationY, Channel, FrameRate, false, false, StartFrame, bNegate);
				}
			}
			else if (Mapping.FBXChannel == FTransformChannelEnum::RotateZ)
			{
				if (ChannelEnum == FControlRigChannelEnum::Bool && BoolChannel)
				{
					bool bDefault = Rotation.Z == 0.0 ? false : true;
					BoolChannel->SetDefault(bDefault);
					ImportTransformChannelToBool(EulerRotationZ, BoolChannel, FrameRate, false, StartFrame);
				}
				else if (ChannelEnum == FControlRigChannelEnum::Enum && EnumChannel)
				{
					uint8 bDefault = (uint8)bNegate ? -Rotation.Z : Rotation.Z;
					EnumChannel->SetDefault(bDefault);
					ImportTransformChannelToEnum(EulerRotationZ, EnumChannel, FrameRate, false, StartFrame);
				}
				else if (ChannelEnum == FControlRigChannelEnum::Integer && IntegerChannel)
				{
					int32 bDefault = (int32)bNegate ? -Rotation.Z : Rotation.Z;
					IntegerChannel->SetDefault(bDefault);
					ImportTransformChannelToInteger(EulerRotationZ, IntegerChannel, FrameRate, false, StartFrame);
				}
				else if (Channel)
				{
					float Default = bNegate ? -Rotation.Z : Rotation.Z;
					Channel->SetDefault(Default);
					ImportTransformChannel(EulerRotationZ, Channel, FrameRate, false, false, StartFrame, bNegate);
				}
			}
			else if (Mapping.FBXChannel == FTransformChannelEnum::ScaleX)
			{
				if (ChannelEnum == FControlRigChannelEnum::Bool && BoolChannel)
				{
					bool bDefault = Scale3D.X == 0.0 ? false : true;
					BoolChannel->SetDefault(bDefault);
					ImportTransformChannelToBool(ScaleX, BoolChannel, FrameRate, false, StartFrame);
				}
				else if (ChannelEnum == FControlRigChannelEnum::Enum && EnumChannel)
				{
					uint8 bDefault = (uint8)bNegate ? -Scale3D.X : Scale3D.X;
					EnumChannel->SetDefault(bDefault);
					ImportTransformChannelToEnum(ScaleX, EnumChannel, FrameRate, false, StartFrame);
				}
				else if (ChannelEnum == FControlRigChannelEnum::Integer && IntegerChannel)
				{
					int32 bDefault = (int32)bNegate ? -Scale3D.X : Scale3D.X;
					IntegerChannel->SetDefault(bDefault);
					ImportTransformChannelToInteger(ScaleX, IntegerChannel, FrameRate, false, StartFrame);
				}
				else if (Channel)
				{
					float Default = bNegate ? -Scale3D.X : Scale3D.X;
					Channel->SetDefault(Default);
					ImportTransformChannel(ScaleX, Channel, FrameRate, false, false, StartFrame, bNegate);
				}
			}
			else if (Mapping.FBXChannel == FTransformChannelEnum::ScaleY)
			{
				if (ChannelEnum == FControlRigChannelEnum::Bool && BoolChannel)
				{
					bool bDefault = Scale3D.Y == 0.0 ? false : true;
					BoolChannel->SetDefault(bDefault);
					ImportTransformChannelToBool(ScaleY, BoolChannel, FrameRate, false, StartFrame);
				}
				else if (ChannelEnum == FControlRigChannelEnum::Enum && EnumChannel)
				{
					uint8 bDefault = (uint8)bNegate ? -Scale3D.Y : Scale3D.Y;
					EnumChannel->SetDefault(bDefault);
					ImportTransformChannelToEnum(ScaleY, EnumChannel, FrameRate, false, StartFrame);
				}
				else if (ChannelEnum == FControlRigChannelEnum::Integer && IntegerChannel)
				{
					int32 bDefault = (int32)bNegate ? -Scale3D.Y : Scale3D.Y;
					IntegerChannel->SetDefault(bDefault);
					ImportTransformChannelToInteger(ScaleY, IntegerChannel, FrameRate, false, StartFrame);
				}
				else if (Channel)
				{
					float Default = bNegate ? -Scale3D.Y : Scale3D.Y;
					Channel->SetDefault(Default);
					ImportTransformChannel(ScaleY, Channel, FrameRate, false, false, StartFrame, bNegate);
				}
			}
			else if (Mapping.FBXChannel == FTransformChannelEnum::ScaleZ)
			{
				if (ChannelEnum == FControlRigChannelEnum::Bool && BoolChannel)
				{
					bool bDefault = Scale3D.Z == 0.0 ? false : true;
					BoolChannel->SetDefault(bDefault);
					ImportTransformChannelToBool(ScaleZ, BoolChannel, FrameRate, false, StartFrame);
				}
				else if (ChannelEnum == FControlRigChannelEnum::Enum && EnumChannel)
				{
					uint8 bDefault = (uint8)bNegate ? -Scale3D.Z : Scale3D.Z;
					EnumChannel->SetDefault(bDefault);
					ImportTransformChannelToEnum(ScaleZ, EnumChannel, FrameRate, false, StartFrame);
				}
				else if (ChannelEnum == FControlRigChannelEnum::Integer && IntegerChannel)
				{
					int32 bDefault = (int32)bNegate ? -Scale3D.Z : Scale3D.Z;
					IntegerChannel->SetDefault(bDefault);
					ImportTransformChannelToInteger(ScaleZ, IntegerChannel, FrameRate, false, StartFrame);
				}
				else if (Channel)
				{
					float Default = bNegate ? -Scale3D.Z : Scale3D.Z;
					Channel->SetDefault(Default);
					ImportTransformChannel(ScaleZ, Channel, FrameRate, false, false, StartFrame, bNegate);
				}
			}
		}
	}

	if (ChannelEnum == FControlRigChannelEnum::Bool && BoolChannel)
	{
		bool bDefault = Location.X == 0.0 ? false : true;
		BoolChannel->SetDefault(bDefault);
		ImportTransformChannelToBool(TranslationX, BoolChannel, FrameRate, false, StartFrame);

	}
	else if (ChannelEnum == FControlRigChannelEnum::Enum && EnumChannel)
	{
		bool bNegate = false;
		uint8 Default = (uint8) Location.X;
		EnumChannel->SetDefault(Default);
		ImportTransformChannelToEnum(TranslationX, EnumChannel, FrameRate, false, StartFrame);
	}
	else if (ChannelEnum == FControlRigChannelEnum::Integer && IntegerChannel)
	{
		bool bNegate = false;
		int32  Default = (int32 )Location.X;
		IntegerChannel->SetDefault(Default);
		ImportTransformChannelToInteger(TranslationX, IntegerChannel, FrameRate, false, StartFrame);
	}
	else if (ChannelEnum == FControlRigChannelEnum::Float)
	{
		bool bNegate = false;
		float Default = Location.X;
		Channel->SetDefault(Default);
		ImportTransformChannel(TranslationX, Channel, FrameRate, false,false, StartFrame, bNegate);
	}
	else if (ChannelEnum == FControlRigChannelEnum::Vector2DX)
	{
		bool bNegate = false;
		float Default = Location.X;
		Channel->SetDefault(Default);
		ImportTransformChannel(TranslationX, Channel, FrameRate, false, false,StartFrame, bNegate);
	}
	else if (ChannelEnum == FControlRigChannelEnum::Vector2DY)
	{
		bool bNegate = true;
		float Default = -Location.Y;
		Channel->SetDefault(Default);
		ImportTransformChannel(TranslationY, Channel, FrameRate, false, false,StartFrame, bNegate);
	}
	else if (ChannelEnum == FControlRigChannelEnum::PositionX)
	{
		bool bNegate = false;
		float Default = Location.X;
		Channel->SetDefault(Default);
		ImportTransformChannel(TranslationX, Channel, FrameRate, false, false, StartFrame, bNegate);
	}
	else if (ChannelEnum == FControlRigChannelEnum::PositionY)
	{
		bool bNegate = true;
		float Default = -Location.Y;
		Channel->SetDefault(Default);
		ImportTransformChannel(TranslationX, Channel, FrameRate, false, false, StartFrame, bNegate);
	}
	else if (ChannelEnum == FControlRigChannelEnum::PositionZ)
	{
		bool bNegate = false;
		float Default = Location.Z;
		Channel->SetDefault(Default);
		ImportTransformChannel(TranslationZ, Channel, FrameRate, false, false, StartFrame, bNegate);
	}
	else if (ChannelEnum == FControlRigChannelEnum::RotatorX)
	{
		bool bNegate = false;
		float Default = Rotation.X;
		Channel->SetDefault(Default);
		ImportTransformChannel(EulerRotationX, Channel, FrameRate, false, false, StartFrame, bNegate);
	}
	else if (ChannelEnum == FControlRigChannelEnum::RotatorY)
	{
		bool bNegate = false;
		float Default = Rotation.Y;
		Channel->SetDefault(Default);		
		ImportTransformChannel(EulerRotationY, Channel, FrameRate, false, false, StartFrame, bNegate);
	}
	else if (ChannelEnum == FControlRigChannelEnum::RotatorZ)
	{
		bool bNegate = false;
		float Default =  Rotation.Z;
		Channel->SetDefault(Default);
		ImportTransformChannel(EulerRotationZ, Channel, FrameRate, false, false, StartFrame, bNegate);
	}
	else if (ChannelEnum == FControlRigChannelEnum::ScaleX)
	{
		bool bNegate = false;
		float Default = Scale3D.X;
		Channel->SetDefault(Default); 
		ImportTransformChannel(ScaleX, Channel, FrameRate, false, false, StartFrame, bNegate);
	}
	else if (ChannelEnum == FControlRigChannelEnum::ScaleY)
	{
		bool bNegate = false;
		float Default = Scale3D.Y;
		Channel->SetDefault(Default); 
		ImportTransformChannel(ScaleY, Channel, FrameRate, false, false, StartFrame, bNegate);
	}
	else if (ChannelEnum == FControlRigChannelEnum::ScaleZ)
	{
		bool bNegate = false;
		float Default = Scale3D.Z;
		Channel->SetDefault(Default);	
		ImportTransformChannel(ScaleZ, Channel, FrameRate, false,false,  StartFrame, bNegate);
	}

}
//if one channel goes to Y
//if two channel go to X Y
//if three channel to to x y z
// if 9 due full
static bool ImportFBXTransformToChannels(FString NodeName, const UMovieSceneUserImportFBXSettings* ImportFBXSettings,UMovieSceneUserImportFBXControlRigSettings* ImportFBXControlRigSettings,  FFrameNumber StartFrame, FFrameRate FrameRate, FFBXNodeAndChannels& NodeAndChannels,
	 UnFbx::FFbxCurvesAPI& CurveAPI)
{

	TArray<FMovieSceneFloatChannel*>& Channels = NodeAndChannels.Channels;
	TArray<FMovieSceneBoolChannel*>& BoolChannels = NodeAndChannels.BoolChannels;
	TArray<FMovieSceneByteChannel*>& EnumChannels = NodeAndChannels.EnumChannels;
	TArray<FMovieSceneIntegerChannel*>& IntegerChannels = NodeAndChannels.IntegerChannels;


	// Look for transforms explicitly
	FRichCurve Translation[3];
	FRichCurve EulerRotation[3];
	FRichCurve Scale[3];
	FTransform DefaultTransform;
	const bool bUseSequencerCurve = true;
	CurveAPI.GetConvertedTransformCurveData(NodeName, Translation[0], Translation[1], Translation[2], EulerRotation[0], EulerRotation[1], EulerRotation[2], Scale[0], Scale[1], Scale[2], DefaultTransform, true, ImportFBXSettings->ImportUniformScale);


	FVector Location = DefaultTransform.GetLocation(), Rotation = DefaultTransform.GetRotation().Euler(), Scale3D = DefaultTransform.GetScale3D();
	//For non-transforms we need to re-negate the Y since it happens automatically(todo double check.).
	//But then if we negate we need to re-re-negate... so leave it alone.

	if (BoolChannels.Num() == 1)
	{
		FControlRigChannelEnum Channel = FControlRigChannelEnum::Bool;
		SetChannelValue(nullptr, BoolChannels[0], nullptr,nullptr, FrameRate, StartFrame,
			Channel, ImportFBXControlRigSettings, DefaultTransform,
			Translation[0], Translation[1], Translation[2], EulerRotation[0],
			EulerRotation[1], EulerRotation[2], Scale[0], Scale[1], Scale[2]);
	}

	if (EnumChannels.Num() == 1)
	{
		FControlRigChannelEnum Channel = FControlRigChannelEnum::Enum;
		SetChannelValue(nullptr, nullptr, EnumChannels[0], nullptr, FrameRate, StartFrame,
			Channel, ImportFBXControlRigSettings, DefaultTransform,
			Translation[0], Translation[1], Translation[2], EulerRotation[0],
			EulerRotation[1], EulerRotation[2], Scale[0], Scale[1], Scale[2]);
	}

	if (IntegerChannels.Num() == 1)
	{
		FControlRigChannelEnum Channel = FControlRigChannelEnum::Integer;
		SetChannelValue(nullptr,nullptr, nullptr, IntegerChannels[0], FrameRate, StartFrame,
			Channel,  ImportFBXControlRigSettings,DefaultTransform,
			Translation[0], Translation[1], Translation[2], EulerRotation[0], 
			EulerRotation[1], EulerRotation[2], Scale[0], Scale[1], Scale[2]);
	}

	if (Channels.Num() == 1)
	{
		FControlRigChannelEnum Channel = FControlRigChannelEnum::Float;
		SetChannelValue(Channels[0], nullptr, nullptr, nullptr, FrameRate, StartFrame,
			Channel, ImportFBXControlRigSettings, DefaultTransform,
			Translation[0], Translation[1], Translation[2], EulerRotation[0],
			EulerRotation[1], EulerRotation[2], Scale[0], Scale[1], Scale[2]);
	}
	else if (Channels.Num() == 2)
	{
		FControlRigChannelEnum Channel = FControlRigChannelEnum::Vector2DX;

		SetChannelValue(Channels[0],nullptr, nullptr, nullptr, FrameRate, StartFrame,
			Channel, ImportFBXControlRigSettings, DefaultTransform,
			Translation[0], Translation[1], Translation[2], EulerRotation[0],
			EulerRotation[1], EulerRotation[2], Scale[0], Scale[1], Scale[2]);

		Channel = FControlRigChannelEnum::Vector2DY;
		SetChannelValue(Channels[1],nullptr, nullptr, nullptr, FrameRate, StartFrame,
			Channel, ImportFBXControlRigSettings, DefaultTransform,
			Translation[0], Translation[1], Translation[2], EulerRotation[0],
			EulerRotation[1], EulerRotation[2], Scale[0], Scale[1], Scale[2]);
	}
	else if (Channels.Num() == 3)
	{
		if (NodeAndChannels.ControlType == FFBXControlRigTypeProxyEnum::Position)
		{
			FControlRigChannelEnum Channel = FControlRigChannelEnum::PositionX;

			SetChannelValue(Channels[0],nullptr, nullptr, nullptr, FrameRate, StartFrame,
				Channel, ImportFBXControlRigSettings, DefaultTransform,
				Translation[0], Translation[1], Translation[2], EulerRotation[0],
				EulerRotation[1], EulerRotation[2], Scale[0], Scale[1], Scale[2]);

			Channel = FControlRigChannelEnum::PositionY;
			SetChannelValue(Channels[1], nullptr, nullptr, nullptr, FrameRate, StartFrame,
				Channel, ImportFBXControlRigSettings, DefaultTransform,
				Translation[0], Translation[1], Translation[2], EulerRotation[0],
				EulerRotation[1], EulerRotation[2], Scale[0], Scale[1], Scale[2]);

			Channel = FControlRigChannelEnum::PositionZ;
			SetChannelValue(Channels[2],nullptr, nullptr, nullptr, FrameRate, StartFrame,
				Channel, ImportFBXControlRigSettings, DefaultTransform,
				Translation[0], Translation[1], Translation[2], EulerRotation[0],
				EulerRotation[1], EulerRotation[2], Scale[0], Scale[1], Scale[2]);
		}
		else if (NodeAndChannels.ControlType == FFBXControlRigTypeProxyEnum::Rotator)
		{
			FControlRigChannelEnum Channel = FControlRigChannelEnum::RotatorX;
			SetChannelValue(Channels[0],nullptr, nullptr, nullptr, FrameRate, StartFrame,
				Channel, ImportFBXControlRigSettings, DefaultTransform,
				Translation[0], Translation[1], Translation[2], EulerRotation[0],
				EulerRotation[1], EulerRotation[2], Scale[0], Scale[1], Scale[2]);

			Channel = FControlRigChannelEnum::RotatorY;
			SetChannelValue(Channels[1], nullptr, nullptr, nullptr, FrameRate, StartFrame,
				Channel, ImportFBXControlRigSettings, DefaultTransform,
				Translation[0], Translation[1], Translation[2], EulerRotation[0],
				EulerRotation[1], EulerRotation[2], Scale[0], Scale[1], Scale[2]);

			Channel = FControlRigChannelEnum::RotatorZ;
			SetChannelValue(Channels[2], nullptr, nullptr, nullptr, FrameRate, StartFrame,
				Channel, ImportFBXControlRigSettings, DefaultTransform,
				Translation[0], Translation[1], Translation[2], EulerRotation[0],
				EulerRotation[1], EulerRotation[2], Scale[0], Scale[1], Scale[2]);
		}
		else if (NodeAndChannels.ControlType == FFBXControlRigTypeProxyEnum::Scale)
		{
			FControlRigChannelEnum Channel = FControlRigChannelEnum::ScaleX;
			SetChannelValue(Channels[0], nullptr, nullptr, nullptr, FrameRate, StartFrame,
				Channel, ImportFBXControlRigSettings, DefaultTransform,
				Translation[0], Translation[1], Translation[2], EulerRotation[0],
				EulerRotation[1], EulerRotation[2], Scale[0], Scale[1], Scale[2]);
			Channel = FControlRigChannelEnum::ScaleY;
			SetChannelValue(Channels[1], nullptr, nullptr, nullptr, FrameRate, StartFrame,
				Channel, ImportFBXControlRigSettings, DefaultTransform,
				Translation[0], Translation[1], Translation[2], EulerRotation[0],
				EulerRotation[1], EulerRotation[2], Scale[0], Scale[1], Scale[2]);
			Channel = FControlRigChannelEnum::ScaleZ;
			SetChannelValue(Channels[2], nullptr, nullptr, nullptr, FrameRate, StartFrame,
				Channel, ImportFBXControlRigSettings, DefaultTransform,
				Translation[0], Translation[1], Translation[2], EulerRotation[0],
				EulerRotation[1], EulerRotation[2], Scale[0], Scale[1], Scale[2]);
		}
	}
	else if (Channels.Num() == 9 || Channels.Num() == 6)
	{
		Channels[0]->SetDefault(Location.X);
		Channels[1]->SetDefault(Location.Y);
		Channels[2]->SetDefault(Location.Z);

		Channels[3]->SetDefault(Rotation.X);
		Channels[4]->SetDefault(Rotation.Y);
		Channels[5]->SetDefault(Rotation.Z);

		if (Channels.Num() > 6) //noscale
		{
			Channels[6]->SetDefault(Scale3D.X);
			Channels[7]->SetDefault(Scale3D.Y);
			Channels[8]->SetDefault(Scale3D.Z);
		}

		ImportTransformChannel(Translation[0], Channels[0], FrameRate, false, false, StartFrame);
		ImportTransformChannel(Translation[1], Channels[1], FrameRate, true, false, StartFrame);
		ImportTransformChannel(Translation[2], Channels[2], FrameRate, false, false, StartFrame);

		ImportTransformChannel(EulerRotation[0], Channels[3], FrameRate, false,false, StartFrame);
		ImportTransformChannel(EulerRotation[1], Channels[4], FrameRate, true, false, StartFrame);
		ImportTransformChannel(EulerRotation[2], Channels[5], FrameRate, true, false, StartFrame);

		if (Channels.Num() > 6) //noscale
		{
			ImportTransformChannel(Scale[0], Channels[6], FrameRate, false, false, StartFrame);
			ImportTransformChannel(Scale[1], Channels[7], FrameRate, false, false, StartFrame);
			ImportTransformChannel(Scale[2], Channels[8], FrameRate, false, false, StartFrame);
		}
	}
	return true;
}
static FString GetNewString(const FString& InString, UMovieSceneUserImportFBXControlRigSettings* ImportFBXControlRigSettings)
{
	FString NewString = InString;
	for (const FControlFindReplaceString& FindReplace : ImportFBXControlRigSettings->FindAndReplaceStrings)
	{
		NewString = NewString.Replace(*FindReplace.Find, *FindReplace.Replace); //ignores tupe
	}
	return NewString;
}

static void PrepForInsertReplaceAnimation(bool bInsert, const FFBXNodeAndChannels& NodeAndChannel,
	FFrameNumber  FrameToInsertOrReplace, FFrameNumber  StartFrame, FFrameNumber  EndFrame)
{

	TArray<FMovieSceneChannel*> Channels;
	for (FMovieSceneFloatChannel* FChannel : NodeAndChannel.Channels)
	{
		Channels.Add(FChannel);
	}
	for (FMovieSceneBoolChannel* BChannel : NodeAndChannel.BoolChannels)
	{
		Channels.Add(BChannel);
	}
	for (FMovieSceneByteChannel* EChannel : NodeAndChannel.EnumChannels)
	{
		Channels.Add(EChannel);
	}
	for (FMovieSceneIntegerChannel* IChannel : NodeAndChannel.IntegerChannels)
	{
		Channels.Add(IChannel);
	}

	FFrameNumber Diff = EndFrame - StartFrame;
	FrameToInsertOrReplace += StartFrame;
	if (bInsert)
	{
		for (FMovieSceneChannel* Channel : Channels)
		{
			TArray<FFrameNumber> KeyTimes;
			TArray<FKeyHandle> Handles;
			Channel->GetKeys(TRange<FFrameNumber>(), &KeyTimes, &Handles);
			for (int32 Index = 0; Index < KeyTimes.Num(); Index++)
			{
				FFrameNumber FrameNumber = KeyTimes[Index];
				if (FrameNumber >= FrameToInsertOrReplace)
				{
					FrameNumber += Diff;
					KeyTimes[Index] += Diff;
				}

			}
			Channel->SetKeyTimes(Handles, KeyTimes);
		}
	}
	else //we replace the animation by first deleting keys in the interval
	{
		for (FMovieSceneChannel* Channel : Channels)
		{
			TArray<FFrameNumber> KeyTimes;
			TArray<FKeyHandle> Handles;
			Channel->GetKeys(TRange<FFrameNumber>(), &KeyTimes, &Handles);
			TArray<FKeyHandle> HandlesToDelete;
			for (int32 Index = 0; Index < KeyTimes.Num(); Index++)
			{
				FFrameNumber FrameNumber = KeyTimes[Index];
				if (FrameNumber >= FrameToInsertOrReplace && FrameNumber <= (FrameToInsertOrReplace + EndFrame))
				{
					HandlesToDelete.Add(Handles[Index]);
				}
			}
			Channel->DeleteKeys(HandlesToDelete);		
		}
	}
}

class SControlRigImportFBXSettings : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SControlRigImportFBXSettings) {}
	SLATE_ARGUMENT(FString, ImportFilename)
	SLATE_END_ARGS()
	~SControlRigImportFBXSettings()
	{
		if (NodeAndChannels != nullptr)
		{
			delete NodeAndChannels;
		}
	}
	void Construct(const FArguments& InArgs,  const TSharedRef<ISequencer>& InSequencer)
	{
		FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

		FDetailsViewArgs DetailsViewArgs;
		DetailsViewArgs.bShowOptions = false;
		DetailsViewArgs.bAllowSearch = false;
		DetailsViewArgs.bShowPropertyMatrixButton = false;
		DetailsViewArgs.bUpdatesFromSelection = false;
		DetailsViewArgs.bLockable = false;
		DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
		DetailsViewArgs.ViewIdentifier = "Import FBX Settings";

		DetailView = PropertyEditor.CreateDetailView(DetailsViewArgs);

		Sequencer = InSequencer;

		TSharedPtr<INumericTypeInterface<double>> NumericTypeInterface = (InSequencer->GetNumericTypeInterface());
		DetailView->RegisterInstancedCustomPropertyTypeLayout("FrameNumber", FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FFrameNumberDetailsCustomization::MakeInstance, NumericTypeInterface));


		ChildSlot
			[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			[
				DetailView.ToSharedRef()
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SComboButton)
				.HasDownArrow(true)
				.OnGetMenuContent(this, &SControlRigImportFBXSettings::HandlePresetMenuContent)
				.ButtonContent()
				[
					SNew(STextBlock)
					.Text(NSLOCTEXT("MovieSceneTools", "ControlMappingPresets", "Control Mapping Presets"))
					.ToolTipText(NSLOCTEXT("MovieSceneTools", "SetControlMappingFromAPreset", "Set Control Mappings From A Preset"))
				]
			]

		+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Right)
			.Padding(5.f)
			[
				SNew(SButton)
				.ContentPadding(FMargin(10, 5))
			.Text(NSLOCTEXT("MovieSceneTools", "ImportFBXButtonText", "Import"))
			.OnClicked(this, &SControlRigImportFBXSettings::OnImportFBXClicked)
			]

			];

		ImportFilename = InArgs._ImportFilename;
		NodeAndChannels = nullptr;
		UMovieSceneUserImportFBXControlRigSettings* ImportFBXSettings = GetMutableDefault<UMovieSceneUserImportFBXControlRigSettings>();
		DetailView->SetObject(ImportFBXSettings);
	}
	TSharedRef<SWidget> HandlePresetMenuContent()
	{
		FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection=*/true, nullptr);

		MenuBuilder.AddMenuEntry(
			NSLOCTEXT("MovieSceneTools", "DefaultControlMappings", "Default Control Mappings"),
			NSLOCTEXT("MovieSceneTools", "DefaultControlMappings_Tooltip", "Use Default Control Mappings Preset"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &SControlRigImportFBXSettings::SetPresets,false)
			),
			NAME_None,
			EUserInterfaceActionType::Button
		);


		MenuBuilder.AddMenuEntry(
			NSLOCTEXT("MovieSceneTools", "MetaHumanControlMappings", "MetaHuman Control Mappings"),
			NSLOCTEXT("MovieSceneTools", "MetaHumanControlMappings_Tooltip", "Use MetaHuman Control Mappings Preset"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &SControlRigImportFBXSettings::SetPresets,true)
			),
			NAME_None,
			EUserInterfaceActionType::Button
		);


		return MenuBuilder.MakeWidget();
	}

	void SetNodeNames(const TArray<FString>& NodeNames)
	{
		UMovieSceneUserImportFBXControlRigSettings* ImportFBXSettings = GetMutableDefault<UMovieSceneUserImportFBXControlRigSettings>();
		if (ImportFBXSettings)
		{
			ImportFBXSettings->ImportedNodeNames = NodeNames;
		}
	}
	void SetFrameRate(const FString& InFrameRate)
	{
		UMovieSceneUserImportFBXControlRigSettings* ImportFBXSettings = GetMutableDefault<UMovieSceneUserImportFBXControlRigSettings>();
		if (ImportFBXSettings)
		{
			ImportFBXSettings->ImportedFrameRate = InFrameRate;
		}
	}
	void SetStartTime(FFrameNumber StartTime)
	{
		UMovieSceneUserImportFBXControlRigSettings* ImportFBXSettings = GetMutableDefault<UMovieSceneUserImportFBXControlRigSettings>();
		if (ImportFBXSettings)
		{
			ImportFBXSettings->ImportedStartTime = StartTime;
			ImportFBXSettings->StartTimeRange = StartTime;
		}
	}
	void SetEndTime(FFrameNumber EndTime)
	{
		UMovieSceneUserImportFBXControlRigSettings* ImportFBXSettings = GetMutableDefault<UMovieSceneUserImportFBXControlRigSettings>();
		if (ImportFBXSettings)
		{
			ImportFBXSettings->ImportedEndTime = EndTime;
			ImportFBXSettings->EndTimeRange = EndTime;
		}
	}
	void SetFileName(const FString& FileName)
	{
		UMovieSceneUserImportFBXControlRigSettings* ImportFBXSettings = GetMutableDefault<UMovieSceneUserImportFBXControlRigSettings>();
		if (ImportFBXSettings)
		{
			ImportFBXSettings->ImportedFileName = FileName;
		}
	}
	void SetNodeAndChannels(TArray<FFBXNodeAndChannels>* InNodeAndChannels)
	{
		NodeAndChannels = InNodeAndChannels;
	}




private:

	FReply OnImportFBXClicked()
	{

		if (Sequencer.IsValid() == false)
		{
			return  FReply::Unhandled();
		}

		UMovieSceneUserImportFBXControlRigSettings* ImportFBXControlRigSettings = GetMutableDefault<UMovieSceneUserImportFBXControlRigSettings>();
		
		TArray<FName> SelectedControlNames;
		for (FFBXNodeAndChannels& NodeAndChannel : *NodeAndChannels)
		{
			if (NodeAndChannel.MovieSceneTrack)
			{
				INodeAndChannelMappings* ChannelMapping = Cast<INodeAndChannelMappings>(NodeAndChannel.MovieSceneTrack);
				if (ChannelMapping)
				{
					TArray<FName> LocalControls;
					ChannelMapping->GetSelectedNodes(LocalControls);
					SelectedControlNames.Append(LocalControls);
				}
			}


		}
		bool bValid = MovieSceneToolHelpers::ImportFBXIntoControlRigChannels(Sequencer.Pin()->GetFocusedMovieSceneSequence()->GetMovieScene(), ImportFilename, ImportFBXControlRigSettings, 
			NodeAndChannels,SelectedControlNames, Sequencer.Pin()->GetFocusedTickResolution());
		

		TSharedPtr<SWindow> Window = FSlateApplication::Get().FindWidgetWindow(AsShared());

		if (Window.IsValid())
		{
			Window->RequestDestroyWindow();
		}
		if (bValid)
		{
			Sequencer.Pin()->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemAdded);
		}
		return bValid ? FReply::Handled() : FReply::Unhandled();

	}

	void SetPresets(bool bMetaHuman)
	{
		//since we can't change the API unfortunately need to do this here.
		UMovieSceneUserImportFBXControlRigSettings* ImportFBXControlRigSettings = GetMutableDefault<UMovieSceneUserImportFBXControlRigSettings>();
		ImportFBXControlRigSettings->ControlChannelMappings.SetNum(0); //clear and reset
		FControlToTransformMappings Bool;
		Bool.bNegate = false;
		Bool.ControlChannel = FControlRigChannelEnum::Bool;
		Bool.FBXChannel = FTransformChannelEnum::TranslateX;
		ImportFBXControlRigSettings->ControlChannelMappings.Add(Bool);

		FControlToTransformMappings Float;
		Float.bNegate = false;
		Float.ControlChannel = FControlRigChannelEnum::Float;
		if (bMetaHuman)
		{
			Float.FBXChannel = FTransformChannelEnum::TranslateY;  //use Y for metahuman
		}
		else
		{
			Float.FBXChannel = FTransformChannelEnum::TranslateX;
		}
		ImportFBXControlRigSettings->ControlChannelMappings.Add(Float);

		FControlToTransformMappings Vector2DX;
		Vector2DX.bNegate = false;
		Vector2DX.ControlChannel = FControlRigChannelEnum::Vector2DX;
		Vector2DX.FBXChannel = FTransformChannelEnum::TranslateX;
		ImportFBXControlRigSettings->ControlChannelMappings.Add(Vector2DX);

		FControlToTransformMappings Vector2DY;
		Vector2DY.bNegate = false;
		Vector2DY.ControlChannel = FControlRigChannelEnum::Vector2DY;
		Vector2DY.FBXChannel = FTransformChannelEnum::TranslateY;
		ImportFBXControlRigSettings->ControlChannelMappings.Add(Vector2DY);

		FControlToTransformMappings PositionX;
		PositionX.bNegate = false;
		PositionX.ControlChannel = FControlRigChannelEnum::PositionX;
		PositionX.FBXChannel = FTransformChannelEnum::TranslateX;
		ImportFBXControlRigSettings->ControlChannelMappings.Add(PositionX);

		FControlToTransformMappings PositionY;
		PositionY.bNegate = false;
		PositionY.ControlChannel = FControlRigChannelEnum::PositionY;
		PositionY.FBXChannel = FTransformChannelEnum::TranslateY;
		ImportFBXControlRigSettings->ControlChannelMappings.Add(PositionY);

		FControlToTransformMappings PositionZ;
		PositionZ.bNegate = false;
		PositionZ.ControlChannel = FControlRigChannelEnum::PositionZ;
		PositionZ.FBXChannel = FTransformChannelEnum::TranslateZ;
		ImportFBXControlRigSettings->ControlChannelMappings.Add(PositionZ);

		FControlToTransformMappings RotatorX;
		RotatorX.bNegate = false;
		RotatorX.ControlChannel = FControlRigChannelEnum::RotatorX;
		RotatorX.FBXChannel = FTransformChannelEnum::RotateX;
		ImportFBXControlRigSettings->ControlChannelMappings.Add(RotatorX);

		FControlToTransformMappings RotatorY;
		RotatorY.bNegate = false;
		RotatorY.ControlChannel = FControlRigChannelEnum::RotatorY;
		RotatorY.FBXChannel = FTransformChannelEnum::RotateY;
		ImportFBXControlRigSettings->ControlChannelMappings.Add(RotatorY);

		FControlToTransformMappings RotatorZ;
		RotatorZ.bNegate = false;
		RotatorZ.ControlChannel = FControlRigChannelEnum::RotatorZ;
		RotatorZ.FBXChannel = FTransformChannelEnum::RotateZ;
		ImportFBXControlRigSettings->ControlChannelMappings.Add(RotatorZ);

		FControlToTransformMappings ScaleX;
		ScaleX.bNegate = false;
		ScaleX.ControlChannel = FControlRigChannelEnum::ScaleX;
		ScaleX.FBXChannel = FTransformChannelEnum::ScaleX;
		ImportFBXControlRigSettings->ControlChannelMappings.Add(ScaleX);

		FControlToTransformMappings ScaleY;
		ScaleY.bNegate = false;
		ScaleY.ControlChannel = FControlRigChannelEnum::ScaleY;
		ScaleY.FBXChannel = FTransformChannelEnum::ScaleY;
		ImportFBXControlRigSettings->ControlChannelMappings.Add(ScaleY);

		FControlToTransformMappings ScaleZ;
		ScaleZ.bNegate = false;
		ScaleZ.ControlChannel = FControlRigChannelEnum::ScaleZ;
		ScaleZ.FBXChannel = FTransformChannelEnum::ScaleZ;
		ImportFBXControlRigSettings->ControlChannelMappings.Add(ScaleZ);
	}

	TSharedPtr<IDetailsView> DetailView;
	FString ImportFilename;
	TArray<FFBXNodeAndChannels>* NodeAndChannels;
	TWeakPtr<ISequencer> Sequencer;

};

bool MovieSceneToolHelpers::ImportFBXIntoControlRigChannels(UMovieScene* MovieScene,const FString& ImportFilename, UMovieSceneUserImportFBXControlRigSettings* ImportFBXControlRigSettings,
	TArray<FFBXNodeAndChannels>* NodeAndChannels, const TArray<FName>& SelectedControlNames, FFrameRate FrameRate)
{
	UnFbx::FFbxImporter* FbxImporter = UnFbx::FFbxImporter::GetInstance();

	bool bValid = true;

	UnFbx::FBXImportOptions* ImportOptions = FbxImporter->GetImportOptions();
	bool bOldbConvertScene = ImportOptions->bConvertScene;
	bool bOldbConvertSceneUnit = ImportOptions->bConvertSceneUnit;
	bool bOldbForceFrontXAxis = ImportOptions->bForceFrontXAxis;
	float OldUniformScale = ImportOptions->ImportUniformScale;
	EFBXAnimationLengthImportType OldAnimLengthType = ImportOptions->AnimationLengthImportType;


	ImportOptions->bConvertScene = true;
	ImportOptions->bConvertSceneUnit = ImportFBXControlRigSettings->bConvertSceneUnit;
	ImportOptions->bForceFrontXAxis = ImportFBXControlRigSettings->bForceFrontXAxis;
	ImportOptions->ImportUniformScale = ImportFBXControlRigSettings->ImportUniformScale;
	ImportOptions->AnimationLengthImportType = FBXALIT_ExportedTime;

	const FString FileExtension = FPaths::GetExtension(ImportFilename);
	if (!FbxImporter->ImportFromFile(*ImportFilename, FileExtension, true))
	{
		// Log the error message and fail the import.
		FbxImporter->ReleaseScene();
		bValid = false;
	}
	else
	{
		const FScopedTransaction Transaction(NSLOCTEXT("MovieSceneTools", "ImportFBXControlRigTransaction", "Import FBX Onto Control Rig"));

		UMovieSceneUserImportFBXSettings* CurrentImportFBXSettings = GetMutableDefault<UMovieSceneUserImportFBXSettings>();
		TArray<uint8> OriginalSettings;
		FObjectWriter(CurrentImportFBXSettings, OriginalSettings);

		CurrentImportFBXSettings->bMatchByNameOnly = false;
		CurrentImportFBXSettings->bConvertSceneUnit = ImportFBXControlRigSettings->bConvertSceneUnit;
		CurrentImportFBXSettings->bForceFrontXAxis = ImportFBXControlRigSettings->bForceFrontXAxis;
		CurrentImportFBXSettings->ImportUniformScale = ImportFBXControlRigSettings->ImportUniformScale;
		CurrentImportFBXSettings->bCreateCameras = false;
		CurrentImportFBXSettings->bReduceKeys = false;
		CurrentImportFBXSettings->ReduceKeysTolerance = 0.01f;

		UnFbx::FFbxCurvesAPI CurveAPI;
		FbxImporter->PopulateAnimatedCurveData(CurveAPI);
		TArray<FString> AllNodeNames;
		CurveAPI.GetAllNodeNameArray(AllNodeNames);

		//if matching selected remove out the non-selected
		if (ImportFBXControlRigSettings->bImportOntoSelectedControls)
		{
			for (int32 Index = NodeAndChannels->Num() - 1; Index >= 0; --Index)
			{
				bool bHasOneMatch = false;
				for (const FName& SelectedName : SelectedControlNames)
				{
					if (FCString::Strcmp(*SelectedName.ToString().ToUpper(), *((*NodeAndChannels)[Index].NodeName).ToUpper()) == 0)
					{
						bHasOneMatch = true;
					}
				}
				if (!bHasOneMatch)
				{
					NodeAndChannels->RemoveAt(Index);
				}
			}
		}

		FFrameNumber  FrameToInsertOrReplace = ImportFBXControlRigSettings->TimeToInsertOrReplaceAnimation;

		FFrameNumber  StartFrame = ImportFBXControlRigSettings->StartTimeRange;
		FFrameNumber  EndFrame = ImportFBXControlRigSettings->EndTimeRange;

		FString RootNodeName = FbxImporter->Scene->GetRootNode()->GetName();

		for (int32 NodeIndex = 0; NodeIndex < AllNodeNames.Num(); ++NodeIndex)
		{
			FString NodeName = AllNodeNames[NodeIndex];
			/** Why was this here I think due to speeed....
			if (NodeName[0] != 'C')
			{
				continue;
			}
			*/
			FString NewNodeName = GetNewString(*(NodeName).ToUpper(), ImportFBXControlRigSettings);

			TSet<UMovieSceneSection*> ModifiedSections;
			for (FFBXNodeAndChannels& NodeAndChannel : *NodeAndChannels)
			{
				if (FCString::Strcmp(*(NodeAndChannel.NodeName).ToUpper(), *NewNodeName.ToUpper()) == 0)
				{
					if (NodeAndChannel.MovieSceneTrack)
					{
						if (NodeAndChannel.MovieSceneTrack->GetSectionToKey())
						{
							if (!ModifiedSections.Contains(NodeAndChannel.MovieSceneTrack->GetSectionToKey()))
							{
								NodeAndChannel.MovieSceneTrack->GetSectionToKey()->SetFlags(RF_Transactional);
								NodeAndChannel.MovieSceneTrack->GetSectionToKey()->Modify();
								ModifiedSections.Add(NodeAndChannel.MovieSceneTrack->GetSectionToKey());
							}
						}
						else if (NodeAndChannel.MovieSceneTrack->GetAllSections().Num() > 0)
						{
							if (!ModifiedSections.Contains(NodeAndChannel.MovieSceneTrack->GetAllSections()[0]))
							{
								NodeAndChannel.MovieSceneTrack->GetAllSections()[0]->SetFlags(RF_Transactional);
								NodeAndChannel.MovieSceneTrack->GetAllSections()[0]->Modify();
								ModifiedSections.Add(NodeAndChannel.MovieSceneTrack->GetAllSections()[0]);

							}
						}
					}

					PrepForInsertReplaceAnimation(ImportFBXControlRigSettings->bInsertAnimation, NodeAndChannel,
						FrameToInsertOrReplace,
						StartFrame, EndFrame);

					ImportFBXTransformToChannels(NodeName, CurrentImportFBXSettings, ImportFBXControlRigSettings, FrameToInsertOrReplace, FrameRate, NodeAndChannel, CurveAPI);
				}
			}
		}

		// restore
		FObjectReader(GetMutableDefault<UMovieSceneUserImportFBXSettings>(), OriginalSettings);
		FbxImporter->ReleaseScene();
	}

	ImportOptions->AnimationLengthImportType = OldAnimLengthType;
	ImportOptions->bConvertScene = bOldbConvertScene;
	ImportOptions->bConvertSceneUnit = bOldbConvertSceneUnit;
	ImportOptions->bForceFrontXAxis = bOldbForceFrontXAxis;
	ImportOptions->ImportUniformScale = OldUniformScale;;
	return bValid;
}

bool MovieSceneToolHelpers::ImportFBXIntoChannelsWithDialog(const TSharedRef<ISequencer>& InSequencer,TArray<FFBXNodeAndChannels>* NodeAndChannels)
{
	TArray<FString> OpenFilenames;
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	bool bOpen = false;
	if (DesktopPlatform)
	{
		FString ExtensionStr;
		ExtensionStr += TEXT("FBX (*.fbx)|*.fbx|");

		bOpen = DesktopPlatform->OpenFileDialog(
			FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
			NSLOCTEXT("MovieSceneTools", "ImportFBX", "Import FBX from...").ToString(),
			FEditorDirectories::Get().GetLastDirectory(ELastDirectory::FBX),
			TEXT(""),
			*ExtensionStr,
			EFileDialogFlags::None,
			OpenFilenames
		);
	}
	if (!bOpen)
	{
		return false;
	}

	if (!OpenFilenames.Num())
	{
		return false;
	}


	const FText TitleText = NSLOCTEXT("MovieSceneTools", "ImportFBXTitleOnToControlRig", "Import FBX Onto Control Rig");

	// Create the window to choose our options
	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(TitleText)
		.HasCloseButton(true)
		.SizingRule(ESizingRule::UserSized)
		.ClientSize(FVector2D(400.0f, 200.0f))
		.AutoCenter(EAutoCenter::PreferredWorkArea)
		.SupportsMinimize(false);

	TSharedRef<SControlRigImportFBXSettings> DialogWidget = SNew(SControlRigImportFBXSettings, InSequencer)
		.ImportFilename(OpenFilenames[0]);


	UnFbx::FFbxImporter* FbxImporter = UnFbx::FFbxImporter::GetInstance();
	UnFbx::FBXImportOptions* ImportOptions = FbxImporter->GetImportOptions();

	EFBXAnimationLengthImportType AnimLengthType = ImportOptions->AnimationLengthImportType;
	ImportOptions->AnimationLengthImportType = FBXALIT_ExportedTime;
	const FString FileExtension = FPaths::GetExtension(OpenFilenames[0]);
	if (!FbxImporter->ImportFromFile(*OpenFilenames[0], FileExtension, true))
	{
		ImportOptions->AnimationLengthImportType = AnimLengthType;
		if (NodeAndChannels)
		{
			delete NodeAndChannels;
		}
		FbxImporter->ReleaseScene();
		return false;
	}
	UnFbx::FFbxCurvesAPI CurveAPI;
	FbxImporter->PopulateAnimatedCurveData(CurveAPI);
	TArray<FString> AllNodeNames;
	CurveAPI.GetAllNodeNameArray(AllNodeNames);
	FbxAnimStack* AnimStack = FbxImporter->Scene->GetMember<FbxAnimStack>(0);

	FbxTimeSpan TimeSpan = FbxImporter->GetAnimationTimeSpan(FbxImporter->Scene->GetRootNode(), AnimStack);
	ImportOptions->AnimationLengthImportType = AnimLengthType;
	FbxImporter->ReleaseScene();
	DialogWidget->SetFileName(OpenFilenames[0]);
	FString FrameRateStr = FString::Printf(TEXT("%.2f"), FbxImporter->GetOriginalFbxFramerate());
	
	DialogWidget->SetFrameRate(FrameRateStr);
	FFrameRate FrameRate = InSequencer->GetFocusedTickResolution();
	FFrameNumber StartTime = FrameRate.AsFrameNumber(TimeSpan.GetStart().GetSecondDouble());
	FFrameNumber EndTime = FrameRate.AsFrameNumber(TimeSpan.GetStop().GetSecondDouble());
	DialogWidget->SetStartTime(StartTime);
	DialogWidget->SetEndTime(EndTime);
	DialogWidget->SetNodeNames(AllNodeNames);
	DialogWidget->SetNodeAndChannels(NodeAndChannels);
	Window->SetContent(DialogWidget);

	FSlateApplication::Get().AddWindow(Window);

	return true;

}
bool ImportFBXTransform(FString NodeName, FGuid ObjectBinding, UnFbx::FFbxCurvesAPI& CurveAPI, UMovieSceneSequence* InSequence)
{
	UMovieScene* MovieScene = InSequence->GetMovieScene();

	const UMovieSceneUserImportFBXSettings* ImportFBXSettings = GetDefault<UMovieSceneUserImportFBXSettings>();

	// Look for transforms explicitly
	FRichCurve Translation[3];
	FRichCurve EulerRotation[3];
	FRichCurve Scale[3];
	FTransform DefaultTransform;
	const bool bUseSequencerCurve = true;
	CurveAPI.GetConvertedTransformCurveData(NodeName, Translation[0], Translation[1], Translation[2], EulerRotation[0], EulerRotation[1], EulerRotation[2], Scale[0], Scale[1], Scale[2], DefaultTransform, bUseSequencerCurve, ImportFBXSettings->ImportUniformScale);

 	UMovieScene3DTransformTrack* TransformTrack = MovieScene->FindTrack<UMovieScene3DTransformTrack>(ObjectBinding); 
	if (!TransformTrack)
	{
		MovieScene->Modify();
		TransformTrack = MovieScene->AddTrack<UMovieScene3DTransformTrack>(ObjectBinding);
	}
	TransformTrack->Modify();

	bool bSectionAdded = false;
	UMovieScene3DTransformSection* TransformSection = Cast<UMovieScene3DTransformSection>(TransformTrack->FindSection(0));
	if (TransformSection && !ImportFBXSettings->bReplaceTransformTrack)
	{
		TransformSection = Cast<UMovieScene3DTransformSection>(TransformTrack->CreateNewSection());
		TransformSection->SetRowIndex(TransformTrack->GetMaxRowIndex()+1);
		TransformTrack->AddSection(*TransformSection);
		bSectionAdded = true;
	}
	else
	{
		TransformSection = Cast<UMovieScene3DTransformSection>(TransformTrack->FindOrAddSection(0, bSectionAdded));
	}

	if (!TransformSection)
	{
		return false;
	}

	TransformSection->Modify();

	FFrameRate FrameRate = TransformSection->GetTypedOuter<UMovieScene>()->GetTickResolution();

	if (bSectionAdded)
	{
		TransformSection->SetRange(TRange<FFrameNumber>::All());
	}

	FVector Location = DefaultTransform.GetLocation(), Rotation = DefaultTransform.GetRotation().Euler(), Scale3D = DefaultTransform.GetScale3D();

	TArrayView<FMovieSceneFloatChannel*> Channels = TransformSection->GetChannelProxy().GetChannels<FMovieSceneFloatChannel>();

	Channels[0]->SetDefault(Location.X);
	Channels[1]->SetDefault(Location.Y);
	Channels[2]->SetDefault(Location.Z);

	Channels[3]->SetDefault(Rotation.X);
	Channels[4]->SetDefault(Rotation.Y);
	Channels[5]->SetDefault(Rotation.Z);

	Channels[6]->SetDefault(Scale3D.X);
	Channels[7]->SetDefault(Scale3D.Y);
	Channels[8]->SetDefault(Scale3D.Z);

	ImportTransformChannel(Translation[0],   Channels[0], FrameRate, false, true);
	ImportTransformChannel(Translation[1],   Channels[1], FrameRate, true, true);
	ImportTransformChannel(Translation[2],   Channels[2], FrameRate, false, true);

	ImportTransformChannel(EulerRotation[0], Channels[3], FrameRate, false, true);
	ImportTransformChannel(EulerRotation[1], Channels[4], FrameRate, true, true);
	ImportTransformChannel(EulerRotation[2], Channels[5], FrameRate, true, true);

	ImportTransformChannel(Scale[0],         Channels[6], FrameRate, false, true);
	ImportTransformChannel(Scale[1],         Channels[7], FrameRate, false, true);
	ImportTransformChannel(Scale[2],         Channels[8], FrameRate, false, true);

	return true;
}

bool MovieSceneToolHelpers::ImportFBXNode(FString NodeName, UnFbx::FFbxCurvesAPI& CurveAPI, UMovieSceneSequence* InSequence, IMovieScenePlayer* Player, FMovieSceneSequenceIDRef TemplateID, FGuid ObjectBinding)
{
	// Look for animated float properties
	TArray<FString> AnimatedPropertyNames;
	CurveAPI.GetNodeAnimatedPropertyNameArray(NodeName, AnimatedPropertyNames);
		
	for (auto AnimatedPropertyName : AnimatedPropertyNames)
	{
		ImportFBXProperty(NodeName, AnimatedPropertyName, ObjectBinding, CurveAPI, InSequence, Player, TemplateID);
	}
	
	ImportFBXTransform(NodeName, ObjectBinding, CurveAPI, InSequence);

	// Custom static string properties
	TArray<TPair<FString, FString> > CustomPropertyPairs;
	CurveAPI.GetCustomStringPropertyArray(NodeName, CustomPropertyPairs);

	for (TPair<FString, FString>& CustomProperty : CustomPropertyPairs)
	{
		FMovieSceneToolsModule::Get().ImportStringProperty(CustomProperty.Key, CustomProperty.Value, ObjectBinding, InSequence->GetMovieScene());
	}

	return true;
}

void MovieSceneToolHelpers::GetCameras( FbxNode* Parent, TArray<FbxCamera*>& Cameras )
{
	FbxCamera* Camera = Parent->GetCamera();
	if( Camera )
	{
		Cameras.Add(Camera);
	}

	int32 NodeCount = Parent->GetChildCount();
	for ( int32 NodeIndex = 0; NodeIndex < NodeCount; ++NodeIndex )
	{
		FbxNode* Child = Parent->GetChild( NodeIndex );
		GetCameras(Child, Cameras);
	}
}

FbxCamera* FindCamera( FbxNode* Parent )
{
	FbxCamera* Camera = Parent->GetCamera();
	if( !Camera )
	{
		int32 NodeCount = Parent->GetChildCount();
		for ( int32 NodeIndex = 0; NodeIndex < NodeCount && !Camera; ++NodeIndex )
		{
			FbxNode* Child = Parent->GetChild( NodeIndex );
			Camera = Child->GetCamera();
		}
	}

	return Camera;
}

FbxNode* RetrieveObjectFromName(const TCHAR* ObjectName, FbxNode* Root)
{
	if (!Root)
	{
		return nullptr;
	}
	
	for (int32 ChildIndex=0;ChildIndex<Root->GetChildCount();++ChildIndex)
	{
		FbxNode* Node = Root->GetChild(ChildIndex);
		if (Node)
		{
			FString NodeName = FString(Node->GetName());

			if ( !FCString::Strcmp(ObjectName,UTF8_TO_TCHAR(Node->GetName())))
			{
				return Node;
			}

			if (FbxNode* NextNode = RetrieveObjectFromName(ObjectName,Node))
			{
				return NextNode;
			}
		}
	}

	return nullptr;
}

void MovieSceneToolHelpers::CopyCameraProperties(FbxCamera* CameraNode, AActor* InCameraActor)
{
	float FieldOfView;
	float FocalLength;

	if (CameraNode->GetApertureMode() == FbxCamera::eFocalLength)
	{
		FocalLength = CameraNode->FocalLength.Get();
		FieldOfView = CameraNode->ComputeFieldOfView(FocalLength);
	}
	else
	{
		FieldOfView = CameraNode->FieldOfView.Get();
		FocalLength = CameraNode->ComputeFocalLength(FieldOfView);
	}

	float ApertureWidth = CameraNode->GetApertureWidth();
	float ApertureHeight = CameraNode->GetApertureHeight();

	UCameraComponent* CameraComponent = nullptr;

	if (ACineCameraActor* CineCameraActor = Cast<ACineCameraActor>(InCameraActor))
	{
		CameraComponent = CineCameraActor->GetCineCameraComponent();

		UCineCameraComponent* CineCameraComponent = CineCameraActor->GetCineCameraComponent();
		CineCameraComponent->Filmback.SensorWidth = FUnitConversion::Convert(ApertureWidth, EUnit::Inches, EUnit::Millimeters);
		CineCameraComponent->Filmback.SensorHeight = FUnitConversion::Convert(ApertureHeight, EUnit::Inches, EUnit::Millimeters);
		CineCameraComponent->FocusSettings.ManualFocusDistance = CameraNode->FocusDistance;
		if (FocalLength < CineCameraComponent->LensSettings.MinFocalLength)
		{
			CineCameraComponent->LensSettings.MinFocalLength = FocalLength;
		}
		if (FocalLength > CineCameraComponent->LensSettings.MaxFocalLength)
		{
			CineCameraComponent->LensSettings.MaxFocalLength = FocalLength;
		}
		CineCameraComponent->CurrentFocalLength = FocalLength;
	}
	else if (ACameraActor* CameraActor = Cast<ACameraActor>(InCameraActor))
	{
		CameraComponent = CameraActor->GetCameraComponent();
	}

	if (!CameraComponent)
	{
		return;
	}

	CameraComponent->SetProjectionMode(CameraNode->ProjectionType.Get() == FbxCamera::ePerspective ? ECameraProjectionMode::Perspective : ECameraProjectionMode::Orthographic);
	CameraComponent->SetAspectRatio(CameraNode->AspectWidth.Get() / CameraNode->AspectHeight.Get());
	CameraComponent->SetOrthoNearClipPlane(CameraNode->NearPlane.Get());
	CameraComponent->SetOrthoFarClipPlane(CameraNode->FarPlane.Get());
	CameraComponent->SetOrthoWidth(CameraNode->OrthoZoom.Get());
	CameraComponent->SetFieldOfView(FieldOfView);
}

FString MovieSceneToolHelpers::GetCameraName(FbxCamera* InCamera)
{
	FbxNode* CameraNode = InCamera->GetNode();
	if (CameraNode)
	{
		return CameraNode->GetName();
	}

	return InCamera->GetName();
}


void MovieSceneToolHelpers::ImportFBXCameraToExisting(UnFbx::FFbxImporter* FbxImporter, UMovieSceneSequence* InSequence, IMovieScenePlayer* Player, FMovieSceneSequenceIDRef TemplateID, TMap<FGuid, FString>& InObjectBindingMap, bool bMatchByNameOnly, bool bNotifySlate)
{
	if (FApp::IsUnattended() || GIsRunningUnattendedScript)
	{
		bNotifySlate = false;
	}

	UMovieScene* MovieScene = InSequence->GetMovieScene();

	for (auto InObjectBinding : InObjectBindingMap)
	{
		TArrayView<TWeakObjectPtr<>> BoundObjects = Player->FindBoundObjects(InObjectBinding.Key,TemplateID);

		FString ObjectName = InObjectBinding.Value;
		FbxCamera* CameraNode = nullptr;
		FbxNode* Node = RetrieveObjectFromName(*ObjectName, FbxImporter->Scene->GetRootNode());
		if (Node)
		{
			CameraNode = FindCamera(Node);
		}

		if (!CameraNode)
		{
			if (bMatchByNameOnly)
			{
				if (bNotifySlate)
				{
					FNotificationInfo Info(FText::Format(NSLOCTEXT("MovieSceneTools", "NoMatchingCameraError", "Failed to find any matching camera for {0}"), FText::FromString(ObjectName)));
					Info.ExpireDuration = 5.0f;
					FSlateNotificationManager::Get().AddNotification(Info)->SetCompletionState(SNotificationItem::CS_Fail);
				}
				continue;
			}

			CameraNode = FindCamera(FbxImporter->Scene->GetRootNode());
			if (CameraNode)
			{
				if (bNotifySlate)
				{
					FString CameraName = GetCameraName(CameraNode);
					FNotificationInfo Info(FText::Format(NSLOCTEXT("MovieSceneTools", "NoMatchingCameraWarning", "Failed to find any matching camera for {0}. Importing onto first camera from fbx {1}"), FText::FromString(ObjectName), FText::FromString(CameraName)));
					Info.ExpireDuration = 5.0f;
					FSlateNotificationManager::Get().AddNotification(Info)->SetCompletionState(SNotificationItem::CS_Fail);
				}
			}
		}

		if (!CameraNode)
		{
			continue;
		}

		float FieldOfView;
		float FocalLength;

		if (CameraNode->GetApertureMode() == FbxCamera::eFocalLength)
		{
			FocalLength = CameraNode->FocalLength.Get();
			FieldOfView = CameraNode->ComputeFieldOfView(FocalLength);
		}
		else
		{
			FieldOfView = CameraNode->FieldOfView.Get();
			FocalLength = CameraNode->ComputeFocalLength(FieldOfView);
		}

		for (TWeakObjectPtr<>& WeakObject : BoundObjects)
		{
			UObject* FoundObject = WeakObject.Get();
			if (FoundObject && FoundObject->GetClass()->IsChildOf(ACameraActor::StaticClass()))
			{
				CopyCameraProperties(CameraNode, Cast<AActor>(FoundObject));

				UCameraComponent* CameraComponent = nullptr;
				FName TrackName;
				float TrackValue;

				if (ACineCameraActor* CineCameraActor = Cast<ACineCameraActor>(FoundObject))
				{
					CameraComponent = CineCameraActor->GetCineCameraComponent();
					TrackName = TEXT("CurrentFocalLength");
					TrackValue = FocalLength;
				}
				else if (ACameraActor* CameraActor = Cast<ACameraActor>(FoundObject))
				{
					CameraComponent = CameraActor->GetCameraComponent();
					TrackName = TEXT("FieldOfView");
					TrackValue = FieldOfView;
				}
				else
				{
					continue;
				}

				// Set the default value of the current focal length or field of view section
				//FGuid PropertyOwnerGuid = Player->GetHandleToObject(CameraComponent);
				FGuid PropertyOwnerGuid = GetHandleToObject(CameraComponent, InSequence, Player, TemplateID);

				if (!PropertyOwnerGuid.IsValid())
				{
					continue;
				}

				UMovieSceneFloatTrack* FloatTrack = MovieScene->FindTrack<UMovieSceneFloatTrack>(PropertyOwnerGuid, TrackName);
				if (FloatTrack)
				{
					FloatTrack->Modify();
					FloatTrack->RemoveAllAnimationData();

					bool bSectionAdded = false;
					UMovieSceneFloatSection* FloatSection = Cast<UMovieSceneFloatSection>(FloatTrack->FindOrAddSection(0, bSectionAdded));
					if (!FloatSection)
					{
						continue;
					}

					FloatSection->Modify();

					if (bSectionAdded)
					{
						FloatSection->SetRange(TRange<FFrameNumber>::All());
					}

					FloatSection->GetChannelProxy().GetChannel<FMovieSceneFloatChannel>(0)->SetDefault(TrackValue);
				}
			}
		}
	}
}

void ImportFBXCamera(UnFbx::FFbxImporter* FbxImporter, UMovieSceneSequence* InSequence, ISequencer& InSequencer,  TMap<FGuid, FString>& InObjectBindingMap, bool bMatchByNameOnly, bool bCreateCameras)
{
	bool bNotifySlate = !FApp::IsUnattended() && !GIsRunningUnattendedScript;

	UMovieScene* MovieScene = InSequence->GetMovieScene();

	TArray<FbxCamera*> AllCameras;
	MovieSceneToolHelpers::GetCameras(FbxImporter->Scene->GetRootNode(), AllCameras);

	if (AllCameras.Num() == 0)
	{
		return;
	}

	if (bCreateCameras)
	{
		UWorld* World = GCurrentLevelEditingViewportClient ? GCurrentLevelEditingViewportClient->GetWorld() : nullptr;

		// Find unmatched cameras
		TArray<FbxCamera*> UnmatchedCameras;
		for (auto Camera : AllCameras)
		{
			FString NodeName = MovieSceneToolHelpers::GetCameraName(Camera);

			bool bMatched = false;
			for (auto InObjectBinding : InObjectBindingMap)
			{		
				FString ObjectName = InObjectBinding.Value;
				if (ObjectName == NodeName)
				{
					// Look for a valid bound object, otherwise need to create a new camera and assign this binding to it
					bool bFoundBoundObject = false;
					TArrayView<TWeakObjectPtr<>> BoundObjects = InSequencer.FindBoundObjects(InObjectBinding.Key, InSequencer.GetFocusedTemplateID());
					for (auto BoundObject : BoundObjects)
					{
						if (BoundObject.IsValid())
						{
							bFoundBoundObject = true;
							break;
						}
					}

					if (!bFoundBoundObject)
					{
						if (bNotifySlate)
						{
							FNotificationInfo Info(FText::Format(NSLOCTEXT("MovieSceneTools", "NoBoundObjectsError", "Existing binding has no objects. Creating a new camera and binding for {0}"), FText::FromString(ObjectName)));
							Info.ExpireDuration = 5.0f;
							FSlateNotificationManager::Get().AddNotification(Info)->SetCompletionState(SNotificationItem::CS_Fail);
						}
					}
				}
			}

			if (!bMatched)
			{
				UnmatchedCameras.Add(Camera);
			}
		}

		// If there are new cameras, clear the object binding map so that we're only assigning values to the newly created cameras
		if (UnmatchedCameras.Num() != 0)
		{
			InObjectBindingMap.Reset();
			bMatchByNameOnly = true;
		}

		// Add any unmatched cameras
		for (auto UnmatchedCamera : UnmatchedCameras)
		{
			FString CameraName = MovieSceneToolHelpers::GetCameraName(UnmatchedCamera);

			AActor* NewCamera = nullptr;
			if (UnmatchedCamera->GetApertureMode() == FbxCamera::eFocalLength)
			{
				FActorSpawnParameters SpawnParams;
				NewCamera = World->SpawnActor<ACineCameraActor>(SpawnParams);
				NewCamera->SetActorLabel(*CameraName);
			}
			else
			{
				FActorSpawnParameters SpawnParams;
				NewCamera = World->SpawnActor<ACameraActor>(SpawnParams);
				NewCamera->SetActorLabel(*CameraName);
			}

			// Copy camera properties before adding default tracks so that initial camera properties match and can be restored after sequencer finishes
			MovieSceneToolHelpers::CopyCameraProperties(UnmatchedCamera, NewCamera);

			TArray<TWeakObjectPtr<AActor> > NewCameras;
			NewCameras.Add(NewCamera);
			TArray<FGuid> NewCameraGuids = InSequencer.AddActors(NewCameras);

			if (NewCameraGuids.Num())
			{
				InObjectBindingMap.Add(NewCameraGuids[0]);
				InObjectBindingMap[NewCameraGuids[0]] = CameraName;
			}
		}
	}
	
	MovieSceneToolHelpers::ImportFBXCameraToExisting(FbxImporter, InSequence, &InSequencer, InSequencer.GetFocusedTemplateID(), InObjectBindingMap, bMatchByNameOnly, true);
}

FGuid FindCameraGuid(FbxCamera* Camera, TMap<FGuid, FString>& InObjectBindingMap)
{
	FString CameraName = MovieSceneToolHelpers::GetCameraName(Camera);

	for (auto& Pair : InObjectBindingMap)
	{
		if (Pair.Value == CameraName)
		{
			return Pair.Key;
		}
	}
	return FGuid();
}

UMovieSceneCameraCutTrack* GetCameraCutTrack(UMovieScene* InMovieScene)
{
	// Get the camera cut
	UMovieSceneTrack* CameraCutTrack = InMovieScene->GetCameraCutTrack();
	if (CameraCutTrack == nullptr)
	{
		InMovieScene->Modify();
		CameraCutTrack = InMovieScene->AddCameraCutTrack(UMovieSceneCameraCutTrack::StaticClass());
	}
	return CastChecked<UMovieSceneCameraCutTrack>(CameraCutTrack);
}

void ImportCameraCut(UnFbx::FFbxImporter* FbxImporter, UMovieScene* InMovieScene, TMap<FGuid, FString>& InObjectBindingMap)
{
	// Find a camera switcher
	FbxCameraSwitcher* CameraSwitcher = FbxImporter->Scene->GlobalCameraSettings().GetCameraSwitcher();
	if (CameraSwitcher == nullptr)
	{
		return;
	}
	// Get the animation layer
	FbxAnimStack* AnimStack = FbxImporter->Scene->GetMember<FbxAnimStack>(0);
	if (AnimStack == nullptr)
	{
		return;
	}
	FbxAnimLayer* AnimLayer = AnimStack->GetMember<FbxAnimLayer>(0);
	if (AnimLayer == nullptr)
	{
		return;
	}

	// The camera switcher camera index refer to depth-first found order of the camera in the FBX
	TArray<FbxCamera*> AllCameras;
	MovieSceneToolHelpers::GetCameras(FbxImporter->Scene->GetRootNode(), AllCameras);

	UMovieSceneCameraCutTrack* CameraCutTrack = GetCameraCutTrack(InMovieScene);
	FFrameRate FrameRate = CameraCutTrack->GetTypedOuter<UMovieScene>()->GetTickResolution();

	FbxAnimCurve* AnimCurve = CameraSwitcher->CameraIndex.GetCurve(AnimLayer);
	if (AnimCurve)
	{
		for (int i = 0; i < AnimCurve->KeyGetCount(); ++i)
		{
			FbxAnimCurveKey key = AnimCurve->KeyGet(i);
			int value = (int)key.GetValue() - 1;
			if (value >= 0 && value < AllCameras.Num())
			{
				FGuid CameraGuid = FindCameraGuid(AllCameras[value], InObjectBindingMap);
				if (CameraGuid != FGuid())
				{
					CameraCutTrack->AddNewCameraCut(UE::MovieScene::FRelativeObjectBindingID(CameraGuid), (key.GetTime().GetSecondDouble() * FrameRate).RoundToFrame());
				}
			}
		}
	}
}

class SMovieSceneImportFBXSettings : public SCompoundWidget, public FGCObject
{
	SLATE_BEGIN_ARGS(SMovieSceneImportFBXSettings) {}
		SLATE_ARGUMENT(FString, ImportFilename)
		SLATE_ARGUMENT(UMovieSceneSequence*, Sequence)
		SLATE_ARGUMENT(ISequencer*, Sequencer)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

		FDetailsViewArgs DetailsViewArgs;
		DetailsViewArgs.bShowOptions = false;
		DetailsViewArgs.bAllowSearch = false;
		DetailsViewArgs.bShowPropertyMatrixButton = false;
		DetailsViewArgs.bUpdatesFromSelection = false;
		DetailsViewArgs.bLockable = false;
		DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
		DetailsViewArgs.ViewIdentifier = "Import FBX Settings";

		DetailView = PropertyEditor.CreateDetailView(DetailsViewArgs);

		ChildSlot
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			[
				DetailView.ToSharedRef()
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Right)
			.Padding(5.f)
			[
				SNew(SButton)
				.ContentPadding(FMargin(10, 5))
				.Text(NSLOCTEXT("MovieSceneTools", "ImportFBXButtonText", "Import"))
				.OnClicked(this, &SMovieSceneImportFBXSettings::OnImportFBXClicked)
			]
			
		];

		ImportFilename = InArgs._ImportFilename;
		Sequence = InArgs._Sequence;
		Sequencer = InArgs._Sequencer;

		UMovieSceneUserImportFBXSettings* ImportFBXSettings = GetMutableDefault<UMovieSceneUserImportFBXSettings>();
		DetailView->SetObject(ImportFBXSettings);
	}

	virtual void AddReferencedObjects( FReferenceCollector& Collector ) override
	{
		Collector.AddReferencedObject(Sequence);
	}

	void SetObjectBindingMap(const TMap<FGuid, FString>& InObjectBindingMap)
	{
		ObjectBindingMap = InObjectBindingMap;
	}

	void SetCreateCameras(TOptional<bool> bInCreateCameras)
	{
		bCreateCameras = bInCreateCameras;
	}

private:

	FReply OnImportFBXClicked()
	{
		
		UMovieSceneUserImportFBXSettings* ImportFBXSettings = GetMutableDefault<UMovieSceneUserImportFBXSettings>();
		FEditorDirectories::Get().SetLastDirectory( ELastDirectory::FBX, FPaths::GetPath( ImportFilename ) ); // Save path as default for next time.

		if (!Sequence || !Sequence->GetMovieScene() || Sequence->GetMovieScene()->IsReadOnly())
		{
			return FReply::Unhandled();
		}

		FFBXInOutParameters InOutParams;
		if (!MovieSceneToolHelpers::ReadyFBXForImport(ImportFilename, ImportFBXSettings,InOutParams))
		{
			return FReply::Unhandled();
		}

		const FScopedTransaction Transaction(NSLOCTEXT("MovieSceneTools", "ImportFBXTransaction", "Import FBX"));
		UnFbx::FFbxImporter* FbxImporter = UnFbx::FFbxImporter::GetInstance();

		const bool bMatchByNameOnly = ImportFBXSettings->bMatchByNameOnly;
		// Import static cameras first
		ImportFBXCamera(FbxImporter, Sequence, *Sequencer, ObjectBindingMap, bMatchByNameOnly, bCreateCameras.IsSet() ? bCreateCameras.GetValue() : ImportFBXSettings->bCreateCameras);

		UWorld* World = Cast<UWorld>(Sequencer->GetPlaybackContext());
		bool bValid = MovieSceneToolHelpers::ImportFBXIfReady(World, Sequence, Sequencer, Sequencer->GetFocusedTemplateID(), ObjectBindingMap, ImportFBXSettings, InOutParams);
	
		Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemAdded);

		TSharedPtr<SWindow> Window = FSlateApplication::Get().FindWidgetWindow(AsShared());

		if ( Window.IsValid() )
		{
			Window->RequestDestroyWindow();
		}

		return bValid ? FReply::Handled() : FReply::Unhandled();
	}

	TSharedPtr<IDetailsView> DetailView;
	FString ImportFilename;
	UMovieSceneSequence* Sequence;
	ISequencer* Sequencer;
	TMap<FGuid, FString> ObjectBindingMap;
	TOptional<bool> bCreateCameras;

};

bool MovieSceneToolHelpers::ReadyFBXForImport(const FString&  ImportFilename, UMovieSceneUserImportFBXSettings* ImportFBXSettings, FFBXInOutParameters& OutParams)
{
	UnFbx::FFbxImporter* FbxImporter = UnFbx::FFbxImporter::GetInstance();

	UnFbx::FBXImportOptions* ImportOptions = FbxImporter->GetImportOptions();
	OutParams.bConvertSceneBackup = ImportOptions->bConvertScene;
	OutParams.bConvertSceneUnitBackup = ImportOptions->bConvertSceneUnit;
	OutParams.bForceFrontXAxisBackup = ImportOptions->bForceFrontXAxis;
	OutParams.ImportUniformScaleBackup = ImportOptions->ImportUniformScale;

	ImportOptions->bIsImportCancelable = false;
	ImportOptions->bConvertScene = true;
	ImportOptions->bConvertSceneUnit = ImportFBXSettings->bConvertSceneUnit;
	ImportOptions->bForceFrontXAxis = ImportFBXSettings->bForceFrontXAxis;
	ImportOptions->ImportUniformScale = ImportFBXSettings->ImportUniformScale;

	const FString FileExtension = FPaths::GetExtension(ImportFilename);
	if (!FbxImporter->ImportFromFile(*ImportFilename, FileExtension, true))
	{
		// Log the error message and fail the import.
		FbxImporter->ReleaseScene();
		ImportOptions->bConvertScene = OutParams.bConvertSceneBackup;
		ImportOptions->bConvertSceneUnit = OutParams.bConvertSceneUnitBackup;
		ImportOptions->bForceFrontXAxis = OutParams.bForceFrontXAxisBackup;
		ImportOptions->ImportUniformScale = OutParams.ImportUniformScaleBackup;
		return false;
	}
	return true;
}

bool ImportFBXOntoControlRigs(UWorld* World, UMovieScene* MovieScene, IMovieScenePlayer* Player, FMovieSceneSequenceIDRef TemplateID,
	TMap<FGuid, FString>& ObjectBindingMap, const TArray<FString>& ControRigControlNames , UMovieSceneUserImportFBXSettings* ImportFBXSettings,
	UMovieSceneUserImportFBXControlRigSettings*  Settings)
{
	UMovieSceneUserImportFBXSettings* CurrentImportFBXSettings = GetMutableDefault<UMovieSceneUserImportFBXSettings>();
	TArray<uint8> OriginalSettings;
	FObjectWriter(CurrentImportFBXSettings, OriginalSettings);

	CurrentImportFBXSettings->bMatchByNameOnly = ImportFBXSettings->bMatchByNameOnly;
	CurrentImportFBXSettings->bForceFrontXAxis = ImportFBXSettings->bForceFrontXAxis;
	CurrentImportFBXSettings->bCreateCameras = ImportFBXSettings->bCreateCameras;
	CurrentImportFBXSettings->bReduceKeys = ImportFBXSettings->bReduceKeys;
	CurrentImportFBXSettings->ReduceKeysTolerance = ImportFBXSettings->ReduceKeysTolerance;
	CurrentImportFBXSettings->bConvertSceneUnit = ImportFBXSettings->bConvertSceneUnit;
	CurrentImportFBXSettings->ImportUniformScale = ImportFBXSettings->ImportUniformScale;


	UnFbx::FFbxImporter* FbxImporter = UnFbx::FFbxImporter::GetInstance();

	return true;
}

bool MovieSceneToolHelpers::ImportFBXIfReady(UWorld* World, UMovieSceneSequence* Sequence, IMovieScenePlayer* Player, FMovieSceneSequenceIDRef TemplateID, TMap<FGuid, FString>& ObjectBindingMap, UMovieSceneUserImportFBXSettings* ImportFBXSettings,
	const FFBXInOutParameters& InParams)
{
	UMovieScene* MovieScene = Sequence->GetMovieScene();

	UMovieSceneUserImportFBXSettings* CurrentImportFBXSettings = GetMutableDefault<UMovieSceneUserImportFBXSettings>();
	TArray<uint8> OriginalSettings;
	FObjectWriter(CurrentImportFBXSettings, OriginalSettings);

	CurrentImportFBXSettings->bMatchByNameOnly = ImportFBXSettings->bMatchByNameOnly;
	CurrentImportFBXSettings->bForceFrontXAxis = ImportFBXSettings->bForceFrontXAxis;
	CurrentImportFBXSettings->bCreateCameras = ImportFBXSettings->bCreateCameras;
	CurrentImportFBXSettings->bReduceKeys = ImportFBXSettings->bReduceKeys;
	CurrentImportFBXSettings->ReduceKeysTolerance = ImportFBXSettings->ReduceKeysTolerance;
	CurrentImportFBXSettings->bConvertSceneUnit = ImportFBXSettings->bConvertSceneUnit;
	CurrentImportFBXSettings->ImportUniformScale = ImportFBXSettings->ImportUniformScale;
	UnFbx::FFbxImporter* FbxImporter = UnFbx::FFbxImporter::GetInstance();

	UnFbx::FFbxCurvesAPI CurveAPI;
	FbxImporter->PopulateAnimatedCurveData(CurveAPI);
	TArray<FString> AllNodeNames;
	CurveAPI.GetAllNodeNameArray(AllNodeNames);

	// Import a camera cut track if cams were created, do it after populating curve data ensure only one animation layer, if any
	ImportCameraCut(FbxImporter, MovieScene, ObjectBindingMap);

	FString RootNodeName = FbxImporter->Scene->GetRootNode()->GetName();

	// First try matching by name
	for (int32 NodeIndex = 0; NodeIndex < AllNodeNames.Num(); )
	{
		FString NodeName = AllNodeNames[NodeIndex];
		if (RootNodeName == NodeName)
		{
			++NodeIndex;
			continue;
		}

		bool bFoundMatch = false;
		for (auto It = ObjectBindingMap.CreateConstIterator(); It; ++It)
		{
			if (FCString::Strcmp(*It.Value().ToUpper(), *NodeName.ToUpper()) == 0)
			{
				MovieSceneToolHelpers::ImportFBXNode(NodeName, CurveAPI, Sequence, Player, TemplateID, It.Key());

				ObjectBindingMap.Remove(It.Key());
				AllNodeNames.RemoveAt(NodeIndex);

				bFoundMatch = true;
				break;
			}
		}

		if (bFoundMatch)
		{
			continue;
		}

		++NodeIndex;
	}

	// Otherwise, get the first available node that hasn't been imported onto yet
	if (!ImportFBXSettings->bMatchByNameOnly)
	{
		for (int32 NodeIndex = 0; NodeIndex < AllNodeNames.Num(); )
		{
			FString NodeName = AllNodeNames[NodeIndex];
			if (RootNodeName == NodeName)
			{
				++NodeIndex;
				continue;
			}

			auto It = ObjectBindingMap.CreateConstIterator();
			if (It)
			{
				MovieSceneToolHelpers::ImportFBXNode(NodeName, CurveAPI, Sequence, Player, TemplateID, It.Key());

				UE_LOG(LogMovieScene, Warning, TEXT("Fbx Import: Failed to find any matching node for (%s). Defaulting to first available (%s)."), *NodeName, *It.Value());
				ObjectBindingMap.Remove(It.Key());
				AllNodeNames.RemoveAt(NodeIndex);
				continue;
			}

			++NodeIndex;
		}
	}

	for (FString NodeName : AllNodeNames)
	{
		UE_LOG(LogMovieScene, Warning, TEXT("Fbx Import: Failed to find any matching node for (%s)."), *NodeName);
	}

	// restore
	FObjectReader(GetMutableDefault<UMovieSceneUserImportFBXSettings>(), OriginalSettings);

	FbxImporter->ReleaseScene();
	UnFbx::FBXImportOptions* ImportOptions = FbxImporter->GetImportOptions();
	ImportOptions->bConvertScene = InParams.bConvertSceneBackup;
	ImportOptions->bConvertSceneUnit = InParams.bConvertSceneUnitBackup;
	ImportOptions->bForceFrontXAxis = InParams.bForceFrontXAxisBackup;
	ImportOptions->ImportUniformScale = InParams.ImportUniformScaleBackup;
	return true;
}

bool MovieSceneToolHelpers::ImportFBXWithDialog(UMovieSceneSequence* InSequence, ISequencer& InSequencer, const TMap<FGuid, FString>& InObjectBindingMap, TOptional<bool> bCreateCameras)
{
	TArray<FString> OpenFilenames;
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	bool bOpen = false;
	if (DesktopPlatform)
	{
		FString ExtensionStr;
		ExtensionStr += TEXT("FBX (*.fbx)|*.fbx|");

		bOpen = DesktopPlatform->OpenFileDialog(
			FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
			NSLOCTEXT("MovieSceneTools", "ImportFBX", "Import FBX from...").ToString(), 
			FEditorDirectories::Get().GetLastDirectory(ELastDirectory::FBX),
			TEXT(""), 
			*ExtensionStr,
			EFileDialogFlags::None,
			OpenFilenames
			);
	}
	if (!bOpen)
	{
		return false;
	}

	if (!OpenFilenames.Num())
	{
		return false;
	}

	const FText TitleText = NSLOCTEXT("MovieSceneTools", "ImportFBXTitle", "Import FBX");

	// Create the window to choose our options
	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(TitleText)
		.HasCloseButton(true)
		.SizingRule(ESizingRule::UserSized)
		.ClientSize(FVector2D(450.0f, 300.0f))
		.AutoCenter(EAutoCenter::PreferredWorkArea)
		.SupportsMinimize(false);

	TSharedRef<SMovieSceneImportFBXSettings> DialogWidget = SNew(SMovieSceneImportFBXSettings)
		.ImportFilename(OpenFilenames[0])
		.Sequence(InSequence)
		.Sequencer(&InSequencer);
	DialogWidget->SetObjectBindingMap(InObjectBindingMap);
	DialogWidget->SetCreateCameras(bCreateCameras);
	Window->SetContent(DialogWidget);

	FSlateApplication::Get().AddWindow(Window);

	return true;
}


EInterpCurveMode MovieSceneToolHelpers::RichCurveInterpolationToMatineeInterpolation( ERichCurveInterpMode InterpMode, ERichCurveTangentMode TangentMode)
{
	switch ( InterpMode )
	{
	case ERichCurveInterpMode::RCIM_Constant:
		return CIM_Constant;
	case ERichCurveInterpMode::RCIM_Cubic:
		if (TangentMode == RCTM_Auto)
		{
			return CIM_CurveAuto;
		}
		else if (TangentMode == RCTM_Break)
		{
			return CIM_CurveBreak;
		}
		return CIM_CurveUser;  
	case ERichCurveInterpMode::RCIM_Linear:
		return CIM_Linear;
	default:
		return CIM_CurveAuto;
	}
}

void MovieSceneToolHelpers::CopyKeyDataToMoveAxis(const TMovieSceneChannelData<FMovieSceneFloatValue>& Channel, UInterpTrackMoveAxis* MoveAxis, FFrameRate InFrameRate)
{
	MoveAxis->FloatTrack.Points.Reset();

	static FName LookupName(NAME_None);
	
	TArrayView<const FFrameNumber>          Times  = Channel.GetTimes();
	TArrayView<const FMovieSceneFloatValue> Values = Channel.GetValues();

	for (int32 KeyIndex = 0; KeyIndex < Times.Num(); ++KeyIndex)
	{
		const float Time = Times[KeyIndex] / InFrameRate;
		const FMovieSceneFloatValue& Value = Values[KeyIndex];

		const int32 PointIndex = MoveAxis->FloatTrack.AddPoint(Time, Value.Value);
		MoveAxis->LookupTrack.AddPoint(Time, LookupName);

		FInterpCurvePoint<float>& Point = MoveAxis->FloatTrack.Points[PointIndex];
		Point.ArriveTangent = Value.Tangent.ArriveTangent * InFrameRate.AsDecimal();
		Point.LeaveTangent = Value.Tangent.LeaveTangent * InFrameRate.AsDecimal();
		Point.InterpMode = RichCurveInterpolationToMatineeInterpolation(Value.InterpMode, Value.TangentMode);
	}
}

UObject* MovieSceneToolHelpers::ExportToCameraAnim(UMovieScene* InMovieScene, FGuid& InObjectBinding)
{
	// Create a new camera anim
	IAssetTools& AssetTools = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools").Get();

	UObject* NewAsset = nullptr;

	// Attempt to create a new asset
	for (TObjectIterator<UClass> It ; It ; ++It)
	{
		UClass* CurrentClass = *It;
		if (CurrentClass->IsChildOf(UFactory::StaticClass()) && !(CurrentClass->HasAnyClassFlags(CLASS_Abstract)))
		{
			UFactory* Factory = Cast<UFactory>(CurrentClass->GetDefaultObject());
			if (Factory->CanCreateNew() && Factory->ImportPriority >= 0 && Factory->SupportedClass == UCameraAnim::StaticClass())
			{
				NewAsset = AssetTools.CreateAssetWithDialog(UCameraAnim::StaticClass(), Factory);
				break;
			}
		}
	}

	if (!NewAsset)
	{
		return NewAsset;
	}

	static FName Transform("Transform");
	UMovieScene3DTransformTrack* TransformTrack = InMovieScene->FindTrack<UMovieScene3DTransformTrack>(InObjectBinding, Transform); 
	if (TransformTrack)
	{
		UCameraAnim* CameraAnim = CastChecked<UCameraAnim>(NewAsset);
		UInterpGroup* CameraInterpGroup = CameraAnim->CameraInterpGroup;
		CameraAnim->bRelativeToInitialTransform=false;

		UInterpGroupInst* CameraInst = NewObject<UInterpGroupInst>(CameraAnim, NAME_None, RF_Transactional);
		CameraInst->InitGroupInst(CameraInterpGroup, nullptr);

		UInterpTrackMove* MovementTrack = NewObject<UInterpTrackMove>(CameraInterpGroup, NAME_None, RF_Transactional);
		CameraInterpGroup->InterpTracks.Add(MovementTrack);
		
		UInterpTrackInstMove* MovementTrackInst = NewObject<UInterpTrackInstMove>(CameraInst, NAME_None, RF_Transactional);
		CameraInst->TrackInst.Add(MovementTrackInst);
		MovementTrackInst->InitTrackInst(MovementTrack);
			
		MovementTrack->CreateSubTracks(false);

		UInterpTrackMoveAxis* MoveAxies[6];
		for( int32 SubTrackIndex = 0; SubTrackIndex < 6; ++SubTrackIndex )
		{
			MoveAxies[ SubTrackIndex ] = Cast<UInterpTrackMoveAxis>( MovementTrack->SubTracks[ SubTrackIndex ] );
		}

		TArray<UMovieSceneSection*> Sections = TransformTrack->GetAllSections();

		if (Sections.Num())
		{
			if (Sections.Num() > 1)
			{
				UE_LOG(LogMovieScene, Error, TEXT("Export to Camera Anim: Failed to export, multiple sections (%d) are not supported"), Sections.Num());
			}
			else
			{
				FFrameRate TickResolution = InMovieScene->GetTickResolution();
				UMovieScene3DTransformSection* TransformSection = Cast<UMovieScene3DTransformSection>(Sections[0]);
				TArrayView<FMovieSceneFloatChannel*> FloatChannels = TransformSection->GetChannelProxy().GetChannels<FMovieSceneFloatChannel>();

				CopyKeyDataToMoveAxis(FloatChannels[0]->GetData(), MoveAxies[AXIS_TranslationX], TickResolution);
				CopyKeyDataToMoveAxis(FloatChannels[1]->GetData(), MoveAxies[AXIS_TranslationY], TickResolution);
				CopyKeyDataToMoveAxis(FloatChannels[2]->GetData(), MoveAxies[AXIS_TranslationZ], TickResolution);
				CopyKeyDataToMoveAxis(FloatChannels[3]->GetData(), MoveAxies[AXIS_RotationX],    TickResolution);
				CopyKeyDataToMoveAxis(FloatChannels[4]->GetData(), MoveAxies[AXIS_RotationY],    TickResolution);
				CopyKeyDataToMoveAxis(FloatChannels[5]->GetData(), MoveAxies[AXIS_RotationZ],    TickResolution);
			}
		}
	}

	return NewAsset;
}


bool MovieSceneToolHelpers::HasHiddenMobility(const UClass* ObjectClass)
{
	if (ObjectClass)
	{
		static const FName NAME_HideCategories(TEXT("HideCategories"));
		if (ObjectClass->HasMetaData(NAME_HideCategories))
		{
			if (ObjectClass->GetMetaData(NAME_HideCategories).Contains(TEXT("Mobility")))
			{
				return true;
			}
		}
	}

	return false;
}

const FMovieSceneEvaluationTrack* MovieSceneToolHelpers::GetEvaluationTrack(ISequencer *Sequencer, const FGuid& TrackSignature)
{
	FMovieSceneRootEvaluationTemplateInstance& Instance = Sequencer->GetEvaluationTemplate();
	FMovieSceneCompiledDataID SubDataID = Instance.GetCompiledDataManager()->GetSubDataID(Instance.GetCompiledDataID(), Sequencer->GetFocusedTemplateID());

	{
		const FMovieSceneEvaluationTemplate* Template  = SubDataID.IsValid() ? Instance.GetCompiledDataManager()->FindTrackTemplate(SubDataID) : nullptr;
		const FMovieSceneEvaluationTrack*    EvalTrack = Template ? Template->FindTrack(TrackSignature) : nullptr;
		if (EvalTrack)
		{
			return EvalTrack;
		}
	}
	return nullptr;
}

void ExportLevelMesh(UnFbx::FFbxExporter* Exporter, ULevel* Level, IMovieScenePlayer* Player, TArray<FGuid>& Bindings, INodeNameAdapter& NodeNameAdapter, FMovieSceneSequenceIDRef& Template)
{
	// Get list of actors based upon bindings...
	const bool bSelectedOnly = (Bindings.Num()) != 0;

	const bool bSaveAnimSeq = false; //force off saving any AnimSequences since this can conflict when we export the level sequence animations.

	TArray<AActor*> ActorToExport;

	int32 ActorCount = Level->Actors.Num();
	for (int32 ActorIndex = 0; ActorIndex < ActorCount; ++ActorIndex)
	{
		AActor* Actor = Level->Actors[ActorIndex];
		if (Actor != NULL)
		{
			FGuid ExistingGuid = Player->FindObjectId(*Actor, Template);
			if (ExistingGuid.IsValid() && (!bSelectedOnly || Bindings.Contains(ExistingGuid)))
			{
				ActorToExport.Add(Actor);
			}
		}
	}

	// Export the persistent level and all of it's actors
	Exporter->ExportLevelMesh(Level, !bSelectedOnly, ActorToExport, NodeNameAdapter, bSaveAnimSeq);
}

bool MovieSceneToolHelpers::ExportFBX(UWorld* World, UMovieScene* MovieScene, IMovieScenePlayer* Player, TArray<FGuid>& Bindings, INodeNameAdapter& NodeNameAdapter, FMovieSceneSequenceIDRef& Template, const FString& InFBXFileName, FMovieSceneSequenceTransform& RootToLocalTransform)
{
	UnFbx::FFbxExporter* Exporter = UnFbx::FFbxExporter::GetInstance();

	Exporter->CreateDocument();
	Exporter->SetTrasformBaking(false);
	Exporter->SetKeepHierarchy(true);

	ExportLevelMesh(Exporter, World->PersistentLevel, Player, Bindings, NodeNameAdapter, Template);

	// Export streaming levels and actors
	for (ULevelStreaming* StreamingLevel : World->GetStreamingLevels())
	{
		if (StreamingLevel)
		{
			if (ULevel* Level = StreamingLevel->GetLoadedLevel())
			{
				ExportLevelMesh(Exporter, Level, Player, Bindings, NodeNameAdapter, Template);
			}
		}
	}

	Exporter->ExportLevelSequence(MovieScene, Bindings, Player, NodeNameAdapter, Template, RootToLocalTransform);

	//Export all master tracks

	for (UMovieSceneTrack* MasterTrack : MovieScene->GetMasterTracks())
	{
		TArray<UMovieSceneTrack*> Tracks;
		Tracks.Add(MasterTrack);
		Exporter->ExportLevelSequenceTracks(MovieScene, Player, Template, nullptr, nullptr, Tracks, RootToLocalTransform);
	}
	// Save to disk
	Exporter->WriteToFile(*InFBXFileName);

	return true;
}
static void TickLiveLink(ILiveLinkClient* LiveLinkClient, TMap<FGuid, ELiveLinkSourceMode>&  SourceAndMode)
{

	//This first bit lookes for a Sequencer Live Link Source which can show up any frame and we need to set it to Latest mode
	if (LiveLinkClient)
	{
		TArray<FGuid> Sources = LiveLinkClient->GetSources();
		for (const FGuid& Guid : Sources)
		{
			FText SourceTypeText = LiveLinkClient->GetSourceType(Guid);
			FString SourceTypeStr = SourceTypeText.ToString();
			if (SourceTypeStr.Contains(TEXT("Sequencer Live Link")))
			{
				ULiveLinkSourceSettings* Settings = LiveLinkClient->GetSourceSettings(Guid);
				if (Settings)
				{
					if (Settings->Mode != ELiveLinkSourceMode::Latest)
					{
						SourceAndMode.Add(Guid, Settings->Mode);
						Settings->Mode = ELiveLinkSourceMode::Latest;
					}
				}
			}
		}
	
		LiveLinkClient->ForceTick();
	}
}
bool MovieSceneToolHelpers::BakeToSkelMeshToCallbacks(UMovieScene* MovieScene, IMovieScenePlayer* Player,
	USkeletalMeshComponent* InSkelMeshComp, FMovieSceneSequenceIDRef& Template, FMovieSceneSequenceTransform& RootToLocalTransform, UAnimSeqExportOption* ExportOptions,
	FInitAnimationCB InitCallback, FStartAnimationCB StartCallback, FTickAnimationCB TickCallback, FEndAnimationCB EndCallback)
{
	TArray< USkeletalMeshComponent*> SkelMeshComps;
	if (ExportOptions->bEvaluateAllSkeletalMeshComponents)
	{
		AActor* Actor = InSkelMeshComp->GetTypedOuter<AActor>();
		if (Actor)
		{
			Actor->GetComponents(SkelMeshComps, false);
		}
	}
	else
	{
		SkelMeshComps.Add(InSkelMeshComp);
	}
	//if we have no allocated bone space transforms something wrong so try to recalc them,only need to do this on the recorded skelmesh
	if (InSkelMeshComp->GetBoneSpaceTransforms().Num() <= 0)
	{
		InSkelMeshComp->RecalcRequiredBones(0);
		if (InSkelMeshComp->GetBoneSpaceTransforms().Num() <= 0)
		{
			UE_LOG(LogMovieScene, Error, TEXT("Error Ba"));
			return false;
		}
	}

	UnFbx::FLevelSequenceAnimTrackAdapter AnimTrackAdapter(Player, MovieScene, RootToLocalTransform);
	int32 LocalStartFrame = AnimTrackAdapter.GetLocalStartFrame();
	int32 StartFrame = AnimTrackAdapter.GetStartFrame();
	int32 AnimationLength = AnimTrackAdapter.GetLength();
	float FrameRate = AnimTrackAdapter.GetFrameRate();
	float DeltaTime = 1.0f / FrameRate;
	FFrameRate SampleRate = MovieScene->GetDisplayRate();


	//If we are running with a live link track we need to do a few things.
	// 1. First test to see if we have one, only way to really do that is to see if we have a source that has the `Sequencer Live Link Track`.  We also evalute the first frame in case we are out of range and the sources aren't created yet.
	// 2. Make sure Sequencer.AlwaysSendInterpolated.LiveLink is non-zero, and then set it back to zero if it's not.
	// 3. For each live link sequencer source we need to set the ELiveLinkSourceMode to Latest so that we just get the latest and don't use engine/timecode for any interpolation.
	ILiveLinkClient* LiveLinkClient = nullptr;
	IModularFeatures& ModularFeatures = IModularFeatures::Get();
	TMap<FGuid, ELiveLinkSourceMode> SourceAndMode;
	if (ModularFeatures.IsModularFeatureAvailable(ILiveLinkClient::ModularFeatureName))
	{
		LiveLinkClient = &ModularFeatures.GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);
	}
	TOptional<int32> SequencerAlwaysSenedLiveLinkInterpolated;
	IConsoleVariable* CVarAlwaysSendInterpolatedLiveLink = IConsoleManager::Get().FindConsoleVariable(TEXT("Sequencer.AlwaysSendInterpolatedLiveLink"));
	if (CVarAlwaysSendInterpolatedLiveLink)
	{
		SequencerAlwaysSenedLiveLinkInterpolated = CVarAlwaysSendInterpolatedLiveLink->GetInt();
		CVarAlwaysSendInterpolatedLiveLink->Set(1, ECVF_SetByConsole);
	}

	const TArray<IMovieSceneToolsAnimationBakeHelper*>&  BakeHelpers = FMovieSceneToolsModule::Get().GetAnimationBakeHelpers();
	for (IMovieSceneToolsAnimationBakeHelper* BakeHelper : BakeHelpers)
	{
		if (BakeHelper)
		{
			BakeHelper->StartBaking(MovieScene);
		}
	}

	InitCallback.ExecuteIfBound();

	//if we have warmup frames
	if (ExportOptions->WarmUpFrames > 0)
	{
		for (int32 Index = -ExportOptions->WarmUpFrames.Value; Index < 0; ++Index)
		{
			//Begin records a frame so need to set things up first
			for (IMovieSceneToolsAnimationBakeHelper* BakeHelper : BakeHelpers)
			{
				if (BakeHelper)
				{
					BakeHelper->PreEvaluation(MovieScene,Index);
				}
			}
			// This will call UpdateSkelPose on the skeletal mesh component to move bones based on animations in the matinee group
			AnimTrackAdapter.UpdateAnimation(Index);
			for (IMovieSceneToolsAnimationBakeHelper* BakeHelper : BakeHelpers)
			{
				if (BakeHelper)
				{
					BakeHelper->PostEvaluation(MovieScene,Index);
				}
			}
			//Live Link sourcer can show up at any time so we unfortunately need to check for it
			TickLiveLink(LiveLinkClient, SourceAndMode);

			// Update space bases so new animation position has an effect.
			for (USkeletalMeshComponent* SkelMeshComp : SkelMeshComps)
			{
				SkelMeshComp->TickAnimation(DeltaTime, false);

				SkelMeshComp->RefreshBoneTransforms();
				SkelMeshComp->RefreshSlaveComponents();
				SkelMeshComp->UpdateComponentToWorld();
				SkelMeshComp->FinalizeBoneTransform();
				SkelMeshComp->MarkRenderTransformDirty();
				SkelMeshComp->MarkRenderDynamicDataDirty();
			}

		}
	}
	
	//Begin records a frame so need to set things up first
	for (IMovieSceneToolsAnimationBakeHelper* BakeHelper : BakeHelpers)
	{
		if (BakeHelper)
		{
			BakeHelper->PreEvaluation(MovieScene,LocalStartFrame);
		}
	}
	// This evaluates the MoviePlayer
	AnimTrackAdapter.UpdateAnimation(LocalStartFrame);
	for (IMovieSceneToolsAnimationBakeHelper* BakeHelper : BakeHelpers)
	{		
		if (BakeHelper)
		{
			BakeHelper->PostEvaluation(MovieScene,LocalStartFrame);
		}
	}
	for (USkeletalMeshComponent* SkelMeshComp : SkelMeshComps)
	{
		SkelMeshComp->TickAnimation(DeltaTime, false);
		SkelMeshComp->RefreshBoneTransforms();
		SkelMeshComp->RefreshSlaveComponents();
		SkelMeshComp->UpdateComponentToWorld();
		SkelMeshComp->FinalizeBoneTransform();
		SkelMeshComp->MarkRenderTransformDirty();
		SkelMeshComp->MarkRenderDynamicDataDirty();
	}
	
	TickLiveLink(LiveLinkClient, SourceAndMode);

	StartCallback.ExecuteIfBound();
	for (int32 FrameCount = 1; FrameCount <= AnimationLength; ++FrameCount)
	{
		int32 LocalFrame = LocalStartFrame + FrameCount;

		for (IMovieSceneToolsAnimationBakeHelper* BakeHelper : BakeHelpers)
		{
			if (BakeHelper)
			{
				BakeHelper->PreEvaluation(MovieScene, LocalStartFrame);
			}
		}
		// This will call UpdateSkelPose on the skeletal mesh component to move bones based on animations in the matinee group
		AnimTrackAdapter.UpdateAnimation(LocalFrame);
		for (IMovieSceneToolsAnimationBakeHelper* BakeHelper : BakeHelpers)
		{
			if (BakeHelper)
			{
				BakeHelper->PostEvaluation(MovieScene, LocalStartFrame);
			}
		}

		//Live Link sourcer can show up at any time so we unfortunately need to check for it
		TickLiveLink(LiveLinkClient, SourceAndMode);

		// Update space bases so new animation position has an effect.
		for (USkeletalMeshComponent* SkelMeshComp : SkelMeshComps)
		{
			SkelMeshComp->TickAnimation(DeltaTime, false);

			SkelMeshComp->RefreshBoneTransforms();
			SkelMeshComp->RefreshSlaveComponents();
			SkelMeshComp->UpdateComponentToWorld();
			SkelMeshComp->FinalizeBoneTransform();
			SkelMeshComp->MarkRenderTransformDirty();
			SkelMeshComp->MarkRenderDynamicDataDirty();
		}

		TickCallback.ExecuteIfBound(DeltaTime);
	}

	for (IMovieSceneToolsAnimationBakeHelper* BakeHelper : BakeHelpers)
	{
		if (BakeHelper)
		{
			BakeHelper->StopBaking(MovieScene);
		}
	}
	EndCallback.ExecuteIfBound();

	//now do any sequencer live link cleanup
	if (LiveLinkClient)
	{
		for (TPair<FGuid, ELiveLinkSourceMode>& Item : SourceAndMode)
		{
			ULiveLinkSourceSettings* Settings = LiveLinkClient->GetSourceSettings(Item.Key);
			if (Settings)
			{
				Settings->Mode = Item.Value;
			}
		}
	}

	if (SequencerAlwaysSenedLiveLinkInterpolated.IsSet() && CVarAlwaysSendInterpolatedLiveLink)
	{
		CVarAlwaysSendInterpolatedLiveLink->Set(0, ECVF_SetByConsole);
	}
	return true;
}

bool MovieSceneToolHelpers::ExportToAnimSequence(UAnimSequence* AnimSequence, UAnimSeqExportOption* ExportOptions, UMovieScene* MovieScene, IMovieScenePlayer* Player,
	USkeletalMeshComponent* SkelMeshComp, FMovieSceneSequenceIDRef& Template, FMovieSceneSequenceTransform& RootToLocalTransform)
{
	FAnimRecorderInstance AnimationRecorder;
	FFrameRate SampleRate = MovieScene->GetDisplayRate();
	FInitAnimationCB InitCallback = FInitAnimationCB::CreateLambda([&AnimationRecorder,SampleRate,ExportOptions,SkelMeshComp,AnimSequence]
	{
		FAnimationRecordingSettings RecordingSettings;
		RecordingSettings.SampleRate = SampleRate.AsDecimal();
		RecordingSettings.InterpMode = ERichCurveInterpMode::RCIM_Cubic;
		RecordingSettings.TangentMode = ERichCurveTangentMode::RCTM_Auto;
		RecordingSettings.Length = 0;
		RecordingSettings.bRemoveRootAnimation = false;
		RecordingSettings.bCheckDeltaTimeAtBeginning = false;
		RecordingSettings.bRecordTransforms = ExportOptions->bExportTransforms;
		RecordingSettings.bRecordCurves = ExportOptions->bExportCurves;
		RecordingSettings.bRecordInWorldSpace = ExportOptions->bRecordInWorldSpace;
		AnimationRecorder.Init(SkelMeshComp, AnimSequence, nullptr, RecordingSettings);	
		});

	
	FStartAnimationCB StartCallback = FStartAnimationCB::CreateLambda([&AnimationRecorder]
	{
		AnimationRecorder.BeginRecording();
	});

	FTickAnimationCB TickCallback = FTickAnimationCB::CreateLambda([&AnimationRecorder](float DeltaTime)
	{
		AnimationRecorder.Update(DeltaTime);

	});

	FEndAnimationCB EndCallback = FEndAnimationCB::CreateLambda([&AnimationRecorder]
	{
			const bool bShowAnimationAssetCreatedToast = false;
			AnimationRecorder.FinishRecording(bShowAnimationAssetCreatedToast);
	});
	

	MovieSceneToolHelpers::BakeToSkelMeshToCallbacks(MovieScene,Player,
		SkelMeshComp, Template, RootToLocalTransform, ExportOptions,
		InitCallback, StartCallback, TickCallback, EndCallback);
	return true;
}

FSpawnableRestoreState::FSpawnableRestoreState(UMovieScene* MovieScene)
	: bWasChanged(false)
	, WeakMovieScene(MovieScene)
{
	for (int32 SpawnableIndex = 0; SpawnableIndex < WeakMovieScene->GetSpawnableCount(); ++SpawnableIndex)
	{
		FMovieSceneSpawnable& Spawnable = WeakMovieScene->GetSpawnable(SpawnableIndex);

		UMovieSceneSpawnTrack* SpawnTrack = WeakMovieScene->FindTrack<UMovieSceneSpawnTrack>(Spawnable.GetGuid());

		if (SpawnTrack && SpawnTrack->GetAllSections().Num() > 0)
		{
			// Start a transaction that will be undone later for the modifications to the spawn track
			if (!bWasChanged)
			{
				GEditor->BeginTransaction(NSLOCTEXT("MovieSceneToolHelpers", "SpwanableRestoreState", "SpawnableRestoreState"));
			}

			bWasChanged = true;
			
			// Spawnable could be in a subscene, so temporarily override it to persist throughout
			SpawnOwnershipMap.Add(Spawnable.GetGuid(), Spawnable.GetSpawnOwnership());
			Spawnable.SetSpawnOwnership(ESpawnOwnership::MasterSequence);

			UMovieSceneSpawnSection* SpawnSection = Cast<UMovieSceneSpawnSection>(SpawnTrack->GetAllSections()[0]);
			SpawnSection->Modify();
			SpawnSection->GetChannel().Reset();
			SpawnSection->GetChannel().SetDefault(true);
		}
	}

	if (bWasChanged)
	{
		GEditor->EndTransaction();
	}
}
FSpawnableRestoreState::~FSpawnableRestoreState()
{
	if (!bWasChanged || !WeakMovieScene.IsValid())
	{
		return;
	}

	// Restore spawnable owners
	for (int32 SpawnableIndex = 0; SpawnableIndex < WeakMovieScene->GetSpawnableCount(); ++SpawnableIndex)
	{
		FMovieSceneSpawnable& Spawnable = WeakMovieScene->GetSpawnable(SpawnableIndex);
		Spawnable.SetSpawnOwnership(SpawnOwnershipMap[Spawnable.GetGuid()]);
	}

	// Restore modified spawned sections
	bool bOrigSquelchTransactionNotification = GEditor->bSquelchTransactionNotification;
	GEditor->bSquelchTransactionNotification = true;
	GEditor->UndoTransaction(false);
	GEditor->bSquelchTransactionNotification = bOrigSquelchTransactionNotification;
}


void MovieSceneToolHelpers::GetParents(TArray<const UObject*>& Parents, const UObject* InObject)
{
	const AActor* Actor = Cast<AActor>(InObject);
	if (Actor)
	{
		Parents.Emplace(Actor);
		const AActor* ParentActor = Actor->GetAttachParentActor();
		if (ParentActor)
		{
			GetParents(Parents, ParentActor);
		}
	}
}
/** This is not that scalable moving forward with stuff like the control rig , need a better caching solution there */
bool MovieSceneToolHelpers::GetParentTM(FTransform& CurrentRefTM, const TSharedPtr<ISequencer>& Sequencer, UObject* ParentObject, FFrameTime KeyTime)
{
	UMovieSceneSequence* Sequence = Sequencer->GetFocusedMovieSceneSequence();
	if (!Sequence)
	{
		return false;
	}

	FGuid ObjectBinding = Sequencer->FindCachedObjectId(*ParentObject, Sequencer->GetFocusedTemplateID());
	if (!ObjectBinding.IsValid())
	{
		return false;
	}

	const FMovieSceneBinding* Binding = Sequence->GetMovieScene()->FindBinding(ObjectBinding);
	if (!Binding)
	{
		return false;
	}
	//TODO this doesn't handle blended sections at all
	for (const UMovieSceneTrack* Track : Binding->GetTracks())
	{
		const UMovieScene3DTransformTrack* TransformTrack = Cast<UMovieScene3DTransformTrack>(Track);
		if (!TransformTrack)
		{
			continue;
		}

		//we used to loop between sections here and only evaluate if we are in a section, this will give us wrong transfroms though
		//when in between or outside of the section range. We still want to evaluate, though it is heavy.

		const FMovieSceneEvaluationTrack* EvalTrack = MovieSceneToolHelpers::GetEvaluationTrack(Sequencer.Get(), TransformTrack->GetSignature());
		if (EvalTrack)
		{
			FVector ParentKeyPos;
			FRotator ParentKeyRot;
			GetLocationAtTime(EvalTrack, ParentObject, KeyTime, ParentKeyPos, ParentKeyRot, Sequencer);
			CurrentRefTM = FTransform(ParentKeyRot, ParentKeyPos);
			return true;
		}

	}

	return false;
}

FTransform MovieSceneToolHelpers::GetRefFrameFromParents(const TSharedPtr<ISequencer>& Sequencer, const TArray<const UObject*>& Parents, FFrameTime KeyTime)
{
	FTransform RefTM = FTransform::Identity;
	FTransform ParentRefTM = FTransform::Identity;

	for (const UObject* Object : Parents)
	{
		const AActor* Actor = Cast<AActor>(Object);
		if (Actor != nullptr)
		{
			if (Actor->GetRootComponent() != nullptr && Actor->GetRootComponent()->GetAttachParent() != nullptr)
			{
				//Always get local ref tm since we don't know which parent is in the sequencer or not.
				if (!GetParentTM(ParentRefTM, Sequencer, Actor->GetRootComponent()->GetAttachParent()->GetOwner(), KeyTime))
				{
					AActor* Parent = Actor->GetRootComponent()->GetAttachParent()->GetOwner();
					if (Parent && Parent->GetRootComponent())
					{
						ParentRefTM = Parent->GetRootComponent()->GetRelativeTransform();
					}
					else
					{
						continue;
					}
				}
				RefTM = ParentRefTM * RefTM;
			}
		}
		else
		{
			const USceneComponent* SceneComponent = Cast<USceneComponent>(Object);
			FTransform CurrentRefTM = FTransform::Identity;
			UObject* ParentObject = SceneComponent->GetAttachParent() == SceneComponent->GetOwner()->GetRootComponent() ? static_cast<UObject*>(SceneComponent->GetOwner()) : SceneComponent->GetAttachParent();

			if (SceneComponent->GetAttachParent() != nullptr)
			{
				if (!GetParentTM(CurrentRefTM, Sequencer, ParentObject, KeyTime))
				{
					CurrentRefTM = RefTM * SceneComponent->GetAttachParent()->GetRelativeTransform();
				}
			}
			RefTM = CurrentRefTM * RefTM;
		}
	}
	return RefTM;
}

void MovieSceneToolHelpers::GetLocationAtTime(const FMovieSceneEvaluationTrack* Track, UObject* Object, FFrameTime KeyTime, FVector& KeyPos, FRotator& KeyRot, const TSharedPtr<ISequencer>& Sequencer)
{
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	// TODO: Reimplement trajectory rendering
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	UE_MOVIESCENE_TODO(Reimplement trajectory rendering)
}
