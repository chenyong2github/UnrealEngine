// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDLevelSequenceHelper.h"

#include "USDConversionUtils.h"
#include "USDLayerUtils.h"
#include "USDListener.h"
#include "USDLog.h"
#include "USDMemory.h"
#include "USDPrimConversion.h"
#include "USDStageActor.h"
#include "USDTypesConversion.h"

#include "UsdWrappers/SdfChangeBlock.h"
#include "UsdWrappers/SdfLayer.h"
#include "UsdWrappers/UsdAttribute.h"
#include "UsdWrappers/UsdGeomXformable.h"
#include "UsdWrappers/UsdStage.h"
#include "UsdWrappers/UsdTyped.h"

#include "Channels/MovieSceneChannelProxy.h"
#include "Compilation/MovieSceneCompiledDataManager.h"
#include "Components/SceneComponent.h"
#include "CoreMinimal.h"
#include "Editor.h"
#include "ILevelSequenceEditorToolkit.h"
#include "ISequencer.h"
#include "LevelSequence.h"
#include "MovieScene.h"
#include "MovieSceneTimeHelpers.h"
#include "ObjectTools.h"
#include "Sections/MovieSceneFloatSection.h"
#include "Sections/MovieSceneSubSection.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Templates/SharedPointer.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "Tracks/MovieSceneSubTrack.h"
#include "Tracks/MovieScene3DTransformTrack.h"
#include "Tracks/MovieSceneVectorTrack.h"
#include "UObject/UObjectGlobals.h"

#if USE_USD_SDK

namespace UsdLevelSequenceHelperImpl
{
	/** Sets the readonly value of the scene on construction and reverts it on destruction */
	class FMovieSceneReadonlyGuard
	{
	public:
		explicit FMovieSceneReadonlyGuard( UMovieScene& InMovieScene, const bool bNewReadonlyValue )
			: MovieScene( InMovieScene )
			, bWasReadonly( InMovieScene.IsReadOnly() )
		{
			MovieScene.SetReadOnly( bNewReadonlyValue );
		}

		~FMovieSceneReadonlyGuard()
		{
			MovieScene.SetReadOnly( bWasReadonly );
		}

	private:
		UMovieScene& MovieScene;
		bool bWasReadonly;
	};

	/**
	 * Similar to FrameRate.AsFrameNumber(TimeSeconds) except that it uses RoundToDouble instead of FloorToDouble, to
	 * prevent issues with floating point precision
	 */
	FFrameNumber RoundAsFrameNumber( const FFrameRate& FrameRate, double TimeSeconds )
	{
		const double TimeAsFrame = ( double( TimeSeconds ) * FrameRate.Numerator ) / FrameRate.Denominator;
		return FFrameNumber(static_cast<int32>( FMath::RoundToDouble( TimeAsFrame ) ));
	}

}

class FUsdLevelSequenceHelperImpl
{
public:
	FUsdLevelSequenceHelperImpl(TWeakObjectPtr<AUsdStageActor> InStageActor);
	~FUsdLevelSequenceHelperImpl();

private:
	struct FLayerOffsetInfo
	{
		FString LayerIdentifier;
		UE::FSdfLayerOffset LayerOffset;
	};

	struct FLayerTimeInfo
	{
		FString Identifier;
		FString FilePath;

		TArray< FLayerOffsetInfo > SubLayersOffsets;

		TOptional<double> StartTimeCode;
		TOptional<double> EndTimeCode;

		bool IsAnimated() const
		{
			return !FMath::IsNearlyEqual( StartTimeCode.Get( 0.0 ), EndTimeCode.Get( 0.0 ) );
		}
	};

// Sequences handling
public:

	/** Creates a Level Sequence and its SubSequenceSection for each layer in the local layer stack (root layer and sub layers) */
	void CreateLocalLayersSequences();

private:
	ULevelSequence* FindSequenceForAttribute( const UE::FUsdAttribute& Attribute );
	ULevelSequence* FindOrAddSequenceForAttribute( const UE::FUsdAttribute& Attribute );
	ULevelSequence* FindSequenceForIdentifier( const FString& SequenceIdentitifer ) const;
	ULevelSequence* FindOrAddSequenceForLayer( const UE::FSdfLayer& Layer, const FString& SequenceIdentifier, const FString& SequenceDisplayName );

	/** Removes PrimTwin as a user of Sequence. If Sequence is now unused, remove its subsection and itself. */
	void RemoveSequenceForPrim( ULevelSequence& Sequence, const UUsdPrimTwin& PrimTwin );

private:
	TSet< FName > LocalLayersSequences; // List of sequences associated with sublayers

	FMovieSceneSequenceHierarchy SequenceHierarchyCache; // Cache for the hierarchy of level sequences and subsections
	TMap< ULevelSequence*, FMovieSceneSequenceID > SequencesID; // Tracks the FMovieSceneSequenceID for each Sequence in the hierarchy. We assume that each Sequence is only present once in the hierarchy.

	// Sequence Name to Layer Identifier Map. Relationship: N Sequences to 1 Layer.
	TMap<FName, FString> LayerIdentifierByLevelSequenceName;

// Sections handling
private:
	/** Returns the UMovieSceneSubSection associated with SubSequence on the Sequence UMovieSceneSubTrack if it exists */
	UMovieSceneSubSection* FindSubSequenceSection( ULevelSequence& Sequence, ULevelSequence& SubSequence );
	void CreateSubSequenceSection( ULevelSequence& Sequence, ULevelSequence& Subsequence );
	void RemoveSubSequenceSection( ULevelSequence& Sequence, ULevelSequence& SubSequence );

// Tracks handling
private:
	/** Creates a time track on the ULevelSequence corresponding to Info */
	void CreateTimeTrack(const FUsdLevelSequenceHelperImpl::FLayerTimeInfo& Info);

	/** Adds a transform track for the prim xform transform op. */
	void AddXformTrack( UUsdPrimTwin& PrimTwin, ULevelSequence& Sequence );
	void RemoveXformTrack( ULevelSequence& Sequence, const UUsdPrimTwin& PrimTwin );

// Prims handling
public:
	void AddPrim( UUsdPrimTwin& PrimTwin );
	void RemovePrim(const UUsdPrimTwin& PrimTwin);

private:
	UE::FUsdAttribute GetXformAttribute( const UE::FUsdPrim& UsdPrim ) const;

	// Sequence Name to Prim Path. Relationship: 1 Sequence to N Prim Path.
	TMultiMap<FName, FString> PrimPathByLevelSequenceName;

	TMap< TWeakObjectPtr< const UUsdPrimTwin >, TPair< ULevelSequence*, FGuid > > SceneComponentsBindings;

// Time codes handling
private:
	/** Returns the FLayerTimeInfo corresponding to the root layer */
	FLayerTimeInfo* GetRootLayerInfo();

	FLayerTimeInfo& FindOrAddLayerTimeInfo(const UE::FSdfLayer& Layer);

	/** Updates the Usd LayerOffset with new offset/scale values when Section has been moved by the user */
	void UpdateUsdLayerOffsetFromSection(const UMovieSceneSequence* Sequence, const UMovieSceneSubSection* Section);

	/** Updates LayerTimeInfo with Layer */
	void UpdateLayerTimeInfoFromLayer( FLayerTimeInfo& LayerTimeInfo, const UE::FSdfLayer& Layer );

	/** Updates MovieScene with LayerTimeInfo */
	void UpdateMovieSceneTimeRanges( UMovieScene& MovieScene, const FLayerTimeInfo& LayerTimeInfo );

	double GetFramesPerSecond() const;
	double GetTimeCodesPerSecond() const;

