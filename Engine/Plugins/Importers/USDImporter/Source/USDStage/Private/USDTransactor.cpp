// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDTransactor.h"

#include "UnrealUSDWrapper.h"
#include "USDLog.h"
#include "USDStageActor.h"
#include "USDTypesConversion.h"
#include "USDValueConversion.h"

#include "UsdWrappers/SdfLayer.h"
#include "UsdWrappers/SdfPath.h"
#include "UsdWrappers/UsdAttribute.h"
#include "UsdWrappers/UsdPrim.h"
#include "UsdWrappers/UsdStage.h"

#if WITH_EDITOR
#include "Editor/TransBuffer.h"
#include "Editor/Transactor.h"
#include "Editor.h"
#endif // WITH_EDITOR

#define LOCTEXT_NAMESPACE "USDTransactor"

namespace UsdUtils
{
	/** Converts the received VtValue map to an analogue using converted UE types that can be serialized with the UUsdTransactor */
	UsdUtils::FConvertedFieldValueMap ConvertFieldValueMap( const UsdUtils::FUsdFieldValueMap& WrapperMap )
	{
		UsdUtils::FConvertedFieldValueMap Result;
		Result.Reserve( WrapperMap.Num() );

		for ( const TPair<FString, UE::FVtValue>& WrapperPair : WrapperMap )
		{
			UsdUtils::FConvertedVtValue ConvertedValue;
			if ( UsdToUnreal::ConvertValue( WrapperPair.Value, ConvertedValue ) )
			{
				Result.Add( WrapperPair.Key, ConvertedValue );
			}
		}

		return Result;
	}

