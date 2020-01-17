// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDLevelSequenceHelper.h"

#include "USDErrorUtils.h"
#include "USDListener.h"
#include "USDMemory.h"
#include "USDStageActor.h"
#include "USDTypesConversion.h"

#include "Channels/MovieSceneChannelProxy.h"
#include "CoreMinimal.h"
#include "Editor.h"
#include "LevelSequence.h"
#include "MovieScene.h"
#include "ObjectTools.h"
#include "Sections/MovieSceneFloatSection.h"
#include "Sections/MovieSceneSubSection.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "Tracks/MovieSceneSubTrack.h"
#include "UObject/UObjectGlobals.h"

#if USE_USD_SDK
#include "USDIncludesStart.h"

#include "pxr/pxr.h"
#include "pxr/usd/sdf/layer.h"
#include "pxr/usd/sdf/layerUtils.h"
#include "pxr/usd/usd/stage.h"
#include "boost/functional/hash.hpp"

#include "USDIncludesEnd.h"

#define DEFAULT_FRAMERATE 24.0
#define TIME_TRACK_NAME TEXT("Time")

class FUsdLevelSequenceHelperImpl
{
public:
	struct FLayerTimeInfo
	{
		FString Identifier;
		FString FilePath;

		TArray<FString> SubLayerIdentifiers;
		TArray<double> OffsetTimeCodes;
		TArray<double> Scales;

		TOptional<double> StartTimeCode;
		TOptional<double> EndTimeCode;
		TOptional<double> TimeCodesPerSecond;

		bool IsAnimated() const
		{
			return StartTimeCode.IsSet() && EndTimeCode.IsSet() && TimeCodesPerSecond.IsSet();
		}
	};

	FUsdLevelSequenceHelperImpl(TWeakObjectPtr<AUsdStageActor> InStageActor);
	~FUsdLevelSequenceHelperImpl();

	/** Returns the FLayerTimeInfo corresponding to the root layer */
	FLayerTimeInfo* GetRootLayerInfo();

	/** Creates all the ULevelSequence assets for all FLayerTimeInfos we have */
	void CreateLevelSequences();

	/**
	 * Creates the subsequence tracks on the ULevelSequence corresponding to Info, as well
	 * as recursively for all sublayers of Info
	 */
	void CreateSubsequenceTrack(const FUsdLevelSequenceHelperImpl::FLayerTimeInfo* Info) const;

	/** Creates a time track on the ULevelSequence corresponding to Info */
	void CreateTimeTrack(const FUsdLevelSequenceHelperImpl::FLayerTimeInfo* Info) const;

	/** Construct and cache a FLayerTimeInfo object for each SdfLayer within UsdStage */
	void RebuildLayerTimeInfoCache(const pxr::UsdStageRefPtr& UsdStage);

	/**
	 * Initializes the StageActor Start and End timecodes (as well as our root layer info)
	 * with the "top-level" timecodes from UsdStage
	 */
	void InitializeActorTimeCodes(const pxr::UsdStageRefPtr& UsdStage);

private:
	/** Create a FLayerTimeInfo object with data from LayerHandle, and calls itself for all of its sublayers */
	FLayerTimeInfo* RecursivelyCreateLayerTimeInfo(const TUsdStore<pxr::SdfLayerHandle>& LayerHandle, TSet<FString>& ExploredFiles);

	/**
	 * Propagate timecodes for leaf FLayerTimeInfo all the way up to Info. We use this to calculate enveloping
	 * time codes that we can use for parent layers, in case they don't have time code information but have animated sublayers
	 */
	void RecursivelyPropagateTimeCodes(FUsdLevelSequenceHelperImpl::FLayerTimeInfo* Info);

	/** Updates a FLayerTimeInfo object with new offset/scale values when Section has been moved by the user */
	void UpdateInfoFromSection(FUsdLevelSequenceHelperImpl::FLayerTimeInfo* Info, const UMovieSceneSubSection* Section, const UMovieSceneSequence* Sequence);

	/** Capture whenever a UMovieSceneSubSection has transacted and call UpdateInfoFromSection */
	void OnObjectTransacted(UObject* Object, const class FTransactionObjectEvent& Event);

private:
	FString RootLayerInfoIdentifier;
	TMap<FString, FUsdLevelSequenceHelperImpl::FLayerTimeInfo> LayerTimeInfosByIdentifier;
	TMap<FName, FString> LayerInfoIdentifierByLevelSequenceName;
	TWeakObjectPtr<AUsdStageActor> StageActor;
	FGuid StageActorBinding;
	static EObjectFlags DefaultObjFlags;
	FDelegateHandle OnObjectTransactedHandle;
};

EObjectFlags FUsdLevelSequenceHelperImpl::DefaultObjFlags = EObjectFlags::RF_Transactional | EObjectFlags::RF_Transient | EObjectFlags::RF_Public;