	TMap<FString, FLayerTimeInfo> LayerTimeInfosByLayerIdentifier; // Maps a LayerTimeInfo to a given Layer through its identifier

// Changes handling
public:
	void StartMonitoringChanges() { bMonitorChanges = true; }
	void StopMonitoringChanges() { bMonitorChanges = false; }

private:
	void OnObjectTransacted(UObject* Object, const class FTransactionObjectEvent& Event);
	void HandleMovieSceneChange(UMovieScene& MovieScene);
	void HandleSubSectionChange(UMovieSceneSubSection& Section);
	void HandleTransformTrackChange(const UMovieScene3DTransformTrack& TransformTrack);
	void HandleDisplayRateChange(const double DisplayRate);

	bool bMonitorChanges; // Flag to handle or not changes done to the level sequence or one of its subobject

	FDelegateHandle OnObjectTransactedHandle;
	FDelegateHandle OnStageEditTargetChangedHandle;

// Readonly handling
private:
	void UpdateMovieSceneReadonlyFlags();
	void UpdateMovieSceneReadonlyFlag( UMovieScene& MovieScene, const FString& LayerIdentifier );

private:
	void RefreshSequencer();

private:
	static const EObjectFlags DefaultObjFlags;
	static const double DefaultFramerate;
	static const TCHAR* TimeTrackName;
	static const double EmptySubSectionRange; // How many frames should an empty subsection cover, only needed so that the subsection is visible and the user can edit it

	TWeakObjectPtr<AUsdStageActor> StageActor;
	FGuid StageActorBinding;
};

const EObjectFlags FUsdLevelSequenceHelperImpl::DefaultObjFlags = EObjectFlags::RF_Transactional | EObjectFlags::RF_Transient | EObjectFlags::RF_Public;
const double FUsdLevelSequenceHelperImpl::DefaultFramerate = 24.0;
const TCHAR* FUsdLevelSequenceHelperImpl::TimeTrackName = TEXT("Time");
const double FUsdLevelSequenceHelperImpl::EmptySubSectionRange = 10.0;

FUsdLevelSequenceHelperImpl::FUsdLevelSequenceHelperImpl(TWeakObjectPtr<AUsdStageActor> InStageActor)
	: bMonitorChanges( true )
	, StageActor(InStageActor)
{
	OnObjectTransactedHandle = FCoreUObjectDelegates::OnObjectTransacted.AddRaw(this, &FUsdLevelSequenceHelperImpl::OnObjectTransacted);

	if ( StageActor.IsValid() )
	{
		OnStageEditTargetChangedHandle = StageActor->GetUsdListener().GetOnStageEditTargetChanged().AddLambda(
			[ this ]()
			{
				UpdateMovieSceneReadonlyFlags();
			}
		);
	}
}

FUsdLevelSequenceHelperImpl::~FUsdLevelSequenceHelperImpl()
{
	if ( StageActor.IsValid() )
	{
		StageActor->GetUsdListener().GetOnStageEditTargetChanged().Remove(OnStageEditTargetChangedHandle);
		OnStageEditTargetChangedHandle.Reset();
	}

	FCoreUObjectDelegates::OnObjectTransacted.Remove(OnObjectTransactedHandle);
	OnObjectTransactedHandle.Reset();
}

void FUsdLevelSequenceHelperImpl::CreateLocalLayersSequences()
{
	LocalLayersSequences.Empty();
	LayerIdentifierByLevelSequenceName.Empty();
	LayerTimeInfosByLayerIdentifier.Empty();
	PrimPathByLevelSequenceName.Empty();
	StageActorBinding = FGuid::NewGuid();
	SequencesID.Empty();
	SceneComponentsBindings.Empty();
	SequenceHierarchyCache = FMovieSceneSequenceHierarchy();

	AUsdStageActor* ValidStageActor = StageActor.Get();
	if (!ValidStageActor)
	{
		return;
	}

	if ( ValidStageActor->LevelSequence )
	{
		return;
	}

	UE::FSdfLayer RootLayer = ValidStageActor->GetUsdStage().GetRootLayer();
	const FLayerTimeInfo& RootLayerInfo = FindOrAddLayerTimeInfo( RootLayer );

	UE_LOG(LogUsd, Verbose, TEXT("CreateLayerSequences: Initializing level sequence for '%s'"), *RootLayerInfo.Identifier);

	// Create level sequence for root layer (visible on StageActor's details panel)
	ValidStageActor->LevelSequence = FindOrAddSequenceForLayer( RootLayer, RootLayer.GetIdentifier(), RootLayer.GetDisplayName() );

	if ( !ValidStageActor->LevelSequence )
	{
		return;
	}

	UMovieScene* MovieScene = ValidStageActor->LevelSequence->GetMovieScene();
	if ( !MovieScene )
	{
		return;
	}

	SequencesID.Add( ValidStageActor->LevelSequence ) = MovieSceneSequenceID::Root;

	LocalLayersSequences.Add( ValidStageActor->LevelSequence->GetFName() );

	// Bind stage actor
	StageActorBinding = MovieScene->AddPossessable( ValidStageActor->GetActorLabel(), ValidStageActor->GetClass() );
	ValidStageActor->LevelSequence->BindPossessableObject( StageActorBinding, *ValidStageActor, ValidStageActor->GetWorld() );

	TFunction< void( const FLayerTimeInfo* LayerTimeInfo, ULevelSequence& ParentSequence ) > RecursivelyCreateSequencesForLayer;
	RecursivelyCreateSequencesForLayer = [ &RecursivelyCreateSequencesForLayer, this ]( const FLayerTimeInfo* LayerTimeInfo, ULevelSequence& ParentSequence )
	{
		if ( !LayerTimeInfo )
		{
			return;
		}

		if ( UE::FSdfLayer Layer = UE::FSdfLayer::FindOrOpen( *LayerTimeInfo->Identifier ) )
		{
			for ( const FString& SubLayerPath : Layer.GetSubLayerPaths() )
			{
				if ( UE::FSdfLayer SubLayer = UsdUtils::FindLayerForSubLayerPath( Layer, SubLayerPath ) )
				{
					if ( ULevelSequence* SubSequence = FindOrAddSequenceForLayer( SubLayer, SubLayer.GetIdentifier(), SubLayer.GetDisplayName() ) )
					{
						if ( !LocalLayersSequences.Contains( SubSequence->GetFName() ) ) // Make sure we don't parse an already parsed layer
						{
							LocalLayersSequences.Add( SubSequence->GetFName() );

							CreateSubSequenceSection( ParentSequence, *SubSequence );

							RecursivelyCreateSequencesForLayer( LayerTimeInfosByLayerIdentifier.Find( SubLayer.GetIdentifier() ), *SubSequence );
						}
					}
				}
			}
		}
	};

	// Create level sequences for all sub layers (accessible via the main level sequence but otherwise hidden)
	RecursivelyCreateSequencesForLayer( &RootLayerInfo, *ValidStageActor->LevelSequence );

	CreateTimeTrack( FindOrAddLayerTimeInfo( ValidStageActor->GetUsdStage().GetRootLayer() ) );
}

ULevelSequence* FUsdLevelSequenceHelperImpl::FindSequenceForAttribute( const UE::FUsdAttribute& Attribute )
{
	if ( !Attribute || !Attribute.GetPrim() )
	{
		return nullptr;
	}

	AUsdStageActor* ValidStageActor = StageActor.Get();
	if ( !ValidStageActor )
	{
		return nullptr;
	}

	UE::FSdfLayer AttributeLayer = UsdUtils::FindLayerForAttribute( Attribute, 0.0 );

	if ( !AttributeLayer )
	{
		return nullptr;
	}

	FString AttributeLayerIdentifier = AttributeLayer.GetIdentifier();

	UE::FUsdPrim Prim = Attribute.GetPrim();

	UE::FSdfLayer PrimLayer = UsdUtils::FindLayerForPrim( Prim );
	FString PrimLayerIdentifier = PrimLayer.GetIdentifier();

	ULevelSequence* Sequence = nullptr;

	// If the attribute is on the Root or a SubLayer, return the Sequence associated with that layer
	if ( AttributeLayer.HasSpec( Prim.GetPrimPath() ) && ValidStageActor->GetUsdStage().HasLocalLayer( AttributeLayer ) )
	{
		Sequence = FindSequenceForIdentifier( AttributeLayer.GetIdentifier() );
	}
	// The prim should have its own sequence, return that
	else
	{
		Sequence = FindSequenceForIdentifier( Prim.GetPrimPath().GetString() );
	}

	return Sequence;
}

