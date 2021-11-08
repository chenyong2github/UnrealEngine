// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelSequenceExporterUSD.h"

#include "LevelSequenceExporterUSDOptions.h"
#include "UnrealUSDWrapper.h"
#include "USDConversionUtils.h"
#include "USDLayerUtils.h"
#include "USDLog.h"
#include "USDOptionsWindow.h"
#include "USDPrimConversion.h"

#include "UsdWrappers/SdfLayer.h"
#include "UsdWrappers/SdfPath.h"
#include "UsdWrappers/UsdAttribute.h"
#include "UsdWrappers/UsdPrim.h"
#include "UsdWrappers/UsdStage.h"

#include "AssetExportTask.h"
#include "Components/SkeletalMeshComponent.h"
#include "ISequencer.h"
#include "ISequencerModule.h"
#include "LevelSequence.h"
#include "Misc/LevelSequenceEditorSpawnRegister.h"
#include "Modules/ModuleManager.h"
#include "MovieSceneSequence.h"
#include "MovieSceneSpawnRegister.h"
#include "MovieSceneTimeHelpers.h"
#include "MovieSceneTrack.h"
#include "Sections/MovieSceneSubSection.h"
#include "Tracks/MovieScene3DTransformTrack.h"
#include "Tracks/MovieSceneBoolTrack.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "Tracks/MovieScenePropertyTrack.h"
#include "Tracks/MovieSceneSkeletalAnimationTrack.h"
#include "Tracks/MovieSceneSpawnTrack.h"
#include "Tracks/MovieSceneSubTrack.h"
#include "Tracks/MovieSceneVisibilityTrack.h"
#include "UObject/UObjectGlobals.h"

namespace UE
{
	namespace LevelSequenceExporterUSD
	{
		namespace Private
		{
#if USE_USD_SDK
			// Custom spawn register so that when DestroySpawnedObject is called while bDestroyingJustHides is true we
			// actually just hide the objects, so that we can keep a live reference to components within the bakers.
			// We're going to convert the spawnable tracks into visibility tracks when exporting to USD, which
			// also works well with this approach
			class FLevelSequenceHidingSpawnRegister : public FLevelSequenceEditorSpawnRegister
			{
			public:
				bool bDestroyingJustHides = true;

				virtual UObject* SpawnObject( FMovieSceneSpawnable& Spawnable, FMovieSceneSequenceIDRef TemplateID, IMovieScenePlayer& Player ) override
				{
					UObject* Object = ExistingSpawns.FindRef( Spawnable.GetGuid() );

					if ( !Object )
					{
						Object = FLevelSequenceEditorSpawnRegister::SpawnObject( Spawnable, TemplateID, Player );
						ExistingSpawns.Add( Spawnable.GetGuid(), Object );
					}

					if ( Object )
					{
						if ( USceneComponent* Component = Cast<USceneComponent>( Object ) )
						{
							const bool bNewVisibility = true;
							Component->SetVisibility( bNewVisibility );
						}
						else if ( AActor* Actor = Cast<AActor>( Object ) )
						{
							const bool bIsHidden = false;
							Actor->SetHidden( bIsHidden );
						}
					}

					return Object;
				}

				virtual void DestroySpawnedObject( UObject& Object ) override
				{
					if ( bDestroyingJustHides )
					{
						if ( USceneComponent* Component = Cast<USceneComponent>( &Object ) )
						{
							const bool bNewVisibility = false;
							Component->SetVisibility( bNewVisibility );
						}
						else if ( AActor* Actor = Cast<AActor>( &Object ) )
						{
							const bool bIsHidden = true;
							Actor->SetHidden( bIsHidden );
						}
					}
					else
					{
						FLevelSequenceEditorSpawnRegister::DestroySpawnedObject( Object );
						if ( const FGuid* ExistingKey = ExistingSpawns.FindKey( &Object ) )
						{
							ExistingSpawns.Remove( *ExistingKey );
						}
					}
				}

				// FLevelSequenceHidingSpawnRegister is a bit of a hack and just hides it's spawned actors instead
				// of deleting them (when we want it to do so). Unfortunately, the base FMovieSceneSpawnRegister part
				// will still nevertheless clear it's Register entry for the spawnable when deleting (even if just hiding),
				// and there's nothing we can do to prevent it. This means we can't call FindSpawnedObject and must use our
				// own GetExistingSpawn and ExistingSpawns member
				UObject* GetExistingSpawn( const FGuid& Guid )
				{
					return ExistingSpawns.FindRef( Guid );
				}

