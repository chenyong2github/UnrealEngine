// Copyright Epic Games, Inc. All Rights Reserved.


#include "LevelUtils.h"
#include "Engine/Engine.h"
#include "Engine/LevelStreaming.h"
#include "HAL/FileManager.h"
#include "UObject/Package.h"
#include "Misc/PackageName.h"
#include "EditorSupportDelegates.h"
#include "EngineGlobals.h"
#include "Misc/FeedbackContext.h"
#include "GameFramework/WorldSettings.h"
#include "Components/ModelComponent.h"

#if WITH_EDITOR
#include "ScopedTransaction.h"
#endif

#define LOCTEXT_NAMESPACE "LevelUtils"


#if WITH_EDITOR
// Structure to hold the state of the Level file on disk, the goal is to query it only one time per frame.
struct FLevelReadOnlyData
{
	FLevelReadOnlyData()
		:IsReadOnly(false)
		,LastUpdateTime(-1.0f)
	{}
	/** the current level file state */
	bool IsReadOnly;
	/** Last time when the level file state was update */
	float LastUpdateTime;
};
// Map to link the level data with a level
static TMap<ULevel*, FLevelReadOnlyData> LevelReadOnlyCache;

#endif

/////////////////////////////////////////////////////////////////////////////////////////
//
//	FindStreamingLevel methods.
//
/////////////////////////////////////////////////////////////////////////////////////////


#if WITH_EDITOR
bool FLevelUtils::bMovingLevel = false;
bool FLevelUtils::bApplyingLevelTransform = false;
#endif

/**
 * Returns the streaming level corresponding to the specified ULevel, or NULL if none exists.
 *
 * @param		Level		The level to query.
 * @return					The level's streaming level, or NULL if none exists.
 */
ULevelStreaming* FLevelUtils::FindStreamingLevel(const ULevel* Level)
{
	ULevelStreaming* MatchingLevel = NULL;

	if (Level && Level->OwningWorld)
	{
		for (ULevelStreaming* CurStreamingLevel : Level->OwningWorld->GetStreamingLevels())
		{
			if( CurStreamingLevel && CurStreamingLevel->GetLoadedLevel() == Level )
			{
				MatchingLevel = CurStreamingLevel;
				break;
			}
		}
	}

	return MatchingLevel;
}

/**
 * Returns the streaming level by package name, or NULL if none exists.
 *
 * @param		PackageName		Name of the package containing the ULevel to query
 * @return						The level's streaming level, or NULL if none exists.
 */
ULevelStreaming* FLevelUtils::FindStreamingLevel(UWorld* InWorld, const TCHAR* InPackageName)
{
	const FName PackageName( InPackageName );
	ULevelStreaming* MatchingLevel = NULL;
	if( InWorld)
	{
		for (ULevelStreaming* CurStreamingLevel : InWorld->GetStreamingLevels())
		{
			if( CurStreamingLevel && CurStreamingLevel->GetWorldAssetPackageFName() == PackageName )
			{
				MatchingLevel = CurStreamingLevel;
				break;
			}
		}
	}
	return MatchingLevel;
}

/////////////////////////////////////////////////////////////////////////////////////////
//
//	Level locking/unlocking.
//
/////////////////////////////////////////////////////////////////////////////////////////

/**
 * Returns true if the specified level is locked for edit, false otherwise.
 *
 * @param	Level		The level to query.
 * @return				true if the level is locked, false otherwise.
 */