ULevelSequence* FUsdLevelSequenceHelperImpl::FindOrAddSequenceForAttribute( const UE::FUsdAttribute& Attribute )
{
	if ( !Attribute || !Attribute.GetPrim() )
	{
		return nullptr;
	}

	AUsdStageActor* ValidStageActor = StageActor.Get();
	if ( !ValidStageActor )
	{
		return nullptr;
	}

	ULevelSequence* Sequence = FindSequenceForAttribute( Attribute );
	if ( !Sequence )
	{
		if ( UE::FSdfLayer AttributeLayer = UsdUtils::FindLayerForAttribute( Attribute, 0.0 ) )
		{
			const FString SequenceIdentifier = Attribute.GetPrim().GetPrimPath().GetString();

			Sequence = FindOrAddSequenceForLayer( AttributeLayer, SequenceIdentifier, SequenceIdentifier );
		}
	}

	return Sequence;
}

ULevelSequence* FUsdLevelSequenceHelperImpl::FindOrAddSequenceForLayer( const UE::FSdfLayer& Layer, const FString& SequenceIdentifier, const FString& SequenceDisplayName )
{
	if ( !Layer )
	{
		return nullptr;
	}

	ULevelSequence* Sequence = FindSequenceForIdentifier( SequenceIdentifier );

	if ( !Sequence )
	{
		AUsdStageActor* ValidStageActor = StageActor.Get();
		if ( !ValidStageActor )
		{
			return nullptr;
		}

		// This needs to be unique, or else when we reload the stage we will end up with a new ULevelSequence with the same class, outer and name as the
		// previous one. Also note that the previous level sequence, even though unreferenced by the stage actor, is likely still alive and valid due to references
		// from the transaction buffer, so we would basically end up creating a identical new object on top of an existing one (the new object has the same address as the existing one)
		FName UniqueSequenceName = MakeUniqueObjectName( GetTransientPackage(), ULevelSequence::StaticClass(), *ObjectTools::SanitizeObjectName( SequenceDisplayName ) );

		Sequence = NewObject< ULevelSequence >( GetTransientPackage(), UniqueSequenceName, FUsdLevelSequenceHelperImpl::DefaultObjFlags );
		Sequence->Initialize();

		UMovieScene* MovieScene = Sequence->MovieScene;
		if ( !MovieScene )
		{
			return nullptr;
		}

		LayerIdentifierByLevelSequenceName.Add( Sequence->GetFName(), Layer.GetIdentifier() );
		ValidStageActor->LevelSequencesByIdentifier.Add( SequenceIdentifier, Sequence );

		const FLayerTimeInfo LayerTimeInfo = FindOrAddLayerTimeInfo( Layer );

		UpdateMovieSceneTimeRanges( *MovieScene, LayerTimeInfo );
		UpdateMovieSceneReadonlyFlag( *MovieScene, LayerTimeInfo.Identifier );

		UE_LOG( LogUsd, Verbose, TEXT("Created Sequence for identifier: '%s'"), *SequenceIdentifier );
	}

	return Sequence;
}

UMovieSceneSubSection* FUsdLevelSequenceHelperImpl::FindSubSequenceSection( ULevelSequence& Sequence, ULevelSequence& SubSequence )
{
	UMovieScene* MovieScene = Sequence.GetMovieScene();
	if ( !MovieScene )
	{
		return nullptr;
	}

	UMovieSceneSubTrack* SubTrack = MovieScene->FindMasterTrack< UMovieSceneSubTrack >();

	if ( !SubTrack )
	{
		return nullptr;
	}

	UMovieSceneSection* const * SubSection = Algo::FindByPredicate( SubTrack->GetAllSections(),
	[ &SubSequence ]( UMovieSceneSection* Section ) -> bool
	{
		if ( UMovieSceneSubSection* SubSection = Cast< UMovieSceneSubSection >( Section ) )
		{
			return  ( SubSection->GetSequence() == &SubSequence );
		}
		else
		{
			return false;
		}
	} );

	if ( SubSection )
	{
		return  Cast< UMovieSceneSubSection >( *SubSection );
	}
	else
	{
		return nullptr;
	}
}

void FUsdLevelSequenceHelperImpl::CreateSubSequenceSection( ULevelSequence& Sequence, ULevelSequence& SubSequence )
{
	if ( &Sequence == &SubSequence )
	{
		return;
	}

	UMovieScene* MovieScene = Sequence.GetMovieScene();
	if ( !MovieScene )
	{
		return;
	}

	if ( !StageActor.IsValid() || !StageActor->GetUsdStage() )
	{
		return;
	}

	const bool bReadonly = false;
	UsdLevelSequenceHelperImpl::FMovieSceneReadonlyGuard MovieSceneReadonlyGuard{ *MovieScene, bReadonly };

	FFrameRate FrameRate = MovieScene->GetTickResolution();

	UMovieSceneSubTrack* SubTrack = MovieScene->FindMasterTrack< UMovieSceneSubTrack >();
	if ( !SubTrack )
	{
		SubTrack = MovieScene->AddMasterTrack< UMovieSceneSubTrack >();
	}

	const FString* LayerIdentifier = LayerIdentifierByLevelSequenceName.Find( Sequence.GetFName() );
	const FString* SubLayerIdentifier = LayerIdentifierByLevelSequenceName.Find( SubSequence.GetFName() );

	if ( !LayerIdentifier || !SubLayerIdentifier )
	{
		return;
	}

	FLayerTimeInfo* LayerTimeInfo = LayerTimeInfosByLayerIdentifier.Find( *LayerIdentifier );
	FLayerTimeInfo* SubLayerTimeInfo = LayerTimeInfosByLayerIdentifier.Find( *SubLayerIdentifier );

	if ( !LayerTimeInfo || !SubLayerTimeInfo )
	{
		return;
	}

	FMovieSceneSequenceTransform SequenceTransform;

	if ( const FMovieSceneSequenceID* SequenceID = SequencesID.Find( &Sequence ) )
	{
		if ( FMovieSceneSubSequenceData* SubSequenceData = SequenceHierarchyCache.FindSubData( *SequenceID ) )
		{
			SequenceTransform = SubSequenceData->RootToSequenceTransform;
		}
	}

	UE::FSdfLayerOffset SubLayerOffset;

	UE::FSdfLayer SubLayer = UE::FSdfLayer::FindOrOpen( **SubLayerIdentifier );

	TArray< FString > PrimPathsForSequence;
	PrimPathByLevelSequenceName.MultiFind( SubSequence.GetFName(), PrimPathsForSequence );

	if ( PrimPathsForSequence.Num() > 0 )
	{
		if ( UE::FUsdPrim SequencePrim = StageActor->GetUsdStage().GetPrimAtPath( UE::FSdfPath( *PrimPathsForSequence[0] ) ) )
		{
			SubLayerOffset = UsdUtils::GetLayerToStageOffset( GetXformAttribute( SequencePrim ) );
		}
	}
	else if ( StageActor->GetUsdStage().HasLocalLayer( SubLayer ) )
	{
		const FLayerOffsetInfo* SubLayerOffsetPtr = Algo::FindByPredicate( LayerTimeInfo->SubLayersOffsets,
			[ &SubLayerIdentifier ]( const FLayerOffsetInfo& Other )
		{
			return ( Other.LayerIdentifier == *SubLayerIdentifier );
		} );

		if ( SubLayerOffsetPtr )
		{
			SubLayerOffset = SubLayerOffsetPtr->LayerOffset;
		}
	}

	const double TimeCodesPerSecond = GetTimeCodesPerSecond();

	const double SubStartTimeSeconds  = ( SubLayerOffset.Offset + SubLayerOffset.Scale * SubLayerTimeInfo->StartTimeCode.Get( 0.0 ) ) / TimeCodesPerSecond;
	const double SubEndTimeSeconds = ( SubLayerOffset.Offset + SubLayerOffset.Scale * SubLayerTimeInfo->EndTimeCode.Get( 0.0 ) ) / TimeCodesPerSecond;

	const FFrameNumber StartFrame = UsdLevelSequenceHelperImpl::RoundAsFrameNumber( FrameRate, SubStartTimeSeconds );
	const int32 Duration = UsdLevelSequenceHelperImpl::RoundAsFrameNumber( FrameRate, SubEndTimeSeconds ).Value - StartFrame.Value;

	TRange< FFrameNumber > SubSectionRange( StartFrame, StartFrame + Duration );
	SubSectionRange = SequenceTransform.TransformRangeUnwarped( SubSectionRange );

	UMovieSceneSubSection* SubSection = FindSubSequenceSection( Sequence, SubSequence );

	if ( SubSection )
	{
		SubSection->SetRange( SubSectionRange );
	}
	else
	{
		SubSection = SubTrack->AddSequence( &SubSequence, SubSectionRange.GetLowerBoundValue(), SubSectionRange.Size< FFrameNumber >().Value );

		UE_LOG(LogUsd, Verbose, TEXT("Adding subsection '%s' to sequence '%s'. StartFrame: '%d', Duration: '%d'"),
			*SubSection->GetName(),
			*Sequence.GetName(),
			StartFrame.Value,
			Duration);
	}

	SubSection->Parameters.TimeScale = FMath::IsNearlyZero( SubLayerOffset.Scale ) ? 0.f : 1.f / SubLayerOffset.Scale;

	if ( StageActor->LevelSequence )
	{
		UMovieSceneCompiledDataManager::CompileHierarchy( StageActor->LevelSequence, &SequenceHierarchyCache );

		for ( const TTuple< FMovieSceneSequenceID, FMovieSceneSubSequenceData >& Pair : SequenceHierarchyCache.AllSubSequenceData() )
		{
			if ( UMovieSceneSequence* CachedSubSequence = Pair.Value.GetSequence() )
			{
				if ( CachedSubSequence == &SubSequence )
				{
					SequencesID.Add( &SubSequence, Pair.Key );
					break;
				}
			}
		}
	}
}