FUsdLevelSequenceHelperImpl::FUsdLevelSequenceHelperImpl(TWeakObjectPtr<AUsdStageActor> InStageActor)
	: StageActor(InStageActor)
	, StageActorBinding()
{
	OnObjectTransactedHandle = FCoreUObjectDelegates::OnObjectTransacted.AddRaw(this, &FUsdLevelSequenceHelperImpl::OnObjectTransacted);
}

FUsdLevelSequenceHelperImpl::~FUsdLevelSequenceHelperImpl()
{
	FCoreUObjectDelegates::OnObjectTransacted.Remove(OnObjectTransactedHandle);
	OnObjectTransactedHandle.Reset();
}

FUsdLevelSequenceHelperImpl::FLayerTimeInfo* FUsdLevelSequenceHelperImpl::GetRootLayerInfo()
{
	return LayerTimeInfosByIdentifier.Find(RootLayerInfoIdentifier);
}

void FUsdLevelSequenceHelperImpl::CreateLevelSequences()
{
	AUsdStageActor* ValidStageActor = StageActor.Get();
	if (!ValidStageActor)
	{
		return;
	}

	double TimeCodesPerSecond = ValidStageActor->TimeCodesPerSecond;

	UE_LOG(LogUsdStage, Verbose, TEXT("CreateLevelSequences: Initializing level sequence with '%f' fps"), TimeCodesPerSecond);
	if ( ValidStageActor->LevelSequence )
	{
		return;
	}

	const FLayerTimeInfo* RootLayerInfo = GetRootLayerInfo();
	if (!RootLayerInfo)
	{
		return;
	}
	FString Name = ObjectTools::SanitizeObjectName(FPaths::GetCleanFilename(RootLayerInfo->FilePath));

	// Create level sequence for root layer (visible on StageActor's details panel)
	ValidStageActor->LevelSequence = NewObject<ULevelSequence>(GetTransientPackage(), *Name, FUsdLevelSequenceHelperImpl::DefaultObjFlags);
	ValidStageActor->LevelSequence->Initialize();
	ValidStageActor->LevelSequence->MovieScene->SetDisplayRate(FFrameRate(TimeCodesPerSecond, 1));
	LayerInfoIdentifierByLevelSequenceName.Add(ValidStageActor->LevelSequence->GetFName(), RootLayerInfoIdentifier);

	// Bind stage actor
	UMovieScene* MovieScene = ValidStageActor->LevelSequence->GetMovieScene();
	StageActorBinding = MovieScene->AddPossessable(ValidStageActor->GetActorLabel(), ValidStageActor->GetClass());
	ValidStageActor->LevelSequence->BindPossessableObject(StageActorBinding, *ValidStageActor, ValidStageActor->GetWorld());

	// Create level sequences for all sub layers (accessible via the main level sequence but otherwise hidden)
	for (TPair<FString, FUsdLevelSequenceHelperImpl::FLayerTimeInfo>& InfoPair : LayerTimeInfosByIdentifier)
	{
		FUsdLevelSequenceHelperImpl::FLayerTimeInfo& Info = InfoPair.Value;
		const FString& Identifier = InfoPair.Key;
		if (Identifier == RootLayerInfoIdentifier)
		{
			continue;
		}

		ULevelSequence*& Sequence = ValidStageActor->SubLayerLevelSequencesByIdentifier.FindOrAdd(Identifier);
		if (!Sequence)
		{
			FString SubName = ObjectTools::SanitizeObjectName(FPaths::GetCleanFilename(Info.FilePath));

			Sequence = NewObject<ULevelSequence>(GetTransientPackage(), *SubName, FUsdLevelSequenceHelperImpl::DefaultObjFlags);
			Sequence->Initialize();
			LayerInfoIdentifierByLevelSequenceName.Add(Sequence->GetFName(), Identifier);

			UMovieScene* SubMovieScene = Sequence->MovieScene;
			if (!SubMovieScene)
			{
				continue;
			}
			SubMovieScene->SetDisplayRate(FFrameRate(Info.TimeCodesPerSecond.GetValue(), 1));

			UE_LOG(LogUsdStage, Verbose, TEXT("CreateLevelSequences: Created new LevelSequence '%s' for layer '%s'"), *Sequence->GetName(), *Identifier);
		}
	}
}