			private:
				TMap<FGuid, UObject*> ExistingSpawns;
			};

			// Contain all of the baker lambda functions for a given component. Only one baker per baking type is allowed.
			struct FCombinedComponentBakers
			{
				UnrealToUsd::EBakingType CombinedBakingType = UnrealToUsd::EBakingType::None;
				TArray<TFunction<void( double )>> Bakers;
			};

			class FLevelSequenceExportContext
			{
			public:
				FLevelSequenceExportContext( ULevelSequence& InSequence, TSharedRef<ISequencer> InSequencer, TSharedRef<FLevelSequenceHidingSpawnRegister> InSpawnRegister )
					: RootSequence( InSequence )
					, Sequencer( InSequencer )
					, SpawnRegister( InSpawnRegister )
				{
				}

				FLevelSequenceExportContext() = delete;
				FLevelSequenceExportContext( const FLevelSequenceExportContext& Other ) = delete;
				FLevelSequenceExportContext( const FLevelSequenceExportContext&& Other ) = delete;
				FLevelSequenceExportContext& operator=( const FLevelSequenceExportContext& Other ) = delete;
				FLevelSequenceExportContext& operator=( const FLevelSequenceExportContext&& Other ) = delete;

			public:
				// The actual content asset that is being exported
				ULevelSequence& RootSequence;

				ULevelSequenceExporterUsdOptions* ExportOptions;

				// Our own read-only sequencer that we use to play the level sequences while we bake them out one frame at a time
				TSharedRef<ISequencer> Sequencer;

				// Object that manages spawned instances for FMovieScenePossessables
				TSharedRef<FLevelSequenceHidingSpawnRegister> SpawnRegister;

				// Used to keep track of which sequences we already baked
				TMap<UMovieSceneSequence*, FString> ExportedMovieScenes;

				// File paths that we already used during this export.
				// Used so that we can prevent conflicts between files emitted for this export, but can still
				// overwrite other files on disk
				TSet<FString> UsedFilePaths;

				// File path of the exported USD root layer, in case we also exported the level along with the level sequence
				FString LevelFilePath;

			public:
				~FLevelSequenceExportContext()
				{
					SpawnRegister->bDestroyingJustHides = false;
					SpawnRegister->CleanUp( *Sequencer );
				}
			};

			// Returns a spawned UObject for a given spawnable from a MovieScene.
			// This assumes it has already been spawned and is tracked by SpawnRegister.
			UObject* LocateBoundObject( const UMovieScene* MovieScene, FMovieSceneSpawnable& Spawnable, const TSharedRef<FLevelSequenceHidingSpawnRegister>& SpawnRegister )
			{
				// Everything should have been spawned by now so just check our spawn register
				return SpawnRegister->GetExistingSpawn( Spawnable.GetGuid() );
			}

			// Returns the UObject possessed by a possessable from a MovieScene
			UObject* LocateBoundObject( const UMovieSceneSequence& MovieSceneSequence, UMovieScene* MovieScene, const FMovieScenePossessable& Possessable, const TSharedRef<FLevelSequenceHidingSpawnRegister>& SpawnRegister )
			{
				const FGuid& Guid = Possessable.GetGuid();
				const FGuid& ParentGuid = Possessable.GetParent();

				// If we have a parent guid, we must provide the object as a context because really the binding path
				// will just contain the component name
				UObject* ParentContext = nullptr;
				if ( ParentGuid.IsValid() )
				{
					if ( FMovieScenePossessable* ParentPossessable = MovieScene->FindPossessable( ParentGuid ) )
					{
						ParentContext = LocateBoundObject( MovieSceneSequence, MovieScene, *ParentPossessable, SpawnRegister );
					}
					else if ( FMovieSceneSpawnable* ParentSpawnable = MovieScene->FindSpawnable( ParentGuid ) )
					{
						ParentContext = LocateBoundObject( MovieScene, *ParentSpawnable, SpawnRegister );
					}
				}

				TArray<UObject*, TInlineAllocator<1>> Objects = MovieSceneSequence.LocateBoundObjects( Guid, ParentContext );
				if ( Objects.Num() > 0 )
				{
					return Objects[ 0 ];
				}

				return nullptr;
			}

			// Returns false if this track has no keys and no skeletal animations, and true otherwise
			bool IsTrackAnimated( const UMovieSceneTrack* Track )
			{
				for ( const UMovieSceneSection* Section : Track->GetAllSections() )
				{
					TOptional< TRange<FFrameNumber> > Range = Section->GetAutoSizeRange();
					if ( Range.IsSet() && !Range.GetValue().IsEmpty() )
					{
						return true;
					}
				}

				return false;
			}

