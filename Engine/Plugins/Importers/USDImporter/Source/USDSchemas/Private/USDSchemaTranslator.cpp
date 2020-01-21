// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDSchemaTranslator.h"

#include "USDTypesConversion.h"

#include "Algo/Find.h"
#include "Async/Async.h"

int32 FRegisteredSchemaTranslatorHandle::CurrentSchemaTranslatorId = 0;

#if USE_USD_SDK

#include "USDIncludesStart.h"
	#include "pxr/base/tf/token.h"
#include "USDIncludesEnd.h"

TSharedPtr< FUsdSchemaTranslator > FUsdSchemaTranslatorRegistry::CreateTranslatorForSchema( TSharedRef< FUsdSchemaTranslationContext > InTranslationContext, const pxr::UsdTyped& InSchema )
{
	TUsdStore< pxr::UsdPrim > Prim = InSchema.GetPrim();

	for ( TPair< FString, FSchemaTranslatorsStack >& RegisteredSchemasStack : RegisteredSchemaTranslators )
	{
		pxr::TfToken RegisteredSchemaToken( UnrealToUsd::ConvertString( *RegisteredSchemasStack.Key ).Get() );
		pxr::TfType RegisteredSchemaType = pxr::UsdSchemaRegistry::GetTypeFromName( RegisteredSchemaToken );

		if ( Prim.Get().IsA( RegisteredSchemaType ) && RegisteredSchemasStack.Value.Num() > 0 )
		{
			return RegisteredSchemasStack.Value.Top().CreateFunction( InTranslationContext, InSchema );
		}
	}

	return {};
}

FRegisteredSchemaTranslatorHandle FUsdSchemaTranslatorRegistry::Register( const FString& SchemaName, FCreateTranslator CreateFunction )
{
	FSchemaTranslatorsStack* SchemaTranslatorsStack = FindSchemaTranslatorStack( SchemaName );

	if ( !SchemaTranslatorsStack )
	{
		// Insert most specialized first
		int32 SchemaRegistryIndex = 0;

		pxr::TfToken SchemaToRegisterToken( UnrealToUsd::ConvertString( *SchemaName ).Get() );
		pxr::TfType SchemaToRegisterType = pxr::UsdSchemaRegistry::GetTypeFromName( SchemaToRegisterToken );

		for ( TPair< FString, FSchemaTranslatorsStack >& RegisteredSchemasStack : RegisteredSchemaTranslators )
		{
			pxr::TfToken RegisteredSchemaToken( UnrealToUsd::ConvertString( *RegisteredSchemasStack.Key ).Get() );
			pxr::TfType RegisteredSchemaType = pxr::UsdSchemaRegistry::GetTypeFromName( RegisteredSchemaToken );

			if ( SchemaToRegisterType.IsA( RegisteredSchemaType ) )
			{
				// We need to be registered before our ancestor types
				break;
			}
			else
			{
				++SchemaRegistryIndex;
			}
		}

		SchemaTranslatorsStack = &RegisteredSchemaTranslators.EmplaceAt_GetRef( SchemaRegistryIndex, SchemaName, FSchemaTranslatorsStack() ).Value;
	}

	FRegisteredSchemaTranslator RegisteredSchemaTranslator;
	RegisteredSchemaTranslator.Handle = FRegisteredSchemaTranslatorHandle( SchemaName );
	RegisteredSchemaTranslator.CreateFunction = CreateFunction;

	SchemaTranslatorsStack->Push( RegisteredSchemaTranslator );

	return RegisteredSchemaTranslator.Handle;
}