void FUsdLevelSequenceHelperImpl::CreateSubsequenceTrack(const FUsdLevelSequenceHelperImpl::FLayerTimeInfo* Info) const
{
	if (!Info)
	{
		return;
	}

	AUsdStageActor* ValidStageActor = StageActor.Get();
	if (!ValidStageActor)
	{
		return;
	}

	UE_LOG(LogUsdStage, Verbose, TEXT("CreateSubsequenceSections: Initializing for layer '%s'"), *Info->Identifier);

	// Recurse first as we'll need the sublayer LevelSequences to be completed to insert them as subsections
	bool bFoundAnimatedSubLayer = false;
	for (const FString& SubLayerIdentifier : Info->SubLayerIdentifiers)
	{
		if (const FUsdLevelSequenceHelperImpl::FLayerTimeInfo* SubLayerInfo = LayerTimeInfosByIdentifier.Find(SubLayerIdentifier))
		{
			if (SubLayerInfo->IsAnimated())
			{
				CreateSubsequenceTrack(SubLayerInfo);
				bFoundAnimatedSubLayer = true;
			}
		}
	}

	ULevelSequence* Sequence = nullptr;
	if (Info->Identifier == RootLayerInfoIdentifier)
	{
		Sequence = ValidStageActor->LevelSequence;
	}
	else if (ULevelSequence** FoundSequence = ValidStageActor->SubLayerLevelSequencesByIdentifier.Find(Info->Identifier))
	{
		Sequence = *FoundSequence;
	}

	if (Sequence != nullptr && Info->IsAnimated() && bFoundAnimatedSubLayer)
	{
		UE_LOG(LogUsdStage, Verbose, TEXT("CreateSubsequenceSections: Layer '%s' is animated"), *Info->Identifier);

		UMovieScene* MovieScene = Sequence->GetMovieScene();
		if (!MovieScene)
		{
			return;
		}

		FFrameRate FrameRate = MovieScene->GetTickResolution();

		UMovieSceneSubTrack* SubTrack = MovieScene->FindMasterTrack<UMovieSceneSubTrack>();
		if (!SubTrack)
		{
			SubTrack = MovieScene->AddMasterTrack<UMovieSceneSubTrack>();
		}
		SubTrack->RemoveAllAnimationData();

		for (int32 SubLayerIndex = 0; SubLayerIndex < Info->SubLayerIdentifiers.Num(); ++SubLayerIndex)
		{
			const FUsdLevelSequenceHelperImpl::FLayerTimeInfo* SubInfo = LayerTimeInfosByIdentifier.Find(Info->SubLayerIdentifiers[SubLayerIndex]);
			if (!SubInfo || !SubInfo->IsAnimated())
			{
				continue;
			}

			ULevelSequence** SubSequence = ValidStageActor->SubLayerLevelSequencesByIdentifier.Find(SubInfo->Identifier);
			if (!SubSequence || !*SubSequence)
			{
				UE_LOG(LogUsdStage, Warning, TEXT("CreateSubsequenceSections: Invalid LevelSequence for sublayer '%s'"), *SubInfo->Identifier);
				continue;
			}

			double SubOffsetTimeCode = Info->OffsetTimeCodes[SubLayerIndex];
			double SubScale = Info->Scales[SubLayerIndex];
			double SubTimeCodesPerSecond = SubInfo->TimeCodesPerSecond.GetValue();
			double SubStartSeconds  = (SubOffsetTimeCode + SubScale * SubInfo->StartTimeCode.GetValue()) / SubTimeCodesPerSecond;
			double SubEndSeconds 	= (SubOffsetTimeCode + SubScale * SubInfo->EndTimeCode.GetValue())   / SubTimeCodesPerSecond;

			FFrameNumber StartFrame = FrameRate.AsFrameNumber(SubStartSeconds);
			int32 Duration = FrameRate.AsFrameNumber(SubEndSeconds).Value - StartFrame.Value;
			UMovieSceneSubSection* NewSection = SubTrack->AddSequenceOnRow(*SubSequence, StartFrame, Duration, INDEX_NONE);

			UE_LOG(LogUsdStage, Verbose, TEXT("CreateSubsequenceSections: Layer '%s' received subsection for layer '%s' starting at frame '%d' and with duration '%d'"),
				*Info->Identifier,
				*SubInfo->Identifier,
				StartFrame.Value,
				Duration);
		}

		// Setup view/work ranges
		double TimeCodesPerSecond = Info->TimeCodesPerSecond.GetValue();
		double StartTimeSeconds   = Info->StartTimeCode.GetValue() / TimeCodesPerSecond;
		double EndTimeSeconds     = Info->EndTimeCode.GetValue() / TimeCodesPerSecond;

		FFrameNumber StartFrame = FrameRate.AsFrameNumber(StartTimeSeconds);
		FFrameNumber EndFrame = FrameRate.AsFrameNumber(EndTimeSeconds);
		TRange<FFrameNumber> TimeRange = TRange<FFrameNumber>::Inclusive(StartFrame, EndFrame);

		MovieScene->SetPlaybackRange(TimeRange);
		MovieScene->SetViewRange(StartTimeSeconds - 1.0f, 1.0f + EndTimeSeconds);
		MovieScene->SetWorkingRange(StartTimeSeconds - 1.0f, 1.0f + EndTimeSeconds);

		UE_LOG(LogUsdStage, Verbose, TEXT("CreateSubsequenceSections: Layer '%s' received view range [%f, %f]"),
			*Info->Identifier,
			StartTimeSeconds - 1.0f,
			EndTimeSeconds + 1.0f);
	}
}