void FUsdLevelSequenceHelperImpl::RemoveSubSequenceSection( ULevelSequence& Sequence, ULevelSequence& SubSequence )
{
	if ( UMovieSceneSubTrack* SubTrack = Sequence.GetMovieScene()->FindMasterTrack< UMovieSceneSubTrack >() )
	{
		if ( UMovieSceneSection* SubSection = FindSubSequenceSection( Sequence, SubSequence ) )
		{
			SequencesID.Remove( &SubSequence );
			SubTrack->Modify();
			SubTrack->RemoveSection( *SubSection );

			if ( StageActor->LevelSequence )
			{
				UMovieSceneCompiledDataManager::CompileHierarchy( StageActor->LevelSequence, &SequenceHierarchyCache );
			}
		}
	}
}

void FUsdLevelSequenceHelperImpl::CreateTimeTrack(const FLayerTimeInfo& Info)
{
	ULevelSequence* Sequence = FindSequenceForIdentifier( Info.Identifier );

	if (!Sequence)
	{
		return;
	}

	UMovieScene* MovieScene = Sequence->GetMovieScene();

	if (!MovieScene)
	{
		return;
	}

	UMovieSceneFloatTrack* TimeTrack = MovieScene->FindTrack<UMovieSceneFloatTrack>(StageActorBinding, FName(FUsdLevelSequenceHelperImpl::TimeTrackName));
	if (TimeTrack)
	{
		TimeTrack->RemoveAllAnimationData();
	}
	else
	{
		TimeTrack = MovieScene->AddTrack<UMovieSceneFloatTrack>(StageActorBinding);
		if (!TimeTrack)
		{
			return;
		}

		TimeTrack->SetPropertyNameAndPath(FName(FUsdLevelSequenceHelperImpl::TimeTrackName), "Time");

		MovieScene->SetEvaluationType(EMovieSceneEvaluationType::FrameLocked);
	}

	if ( Info.IsAnimated() )
	{
		const double StartTimeCode = Info.StartTimeCode.Get(0.0);
		const double EndTimeCode = Info.EndTimeCode.Get(0.0);
		const double TimeCodesPerSecond = GetTimeCodesPerSecond();

		FFrameRate DestTickRate = MovieScene->GetTickResolution();
		FFrameNumber StartFrame  = UsdLevelSequenceHelperImpl::RoundAsFrameNumber(DestTickRate, StartTimeCode / TimeCodesPerSecond);
		FFrameNumber EndFrame    = UsdLevelSequenceHelperImpl::RoundAsFrameNumber(DestTickRate, EndTimeCode / TimeCodesPerSecond);

		TRange< FFrameNumber > PlaybackRange( StartFrame, EndFrame );

		bool bSectionAdded = false;

		if ( UMovieSceneFloatSection* TimeSection = Cast<UMovieSceneFloatSection>(TimeTrack->FindOrAddSection(0, bSectionAdded)) )
		{
			TimeSection->EvalOptions.CompletionMode = EMovieSceneCompletionMode::KeepState;
			TimeSection->SetRange(TRange<FFrameNumber>::All());

			TArray<FFrameNumber> FrameNumbers;
			FrameNumbers.Add( UE::MovieScene::DiscreteInclusiveLower( PlaybackRange ) );
			FrameNumbers.Add( UE::MovieScene::DiscreteExclusiveUpper( PlaybackRange ) );

			TArray<FMovieSceneFloatValue> FrameValues;
			FrameValues.Add_GetRef(FMovieSceneFloatValue(StartTimeCode)).InterpMode = ERichCurveInterpMode::RCIM_Linear;
			FrameValues.Add_GetRef(FMovieSceneFloatValue(EndTimeCode)).InterpMode = ERichCurveInterpMode::RCIM_Linear;

			FMovieSceneFloatChannel* TimeChannel = TimeSection->GetChannelProxy().GetChannel<FMovieSceneFloatChannel>(0);
			TimeChannel->Set(FrameNumbers, FrameValues);

			RefreshSequencer();
		}
	}
}

void FUsdLevelSequenceHelperImpl::AddPrim( UUsdPrimTwin& PrimTwin )
{
	AUsdStageActor* ValidStageActor = StageActor.Get();
	if ( !ValidStageActor )
	{
		return;
	}

	UE::FSdfPath PrimPath( *PrimTwin.PrimPath );
	UE::FUsdPrim UsdPrim( ValidStageActor->GetUsdStage().GetPrimAtPath( PrimPath ) );

	TArray< UE::FUsdAttribute > PrimAttributes = UsdPrim.GetAttributes();

	for ( const UE::FUsdAttribute& PrimAttribute : PrimAttributes )
	{
		if ( PrimAttribute.ValueMightBeTimeVarying() )
		{
			if ( ULevelSequence* AttributeSequence = FindOrAddSequenceForAttribute( PrimAttribute ) )
			{
				PrimPathByLevelSequenceName.AddUnique( AttributeSequence->GetFName(), PrimTwin.PrimPath );

				if ( !SequencesID.Contains( AttributeSequence ) )
				{
					UE::FSdfLayer PrimLayer = UsdUtils::FindLayerForPrim( UsdPrim );
					ULevelSequence* PrimSequence = FindSequenceForIdentifier( PrimLayer.GetIdentifier() );

					// Create new subsequence section for this referencing prim
					CreateSubSequenceSection( *PrimSequence, *AttributeSequence );
				}
			}
		}
	}

	if ( UE::FUsdAttribute TransformAttribute = GetXformAttribute( UsdPrim ) )
	{
		if ( TransformAttribute.ValueMightBeTimeVarying() )
		{
			if ( ULevelSequence* TransformSequence = FindOrAddSequenceForAttribute( TransformAttribute ) )
			{
				AddXformTrack( PrimTwin, *TransformSequence );
			}
		}
	}

	RefreshSequencer();
}

