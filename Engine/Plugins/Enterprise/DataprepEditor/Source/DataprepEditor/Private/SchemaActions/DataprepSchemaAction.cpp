// Copyright Epic Games, Inc. All Rights Reserved.

#include "SchemaActions/DataprepSchemaAction.h"

FDataprepSchemaAction::FDataprepSchemaAction(FText InActionCategory, FText InMenuDescription, FText InToolTip, const int32 InGrouping, FText InKeywords, const FOnExecuteAction& InAction, int32 InSectionID)
	: FEdGraphSchemaAction( MoveTemp(InActionCategory), MoveTemp(InMenuDescription), MoveTemp(InToolTip), InGrouping, MoveTemp(InKeywords), InSectionID )
	, Action( InAction )
{}

bool FDataprepSchemaAction::ExecuteAction(const FDataprepSchemaActionContext& Context)
{
	return Action.ExecuteIfBound( Context );
}
