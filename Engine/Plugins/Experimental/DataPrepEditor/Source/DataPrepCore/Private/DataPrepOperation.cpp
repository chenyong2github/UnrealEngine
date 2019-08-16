// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DataPrepOperation.h"

#include "DataprepCoreLogCategory.h"
#include "DataprepOperationContext.h"
#include "IDataprepLogger.h"
#include "IDataprepProgressReporter.h"

#define LOCTEXT_NAMESPACE "DataprepOperation"

FText FDataprepOperationCategories::ActorOperation(LOCTEXT("DataprepOperation_ActorOperationName", "On Actor"));
FText FDataprepOperationCategories::MeshOperation( LOCTEXT("DataprepOperation_MeshOperationName", "On Mesh"));
FText FDataprepOperationCategories::ObjectOperation( LOCTEXT("DataprepOperation_ObjectOperationName", "On Object"));

void UDataprepOperation::Execute(const TArray<UObject *>& InObjects)
{
	FDataprepContext Context;
	Context.Objects = InObjects;
	OnExecution( Context );
}

void UDataprepOperation::OnExecution_Implementation(const FDataprepContext& InContext)
{
	// This function should never called on a UDataprepOperation since this class is a abstract base.
	LogError( LOCTEXT("OnExecutionNotOverrided","Please define an implementation to OnExecution for your operation.") );
}

void UDataprepOperation::LogInfo(const FText& InLogText)
{
	if ( OperationContext && OperationContext->DataprepLogger )
	{
		OperationContext->DataprepLogger->LogInfo( InLogText, *this );
	}
}

void UDataprepOperation::LogWarning(const FText& InLogText)
{
	if ( OperationContext && OperationContext->DataprepLogger )
	{
		OperationContext->DataprepLogger->LogWarning( InLogText, *this );
	}
}

void UDataprepOperation::LogError(const FText& InLogText)
{
	if ( OperationContext && OperationContext->DataprepLogger )
	{
		OperationContext->DataprepLogger->LogError( InLogText, *this );
	}
}

void UDataprepOperation::ExecuteOperation(TSharedRef<FDataprepOperationContext>&  InOperationContext)
{
	OperationContext = InOperationContext;
	if ( OperationContext->Context )
	{
		OnExecution( *OperationContext->Context );
	}
	else
	{
		ensureMsgf( false, TEXT("ExcuteOperation should never be called with an operation context without a context!") );
	}
}

FText UDataprepOperation::GetDisplayOperationName_Implementation() const
{
	return this->GetClass()->GetDisplayNameText();
}

FText UDataprepOperation::GetTooltip_Implementation() const
{
	return this->GetClass()->GetToolTipText();
}

FText UDataprepOperation::GetCategory_Implementation() const
{
	return FText::FromString( TEXT("Undefined Category") );
}

FText UDataprepOperation::GetAdditionalKeyword_Implementation() const
{
	return FText();
}

#undef LOCTEXT_NAMESPACE
