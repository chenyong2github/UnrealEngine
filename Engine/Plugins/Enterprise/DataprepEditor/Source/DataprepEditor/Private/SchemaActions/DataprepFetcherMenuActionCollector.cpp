// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SchemaActions/DataprepFetcherMenuActionCollector.h"

#include "DataprepEditorUtils.h"
#include "DataprepMenuActionCollectorUtils.h"
#include "SelectionSystem/DataprepFetcher.h"

#include "ScopedTransaction.h"
#include "Templates/SubclassOf.h"
#include "UObject/Class.h"

FDataprepFetcherMenuActionCollector::FDataprepFetcherMenuActionCollector(UDataprepFilter& Filter)
{
	FilterPtr = &Filter;
}

TArray<TSharedPtr<FDataprepSchemaAction>> FDataprepFetcherMenuActionCollector::CollectActions()
{
	UDataprepFilter* Filter = FilterPtr.Get();
	if ( Filter )
	{
		UClass* FetcherClass = Filter->GetAcceptedFetcherClass().Get();
		if ( FetcherClass )
		{
			return DataprepMenuActionCollectorUtils::GatherMenuActionForDataprepClass( *FetcherClass
				, DataprepMenuActionCollectorUtils::FOnCreateMenuAction::CreateRaw( this, &FDataprepFetcherMenuActionCollector::CreateMenuActionFromClass ) );
		}
	}

	return {};
}

bool FDataprepFetcherMenuActionCollector::ShouldAutoExpand()
{
	return false;
}

TSharedPtr<FDataprepSchemaAction> FDataprepFetcherMenuActionCollector::CreateMenuActionFromClass(UClass& Class)
{
	UDataprepFilter* Filter = FilterPtr.Get();
	check( Filter && Class.IsChildOf( Filter->GetAcceptedFetcherClass() ) );

	FDataprepSchemaAction::FOnExecuteAction OnExcuteMenuAction;
	OnExcuteMenuAction.BindLambda( [ FilterPtr = FilterPtr, Class = &Class] (const FDataprepSchemaActionContext& InContext)
	{
		UDataprepFilter* Filter = FilterPtr.Get();
		if ( Filter && ( !Filter->GetFetcher() || Filter->GetFetcher()->GetClass() != Class ) )
		{
			Filter->SetFetcher( TSubclassOf< UDataprepFetcher >( Class ) );
			FDataprepEditorUtils::NotifySystemOfChangeInPipeline( Filter );
		}
	});

	UDataprepFetcher* Fetcher = Class.GetDefaultObject<UDataprepFetcher>();
	return MakeShared< FDataprepSchemaAction >( FText::FromString( TEXT("") )
		, Fetcher->GetDisplayFetcherName(), Fetcher->GetTooltipText()
		, 0, Fetcher->GetAdditionalKeyword(), OnExcuteMenuAction
		);
}