#if WITH_EDITOR
bool FLevelUtils::IsLevelLocked(ULevel* Level)
{
	//We should not check file status on disk if we are not running the editor
	// Don't permit spawning in read only levels if they are locked
	if ( GIsEditor && !GIsEditorLoadingPackage )
	{
		if ( GEngine && GEngine->bLockReadOnlyLevels )
		{
			if (!LevelReadOnlyCache.Contains(Level))
			{
				LevelReadOnlyCache.Add(Level, FLevelReadOnlyData());
			}
			check(LevelReadOnlyCache.Contains(Level));
			FLevelReadOnlyData &LevelData = LevelReadOnlyCache[Level];
			//Make sure we test if the level file on disk is readonly only once a frame,
			//when the frame time get updated.
			if (LevelData.LastUpdateTime < Level->OwningWorld->GetRealTimeSeconds())
			{
				LevelData.LastUpdateTime = Level->OwningWorld->GetRealTimeSeconds();
				//If we dont find package we dont consider it as readonly
				LevelData.IsReadOnly = false;
				const UPackage* pPackage = Level->GetOutermost();
				if (pPackage)
				{
					FString PackageFileName;
					if (FPackageName::DoesPackageExist(pPackage->GetName(), NULL, &PackageFileName))
					{
						LevelData.IsReadOnly = IFileManager::Get().IsReadOnly(*PackageFileName);
					}
				}
			}

			if (LevelData.IsReadOnly)
			{
				return true;
			}
		}
	}

	// PIE levels and transient move levels are usually never locked.
	if ( Level->RootPackageHasAnyFlags(PKG_PlayInEditor) || Level->GetName() == TEXT("TransLevelMoveBuffer") )
	{
		return false;
	}

	ULevelStreaming* StreamingLevel = FindStreamingLevel( Level );
	if ( StreamingLevel != NULL )
	{
		return StreamingLevel->bLocked;
	}
	else
	{
		return Level->bLocked;
	}
}
bool FLevelUtils::IsLevelLocked( AActor* Actor )
{
	return Actor != NULL && !Actor->IsTemplate() && Actor->GetLevel() != NULL && IsLevelLocked(Actor->GetLevel());
}

/**
 * Sets a level's edit lock.
 *
 * @param	Level		The level to modify.
 */
void FLevelUtils::ToggleLevelLock(ULevel* Level)
{
	if ( !Level )
	{
		return;
	}

	ULevelStreaming* StreamingLevel = FindStreamingLevel( Level );
	if ( StreamingLevel != NULL )
	{
		// We need to set the RF_Transactional to make a streaming level serialize itself. so store the original ones, set the flag, and put the original flags back when done
		EObjectFlags cachedFlags = StreamingLevel->GetFlags();
		StreamingLevel->SetFlags( RF_Transactional );
		StreamingLevel->Modify();			
		StreamingLevel->SetFlags( cachedFlags );

		StreamingLevel->bLocked = !StreamingLevel->bLocked;
	}
	else
	{
		Level->Modify();
		Level->bLocked = !Level->bLocked;	
	}
}
#endif //#if WITH_EDITOR

/////////////////////////////////////////////////////////////////////////////////////////
//
//	Level loading/unloading.
//
/////////////////////////////////////////////////////////////////////////////////////////

/**
 * Returns true if the level is currently loaded in the editor, false otherwise.
 *
 * @param	Level		The level to query.
 * @return				true if the level is loaded, false otherwise.
 */
bool FLevelUtils::IsLevelLoaded(ULevel* Level)
{
	if ( Level && Level->IsPersistentLevel() )
	{
		// The persistent level is always loaded.
		return true;
	}

	ULevelStreaming* StreamingLevel = FindStreamingLevel( Level );
	return (StreamingLevel != nullptr);
}


/////////////////////////////////////////////////////////////////////////////////////////
//
//	Level visibility.
//
/////////////////////////////////////////////////////////////////////////////////////////

#if WITH_EDITORONLY_DATA
/**
 * Returns true if the specified level is visible in the editor, false otherwise.
 *
 * @param	StreamingLevel		The level to query.
 */
bool FLevelUtils::IsStreamingLevelVisibleInEditor(const ULevelStreaming* StreamingLevel)
{
	const bool bVisible = StreamingLevel && StreamingLevel->GetShouldBeVisibleInEditor();
	return bVisible;
}
#endif

/**
 * Returns true if the specified level is visible in the editor, false otherwise.
 *
 * @param	Level		The level to query.
 */
bool FLevelUtils::IsLevelVisible(const ULevel* Level)
{
	if (!Level)
	{
		return false;
	}

	// P-level is specially handled
	if ( Level->IsPersistentLevel() )
	{
#if WITH_EDITORONLY_DATA
		return !( Level->OwningWorld->PersistentLevel->GetWorldSettings()->bHiddenEdLevel );
#else
		return true;
#endif
	}

	static const FName NAME_TransLevelMoveBuffer(TEXT("TransLevelMoveBuffer"));
	if (Level->GetFName() == NAME_TransLevelMoveBuffer)
	{
		// The TransLevelMoveBuffer does not exist in the streaming list and is never visible
		return false;
	}

	return Level->bIsVisible;
}

