// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDSchemaTranslator.h"

#include "USDSchemasModule.h"
#include "USDTypesConversion.h"

#include "Algo/Find.h"
#include "Async/Async.h"
#include "Misc/ScopedSlowTask.h"
#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "USDSchemaTranslator"

int32 FRegisteredSchemaTranslatorHandle::CurrentSchemaTranslatorId = 0;

#if USE_USD_SDK

#include "USDIncludesStart.h"
	#include "pxr/base/tf/token.h"
#include "USDIncludesEnd.h"

TSharedPtr< FUsdSchemaTranslator > FUsdSchemaTranslatorRegistry::CreateTranslatorForSchema( TSharedRef< FUsdSchemaTranslationContext > InTranslationContext, const pxr::UsdTyped& InSchema )
{
	TUsdStore< pxr::UsdPrim > Prim = InSchema.GetPrim();
	if ( !Prim.Get() )
	{
		return {};
	}

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
	TRACE_CPUPROFILER_EVENT_SCOPE( FUsdSchemaTranslationContext::CompleteTasks );

	FScopedSlowTask SlowTask( TranslatorTasks.Num(), LOCTEXT( "TasksProgress", "Executing USD Schema tasks" ) );

	bool bFinished = ( TranslatorTasks.Num() == 0 );
	while ( !bFinished )
	{
		for ( TArray< TSharedPtr< FUsdSchemaTranslatorTaskChain > >::TIterator TaskChainIterator = TranslatorTasks.CreateIterator(); TaskChainIterator; ++TaskChainIterator )
		{
			if ( (*TaskChainIterator)->Execute() == ESchemaTranslationStatus::Done )
			{
 				SlowTask.EnterProgressFrame();
				TaskChainIterator.RemoveCurrent();
			}
		}

		bFinished = ( TranslatorTasks.Num() == 0 );
	}
}

bool FUsdSchemaTranslator::IsCollapsed( ECollapsingType CollapsingType ) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE( FUsdSchemaTranslator::IsCollapsed );

	if ( !CanBeCollapsed( CollapsingType ) )
	{
		return false;
	}

	TUsdStore< pxr::UsdPrim > Prim = Schema.Get().GetPrim();
	TUsdStore< pxr::UsdPrim > ParentPrim = Prim.Get().GetParent();

	IUsdSchemasModule& UsdSchemasModule = FModuleManager::Get().LoadModuleChecked< IUsdSchemasModule >( TEXT("USDSchemas") );

	while ( ParentPrim.Get() )
	{
		TSharedPtr< FUsdSchemaTranslator > ParentSchemaTranslator = UsdSchemasModule.GetTranslatorRegistry().CreateTranslatorForSchema( Context, pxr::UsdTyped( ParentPrim.Get() ) );

		if ( ParentSchemaTranslator && ParentSchemaTranslator->CollapsesChildren( CollapsingType ) )
		{
			return true;
		}
		else
		{
			ParentPrim = ParentPrim.Get().GetParent();
		}
	}

	return false;
}

void FSchemaTranslatorTask::Start()
{
	if ( bAsync && IsInGameThread() )
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
	if ( !CurrentTask )
	{
		CurrentTask = MakeShared< FSchemaTranslatorTask >( bAsync, Callable );

		CurrentTask->StartIfAsync(); // Queue it right now if async
	}
	else
	{
		Then( bAsync, Callable );
	}

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
			if ( IsInGameThread() )
			{
				CurrentTask->StartIfAsync(); // Queue the next task asap if async
			}
			else
			{
				CurrentTask->Start();
			}
		}
	}

	return CurrentTask ? ESchemaTranslationStatus::InProgress : ESchemaTranslationStatus::Done;
}

#endif //#if USE_USD_SDK

#undef LOCTEXT_NAMESPACE