			// Recursively go over Sequence and spawn all bound spawnables and then hide them
			void PreSpawnSpawnables( FLevelSequenceExportContext& Context, UMovieSceneSequence& Sequence )
			{
				UMovieScene* MovieScene = Sequence.GetMovieScene();
				if ( !MovieScene )
				{
					return;
				}

				// Spawn everything on this movie scene
				int32 NumSpawnables = MovieScene->GetSpawnableCount();
				for ( int32 Index = 0; Index < NumSpawnables; ++Index )
				{
					const FMovieSceneSpawnable& Spawnable = MovieScene->GetSpawnable( Index );
					const FGuid& Guid = Spawnable.GetGuid();

					UObject* SpawnedObject = StaticCastSharedRef<FMovieSceneSpawnRegister>( Context.SpawnRegister )->SpawnObject( Guid, *MovieScene, Context.Sequencer->GetFocusedTemplateID(), *Context.Sequencer );

					// HACK: Remove the SequencerActor tag from the spawned actor because the USD level exporter
					// uses these as an indicator that it shouldn't export a given actor, because the transient actors spawned by the
					// sequencer and the AUsdStageActor contain them.
					// We will manually delete all of those actors with our spawn register later and never actually use them
					// for anything else though, so this should be fine
					if ( AActor* SpawnedActor = Cast<AActor>( SpawnedObject ) )
					{
						SpawnedActor->Tags.Remove( TEXT( "SequencerActor" ) );
					}
				}

				// Hide all our spawns again so we can pretend they haven't actually spawned yet
				Context.SpawnRegister->bDestroyingJustHides = true;
				Context.SpawnRegister->CleanUp( *Context.Sequencer );

				// Recurse to subsequences
				for ( const UMovieSceneTrack* MasterTrack : MovieScene->GetMasterTracks() )
				{
					const UMovieSceneSubTrack* SubTrack = Cast<const UMovieSceneSubTrack>( MasterTrack );
					if ( !SubTrack )
					{
						continue;
					}

					for ( UMovieSceneSection* Section : SubTrack->GetAllSections() )
					{
						UMovieSceneSubSection* SubSection = Cast<UMovieSceneSubSection>( Section );
						if ( !SubSection )
						{
							continue;
						}

						UMovieSceneSequence* SubSequence = SubSection->GetSequence();
						if ( !SubSequence )
						{
							continue;
						}

						// Focus on this section before recursing so that every time we spawn a spawnable we can just query
						// Sequencer->GetFocusedTemplateID() to fetch the FMovieSceneSequenceIDRef for the movie scene that
						// actually owns the spawnable.
						// We always reset the sequencer to a new root sequence before baking anyway
						Context.Sequencer->FocusSequenceInstance( *SubSection );

						PreSpawnSpawnables( Context, *SubSequence );
					}
				}
			}

