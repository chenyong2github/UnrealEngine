// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SchemaActions/IDataprepMenuActionCollector.h"
#include "SelectionSystem/DataprepFilter.h"

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtrTemplates.h"

struct FDataprepSchemaAction;

/**
 * Help collecting the dataprep fetcher menu action for a specified filter
 * When the action is executed it will set the fetcher on the filter that was pass to this collector
 */
class FDataprepFetcherMenuActionCollector : public IDataprepMenuActionCollector
{
public:
	FDataprepFetcherMenuActionCollector(UDataprepFilter& Filter);

	virtual TArray<TSharedPtr<FDataprepSchemaAction>> CollectActions() override;
	virtual bool ShouldAutoExpand() override;

private:

	TSharedPtr<FDataprepSchemaAction> CreateMenuActionFromClass(UClass& Class);

	TWeakObjectPtr<UDataprepFilter> FilterPtr;
};