void FUsdLevelSequenceHelperImpl::CreateTimeTrack(const FUsdLevelSequenceHelperImpl::FLayerTimeInfo* Info) const
{
	if (!Info || !Info->IsAnimated())
	{
		return;
	}

	AUsdStageActor* ValidStageActor = StageActor.Get();
	if (!ValidStageActor)
	{
		return;
	}

	ULevelSequence* Sequence = nullptr;
	if (Info->Identifier == RootLayerInfoIdentifier)
	{
		Sequence = ValidStageActor->LevelSequence;
	}
	else if (ULevelSequence** FoundSequence = ValidStageActor->SubLayerLevelSequencesByIdentifier.Find(Info->Identifier))
	{
		Sequence = *FoundSequence;
	}
	if (!Sequence)
	{
		return;
	}

	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if(!MovieScene)
	{
		return;
	}

	UMovieSceneFloatTrack* TimeTrack = MovieScene->FindTrack<UMovieSceneFloatTrack>(StageActorBinding, FName(TIME_TRACK_NAME));
	if (TimeTrack)
	{
		TimeTrack->RemoveAllAnimationData();
	}
	else
	{
		TimeTrack = MovieScene->AddTrack<UMovieSceneFloatTrack>(StageActorBinding);
		TimeTrack->SetPropertyNameAndPath(FName(TIME_TRACK_NAME), "Time");

		MovieScene->SetEvaluationType(EMovieSceneEvaluationType::FrameLocked);
	}

	double StartTimeCode = Info->StartTimeCode.GetValue();
	double EndTimeCode = Info->EndTimeCode.GetValue();
	double TimeCodesPerSecond = Info->TimeCodesPerSecond.Get(DEFAULT_FRAMERATE);

	FFrameRate DestFrameRate = MovieScene->GetTickResolution();
	FFrameNumber StartFrame  = DestFrameRate.AsFrameNumber(StartTimeCode / TimeCodesPerSecond);
	FFrameNumber EndFrame    = DestFrameRate.AsFrameNumber(EndTimeCode / TimeCodesPerSecond);

	bool bSectionAdded = false;
	UMovieSceneFloatSection* TimeSection = Cast<UMovieSceneFloatSection>(TimeTrack->FindOrAddSection(0, bSectionAdded));
	TimeSection->EvalOptions.CompletionMode = EMovieSceneCompletionMode::KeepState;
	TimeSection->SetRange(TRange<FFrameNumber>::All());

	TArray<FFrameNumber> FrameNumbers;
	FrameNumbers.Add(StartFrame);
	FrameNumbers.Add(EndFrame);

	TArray<FMovieSceneFloatValue> FrameValues;
	FrameValues.Add_GetRef(FMovieSceneFloatValue(StartTimeCode)).InterpMode = ERichCurveInterpMode::RCIM_Linear;
	FrameValues.Add_GetRef(FMovieSceneFloatValue(EndTimeCode)).InterpMode = ERichCurveInterpMode::RCIM_Linear;

	FMovieSceneFloatChannel* TimeChannel = TimeSection->GetChannelProxy().GetChannel<FMovieSceneFloatChannel>(0);
	TimeChannel->Set(FrameNumbers, FrameValues);

	TRange<FFrameNumber> TimeRange = TRange<FFrameNumber>::Inclusive(StartFrame, EndFrame);

	MovieScene->SetPlaybackRange( TimeRange );
	MovieScene->SetViewRange( StartTimeCode / TimeCodesPerSecond -1.0f, 1.0f + EndTimeCode / TimeCodesPerSecond );
	MovieScene->SetWorkingRange( StartTimeCode / TimeCodesPerSecond -1.0f, 1.0f + EndTimeCode / TimeCodesPerSecond );
}

void FUsdLevelSequenceHelperImpl::RebuildLayerTimeInfoCache(const pxr::UsdStageRefPtr& UsdStage)
{
	if (!UsdStage)
	{
		return;
	}

	UE_LOG(LogUsdStage, Verbose, TEXT("RebuildLayerTimeInfoCache"));

	LayerTimeInfosByIdentifier.Reset();
	LayerInfoIdentifierByLevelSequenceName.Reset();

	TUsdStore<pxr::SdfLayerHandle> RootLayerHandle = MakeUsdStore<pxr::SdfLayerHandle>(UsdStage->GetRootLayer());

	// Used to catch N-item infinite loops (e.g. A/B/C/A/B/C/etc)
	TSet<FString> ExploredPaths;
	FLayerTimeInfo* RootLayerInfo = RecursivelyCreateLayerTimeInfo(RootLayerHandle.Get(), ExploredPaths);
	RootLayerInfoIdentifier = RootLayerInfo->Identifier;

	RecursivelyPropagateTimeCodes(RootLayerInfo);
}