			// Appends to InOutComponentBakers all of the component bakers for all components bound to MovieSceneSequence.
			// In the process it will generate the output prims for each of these components, and keep track of them
			// within the bakers themselves
			void GenerateBakersForMovieScene(
				FLevelSequenceExportContext& Context,
				UMovieSceneSequence& MovieSceneSequence,
				UE::FUsdStage& UsdStage,
				TMap<USceneComponent*, FCombinedComponentBakers>& InOutComponentBakers
			)
			{
				UMovieScene* MovieScene = MovieSceneSequence.GetMovieScene();
				if ( !MovieScene )
				{
					return;
				}

				// Find all components (if actors, the root components of those actors) bound to the movie scene.
				// We use components here because when exporting actors and components to USD we basically just ignore
				// actors altogether and export the component attachment hierarchy instead
				TArray<TPair<FGuid, USceneComponent*>> BoundComponents;

				// Possessables
				int32 NumPossessables = MovieScene->GetPossessableCount();
				for ( int32 Index = 0; Index < NumPossessables; ++Index )
				{
					const FMovieScenePossessable& Possessable = MovieScene->GetPossessable( Index );
					const FGuid& Guid = Possessable.GetGuid();

					UObject* BoundObject = LocateBoundObject( MovieSceneSequence, MovieScene, Possessable, Context.SpawnRegister );

					USceneComponent* BoundComponent = Cast<USceneComponent>( BoundObject );
					if ( !BoundComponent )
					{
						if ( const AActor* Actor = Cast<AActor>( BoundObject ) )
						{
							BoundComponent = Actor->GetRootComponent();
						}
					}
					if ( !BoundComponent )
					{
						continue;
					}

					BoundComponents.Add( TPair<FGuid, USceneComponent*>( Guid, BoundComponent ) );
				}

				// Spawnables
				int32 NumSpawnables = MovieScene->GetSpawnableCount();
				for ( int32 Index = 0; Index < NumSpawnables; ++Index )
				{
					FMovieSceneSpawnable& Spawnable = MovieScene->GetSpawnable( Index );
					const FGuid& Guid = Spawnable.GetGuid();

					UObject* BoundObject = Context.SpawnRegister->GetExistingSpawn( Guid );
					if ( !BoundObject )
					{
						// This should never happen as we preemptively spawn everything:
						// At this point all our spawns should be spawned, but invisible
						UE_LOG( LogUsd, Warning, TEXT( "Failed to find spawned object for spawnable with Guid '%s'" ), *Guid.ToString() );
						continue;
					}

					USceneComponent* BoundComponent = Cast<USceneComponent>( BoundObject );
					if ( !BoundComponent )
					{
						if ( const AActor* Actor = Cast<AActor>( BoundObject ) )
						{
							BoundComponent = Actor->GetRootComponent();
						}
					}
					if ( !BoundComponent )
					{
						continue;
					}

					BoundComponents.Add( TPair<FGuid, USceneComponent*>( Guid, BoundComponent ) );
				}

				// Generate bakers
				for ( const TPair<FGuid, USceneComponent*>& Pair : BoundComponents )
				{
					const FGuid& Guid = Pair.Key;
					USceneComponent* BoundComponent = Pair.Value;

					FString PrimPath = UsdUtils::GetPrimPathForObject(
						BoundComponent,
						TEXT(""),
						Context.ExportOptions && Context.ExportOptions->LevelExportOptions.bExportActorFolders
					);
					if ( PrimPath.IsEmpty() )
					{
						continue;
					}

					FString SchemaName = UsdUtils::GetSchemaNameForComponent( *BoundComponent );
					if ( SchemaName.IsEmpty() )
					{
						continue;
					}

					// We will define a prim here so that we can apply schemas and use the shortcut CreateXAttribute functions,
					// and not have to worry about attribute names and types. Later on we will convert these prims back into just 'overs' though
					UE::FUsdPrim Prim = UsdStage.DefinePrim( UE::FSdfPath{ *PrimPath }, *SchemaName );
					if ( !Prim )
					{
						continue;
					}

					if ( const FMovieSceneBinding* Binding = MovieScene->FindBinding( Guid ) )
					{
						for ( const UMovieSceneTrack* Track : Binding->GetTracks() )
						{
							if ( !IsTrackAnimated( Track ) )
							{
								continue;
							}

							UnrealToUsd::FComponentBaker Baker;

							if ( const UMovieScenePropertyTrack* PropertyTrack = Cast<UMovieScenePropertyTrack>( Track ) )
							{
								const FString PropertyPath = PropertyTrack->GetPropertyPath().ToString();
								UnrealToUsd::CreateComponentPropertyBaker( Prim, *BoundComponent, PropertyPath, Baker );
							}
							else if ( const UMovieSceneSpawnTrack* SpawnTrack = Cast<UMovieSceneSpawnTrack>( Track ) )
							{
								// Just handle spawnable tracks as if they're visibility tracks, and hide the prim when not "spawned"
								// Remember that our spawn register just hides the spawnables when they're not spawned anyway, so this
								// is essentially the same
								const FString PropertyPath = TEXT( "bHidden" );
								UnrealToUsd::CreateComponentPropertyBaker( Prim, *BoundComponent, PropertyPath, Baker);
							}
							else if ( const UMovieSceneSkeletalAnimationTrack* SkeletalTrack = Cast<UMovieSceneSkeletalAnimationTrack>( Track ) )
							{
								if ( USkeletalMeshComponent* SkeletalBoundComponent = Cast<USkeletalMeshComponent>( BoundComponent ) )
								{
									UE::FUsdPrim SkelAnimPrim = UsdStage.DefinePrim( UE::FSdfPath{ *PrimPath }.AppendChild( TEXT( "Anim" ) ), TEXT( "SkelAnimation" ) );
									if ( !SkelAnimPrim )
									{
										UE_LOG( LogUsd, Warning, TEXT( "Failed to generate SkelAnimation prim when baking out SkelRoot '%s'" ), *PrimPath );
										continue;
									}
									UnrealToUsd::CreateSkeletalAnimationBaker( Prim, SkelAnimPrim, *SkeletalBoundComponent, Baker );
								}
							}

							// If we made a baker and we don't have one of this type for this component yet, add its lambda to the array
							FCombinedComponentBakers& ExistingBakers = InOutComponentBakers.FindOrAdd( BoundComponent );
							if ( Baker.BakerType != UnrealToUsd::EBakingType::None && !EnumHasAnyFlags( ExistingBakers.CombinedBakingType, Baker.BakerType ) )
							{
								ExistingBakers.Bakers.Add( Baker.BakerFunction );
								ExistingBakers.CombinedBakingType |= Baker.BakerType;
							}
						}
					}
				}

				// We always have to generate bakers for subsequences (even if we're not exporting separate files for subsequences) since we
				// will bake the full combined, composed level sequence as the main output USD layer. This because USD doesn't allow any form
				// of animation blending, and so composing individual USD layers exported for each subsequence with the same result as the sequencer
				// is impossible... we have to settle for having each USD layer represent the full effect of it's level sequence.
				// Also note that we can't share these bakers with our parent movie scenes in case we're a subsequence, unfortunately, because our
				// bakers will contain lambdas that write directly to a given prim, and those prims are specific to each layer that we're exporting
				// (e.g. our parent level sequence will export to different prims than this movie scene will)
				for ( const UMovieSceneTrack* MasterTrack : MovieScene->GetMasterTracks() )
				{
					const UMovieSceneSubTrack* SubTrack = Cast<const UMovieSceneSubTrack>( MasterTrack );
					if ( !SubTrack )
					{
						continue;
					}

					for ( const UMovieSceneSection* Section : SubTrack->GetAllSections() )
					{
						const UMovieSceneSubSection* SubSection = Cast<const UMovieSceneSubSection>( Section );
						if ( !SubSection )
						{
							continue;
						}

						UMovieSceneSequence* SubSequence = SubSection->GetSequence();
						if ( !SubSequence )
						{
							continue;
						}

						GenerateBakersForMovieScene( Context, *SubSequence, UsdStage, InOutComponentBakers );
					}
				}
			}