	/** Applies the field value pairs to all prims on the stage, and returns a list of prim paths for modified prims */
	TArray<FString> ApplyFieldMapToStage( const UsdUtils::FConvertedFieldValueMap& Map, UE::FUsdStage& Stage, double Time )
	{
		TArray<FString> PrimsChanged;
#if USE_USD_SDK

		for ( const TPair< FString, UsdUtils::FConvertedVtValue >& Pair : Map )
		{
			const UsdUtils::FConvertedVtValue& Value = Pair.Value;

			FString FullAttributeString; // e.g. "/root/cube.my_property"
			FString FieldChangeType; // e.g. "default"
			Pair.Key.Split( TEXT( "." ), &FullAttributeString, &FieldChangeType, ESearchCase::IgnoreCase, ESearchDir::FromEnd );
			if ( FullAttributeString.IsEmpty() || FieldChangeType != TEXT( "default" ) )
			{
				continue;
			}

			UE::FSdfPath PrimPath;
			FString PropertyName;
			bool bIsStageMetadata;

			// For stage properties we send notices like "/.metersPerSecond.default", as there's no neat way of representing stage metadata with valid
			// USD paths. This means we need to clean that string before we try making a pxr::SdfPath with it or else it will fail
			if ( FullAttributeString.RemoveFromStart( TEXT( "/." ) ) )
			{
				PrimPath = UE::FSdfPath::AbsoluteRootPath();
				PropertyName = FullAttributeString; // e.g. "metersPerSecond"
				bIsStageMetadata = true;
			}
			else
			{
				UE::FSdfPath UsdFullAttributeString{ *FullAttributeString };

				// Let USD split the prim path and property parts
				PrimPath = UsdFullAttributeString.GetAbsoluteRootOrPrimPath(); // e.g. "/root/cube"
				PropertyName = UsdFullAttributeString.GetName(); // e.g. "my_property"
				bIsStageMetadata = false;
			}
			if ( PropertyName.IsEmpty() )
			{
				continue;
			}

			UE::FUsdPrim Prim = Stage.GetPrimAtPath( PrimPath );
			if ( !Prim )
			{
				continue;
			}

			UE::FVtValue WrapperValue;
			if ( !UnrealToUsd::ConvertValue( Value, WrapperValue ) )
			{
				UE_LOG( LogUsd, Warning, TEXT( "Failed to convert VtValue back to USD when applying it to field '%s'" ), *FullAttributeString );
				continue;
			}

			if ( bIsStageMetadata )
			{
				UE::FSdfLayer OldEditTarget = Stage.GetEditTarget();
				Stage.SetEditTarget( Stage.GetRootLayer() );
				{
					PrimsChanged.Add( TEXT( "/" ) );

					// If we're trying to set an empty value, just clear the authored value instead so that the fallback can be shown.
					// The "oldValue" for a field change emitted by the original USD notice can be empty: This happens when we're
					// first authoring the value for an attribute that was previously displaying a fallback value, so clearing will
					// revert back to displaying the fallback value
					if ( WrapperValue.IsEmpty() )
					{
						Stage.ClearMetadata( *PropertyName );
					}
					else
					{
						Stage.SetMetadata( *PropertyName, WrapperValue );
					}
				}
				Stage.SetEditTarget( OldEditTarget );
			}
			else if ( UE::FUsdAttribute Attribute = Prim.GetAttribute( *PropertyName ) )
			{
				TOptional<double> TimeOption = Attribute.ValueMightBeTimeVarying() ? Time : TOptional<double>{};
				PrimsChanged.Add( PrimPath.GetString() );

				if ( WrapperValue.IsEmpty() )
				{
					if ( TimeOption.IsSet() )
					{
						Attribute.ClearAtTime( TimeOption.GetValue() );
					}
					else
					{
						Attribute.Clear();
					}
				}
				else
				{
					Attribute.Set( WrapperValue, TimeOption );
				}
			}
			// Kind is not an attribute, but metadata
			else if ( PropertyName == TEXT( "kind" ) )
			{
				PrimsChanged.Add( PrimPath.GetString() );

				if ( Value.Entries.Num() == 1 && Value.Entries[ 0 ].Num() == 1 )
				{
					if ( const FString* KindString = Value.Entries[ 0 ][ 0 ].TryGet<FString>() )
					{
						if ( !IUsdPrim::SetKind( Prim, UnrealToUsd::ConvertToken( **KindString ).Get() ) )
						{
							UE_LOG( LogUsd, Warning, TEXT( "Failed to set Kind '%s' for prim '%s'" ), **KindString, *PrimPath.GetString() );
						}
					}
				}
				else
				{
					if ( !IUsdPrim::ClearKind( Prim ) )
					{
						UE_LOG( LogUsd, Warning, TEXT( "Failed to clear Kind for prim '%s'" ), *PrimPath.GetString() );
					}
				}
			}
			else
			{
				UE_LOG( LogUsd, Warning, TEXT( "Failed to find USD attribute '%s' on prim '%s'" ), *PropertyName, *PrimPath.GetString() );
				continue;
			}
		}

#endif // USE_USD_SDK
		return PrimsChanged;
	}

	// Helps us know when we should respond to undo/redo. We need this because we respond to undo from PreEditUndo,
	// and respond to redo from PostEditUndo. This is because:
	// - In PreEditUndo we still have OldValues of the current transaction, and we want to apply those OldValues to the stage;
	// - In PostEditUndo we have the NewValues of the next transaction, and we want to apply those NewValues to the stage;
	// - ConcertSync always applies changes and then calls PostEditUndo, and we want to apply those received NewValues to the stage.
	class FUsdTransactorImpl
	{
	public:
		FUsdTransactorImpl();
		~FUsdTransactorImpl();

		// Returns whether the transaction buffer is currently in the middle of an Undo operation.
		// WARNING: This approach is only accurate if we're checking from within PreEditUndo/PostEditUndo/PostTransacted/Serialize (which we do in this file)
		bool IsTransactionUndoing() const;

		// Returns whether the transaction buffer is currently in the middle of a Redo operation. Returns false when we're applying
		// a ConcertSync transaction, even though concert sync sort of works by applying transactions via Redo
		// WARNING: This approach is only accurate if we're checking from within PreEditUndo/PostEditUndo/PostTransacted/Serialize (which we do in this file)
		bool IsTransactionRedoing() const;

		// Whether ConcertSync (multi-user) is currently applying a transaction received from the network
		bool IsApplyingConcertSyncTransaction() const { return bApplyingConcertSync; };