void FUsdLevelSequenceHelperImpl::InitializeActorTimeCodes(const pxr::UsdStageRefPtr& UsdStage)
{
	AUsdStageActor* ValidStageActor = StageActor.Get();
	FUsdLevelSequenceHelperImpl::FLayerTimeInfo* RootInfo = GetRootLayerInfo();
	if (!ValidStageActor || !RootInfo)
	{
		return;
	}

	{
		FScopedUsdAllocs Allocs;
		pxr::SdfLayerHandle RootLayer = UsdStage->GetRootLayer();
		pxr::SdfLayerHandle SessionLayer = UsdStage->GetSessionLayer();

		// If the stage has a valid start time code, use that
		if ((RootLayer && RootLayer->HasStartTimeCode()) ||
			(SessionLayer && SessionLayer->HasStartTimeCode()))
		{
			RootInfo->StartTimeCode = UsdStage->GetStartTimeCode();
		}
		// Or else, instead of wrapping the first animated layer, start at timecode zero
		else
		{
			RootInfo->StartTimeCode = 0;
		}

		// If the stage has a valid end time code, use that. Or else we'll
		// keep the one we calculated (range of all sublayers)
		if ((RootLayer && RootLayer->HasEndTimeCode()) ||
			(SessionLayer && SessionLayer->HasEndTimeCode()))
		{
			RootInfo->EndTimeCode = UsdStage->GetEndTimeCode();
		}
	}

	ValidStageActor->StartTimeCode = RootInfo->StartTimeCode.Get(0);
	ValidStageActor->EndTimeCode = RootInfo->EndTimeCode.Get(0);
	ValidStageActor->TimeCodesPerSecond = RootInfo->TimeCodesPerSecond.Get(DEFAULT_FRAMERATE);
	UE_LOG(LogUsdStage, Verbose, TEXT("Initialized AUsdStageActor's Stage. StartTimeCode: %f, EndTimeCode: %f, TimeCodesPerSeconds: %f"), ValidStageActor->StartTimeCode, ValidStageActor->EndTimeCode, ValidStageActor->TimeCodesPerSecond);
}

FUsdLevelSequenceHelperImpl::FLayerTimeInfo* FUsdLevelSequenceHelperImpl::RecursivelyCreateLayerTimeInfo(const TUsdStore<pxr::SdfLayerHandle>& LayerHandleWrapper, TSet<FString>& ExploredFiles)
{
	const pxr::SdfLayerHandle& LayerHandle = LayerHandleWrapper.Get();

	// We want to keep track of all SdfLayers we find, but each layer should only appear once in a path to the root, or else we infinitely loop
	FString Identifier = UsdToUnreal::ConvertString(LayerHandle->GetIdentifier());
	if (ExploredFiles.Contains(Identifier))
	{
		UE_LOG(LogUsdStage, Warning, TEXT("Detected infinite SubLayer recursion when visiting layer with identifier '%s'!"), *Identifier);
		return nullptr;
	}

	FString FilePath = UsdToUnreal::ConvertString(LayerHandle->GetRealPath());
	ExploredFiles.Add(FilePath);

	FUsdLevelSequenceHelperImpl::FLayerTimeInfo LayerTimeInfo;
	LayerTimeInfo.Identifier = Identifier;
	LayerTimeInfo.FilePath = FilePath;
	LayerTimeInfo.StartTimeCode      = LayerHandle->HasStartTimeCode() ? LayerHandle->GetStartTimeCode() : TOptional<double>();
	LayerTimeInfo.EndTimeCode        = LayerHandle->HasEndTimeCode() ? LayerHandle->GetEndTimeCode() : TOptional<double>();
	LayerTimeInfo.TimeCodesPerSecond = LayerHandle->HasTimeCodesPerSecond() ? LayerHandle->GetTimeCodesPerSecond() : TOptional<double>();

	UE_LOG(LogUsdStage, Verbose, TEXT("Creating layer time info for layer '%s'. Original timecodes: '%s', '%s' and '%s'"),
		*LayerTimeInfo.Identifier,
		LayerTimeInfo.StartTimeCode.IsSet() ? *LexToString(LayerTimeInfo.StartTimeCode.GetValue()) : TEXT("null"),
		LayerTimeInfo.EndTimeCode.IsSet() ? *LexToString(LayerTimeInfo.EndTimeCode.GetValue()) : TEXT("null"),
		LayerTimeInfo.TimeCodesPerSecond.IsSet() ? *LexToString(LayerTimeInfo.TimeCodesPerSecond.GetValue()) : TEXT("null"));

	TUsdStore<std::vector<std::string>> SubLayerPaths;
	TUsdStore<std::vector<pxr::SdfLayerOffset>> SubLayerOffsetScales;
	{
		FScopedUsdAllocs USDAllocs;
		SubLayerPaths = LayerHandle->GetSubLayerPaths();
		SubLayerOffsetScales = LayerHandle->GetSubLayerOffsets();
	}

	for (int64 LayerIndex = 0; LayerIndex < static_cast<int64>(LayerHandle->GetNumSubLayerPaths()); ++LayerIndex)
	{
		TUsdStore<pxr::SdfLayerHandle> SubLayer;
		{
			FScopedUsdAllocs USDAllocs;
			std::string RelativePath = pxr::SdfComputeAssetPathRelativeToLayer(LayerHandle, SubLayerPaths.Get()[LayerIndex]);
			SubLayer = LayerHandle->Find(RelativePath);
		}

		if (!SubLayer.Get())
		{
			continue;
		}

		const pxr::SdfLayerOffset& SubLayerOffsetScale = SubLayerOffsetScales.Get()[LayerIndex];
		double SubOffset = 0.0;
		double SubScale = 1.0;
		if (SubLayerOffsetScale.IsValid())
		{
			SubOffset = SubLayerOffsetScale.GetOffset();
			SubScale = SubLayerOffsetScale.GetScale();
		}

		if (FUsdLevelSequenceHelperImpl::FLayerTimeInfo* SubLayerInfo = RecursivelyCreateLayerTimeInfo(SubLayer, ExploredFiles))
		{
			UE_LOG(LogUsdStage, Verbose, TEXT("Adding sublayer '%s' to layer '%s'. Offset: '%f', Scale: '%f'"),
				*SubLayerInfo->Identifier,
				*LayerTimeInfo.Identifier,
				SubOffset,
				SubScale);

			LayerTimeInfo.SubLayerIdentifiers.Add(SubLayerInfo->Identifier);
			LayerTimeInfo.OffsetTimeCodes.Add(SubOffset);
			LayerTimeInfo.Scales.Add(SubScale);
		}
	}

	ExploredFiles.Remove(FilePath);
	return &(LayerTimeInfosByIdentifier.Add(Identifier, LayerTimeInfo));
}