			// Steps through MovieSceneSequence frame by frame, invoking all baker lambdas every frame
			void BakeMovieSceneSequence(
				FLevelSequenceExportContext& Context,
				UMovieSceneSequence& MovieSceneSequence,
				UE::FUsdStage& UsdStage,
				const TMap<USceneComponent*, FCombinedComponentBakers>& ComponentBakers
			)
			{
				UMovieScene* MovieScene = MovieSceneSequence.GetMovieScene();
				if ( !MovieScene )
				{
					return;
				}

				// Currently we bake the full composed result of each level sequence into a single layer,
				// because USD can't compose individual layers in the same way (with blending and so on). So here
				// we make sure that our sequencer exports each MovieSceneSequence as if it was a root, emitting the
				// same result as if we had exported that subsequence's LevelSequence by itself
				Context.Sequencer->ResetToNewRootSequence( MovieSceneSequence );

				const TRange< FFrameNumber > PlaybackRange = MovieScene->GetPlaybackRange();
				const FFrameRate Resolution = MovieScene->GetTickResolution();
				const FFrameRate DisplayRate = MovieScene->GetDisplayRate();
				const double StageTimeCodesPerSecond = UsdStage.GetTimeCodesPerSecond();
				const FFrameRate StageFrameRate( StageTimeCodesPerSecond, 1 );

				const FFrameTime Interval = FFrameRate::TransformTime( 1, DisplayRate, Resolution );
				FFrameNumber StartFrame = UE::MovieScene::DiscreteInclusiveLower( PlaybackRange );
				FFrameNumber EndFrame = UE::MovieScene::DiscreteExclusiveUpper( PlaybackRange );

				if ( Context.ExportOptions && Context.ExportOptions->bOverrideExportRange )
				{
					StartFrame = FFrameRate::TransformTime( Context.ExportOptions->StartFrame, DisplayRate, Resolution ).FloorToFrame();
					EndFrame = FFrameRate::TransformTime( Context.ExportOptions->EndFrame, DisplayRate, Resolution ).CeilToFrame();
				}

				FFrameTime StartFrameUETime = FFrameRate::Snap( StartFrame, Resolution, DisplayRate ).FloorToFrame();
				double StartTimeCode = FFrameRate::TransformTime( StartFrameUETime, Resolution, StageFrameRate ).AsDecimal();
				UsdStage.SetStartTimeCode( StartTimeCode );

				FFrameTime EndFrameUETime = FFrameRate::Snap( EndFrame, Resolution, DisplayRate ).FloorToFrame();
				double EndTimeCode = FFrameRate::TransformTime( EndFrameUETime, Resolution, StageFrameRate ).AsDecimal();
				UsdStage.SetEndTimeCode( EndTimeCode );

				for ( FFrameTime EvalTime = StartFrame; EvalTime <= EndFrame; EvalTime += Interval )
				{
					Context.Sequencer->SetLocalTimeDirectly( EvalTime );
					Context.Sequencer->ForceEvaluate();

					FFrameTime KeyTime = FFrameRate::Snap( EvalTime, Resolution, DisplayRate ).FloorToFrame();
					double UsdTimeCode = FFrameRate::TransformTime( KeyTime, Resolution, StageFrameRate ).AsDecimal();

					for ( const TPair<USceneComponent*, FCombinedComponentBakers>& Pair : ComponentBakers )
					{
						for ( const TFunction<void( double )>& Lambda : Pair.Value.Bakers )
						{
							if ( Lambda )
							{
								Lambda( UsdTimeCode );
							}
						}
					}
				}

				// Convert all prims back to overs (so that this layer doesn't define anything on a stage that doesn't
				// previously have it - it's only supposed to carry animation data)
				// We need to do this in this way because when going from 'def' to 'over' we need to do it from leaf towards the
				// root, as USD doesn't like a parent 'over' with a child 'def'
				TFunction<void( FUsdPrim& )> RecursiveSetOver;
				RecursiveSetOver = [&RecursiveSetOver]( FUsdPrim& Prim )
				{
					for ( FUsdPrim& Child : Prim.GetChildren() )
					{
						RecursiveSetOver( Child );
					}

					Prim.SetSpecifier( ESdfSpecifier::Over );
				};
				FUsdPrim Root = UsdStage.GetPseudoRoot();
				for ( FUsdPrim& TopLevelPrim : Root.GetChildren() )
				{
					RecursiveSetOver( TopLevelPrim );
				}
			}