	private:
		// This is called after *any* undo/redo transaction is finalized, so our LastFinalizedUndoCount is kept updated
		void HandleTransactionStateChanged( const FTransactionContext& InTransactionContext, const ETransactionStateEventType InTransactionState );
		int32 LastFinalizedUndoCount = 0;

		// We use this to detect when a ConcertSync transaction is starting, as it has a particular title
		void HandleBeforeOnRedoUndo( const FTransactionContext& TransactionContext );

		// We use this to detect when a ConcertSync transaction has ended, as it has a particular title
		void HandleOnRedo( const FTransactionContext& TransactionContext, bool bSucceeded );

	public:
		// We use these to stash our OldValues/NewValues before they're overwritten by ConcertSync, and to restore them afterwards.
		// This is because when we receive a ConcertSync transaction the UUsdTransactor's New/OldValues will be overwritten with the received data.
		// That is fine until we apply it to the stage, but after that we want to discard those values altogether, so that if *we*
		// undo, we won't undo the received transaction, but instead undo the last transaction that *we* made.
		FConvertedFieldValueMap StoredOldValues;
		FConvertedFieldValueMap StoredNewValues;

		// When ClientA undoes a change, it handles it's own undo changes from its PreEditUndo, but its final state after the undo transaction
		// is complete will have the *previous* OldValues/NewValues. This final state is what is sent over the network. ClientB that receives this
		// won't be able to use these previous OldValues/NewValues to undo the change that ClientA just undone: It needs something else, which this
		// member provides. When ClientA starts to undo, it will stash it's *current* OldValues in here, and make sure they are visible when serialized
		// by ConcertSync. ClientB will receive these, and when available will apply those to the scene instead, applying the same undo change.
		TOptional< FConvertedFieldValueMap > OldValuesBeforeUndo;

	private:
		bool bApplyingConcertSync = false;
	};

	FUsdTransactorImpl::FUsdTransactorImpl()
	{
#if WITH_EDITOR
		if ( GEditor )
		{
			if ( UTransBuffer* Transactor = Cast<UTransBuffer>( GEditor->Trans ) )
			{
				Transactor->OnTransactionStateChanged().AddRaw( this, &FUsdTransactorImpl::HandleTransactionStateChanged );
				Transactor->OnBeforeRedoUndo().AddRaw( this, &FUsdTransactorImpl::HandleBeforeOnRedoUndo );
				Transactor->OnRedo().AddRaw( this, &FUsdTransactorImpl::HandleOnRedo );
			}
		}
#endif // WITH_EDITOR
	}

	FUsdTransactorImpl::~FUsdTransactorImpl()
	{
#if WITH_EDITOR
		if ( GEditor )
		{
			if ( UTransBuffer* Transactor = Cast<UTransBuffer>( GEditor->Trans ) )
			{
				Transactor->OnTransactionStateChanged().RemoveAll( this );
				Transactor->OnBeforeRedoUndo().RemoveAll( this );
				Transactor->OnRedo().RemoveAll( this );
			}
		}
#endif // WITH_EDITOR
	}

	bool FUsdTransactorImpl::IsTransactionUndoing() const
	{
#if WITH_EDITOR
		if ( UTransBuffer* Transactor = Cast<UTransBuffer>( GEditor->Trans ) )
		{
			// We moved away from the end of the transaction buffer -> We're undoing
			if ( GIsTransacting && Transactor->UndoCount > LastFinalizedUndoCount )
			{
				return true;
			}
		}
#endif // WITH_EDITOR

		return false;
	}

	bool FUsdTransactorImpl::IsTransactionRedoing() const
	{
#if WITH_EDITOR
		if ( UTransBuffer* Transactor = Cast<UTransBuffer>( GEditor->Trans ) )
		{
			// We moved towards the end of the transaction buffer -> We're redoing
			if ( GIsTransacting && Transactor->UndoCount < LastFinalizedUndoCount )
			{
				return true;
			}
		}
#endif // WITH_EDITOR

		return false;
	}