void FUsdLevelSequenceHelperImpl::AddXformTrack( UUsdPrimTwin& PrimTwin, ULevelSequence& Sequence )
{
	UMovieScene* MovieScene = Sequence.GetMovieScene();

	if ( !MovieScene )
	{
		return;
	}

	USceneComponent* SceneComponent = PrimTwin.GetSceneComponent();

	if ( !SceneComponent )
	{
		return;
	}

	const FGuid ComponentBinding = [&]() -> FGuid
	{
		if ( TPair< ULevelSequence*, FGuid >* SceneComponentBinding = SceneComponentsBindings.Find( &PrimTwin ) )
		{
			return SceneComponentBinding->Value;
		}
		else
		{
			// Bind component
			FGuid Binding = MovieScene->AddPossessable( SceneComponent->GetName(), SceneComponent->GetClass( ));
			Sequence.BindPossessableObject( Binding, *SceneComponent, SceneComponent->GetWorld() );

			SceneComponentsBindings.Emplace( &PrimTwin ) = TPair< ULevelSequence*, FGuid >( &Sequence, Binding );
			return Binding;
		}
	}();

	const bool bReadonly = false;
	UsdLevelSequenceHelperImpl::FMovieSceneReadonlyGuard MovieSceneReadonlyGuard{ *MovieScene, bReadonly };

	const FName TransformPropertyName( TEXT("Transform") );
	UMovieScene3DTransformTrack* XformTrack = MovieScene->FindTrack< UMovieScene3DTransformTrack >( ComponentBinding, TransformPropertyName );

	if ( XformTrack )
	{
		XformTrack->RemoveAllAnimationData();
	}
	else
	{
		XformTrack = MovieScene->AddTrack< UMovieScene3DTransformTrack >( ComponentBinding );
		if ( !XformTrack )
		{
			return;
		}

		XformTrack->SetPropertyNameAndPath( TransformPropertyName, TransformPropertyName.ToString() );
	}

	AUsdStageActor* ValidStageActor = StageActor.Get();
	if ( !ValidStageActor )
	{
		return;
	}

	UE::FUsdGeomXformable Xformable( ValidStageActor->GetUsdStage().GetPrimAtPath( UE::FSdfPath( *PrimTwin.PrimPath ) ) );


	FMovieSceneSequenceTransform SequenceTransform;

	FMovieSceneSequenceID SequenceID = SequencesID.FindRef( &Sequence );
	if ( FMovieSceneSubSequenceData* SubSequenceData = SequenceHierarchyCache.FindSubData( SequenceID ) )
	{
		SequenceTransform = SubSequenceData->RootToSequenceTransform;
	}

	UsdToUnreal::ConvertXformable( Xformable, *XformTrack, SequenceTransform );
}

void FUsdLevelSequenceHelperImpl::RemovePrim( const UUsdPrimTwin& PrimTwin )
{
	// We can't assume that the UsdPrim still exists in the stage, it might have been removed already so work from the PrimTwin PrimPath.

	AUsdStageActor* ValidStageActor = StageActor.Get();
	if ( !ValidStageActor )
	{
		return;
	}

	TSet< FName > PrimSequences;

	for ( TPair< FName, FString >& PrimPathByLevelSequenceNamePair : PrimPathByLevelSequenceName )
	{
		if ( PrimPathByLevelSequenceNamePair.Value == PrimTwin.PrimPath )
		{
			PrimSequences.Add( PrimPathByLevelSequenceNamePair.Key );
		}
	}

	TSet< ULevelSequence* > SequencesToRemoveForPrim;

	for ( const FName& PrimSequenceName : PrimSequences )
	{
		for ( const TPair< FString, ULevelSequence* >& IdentifierSequencePair : ValidStageActor->LevelSequencesByIdentifier )
		{
			if ( IdentifierSequencePair.Value && IdentifierSequencePair.Value->GetFName() == PrimSequenceName )
			{
				SequencesToRemoveForPrim.Add( IdentifierSequencePair.Value );
			}
		}
	}
	
	if ( ULevelSequence* TransformSequence = SceneComponentsBindings.FindRef( &PrimTwin ).Key )
	{
		RemoveXformTrack( *TransformSequence, PrimTwin );
	}

	for ( ULevelSequence* SequenceToRemoveForPrim : SequencesToRemoveForPrim )
	{
		RemoveSequenceForPrim( *SequenceToRemoveForPrim, PrimTwin );
	}

	RefreshSequencer();
}

void FUsdLevelSequenceHelperImpl::RemoveSequenceForPrim( ULevelSequence& Sequence, const UUsdPrimTwin& PrimTwin )
{
	TArray< FString > PrimPathsForSequence;
	PrimPathByLevelSequenceName.MultiFind( Sequence.GetFName(), PrimPathsForSequence );

	if ( PrimPathsForSequence.Find( PrimTwin.PrimPath ) != INDEX_NONE )
	{
		PrimPathByLevelSequenceName.Remove( Sequence.GetFName(), PrimTwin.PrimPath );

		// If Sequence isn't used anymore, remove it and its subsection
		if ( !PrimPathByLevelSequenceName.Contains( Sequence.GetFName() ) && !LocalLayersSequences.Contains( Sequence.GetFName() ) )
		{
			ULevelSequence* ParentSequence = StageActor->LevelSequence;
			FMovieSceneSequenceID SequenceID = SequencesID.FindRef( &Sequence );

			if ( FMovieSceneSequenceHierarchyNode* NodeData = SequenceHierarchyCache.FindNode( SequenceID ) )
			{
				FMovieSceneSequenceID ParentSequenceID = NodeData->ParentID;

				if ( FMovieSceneSubSequenceData* ParentSubSequenceData = SequenceHierarchyCache.FindSubData( ParentSequenceID ) )
				{
					ParentSequence = Cast< ULevelSequence >( ParentSubSequenceData->GetSequence() );
				}
			}

			if ( ParentSequence )
			{
				RemoveSubSequenceSection( *ParentSequence, Sequence );
			}

			StageActor->LevelSequencesByIdentifier.Remove( PrimTwin.PrimPath );
			SequencesID.Remove( &Sequence );
		}
	}
}

void FUsdLevelSequenceHelperImpl::RemoveXformTrack( ULevelSequence& Sequence, const UUsdPrimTwin& PrimTwin )
{
	AUsdStageActor* ValidStageActor = StageActor.Get();
	if ( !ValidStageActor )
	{
		return;
	}

	UMovieScene* MovieScene = Sequence.GetMovieScene();

	if ( !MovieScene )
	{
		return;
	}

	if ( const TPair< ULevelSequence*, FGuid >* SceneComponentBinding = SceneComponentsBindings.Find( &PrimTwin ) )
	{
		if ( UMovieSceneTrack* SceneTrack = MovieScene->FindTrack< UMovieScene3DTransformTrack >( SceneComponentBinding->Value ) )
		{
			MovieScene->RemoveTrack( *SceneTrack );
		}

		if ( MovieScene->RemovePossessable( SceneComponentBinding->Value ) )
		{
			Sequence.UnbindPossessableObjects( SceneComponentBinding->Value );
		}

		SceneComponentsBindings.Remove( &PrimTwin );
	}
}