			void ExportMovieSceneSequence(
				FLevelSequenceExportContext& Context,
				UMovieSceneSequence& MovieSceneSequence,
				const FString& FilePath
			)
			{
				if ( FilePath.IsEmpty() || Context.ExportedMovieScenes.Contains( &MovieSceneSequence ) )
				{
					return;
				}

				UMovieScene* MovieScene = MovieSceneSequence.GetMovieScene();
				if ( !MovieScene )
				{
					return;
				}

				// Make sure we don't overwrite a file we just wrote *during this export*.
				// Overwriting other files is OK, as we want to allow a "repeatedly export over the same files" workflow
				FString UniqueFilePath = FilePath;
				if ( Context.UsedFilePaths.Contains( UniqueFilePath ) )
				{
					int32 Suffix = 0;
					do
					{
						UniqueFilePath = FString::Printf( TEXT( "%s_%d" ), *FilePath, Suffix++ );
					} while ( Context.UsedFilePaths.Contains( UniqueFilePath ) );
				}

				UE::FUsdStage UsdStage = UnrealUSDWrapper::NewStage( *UniqueFilePath );
				if ( !UsdStage )
				{
					return;
				}

				if ( Context.ExportOptions )
				{
					UsdUtils::SetUsdStageMetersPerUnit( UsdStage, Context.ExportOptions->StageOptions.MetersPerUnit );
					UsdUtils::SetUsdStageUpAxis( UsdStage, Context.ExportOptions->StageOptions.UpAxis );
					UsdStage.SetTimeCodesPerSecond( Context.ExportOptions->TimeCodesPerSecond );
				}

				UE::FUsdPrim RootPrim = UsdStage.OverridePrim( UE::FSdfPath( TEXT( "/Root" ) ) );
				if ( !RootPrim )
				{
					return;
				}

				UsdStage.SetDefaultPrim( RootPrim );

				TMap<USceneComponent*, FCombinedComponentBakers> Bakers;
				GenerateBakersForMovieScene( Context, MovieSceneSequence, UsdStage, Bakers );

				// Bake this MovieScene
				// We bake each MovieScene individually instead of doing one large simultaneous bake because this way
				// not only we avoid having to handle FMovieSceneSequenceTransforms when writing out the UsdTimeCodes,
				// we guarantee we'll get the same result as if we exported each subsequence individually.
				// They could have differed, for example, if we had a limited the playback range of a subsequence
				BakeMovieSceneSequence( Context, MovieSceneSequence, UsdStage, Bakers );

				const TRange< FFrameNumber > PlaybackRange = MovieScene->GetPlaybackRange();
				const FFrameRate Resolution = MovieScene->GetTickResolution();
				const FFrameRate DisplayRate = MovieScene->GetDisplayRate();
				const double StageTimeCodesPerSecond = UsdStage.GetTimeCodesPerSecond();
				const FFrameRate StageFrameRate( StageTimeCodesPerSecond, 1 );

				if ( Context.ExportOptions )
				{
					if ( Context.ExportOptions->bExportSubsequencesAsLayers )
					{
						FString Directory;
						FString FileName;
						FString Extension;
						FPaths::Split( UniqueFilePath, Directory, FileName, Extension );

						for ( const UMovieSceneTrack* MasterTrack : MovieScene->GetMasterTracks() )
						{
							const UMovieSceneSubTrack* SubTrack = Cast<const UMovieSceneSubTrack>( MasterTrack );
							if ( !SubTrack )
							{
								continue;
							}

							for ( const UMovieSceneSection* Section : SubTrack->GetAllSections() )
							{
								const UMovieSceneSubSection* SubSection = Cast<const UMovieSceneSubSection>( Section );
								if ( !SubSection )
								{
									continue;
								}

								UMovieSceneSequence* SubSequence = SubSection->GetSequence();
								if ( !SubSequence )
								{
									continue;
								}

								FString SubSequencePath = FPaths::Combine( Directory, FString::Printf( TEXT( "%s.%s" ), *SubSequence->GetName(), *Extension ) );

								ExportMovieSceneSequence( Context, *SubSequence, SubSequencePath );

								// For now we don't want to actually add the subsequence layers as sublayers since each exported level sequence
								// contains the full baked result anyway, but this is how we'd do it:
								/*
								double Offset = 0.0;
								if ( SubSection->HasStartFrame() )
								{
									FFrameNumber LowerBound = SubSection->GetTrueRange().GetLowerBoundValue();
									FFrameTime LowerBoundTime{ FFrameRate::Snap( LowerBound, Resolution, DisplayRate ).FloorToFrame() };
									Offset = FFrameRate::TransformTime( LowerBoundTime, Resolution, StageFrameRate ).AsDecimal();
								}
								float Scale = SubSection->Parameters.TimeScale;
								const int32 Index = -1;
								UsdUtils::InsertSubLayer( UsdStage.GetRootLayer(), *SubSequencePath, Index, Offset, Scale );
								*/
							}
						}
					}

					// We can add the level as a sublayer to every exported subsequence, so that each can be opened individually and
					// automatically load the level layer. It doesn't matter much if the parent stage has composed the level
					// sublayer multiple times (in case we add subsequence layers as sublayers in the future), as the prims will
					// just all override each other with the same data
					if ( Context.ExportOptions->bUseExportedLevelAsSublayer && FPaths::FileExists( Context.LevelFilePath ) )
					{
						UsdUtils::InsertSubLayer( UsdStage.GetRootLayer(), *Context.LevelFilePath );
					}
				}

				Context.ExportedMovieScenes.Add( &MovieSceneSequence, UniqueFilePath );
				Context.UsedFilePaths.Add( UniqueFilePath );

				UsdStage.GetRootLayer().Save();
			}
#endif // USE_USD_SDK
		}
	}
}