	void FUsdTransactorImpl::HandleTransactionStateChanged( const FTransactionContext& InTransactionContext, const ETransactionStateEventType InTransactionState )
	{
#if WITH_EDITOR
		if ( InTransactionState == ETransactionStateEventType::UndoRedoFinalized )
		{
			if ( UTransBuffer* Transactor = Cast<UTransBuffer>( GEditor->Trans ) )
			{
				// Recording UndoCount works because UTransBuffer::Undo preemptively updates it *before* calling any object function, like PreEditUndo/PostEditUndo, so from
				// within PreEditUndo/PostEditUndo we will always have a delta from this value to the value that is recorded after any transaction was finalized,
				// which we record right here
				LastFinalizedUndoCount = Transactor->UndoCount;
			}
		}
#endif // WITH_EDITOR
	}

	const FText ConcertSyncTransactionTitle = LOCTEXT( "ConcertTransactionEvent", "Concert Transaction Event" );

	void FUsdTransactorImpl::HandleBeforeOnRedoUndo( const FTransactionContext& TransactionContext )
	{
		if ( TransactionContext.Title.EqualTo( ConcertSyncTransactionTitle ) )
		{
			bApplyingConcertSync = true;
		}
	}

	void FUsdTransactorImpl::HandleOnRedo( const FTransactionContext& TransactionContext, bool bSucceeded )
	{
		if ( bApplyingConcertSync && TransactionContext.Title.EqualTo( ConcertSyncTransactionTitle ) )
		{
			bApplyingConcertSync = false;
		}
	}

}

// Boilerplate to support Pimpl in an UObject
UUsdTransactor::UUsdTransactor( FVTableHelper& Helper )
	: Super( Helper )
{
}
UUsdTransactor::UUsdTransactor() = default;
UUsdTransactor::~UUsdTransactor() = default;

void UUsdTransactor::Initialize( AUsdStageActor* InStageActor )
{
	StageActor = InStageActor;

#if USE_USD_SDK
	if ( !IsTemplate() )
	{
		Impl = MakeUnique<UsdUtils::FUsdTransactorImpl>();
	}
#endif // USE_USD_SDK
}

void UUsdTransactor::Update( const UsdUtils::FUsdFieldValueMap& InOldValues, const UsdUtils::FUsdFieldValueMap& InNewValues )
{
	// We always send notices even when we're undoing/redoing changes (so that multi-user can broadcast them).
	// Make sure that we only ever update our OldValues/NewValues when we receive *new* updates though
	if ( Impl.IsValid() && ( Impl->IsTransactionUndoing() || Impl->IsTransactionRedoing() || Impl->IsApplyingConcertSyncTransaction() ) )
	{
		return;
	}

	Modify();

	OldValues = UsdUtils::ConvertFieldValueMap( InOldValues );
	NewValues = UsdUtils::ConvertFieldValueMap( InNewValues );
}

void UUsdTransactor::Serialize( FArchive& Ar )
{
	Super::Serialize( Ar );

    Ar << OldValues;
    Ar << NewValues;

	// This allows us to keep our OldValuesBeforeUndo (if we have them) through the Undo operation, where the transaction system
	// will revert all of our data with the pre-transaction serialized data for the actual undo.
	// We need this data to be with us whenever the ConcertSync serializes us to send it over the network during an undo, which happens shortly after this
	if ( !Impl.IsValid() || ( Ar.IsTransacting() && Ar.IsLoading() && Impl.IsValid() && Impl->IsTransactionUndoing() && Impl->OldValuesBeforeUndo.IsSet() ) )
	{
		UsdUtils::FConvertedFieldValueMap Dummy;
		Ar << Dummy;
	}
	else
	{
		Ar << Impl->OldValuesBeforeUndo;
	}
}