void FUsdLevelSequenceHelperImpl::RefreshSequencer()
{
	AUsdStageActor* ValidStageActor = StageActor.Get();
	if ( !ValidStageActor || !ValidStageActor->LevelSequence )
	{
		return;
	}

	const bool bFocusIfOpen = false;
	IAssetEditorInstance* AssetEditor = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->FindEditorForAsset(ValidStageActor->LevelSequence, bFocusIfOpen);
	ILevelSequenceEditorToolkit* LevelSequenceEditor = static_cast<ILevelSequenceEditorToolkit*>(AssetEditor);
	TWeakPtr<ISequencer> WeakSequencer = LevelSequenceEditor ? LevelSequenceEditor->GetSequencer() : nullptr;

	if ( TSharedPtr< ISequencer > Sequencer = WeakSequencer.Pin() )
	{
		Sequencer->RefreshTree();
		Sequencer->NotifyMovieSceneDataChanged( EMovieSceneDataChangeType::TrackValueChanged );
	}
}

void FUsdLevelSequenceHelperImpl::UpdateUsdLayerOffsetFromSection(const UMovieSceneSequence* Sequence, const UMovieSceneSubSection* Section)
{
	if (!Section || !Sequence)
	{
		return;
	}

	UMovieScene* MovieScene = Sequence->GetMovieScene();
	UMovieSceneSequence* SubSequence = Section->GetSequence();
	if (!MovieScene || !SubSequence)
	{
		return;
	}

	const FString* LayerIdentifier = LayerIdentifierByLevelSequenceName.Find( Sequence->GetFName() );
	const FString* SubLayerIdentifier = LayerIdentifierByLevelSequenceName.Find( SubSequence->GetFName() );

	if ( !LayerIdentifier || !SubLayerIdentifier )
	{
		return;
	}

	FLayerTimeInfo* LayerTimeInfo = LayerTimeInfosByLayerIdentifier.Find( *LayerIdentifier );
	FLayerTimeInfo* SubLayerTimeInfo = LayerTimeInfosByLayerIdentifier.Find( *SubLayerIdentifier );

	if ( !LayerTimeInfo || !SubLayerTimeInfo )
	{
		return;
	}

	UE_LOG(LogUsd, Verbose, TEXT("Updating LevelSequence '%s' for sublayer '%s'"), *Sequence->GetName(), **SubLayerIdentifier);

	const double TimeCodesPerSecond = GetTimeCodesPerSecond();
	const double SubStartTimeCode = SubLayerTimeInfo->StartTimeCode.Get(0.0);
	const double SubEndTimeCode = SubLayerTimeInfo->EndTimeCode.Get(0.0);

	FFrameRate FrameRate = MovieScene->GetTickResolution();
	FFrameNumber ModifiedStartFrame = Section->GetInclusiveStartFrame();
	FFrameNumber ModifiedEndFrame   = Section->GetExclusiveEndFrame();

	// This will obviously be quantized to frame intervals for now
	double SubSectionStartTimeCode = FrameRate.AsSeconds(ModifiedStartFrame) * TimeCodesPerSecond;

	UE::FSdfLayerOffset NewLayerOffset;
	NewLayerOffset.Scale = FMath::IsNearlyZero( Section->Parameters.TimeScale ) ? 0.f : 1.f / Section->Parameters.TimeScale;
	NewLayerOffset.Offset = SubSectionStartTimeCode - SubStartTimeCode * NewLayerOffset.Scale;

	if ( FMath::IsNearlyZero( NewLayerOffset.Offset ) )
	{
		NewLayerOffset.Offset = 0.0;
	}

	if ( FMath::IsNearlyEqual( NewLayerOffset.Scale, 1.0 ) )
	{
		NewLayerOffset.Scale = 1.0;
	}

	// Prevent twins from being rebuilt when we update the layer offsets
	FScopedBlockNotices BlockNotices( StageActor.Get()->GetUsdListener() );

	if ( LocalLayersSequences.Contains( SubSequence->GetFName() ) )
	{
		UE::FSdfLayer Layer = UE::FSdfLayer::FindOrOpen(*LayerTimeInfo->Identifier);
		if (!Layer)
		{
			UE_LOG(LogUsd, Warning, TEXT("Failed to update sublayer '%s'"), *LayerTimeInfo->Identifier);
			return;
		}

		int32 SubLayerIndex = INDEX_NONE;
		FLayerOffsetInfo* SubLayerOffset = Algo::FindByPredicate( LayerTimeInfo->SubLayersOffsets,
			[ &SubLayerIndex, &SubLayerIdentifier = SubLayerTimeInfo->Identifier ]( const FLayerOffsetInfo& Other )
			{
				bool bFound = ( Other.LayerIdentifier == SubLayerIdentifier );
				++SubLayerIndex;

				return bFound;
			} );

		if ( SubLayerIndex != INDEX_NONE )
		{
			Layer.SetSubLayerOffset( NewLayerOffset, SubLayerIndex );
			UpdateLayerTimeInfoFromLayer( *LayerTimeInfo, Layer );
		}
	}
	else
	{
		TArray< FString > PrimPathsForSequence;
		PrimPathByLevelSequenceName.MultiFind( Section->GetSequence()->GetFName(), PrimPathsForSequence );

		for ( const FString& PrimPath : PrimPathsForSequence )
		{
			UsdUtils::SetRefOrPayloadLayerOffset( StageActor->GetUsdStage().GetPrimAtPath( UE::FSdfPath( *PrimPath ) ), NewLayerOffset );
		}
	}

	UE_LOG(LogUsd, Verbose, TEXT("\tNew OffsetScale: %f, %f"), NewLayerOffset.Offset, NewLayerOffset.Scale);
}

void FUsdLevelSequenceHelperImpl::UpdateMovieSceneReadonlyFlags()
{
	if ( !StageActor.IsValid() )
	{
		return;
	}

	for ( const TPair< FString, ULevelSequence* >& SequenceIndentifierToSequence : StageActor->LevelSequencesByIdentifier )
	{
		if ( ULevelSequence* Sequence = SequenceIndentifierToSequence.Value )
		{
			if ( FString* LayerIdentifier = LayerIdentifierByLevelSequenceName.Find( Sequence->GetFName() ) )
			{
				if ( UMovieScene* MovieScene = Sequence->GetMovieScene() )
				{
					UpdateMovieSceneReadonlyFlag( *MovieScene, *LayerIdentifier );
				}
			}
		}
	}
}

void FUsdLevelSequenceHelperImpl::UpdateMovieSceneReadonlyFlag( UMovieScene& MovieScene, const FString& LayerIdentifier )
{
	if ( !StageActor.IsValid() )
	{
		return;
	}

	UE::FSdfLayer Layer = UE::FSdfLayer::FindOrOpen( *LayerIdentifier );
	const bool bIsReadOnly = ( Layer != StageActor->GetUsdStage().GetEditTarget() );
	MovieScene.SetReadOnly( bIsReadOnly );
}

void FUsdLevelSequenceHelperImpl::UpdateMovieSceneTimeRanges( UMovieScene& MovieScene, const FLayerTimeInfo& LayerTimeInfo )
{
	const double FramesPerSecond = GetFramesPerSecond();

	if ( LayerTimeInfo.IsAnimated() )
	{
		const double StartTimeCode = LayerTimeInfo.StartTimeCode.Get(0.0);
		const double EndTimeCode = LayerTimeInfo.EndTimeCode.Get(0.0);
		const double TimeCodesPerSecond = GetTimeCodesPerSecond();

		const FFrameRate DestFrameRate = MovieScene.GetTickResolution();
		const FFrameNumber StartFrame  = UsdLevelSequenceHelperImpl::RoundAsFrameNumber( DestFrameRate, StartTimeCode / TimeCodesPerSecond );
		const FFrameNumber EndFrame    = UsdLevelSequenceHelperImpl::RoundAsFrameNumber( DestFrameRate, EndTimeCode / TimeCodesPerSecond );
		TRange< FFrameNumber > TimeRange = TRange<FFrameNumber>::Inclusive( StartFrame, EndFrame );

		MovieScene.SetPlaybackRange( TimeRange );
		MovieScene.SetViewRange( StartTimeCode / TimeCodesPerSecond -1.0f, 1.0f + EndTimeCode / TimeCodesPerSecond );
		MovieScene.SetWorkingRange( StartTimeCode / TimeCodesPerSecond -1.0f, 1.0f + EndTimeCode / TimeCodesPerSecond );
	}

	// Always set these even if we're not animated because if a child layer IS animated and has a different framerate we'll get a warning
	// from the sequencer. Realistically it makes no difference because if the root layer is not animated (i.e. has 0 for start and end timecodes)
	// nothing will actually play, but this just prevents the warning
	MovieScene.SetDisplayRate( FFrameRate( FramesPerSecond, 1 ) );
}