void FUsdSchemaTranslatorRegistry::Unregister( const FRegisteredSchemaTranslatorHandle& TranslatorHandle )
{
	FSchemaTranslatorsStack* SchemaTranslatorsStack = FindSchemaTranslatorStack( TranslatorHandle.GetSchemaName() );

	if ( !SchemaTranslatorsStack )
	{
		return;
	}

	for ( FSchemaTranslatorsStack::TIterator RegisteredSchemaTranslatorIt = SchemaTranslatorsStack->CreateIterator();
		RegisteredSchemaTranslatorIt; ++RegisteredSchemaTranslatorIt )
	{
		if ( RegisteredSchemaTranslatorIt->Handle.GetId() == TranslatorHandle.GetId() )
		{
			RegisteredSchemaTranslatorIt.RemoveCurrent();
			break;
		}
	}
}

FUsdSchemaTranslatorRegistry::FSchemaTranslatorsStack* FUsdSchemaTranslatorRegistry::FindSchemaTranslatorStack( const FString& SchemaName )
{
	TPair< FString, FSchemaTranslatorsStack >* Result = Algo::FindByPredicate( RegisteredSchemaTranslators,
		[ &SchemaName ]( const TPair< FString, FSchemaTranslatorsStack >& Element ) -> bool
		{
			return Element.Key == SchemaName;
		} );

	if ( Result )
	{
		return &Result->Value;
	}
	else
	{
		return nullptr;
	}
}

void FUsdSchemaTranslationContext::CompleteTasks()
{
	bool bFinished = false;
	while ( !bFinished )
	{
		for ( TArray< TSharedPtr< FUsdSchemaTranslatorTaskChain > >::TIterator TaskChainIterator = TranslatorTasks.CreateIterator(); TaskChainIterator; ++TaskChainIterator )
		{
			if ( (*TaskChainIterator)->Execute() == ESchemaTranslationStatus::Done )
			{
				TaskChainIterator.RemoveCurrent();
			}
		}

		bFinished = ( TranslatorTasks.Num() == 0 );
	}
}

void FSchemaTranslatorTask::Start()
{
	if ( bAsync )
	{
		Result = Async( EAsyncExecution::LargeThreadPool,
			[ this ]() -> bool
			{
				return DoWork();
			} );
	}
	else
	{
		// Execute on this thread
		if ( !DoWork() )
		{
			Continuation.Reset();
		}
	}
}

void FSchemaTranslatorTask::StartIfAsync()
{
	if ( bAsync )
	{
		Start();
	}
}

bool FSchemaTranslatorTask::DoWork()
{
	ensure( bIsDone == false );
	bool bContinue = Callable();
	bIsDone = true;

	return bContinue;
}

FUsdSchemaTranslatorTaskChain& FUsdSchemaTranslatorTaskChain::Do( bool bAsync, TFunction< bool() > Callable )
{
	CurrentTask = MakeShared< FSchemaTranslatorTask >( bAsync, Callable );

	CurrentTask->StartIfAsync(); // Queue it right now if async

	return *this;
}

FUsdSchemaTranslatorTaskChain& FUsdSchemaTranslatorTaskChain::Then( bool bAsync, TFunction< bool() > Callable )
{
	TSharedPtr< FSchemaTranslatorTask > LastTask = CurrentTask;

	while ( LastTask->Continuation.IsValid() )
	{
		LastTask = LastTask->Continuation;
	}

	if ( LastTask )
	{
		LastTask->Continuation = MakeShared< FSchemaTranslatorTask >( bAsync, Callable );
	}

	return *this;
}

ESchemaTranslationStatus FUsdSchemaTranslatorTaskChain::Execute()
{
	if ( !CurrentTask )
	{
		return ESchemaTranslationStatus::Done;
	}

	FSchemaTranslatorTask& TranslatorTask = *CurrentTask;

	if ( !TranslatorTask.IsDone() )
	{
		if ( !TranslatorTask.IsStarted() )
		{
			TranslatorTask.Start();
		}
	}
	else
	{
		CurrentTask = CurrentTask->Continuation;

		if ( CurrentTask )
		{
			CurrentTask->StartIfAsync(); // Queue the next task asap if async
		}
	}

	return CurrentTask ? ESchemaTranslationStatus::InProgress : ESchemaTranslationStatus::Done;
}

#endif //#if USE_USD_SDK