ULevelSequenceExporterUsd::ULevelSequenceExporterUsd()
{
#if USE_USD_SDK
	for ( const FString& Extension : UnrealUSDWrapper::GetAllSupportedFileFormats() )
	{
		// USDZ is not supported for writing for now
		if ( Extension.Equals( TEXT( "usdz" ) ) )
		{
			continue;
		}

		FormatExtension.Add( Extension );
		FormatDescription.Add( TEXT( "USD file" ) );
	}
	SupportedClass = ULevelSequence::StaticClass();
	bText = false;
#endif // #if USE_USD_SDK
}

bool ULevelSequenceExporterUsd::ExportBinary( UObject* Object, const TCHAR* Type, FArchive& Ar, FFeedbackContext* Warn, int32 FileIndex, uint32 PortFlags )
{
	namespace LevelSequenceExporterImpl = UE::LevelSequenceExporterUSD::Private;

#if USE_USD_SDK
	ULevelSequence* LevelSequence = Cast< ULevelSequence >( Object );
	if ( !LevelSequence )
	{
		return false;
	}

	UMovieScene* MovieScene = LevelSequence->GetMovieScene();
	if ( !MovieScene )
	{
		return false;
	}

	ULevelSequenceExporterUsdOptions* Options = nullptr;
	if ( ExportTask )
	{
		Options = Cast<ULevelSequenceExporterUsdOptions>( ExportTask->Options );
	}
	if ( !Options && ( !ExportTask || !ExportTask->bAutomated ) )
	{
		Options = GetMutableDefault<ULevelSequenceExporterUsdOptions>();
		if ( Options )
		{
			Options->TimeCodesPerSecond = MovieScene->GetDisplayRate().AsDecimal();

			const bool bIsImport = false;
			const bool bContinue = SUsdOptionsWindow::ShowOptions( *Options, bIsImport );
			if ( !bContinue )
			{
				return false;
			}
		}
	}

	TSharedPtr<LevelSequenceExporterImpl::FLevelSequenceHidingSpawnRegister> SpawnRegister = MakeShared<LevelSequenceExporterImpl::FLevelSequenceHidingSpawnRegister>();
	if ( !SpawnRegister.IsValid() )
	{
		return false;
	}

	FSequencerInitParams Params;
	Params.RootSequence = LevelSequence;
	Params.SpawnRegister = SpawnRegister;
	Params.ViewParams.bReadOnly = true;
	Params.bEditWithinLevelEditor = false;
	Params.PlaybackContext = Options->Level.Get();

	// Set to read only or else CreateSequencer will change the playback range of the moviescene without even calling Modify()
	const bool bOldReadOnly = MovieScene->IsReadOnly();
	const bool bNewReadOnly = true;
	MovieScene->SetReadOnly( bNewReadOnly );
	TSharedPtr<ISequencer> TempSequencer = FModuleManager::LoadModuleChecked<ISequencerModule>( "Sequencer" ).CreateSequencer( Params );
	MovieScene->SetReadOnly( bOldReadOnly );
	if ( !TempSequencer.IsValid() )
	{
		return false;
	}

	TempSequencer->EnterSilentMode();
	TempSequencer->SetPlaybackStatus( EMovieScenePlayerStatus::Playing );

	SpawnRegister->SetSequencer( TempSequencer );

	LevelSequenceExporterImpl::FLevelSequenceExportContext Context{ *LevelSequence, TempSequencer.ToSharedRef(), SpawnRegister.ToSharedRef() };
	Context.ExportOptions = Options;

	// Spawn (but hide) all spawnables so that they will also show up on the level export if we need them to
	LevelSequenceExporterImpl::PreSpawnSpawnables( Context, Context.RootSequence );

	// Capture this first because when we launch UExporter::RunAssetExportTask the CurrentFileName will change
	const FString TargetFileName = UExporter::CurrentFilename;

	// Export level if we need to
	if ( Options && Options->bExportLevel )
	{
		if ( UWorld* WorldToExport = Options->Level.Get() )
		{
			// Come up with a file path for the level
			FString Directory;
			FString Filename;
			FString Extension;
			FPaths::Split( TargetFileName, Directory, Filename, Extension );
			Context.LevelFilePath = FPaths::Combine( Directory, FString::Printf( TEXT( "%s.%s" ), *GWorld->GetFName().ToString(), *Extension ) );

			Context.UsedFilePaths.Add( Context.LevelFilePath );

			ULevelExporterUSDOptions* LevelOptions = GetMutableDefault<ULevelExporterUSDOptions>();
			LevelOptions->StageOptions = Options->StageOptions;
			LevelOptions->Inner = Options->LevelExportOptions;

			UAssetExportTask* LevelExportTask = NewObject<UAssetExportTask>();
			FGCObjectScopeGuard ExportTaskGuard( LevelExportTask );
			LevelExportTask->Object = WorldToExport;
			LevelExportTask->Options = LevelOptions;
			LevelExportTask->Exporter = nullptr;
			LevelExportTask->Filename = Context.LevelFilePath;
			LevelExportTask->bSelected = false;
			LevelExportTask->bReplaceIdentical = true;
			LevelExportTask->bPrompt = false;
			LevelExportTask->bUseFileArchive = false;
			LevelExportTask->bWriteEmptyFiles = false;
			LevelExportTask->bAutomated = true; // Pretend this is an automated task so it doesn't pop the dialog

			UExporter::RunAssetExportTask( LevelExportTask );
		}
	}

	LevelSequenceExporterImpl::ExportMovieSceneSequence( Context, *LevelSequence, TargetFileName );

	// Set this back to Stopped or else it will keep the editor viewport controls permanently hidden
	TempSequencer->SetPlaybackStatus( EMovieScenePlayerStatus::Stopped );

	return true;
#else
	return false;
#endif // #if USE_USD_SDK
}