#if WITH_EDITOR
/////////////////////////////////////////////////////////////////////////////////////////
//
//	Level editor transforms.
//
/////////////////////////////////////////////////////////////////////////////////////////

void FLevelUtils::SetEditorTransform(ULevelStreaming* StreamingLevel, const FTransform& Transform, bool bDoPostEditMove )
{
	check(StreamingLevel);

	// Check we are actually changing the value
	if(StreamingLevel->LevelTransform.Equals(Transform))
	{
		return;
	}

	// Setup an Undo transaction
	const FScopedTransaction LevelOffsetTransaction( LOCTEXT( "ChangeEditorLevelTransform", "Edit Level Transform" ) );
	StreamingLevel->Modify();

	// Ensure that all Actors are in the transaction so that their location is restored and any construction script behaviors 
	// based on being at a different location are correctly applied on undo/redo
	if (ULevel* LoadedLevel = StreamingLevel->GetLoadedLevel())
	{
		for (AActor* Actor : LoadedLevel->Actors)
		{
			if (Actor)
			{
				Actor->Modify();
			}
		}
	}

	// Apply new transform
	RemoveEditorTransform(StreamingLevel, false );
	StreamingLevel->LevelTransform = Transform;
	ApplyEditorTransform(StreamingLevel, bDoPostEditMove);

	// Redraw the viewports to see this change
	FEditorSupportDelegates::RedrawAllViewports.Broadcast();
}

void FLevelUtils::ApplyEditorTransform(const ULevelStreaming* StreamingLevel, bool bDoPostEditMove, AActor* Actor)
{
	check(StreamingLevel);
	if (ULevel* LoadedLevel = StreamingLevel->GetLoadedLevel())
	{	
		FApplyLevelTransformParams TransformParams(LoadedLevel, StreamingLevel->LevelTransform);
		TransformParams.Actor = Actor;
		TransformParams.bDoPostEditMove = bDoPostEditMove;
		ApplyLevelTransform(TransformParams);
	}
}

void FLevelUtils::RemoveEditorTransform(const ULevelStreaming* StreamingLevel, bool bDoPostEditMove, AActor* Actor)
{
	check(StreamingLevel);
	if (ULevel* LoadedLevel = StreamingLevel->GetLoadedLevel())
	{
		const FTransform InverseTransform = StreamingLevel->LevelTransform.Inverse();
		FApplyLevelTransformParams TransformParams(LoadedLevel, InverseTransform);
		TransformParams.Actor = Actor;
		TransformParams.bDoPostEditMove = bDoPostEditMove;
		ApplyLevelTransform(TransformParams);
	}
}

void FLevelUtils::ApplyPostEditMove( ULevel* Level )
{	
	check(Level)	
	GWarn->BeginSlowTask( LOCTEXT( "ApplyPostEditMove", "Updating all actors in level after move" ), true);

	const int32 NumActors = Level->Actors.Num();

	// Iterate over all actors in the level and transform them
	bMovingLevel = true;
	for( int32 ActorIndex=0; ActorIndex < NumActors; ++ActorIndex )
	{
		GWarn->UpdateProgress( ActorIndex, NumActors );
		AActor* Actor = Level->Actors[ActorIndex];
		if( Actor )
		{
			if (!Actor->GetWorld()->IsGameWorld() )
			{
				Actor->PostEditMove(true);				
			}
		}
	}
	bMovingLevel = false;
	GWarn->EndSlowTask();	
}


bool FLevelUtils::IsMovingLevel()
{
	return bMovingLevel;
}

bool FLevelUtils::IsApplyingLevelTransform()
{
	return bApplyingLevelTransform;
}

#endif // WITH_EDITOR