#if WITH_EDITOR
void UUsdTransactor::PreEditUndo()
{
	if ( Impl.IsValid() )
	{
		AUsdStageActor* StageActorPtr = StageActor.Get();
		const bool bIsUndoing = Impl->IsTransactionUndoing();

		if( StageActorPtr && bIsUndoing )
		{
			// We can't respond to notices from the attribute that we'll set. Whatever changes setting the attribute causes in UE actors/components/assets
			// will already be accounted for by those actors/components/assets undoing/redoing by themselves via the UE transaction buffer.
			FScopedBlockNotices BlockNotices( StageActorPtr->GetUsdListener() );

			UE::FUsdStage& Stage = StageActorPtr->GetUsdStage();

			TArray<FString> PrimsChanged = UsdUtils::ApplyFieldMapToStage( OldValues, Stage, StageActorPtr->GetTime() );

			if ( PrimsChanged.Num() > 0 )
			{
				for ( const FString& Prim : PrimsChanged )
				{
					StageActorPtr->OnPrimChanged.Broadcast( Prim, false );
				}
			}
		}

		if ( bIsUndoing )
		{
			// Make sure our OldValues survive the undo in case we need to send them over ConcertSync once the transaction is complete
			Impl->OldValuesBeforeUndo = OldValues;
		}
		else
		{
			Impl->OldValuesBeforeUndo.Reset();

			// ConcertSync calls PreEditUndo, then updates our data with the received data, then calls PostEditUndo
			if ( Impl->IsApplyingConcertSyncTransaction() )
			{
				// Make sure that our own Old/NewValues survive when overwritten by values that we will receive from ConcertSync.
				// We'll restore this to our values once the ConcertSync action has finished applying
				Impl->StoredOldValues = OldValues;
				Impl->StoredNewValues = NewValues;
			}
		}
	}

	Super::PreEditUndo();
}

void UUsdTransactor::PostEditUndo()
{
	if ( Impl.IsValid() )
	{
		AUsdStageActor* StageActorPtr = StageActor.Get();
		const bool bIsRedoing = Impl->IsTransactionRedoing();
		const bool bIsApplyingConcertSync = Impl->IsApplyingConcertSyncTransaction();

		if ( StageActorPtr && ( bIsRedoing || bIsApplyingConcertSync ) )
		{
			// If we're just redoing it's a bit of a waste to let the AUsdStageActor respond to notices from the fields that we'll set,
			// because any relevant changes caused to the level/assets would be redone by themselves if the actors/assets are also in the transaction
			// buffer. If we're receiving a ConcertSync transaction, however, we do want to respond to notices because transient actors/assets
			// aren't tracked by ConcertSync
			TOptional<FScopedBlockNotices> BlockNotices;
			if ( bIsRedoing )
			{
				BlockNotices.Emplace( StageActorPtr->GetUsdListener() );
			}

			UE::FUsdStage& Stage = StageActorPtr->GetUsdStage();

			// If we're applying a received ConcertSync transaction that actually is an undo on the source client then we want to use it's UndoneOldValues
			// to replicate the same undo that they did. Otherwise this is a redo operation or any other type of ConcertSync transaction, so we want to use the NewValues
			TArray<FString> PrimsChanged = UsdUtils::ApplyFieldMapToStage(
				( bIsApplyingConcertSync && Impl->OldValuesBeforeUndo.IsSet() ) ? Impl->OldValuesBeforeUndo.GetValue() : NewValues,
				Stage,
				StageActorPtr->GetTime()
			);

			// Already applied the ConcertSync undo values, we can get rid of these now
			Impl->OldValuesBeforeUndo.Reset();

			if ( PrimsChanged.Num() > 0 )
			{
				for ( const FString& Prim : PrimsChanged )
				{
					StageActorPtr->OnPrimChanged.Broadcast( Prim, false );
				}
			}
		}

		if ( bIsApplyingConcertSync )
		{
			// If we're finishing applying a ConcertSync transaction, revert our Old/NewValues to the state that they were before we received
			// the ConcertSync transaction. This is important so that if we undo now, we undo the last change that *we* made
			OldValues = Impl->StoredOldValues;
			NewValues = Impl->StoredNewValues;
		}
	}

	Super::PostEditUndo();
}
#endif // WITH_EDITOR

#undef LOCTEXT_NAMESPACE