void FUsdLevelSequenceHelperImpl::RecursivelyPropagateTimeCodes(FUsdLevelSequenceHelperImpl::FLayerTimeInfo* Info)
{
	if (!Info)
	{
		return;
	}

	// Recurse first as the timecodes must propagate upwards to the root
	for (const FString& SubLayerIdentifier : Info->SubLayerIdentifiers)
	{
		if (FUsdLevelSequenceHelperImpl::FLayerTimeInfo* SubLayerInfo = LayerTimeInfosByIdentifier.Find(SubLayerIdentifier))
		{
			RecursivelyPropagateTimeCodes(SubLayerInfo);
		}
	}

	if (!Info->TimeCodesPerSecond.IsSet())
	{
		Info->TimeCodesPerSecond = DEFAULT_FRAMERATE;
	}

	// Don't propagate child timecodes if the layer has any manual timecodes set.
	// That should work as kind of an user override
	bool bHaveStartCode	= Info->StartTimeCode.IsSet();
	bool bHaveEndCode	= Info->EndTimeCode.IsSet();
	if (bHaveStartCode && bHaveEndCode)
	{
		UE_LOG(LogUsdStage, Verbose, TEXT("RecursivelyPropagateTimeCodes: Aborting for layer '%s' as it already has '%s', '%s' and '%s' set"),
			*Info->Identifier,
			Info->StartTimeCode.IsSet() ? *LexToString(Info->StartTimeCode.GetValue()) : TEXT("null"),
			Info->EndTimeCode.IsSet() ? *LexToString(Info->EndTimeCode.GetValue()) : TEXT("null"),
			Info->TimeCodesPerSecond.IsSet() ? *LexToString(Info->TimeCodesPerSecond.GetValue()) : TEXT("null"));
		return;
	}

	// Calculate bounds for our level sequence section. We do this in seconds as
	// our subs may each have their own different TimeCodesPerSecond
	TOptional<double> MinStartSeconds;
	TOptional<double> MaxEndSeconds;
	for (int32 SubLayerIndex = 0; SubLayerIndex < Info->SubLayerIdentifiers.Num(); ++SubLayerIndex)
	{
		FUsdLevelSequenceHelperImpl::FLayerTimeInfo* SubLayerInfo = LayerTimeInfosByIdentifier.Find(Info->SubLayerIdentifiers[SubLayerIndex]);
		if (!SubLayerInfo || !SubLayerInfo->IsAnimated())
		{
			continue;
		}

		double OffsetForSub = Info->OffsetTimeCodes[SubLayerIndex];
		double ScaleForSub  = Info->Scales[SubLayerIndex];

		double SubCodesPerSecond = SubLayerInfo->TimeCodesPerSecond.GetValue();
		double SubStartSeconds	 = (OffsetForSub + ScaleForSub * SubLayerInfo->StartTimeCode.GetValue()) / SubCodesPerSecond;
		double SubEndSeconds	 = (OffsetForSub + ScaleForSub * SubLayerInfo->EndTimeCode.GetValue()) / SubCodesPerSecond;

		MinStartSeconds = FMath::Min(SubStartSeconds, MinStartSeconds.Get(DBL_MAX));
		MaxEndSeconds	= FMath::Max(SubEndSeconds,   MaxEndSeconds.Get(-DBL_MAX));
	}

	double TimeCodesPerSecond = Info->TimeCodesPerSecond.GetValue();
	if (!bHaveStartCode && MinStartSeconds.IsSet())
	{
		Info->StartTimeCode = MinStartSeconds.GetValue() * TimeCodesPerSecond;
	}
	if (!bHaveEndCode && MaxEndSeconds.IsSet())
	{
		Info->EndTimeCode = MaxEndSeconds.GetValue() * TimeCodesPerSecond;
	}

	UE_LOG(LogUsdStage, Verbose, TEXT("RecursivelyPropagateTimeCodes: Finished for layer '%s': '%s', '%s' and '%s' set"),
		*Info->Identifier,
		Info->StartTimeCode.IsSet() ? *LexToString(Info->StartTimeCode.GetValue()) : TEXT("null"),
		Info->EndTimeCode.IsSet() ? *LexToString(Info->EndTimeCode.GetValue()) : TEXT("null"),
		Info->TimeCodesPerSecond.IsSet() ? *LexToString(Info->TimeCodesPerSecond.GetValue()) : TEXT("null"));
}