void FLevelUtils::ApplyLevelTransform(const FLevelUtils::FApplyLevelTransformParams& TransformParams)
{
	const bool bTransformActors =  !TransformParams.LevelTransform.Equals(FTransform::Identity);
	if (bTransformActors)
	{
#if WITH_EDITOR
		TGuardValue<bool> ApplyingLevelTransformGuard(bApplyingLevelTransform, true);
#endif
		// Apply the transform only to the specified actor
		if (TransformParams.Actor)
		{
			if (TransformParams.bSetRelativeTransformDirectly)
			{
				USceneComponent* RootComponent = TransformParams.Actor->GetRootComponent();
				// Don't want to transform children they should stay relative to their parents.
				if (RootComponent && RootComponent->GetAttachParent() == nullptr)
				{
					RootComponent->SetRelativeLocation_Direct(TransformParams.LevelTransform.TransformPosition(RootComponent->GetRelativeLocation()));
					RootComponent->SetRelativeRotation_Direct(TransformParams.LevelTransform.TransformRotation(RootComponent->GetRelativeRotation().Quaternion()).Rotator());
				}
			}
			else
			{
				USceneComponent* RootComponent = TransformParams.Actor->GetRootComponent();
				// Don't want to transform children they should stay relative to their parents.
				if (RootComponent && RootComponent->GetAttachParent() == nullptr)
				{
					RootComponent->SetRelativeLocationAndRotation(TransformParams.LevelTransform.TransformPosition(RootComponent->GetRelativeLocation()), TransformParams.LevelTransform.TransformRotation(RootComponent->GetRelativeRotation().Quaternion()));
				}
			}
#if WITH_EDITOR
			if (TransformParams.bDoPostEditMove && !TransformParams.Actor->GetWorld()->IsGameWorld())
			{
				bMovingLevel = true;
				TransformParams.Actor->PostEditMove(true);
				bMovingLevel = false;
			}
#endif
			return;
		}
		// Otherwise do the usual

		if (!TransformParams.LevelTransform.GetRotation().IsIdentity())
		{
			// If there is a rotation applied, then the relative precomputed bounds become invalid.
			TransformParams.Level->bTextureStreamingRotationChanged = true;
		}

		if (TransformParams.bSetRelativeTransformDirectly)
		{
			// Iterate over all model components to transform BSP geometry accordingly
			for (UModelComponent* ModelComponent : TransformParams.Level->ModelComponents)
			{
				if (ModelComponent)
				{
					ModelComponent->SetRelativeLocation_Direct(TransformParams.LevelTransform.TransformPosition(ModelComponent->GetRelativeLocation()));
					ModelComponent->SetRelativeRotation_Direct(TransformParams.LevelTransform.TransformRotation(ModelComponent->GetRelativeRotation().Quaternion()).Rotator());
				}
			}

			// Iterate over all actors in the level and transform them
			for (AActor* Actor : TransformParams.Level->Actors)
			{
				if (Actor)
				{
					USceneComponent* RootComponent = Actor->GetRootComponent();

					// Don't want to transform children they should stay relative to their parents.
					if (RootComponent && RootComponent->GetAttachParent() == nullptr)
					{
						RootComponent->SetRelativeLocation_Direct(TransformParams.LevelTransform.TransformPosition(RootComponent->GetRelativeLocation()));
						RootComponent->SetRelativeRotation_Direct(TransformParams.LevelTransform.TransformRotation(RootComponent->GetRelativeRotation().Quaternion()).Rotator());
					}
				}
			}
		}
		else
		{
			// Iterate over all model components to transform BSP geometry accordingly
			for (UModelComponent* ModelComponent : TransformParams.Level->ModelComponents)
			{
				if (ModelComponent)
				{
					ModelComponent->SetRelativeLocationAndRotation(TransformParams.LevelTransform.TransformPosition(ModelComponent->GetRelativeLocation()), TransformParams.LevelTransform.TransformRotation(ModelComponent->GetRelativeRotation().Quaternion()));
				}
			}

			// Iterate over all actors in the level and transform them
			for (AActor* Actor : TransformParams.Level->Actors)
			{
				if (Actor)
				{
					USceneComponent* RootComponent = Actor->GetRootComponent();

					// Don't want to transform children they should stay relative to their parents.
					if (RootComponent && RootComponent->GetAttachParent() == nullptr)
					{
						RootComponent->SetRelativeLocationAndRotation(TransformParams.LevelTransform.TransformPosition(RootComponent->GetRelativeLocation()), TransformParams.LevelTransform.TransformRotation(RootComponent->GetRelativeRotation().Quaternion()));
					}
				}
			}
		}

#if WITH_EDITOR
		if (TransformParams.bDoPostEditMove)
		{
			ApplyPostEditMove(TransformParams.Level);
		}
#endif // WITH_EDITOR

		TransformParams.Level->OnApplyLevelTransform.Broadcast(TransformParams.LevelTransform);
	}
}

#undef LOCTEXT_NAMESPACE