void FUsdLevelSequenceHelperImpl::OnObjectTransacted(UObject* Object, const class FTransactionObjectEvent& Event)
{
	AUsdStageActor* ValidStageActor = StageActor.Get();

	if ( !ValidStageActor || !ValidStageActor->LevelSequence || !bMonitorChanges )
	{
		return;
	}

	if ( UMovieScene* MovieScene = Cast< UMovieScene >( Object ) )
	{
		HandleMovieSceneChange(*MovieScene);
	}
	else if ( UMovieSceneSubSection* Section = Cast<UMovieSceneSubSection>(Object) )
	{
		HandleSubSectionChange(*Section);
	}
	else if ( UMovieScene3DTransformTrack* TransformTrack = Cast<UMovieScene3DTransformTrack>(Object) )
	{
		HandleTransformTrackChange(*TransformTrack);
	}
	else if ( UMovieScene3DTransformSection* TransformSection = Cast<UMovieScene3DTransformSection>(Object) )
	{
		if ( UMovieScene3DTransformTrack* SectionTrack = TransformSection->GetTypedOuter<UMovieScene3DTransformTrack>() )
		{
			HandleTransformTrackChange(*SectionTrack);
		}
	}
}

double FUsdLevelSequenceHelperImpl::GetFramesPerSecond() const
{
	if ( !StageActor.IsValid() || !StageActor->GetUsdStage() )
	{
		return DefaultFramerate;
	}

	const double StageFramesPerSecond = StageActor->GetUsdStage().GetFramesPerSecond();
	return FMath::IsNearlyZero( StageFramesPerSecond ) ? DefaultFramerate : StageFramesPerSecond;
}

double FUsdLevelSequenceHelperImpl::GetTimeCodesPerSecond() const
{
	if ( !StageActor.IsValid() || !StageActor->GetUsdStage() )
	{
		return DefaultFramerate;
	}

	const double StageTimeCodesPerSecond = StageActor->GetUsdStage().GetTimeCodesPerSecond();
	return FMath::IsNearlyZero( StageTimeCodesPerSecond ) ? DefaultFramerate : StageTimeCodesPerSecond;
}

UE::FUsdAttribute FUsdLevelSequenceHelperImpl::GetXformAttribute( const UE::FUsdPrim& UsdPrim ) const
{
	if ( !UsdPrim )
	{
		return {};
	}

	// Because a xform can have multiple xformOps, we try to get the transform op first, if it doesn't exist, we'll use the XformOpOrderAttr as the main xform attribute
	UE::FUsdAttribute XformAttribute = UsdPrim.GetAttribute( TEXT("xformOp:transform") );
	if ( !XformAttribute )
	{
		XformAttribute = UE::FUsdGeomXformable( UsdPrim ).GetXformOpOrderAttr();
	}

	return XformAttribute;
}

void FUsdLevelSequenceHelperImpl::HandleMovieSceneChange( UMovieScene& MovieScene )
{
	if ( !StageActor.IsValid() )
	{
		return;
	}

	ULevelSequence* Sequence = MovieScene.GetTypedOuter< ULevelSequence >();

	if ( !Sequence )
	{
		return;
	}

	const FString LayerIdentifier = LayerIdentifierByLevelSequenceName.FindRef( Sequence->GetFName() );
	FLayerTimeInfo* LayerTimeInfo = LayerTimeInfosByLayerIdentifier.Find( LayerIdentifier );

	if ( !LayerTimeInfo )
	{
		return;
	}

	UE::FSdfLayer Layer = UE::FSdfLayer::FindOrOpen( *LayerTimeInfo->Identifier );

	if ( !Layer )
	{
		return;
	}

	const TRange< FFrameNumber > PlaybackRange = MovieScene.GetPlaybackRange();
	const FFrameRate DisplayRate = MovieScene.GetDisplayRate();
	const FFrameRate LayerTimeCodesPerSecond( Layer.GetTimeCodesPerSecond(), 1 );
	const FFrameTime StartTime = FFrameRate::TransformTime(UE::MovieScene::DiscreteInclusiveLower( PlaybackRange ).Value, MovieScene.GetTickResolution(), LayerTimeCodesPerSecond );
	const FFrameTime EndTime = FFrameRate::TransformTime(UE::MovieScene::DiscreteExclusiveUpper( PlaybackRange ).Value, MovieScene.GetTickResolution(), LayerTimeCodesPerSecond );

	UE::FSdfChangeBlock ChangeBlock;
	if ( !FMath::IsNearlyEqual( DisplayRate.AsDecimal(), GetFramesPerSecond() ) )
	{
		StageActor->GetUsdStage().SetFramesPerSecond( DisplayRate.AsDecimal() );

		// Propagate to all movie scenes, as USD only uses the stage FramesPerSecond so the sequences should have a unified DisplayRate to reflect that
		for ( TPair< FString, ULevelSequence* >& SequenceByIdentifier : StageActor->LevelSequencesByIdentifier )
		{
			if ( ULevelSequence* OtherSequence = SequenceByIdentifier.Value )
			{
				if ( UMovieScene* OtherMovieScene = OtherSequence->GetMovieScene() )
				{
					OtherMovieScene->SetDisplayRate( DisplayRate );
				}
			}
		}
	}

	Layer.SetStartTimeCode( StartTime.RoundToFrame().Value );
	Layer.SetEndTimeCode( EndTime.RoundToFrame().Value );

	UpdateLayerTimeInfoFromLayer( *LayerTimeInfo, Layer );

	if ( Sequence == StageActor->LevelSequence )
	{
		CreateTimeTrack( FindOrAddLayerTimeInfo( StageActor->GetUsdStage().GetRootLayer() ) );
	}

	// Check if we deleted a track
	for ( TMap< TWeakObjectPtr< const UUsdPrimTwin >, TPair< ULevelSequence*, FGuid > >::TIterator SceneComponentBindingIt = SceneComponentsBindings.CreateIterator(); SceneComponentBindingIt; ++SceneComponentBindingIt )
	{
		if ( SceneComponentBindingIt->Value.Key == Sequence &&
			( !MovieScene.FindPossessable( SceneComponentBindingIt->Value.Value ) ||
			  !MovieScene.FindTrack( UMovieScene3DTransformTrack::StaticClass(), SceneComponentBindingIt->Value.Value ) ) )
		{
			if ( const UUsdPrimTwin* UsdPrimTwin = SceneComponentBindingIt->Key.Get() )
			{
				SceneComponentBindingIt.RemoveCurrent();

				// Clear the anim for that xform
				if ( UE::FUsdPrim UsdPrim = StageActor->GetUsdStage().GetPrimAtPath( UE::FSdfPath( *UsdPrimTwin->PrimPath ) ) )
				{
					UE::FUsdGeomXformable Xformable( UsdPrim );
					if ( Xformable )
					{
						UE::FSdfPath TransformPath = UsdPrim.GetAttribute( TEXT("xformOp:transform") ).GetPath();

						const TSet< double > TimeSamples = Layer.ListTimeSamplesForPath( TransformPath );

						for ( double TimeSample : TimeSamples )
						{
							Layer.EraseTimeSample( TransformPath, TimeSample );
						}
					}
				}
			}
		}
	}
}