void FUsdLevelSequenceHelperImpl::UpdateInfoFromSection(FUsdLevelSequenceHelperImpl::FLayerTimeInfo* Info, const UMovieSceneSubSection* Section, const UMovieSceneSequence* Sequence)
{
	if (!Info || !Section || !Sequence)
	{
		return;
	}

	TUsdStore<pxr::SdfLayerHandle> Layer;
	{
		FScopedUsdAllocs Allocs;
		Layer = MakeUsdStore<pxr::SdfLayerHandle>(pxr::SdfLayer::Find(UnrealToUsd::ConvertString(*Info->Identifier).Get()));
		if (!Layer.Get())
		{
			UE_LOG(LogUsdStage, Warning, TEXT("Failed to update sublayer '%s'"), *Info->Identifier);
			return;
		}
	}

	UE_LOG(LogUsdStage, Verbose, TEXT("Updating LevelSequence '%s' for sublayer '%s'"), *Sequence->GetName(), *Info->Identifier);

	UMovieScene* MovieScene = Sequence->GetMovieScene();
	UMovieSceneSequence* SubSequence = Section->GetSequence();
	if (!MovieScene || !SubSequence)
	{
		return;
	}

	FString* InfoIdentifier = LayerInfoIdentifierByLevelSequenceName.Find(SubSequence->GetFName());
	if (!InfoIdentifier)
	{
		return;
	}

	FUsdLevelSequenceHelperImpl::FLayerTimeInfo* SubLayerInfo = LayerTimeInfosByIdentifier.Find(*InfoIdentifier);
	if (!SubLayerInfo)
	{
		return;
	}

	int32 SubLayerIndex = Info->SubLayerIdentifiers.IndexOfByKey(SubLayerInfo->Identifier);

	double OffsetForSub = Info->OffsetTimeCodes[SubLayerIndex];
	double ScaleForSub  = Info->Scales[SubLayerIndex];
	double SubCodesPerSecond = SubLayerInfo->TimeCodesPerSecond.GetValue();
	double SubStartTimeCode  = SubLayerInfo->StartTimeCode.GetValue();
	double SubEndTimeCode    = SubLayerInfo->EndTimeCode.GetValue();
	double SubStartSeconds = (OffsetForSub + ScaleForSub * SubStartTimeCode) / SubCodesPerSecond;
	double SubEndSeconds   = (OffsetForSub + ScaleForSub * SubEndTimeCode)   / SubCodesPerSecond;

	FFrameRate FrameRate = MovieScene->GetTickResolution();
	FFrameNumber OriginalStartFrame = FrameRate.AsFrameNumber(SubStartSeconds);
	FFrameNumber OriginalEndFrame   = FrameRate.AsFrameNumber(SubEndSeconds);
	FFrameNumber ModifiedStartFrame = Section->GetInclusiveStartFrame();
	FFrameNumber ModifiedEndFrame   = Section->GetExclusiveEndFrame();

	// We haven't modified this subsequence, skip it
	if (ModifiedStartFrame == OriginalStartFrame && ModifiedEndFrame == OriginalEndFrame)
	{
		return;
	}

	// This will obviously be quantized to frame intervals for now
	double ModifiedStartTimeCode = FrameRate.AsSeconds(ModifiedStartFrame) * SubCodesPerSecond;
	double ModifiedEndTimeCode   = FrameRate.AsSeconds(ModifiedEndFrame) * SubCodesPerSecond;
	double ModifiedScale  = (ModifiedEndTimeCode - ModifiedStartTimeCode) / (SubEndTimeCode - SubStartTimeCode);
	double ModifiedOffset = ModifiedStartTimeCode - SubStartTimeCode * ModifiedScale;

	// Prevent twins from being rebuilt when we update the layer offsets
	FScopedBlockNotices BlockNotices( StageActor.Get()->GetUsdListener() );

	// Apply timecode offset and scale to the sublayer
	Info->OffsetTimeCodes[SubLayerIndex] = ModifiedOffset;
	Info->Scales[SubLayerIndex] = ModifiedScale;
	pxr::SdfLayerOffset NewLayerOffset{ModifiedOffset, ModifiedScale};
	Layer.Get()->SetSubLayerOffset(NewLayerOffset, SubLayerIndex);

	UE_LOG(LogUsdStage, Verbose, TEXT("\tNew OffsetScale: %f, %f"), NewLayerOffset.GetOffset(), NewLayerOffset.GetScale());
}