void FUsdLevelSequenceHelperImpl::HandleSubSectionChange( UMovieSceneSubSection& Section )
{
	UMovieSceneSequence* ParentSequence = Section.GetTypedOuter<UMovieSceneSequence>();
	if (!ParentSequence)
	{
		return;
	}

	UpdateUsdLayerOffsetFromSection(ParentSequence, &Section);
}

void FUsdLevelSequenceHelperImpl::HandleTransformTrackChange( const UMovieScene3DTransformTrack& TransformTrack )
{
	ULevelSequence* Sequence = TransformTrack.GetTypedOuter< ULevelSequence >();
	if ( !Sequence )
	{
		return;
	}

	const UMovieScene* MovieScene = Sequence->GetMovieScene();

	if ( !MovieScene )
	{
		return;
	}

	FGuid PossessableGuid;
	MovieScene->FindTrackBinding( TransformTrack, PossessableGuid );

	TArray< UObject*, TInlineAllocator< 1 > > BoundObjects = Cast< UMovieSceneSequence >( Sequence )->LocateBoundObjects( PossessableGuid, nullptr ); // TODO: Binding sur actor?

	for ( UObject* BoundObject : BoundObjects )
	{
		USceneComponent* BoundSceneComponent = nullptr;

		if ( AActor* BoundActor = Cast< AActor >( BoundObject ) )
		{
			BoundSceneComponent = BoundActor->GetRootComponent();
		}
		else
		{
			BoundSceneComponent = Cast< USceneComponent >( BoundObject );
		}

		if ( BoundSceneComponent )
		{
			if ( UUsdPrimTwin* PrimTwin = StageActor->RootUsdTwin->Find( BoundSceneComponent ) )
			{
				FScopedBlockNotices BlockNotices( StageActor->GetUsdListener() );
				UE::FUsdPrim UsdPrim = StageActor->GetUsdStage().GetPrimAtPath( UE::FSdfPath( *PrimTwin->PrimPath ) );

				SceneComponentsBindings.Emplace( PrimTwin ) = TPair< ULevelSequence*, FGuid >( Sequence, PossessableGuid ); // Make sure we track this binding

				FMovieSceneSequenceTransform SequenceTransform;

				if ( const FMovieSceneSequenceID* SequenceID = SequencesID.Find( Sequence ) )
				{
					if ( FMovieSceneSubSequenceData* SubSequenceData = SequenceHierarchyCache.FindSubData( *SequenceID ) )
					{
						SequenceTransform = SubSequenceData->RootToSequenceTransform;
					}
				}

				UnrealToUsd::ConvertXformable( TransformTrack, UsdPrim, SequenceTransform );
			}
		}
	}
}

FUsdLevelSequenceHelperImpl::FLayerTimeInfo& FUsdLevelSequenceHelperImpl::FindOrAddLayerTimeInfo( const UE::FSdfLayer& Layer )
{
	const FString Identifier = Layer.GetIdentifier();

	if ( FLayerTimeInfo* LayerTimeInfo = LayerTimeInfosByLayerIdentifier.Find( Identifier ) )
	{
		return *LayerTimeInfo;
	}

	FLayerTimeInfo LayerTimeInfo;
	UpdateLayerTimeInfoFromLayer( LayerTimeInfo, Layer );

	UE_LOG(LogUsd, Verbose, TEXT("Creating layer time info for layer '%s'. Original timecodes: ['%s', '%s']"),
		*LayerTimeInfo.Identifier,
		LayerTimeInfo.StartTimeCode.IsSet() ? *LexToString(LayerTimeInfo.StartTimeCode.GetValue()) : TEXT("null"),
		LayerTimeInfo.EndTimeCode.IsSet() ? *LexToString(LayerTimeInfo.EndTimeCode.GetValue()) : TEXT("null"));

	return LayerTimeInfosByLayerIdentifier.Add( Identifier, LayerTimeInfo );
}

void FUsdLevelSequenceHelperImpl::UpdateLayerTimeInfoFromLayer( FLayerTimeInfo& LayerTimeInfo, const UE::FSdfLayer& Layer )
{
	if ( !Layer )
	{
		return;
	}

	LayerTimeInfo.Identifier         = Layer.GetIdentifier();
	LayerTimeInfo.FilePath           = Layer.GetRealPath();
	LayerTimeInfo.StartTimeCode      = Layer.HasStartTimeCode() ? Layer.GetStartTimeCode() : TOptional<double>();
	LayerTimeInfo.EndTimeCode        = Layer.HasEndTimeCode() ? Layer.GetEndTimeCode() : TOptional<double>();

	const TArray< FString >& SubLayerPaths = Layer.GetSubLayerPaths();
	LayerTimeInfo.SubLayersOffsets.Empty( SubLayerPaths.Num() );

	int32 SubLayerIndex = 0;
	for ( const UE::FSdfLayerOffset& SubLayerOffset : Layer.GetSubLayerOffsets() )
	{
		if ( SubLayerPaths.IsValidIndex( SubLayerIndex ) )
		{
			if ( UE::FSdfLayer SubLayer = UsdUtils::FindLayerForSubLayerPath( Layer, SubLayerPaths[ SubLayerIndex ] ) )
			{
				FLayerOffsetInfo SubLayerOffsetInfo;
				SubLayerOffsetInfo.LayerIdentifier = SubLayer.GetIdentifier();
				SubLayerOffsetInfo.LayerOffset = SubLayerOffset;

				LayerTimeInfo.SubLayersOffsets.Add( MoveTemp( SubLayerOffsetInfo ) );
			}
		}

		++SubLayerIndex;
	}
}

ULevelSequence* FUsdLevelSequenceHelperImpl::FindSequenceForIdentifier( const FString& SequenceIdentitifer ) const
{
	AUsdStageActor* ValidStageActor = StageActor.Get();
	if ( !ValidStageActor )
	{
		return nullptr;
	}

	ULevelSequence* Sequence = nullptr;
	if ( ULevelSequence** FoundSequence = ValidStageActor->LevelSequencesByIdentifier.Find( SequenceIdentitifer ) )
	{
		Sequence = *FoundSequence;
	}

	return Sequence;
}
#else
class FUsdLevelSequenceHelperImpl
{
public:
	FUsdLevelSequenceHelperImpl(TWeakObjectPtr<AUsdStageActor> InStageActor) {}
	~FUsdLevelSequenceHelperImpl(){}

	void CreateLocalLayersSequences(){}

	void AddPrim( UUsdPrimTwin& PrimTwin ){}
	void RemovePrim(const UUsdPrimTwin& PrimTwin){}

	void StartMonitoringChanges(){}
	void StopMonitoringChanges(){}
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

void FUsdLevelSequenceHelper::InitLevelSequence(const UE::FUsdStage& UsdStage)
{
	if (UsdSequencerImpl.IsValid() && UsdStage)
	{
		UE_LOG(LogUsd, Verbose, TEXT("InitLevelSequence"));

		UsdSequencerImpl->CreateLocalLayersSequences();
	}
}

void FUsdLevelSequenceHelper::AddPrim(UUsdPrimTwin& PrimTwin)
{
	if (UsdSequencerImpl.IsValid())
	{
		UsdSequencerImpl->AddPrim(PrimTwin);
	}
}

void FUsdLevelSequenceHelper::RemovePrim(const UUsdPrimTwin& PrimTwin)
{
	if (UsdSequencerImpl.IsValid())
	{
		UsdSequencerImpl->RemovePrim(PrimTwin);
	}
}

void FUsdLevelSequenceHelper::StartMonitoringChanges()
{
	if (UsdSequencerImpl.IsValid())
	{
		UsdSequencerImpl->StartMonitoringChanges();
	}
}

void FUsdLevelSequenceHelper::StopMonitoringChanges()
{
	if (UsdSequencerImpl.IsValid())
	{
		UsdSequencerImpl->StopMonitoringChanges();
	}
}