void FUsdLevelSequenceHelperImpl::OnObjectTransacted(UObject* Object, const class FTransactionObjectEvent& Event)
{
	UMovieSceneSubSection* Section = Cast<UMovieSceneSubSection>(Object);
	if (!Section)
	{
		return;
	}

	UMovieSceneSequence* ParentSequence = Section->GetTypedOuter<UMovieSceneSequence>();
	if (!ParentSequence)
	{
		return;
	}

	const FString* InfoIdentifier = LayerInfoIdentifierByLevelSequenceName.Find(ParentSequence->GetFName());
	if (!InfoIdentifier)
	{
		return;
	}

	FUsdLevelSequenceHelperImpl::FLayerTimeInfo* Info = LayerTimeInfosByIdentifier.Find(*InfoIdentifier);
	UpdateInfoFromSection(Info, Section, ParentSequence);
}
#else
class FUsdLevelSequenceHelperImpl
{
public:
	FUsdLevelSequenceHelperImpl(TWeakObjectPtr<AUsdStageActor> InStageActor) {}
	~FUsdLevelSequenceHelperImpl(){}
};
#endif // USE_USD_SDK


FUsdLevelSequenceHelper::FUsdLevelSequenceHelper(TWeakObjectPtr<AUsdStageActor> InStageActor)
{
	if (AUsdStageActor* ValidStageActor = InStageActor.Get())
	{
		UsdSequencerImpl = MakeUnique<FUsdLevelSequenceHelperImpl>(InStageActor);
	}
}

// These are required in order to be a member of AUsdStageActor,
// and have a TUniquePtr to a forward declared Impl
FUsdLevelSequenceHelper::FUsdLevelSequenceHelper()
{
}
FUsdLevelSequenceHelper::~FUsdLevelSequenceHelper()
{
}

#if USE_USD_SDK
void FUsdLevelSequenceHelper::InitLevelSequence(const pxr::UsdStageRefPtr& UsdStage)
{
	if (UsdSequencerImpl.IsValid() && UsdStage)
	{
		UE_LOG(LogUsdStage, Verbose, TEXT("InitLevelSequence"));

		UsdSequencerImpl->RebuildLayerTimeInfoCache(UsdStage);

		UsdSequencerImpl->InitializeActorTimeCodes(UsdStage);

		FUsdLevelSequenceHelperImpl::FLayerTimeInfo* RootInfo = UsdSequencerImpl->GetRootLayerInfo();
		UsdSequencerImpl->CreateLevelSequences();
		UsdSequencerImpl->CreateTimeTrack(RootInfo);
		UsdSequencerImpl->CreateSubsequenceTrack(RootInfo);
	}
}

void FUsdLevelSequenceHelper::UpdateLevelSequence(const pxr::UsdStageRefPtr& UsdStage)
{
	if (UsdSequencerImpl.IsValid() && UsdStage)
	{
		UE_LOG(LogUsdStage, Verbose, TEXT("UpdateLevelSequence"));

		FUsdLevelSequenceHelperImpl::FLayerTimeInfo* RootInfo = UsdSequencerImpl->GetRootLayerInfo();
		RootInfo->StartTimeCode = UsdStage->GetStartTimeCode();
		RootInfo->EndTimeCode = UsdStage->GetEndTimeCode();

		UsdSequencerImpl->CreateTimeTrack(RootInfo);
	}
}

#undef DEFAULT_FRAMERATE
#undef TIME_TRACK_NAME
#endif // USE_USD_SDK