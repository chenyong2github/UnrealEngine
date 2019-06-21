// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SDataprepOperation.h"

#include "DataPrepOperation.h"
#include "Widgets/DataprepWidgets.h"
#include "Widgets/SNullWidget.h"

#include "IDetailsView.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"

void SDataprepOperation::Construct(const FArguments& InArgs, UDataprepOperation* InOperation, const TSharedRef<FDataprepSchemaActionContext>& InDataprepActionContext)
{
	Operation = InOperation;
	SDataprepActionBlock::Construct( SDataprepActionBlock::FArguments(), InDataprepActionContext );
}

FText SDataprepOperation::GetBlockTitle() const
{
	return Operation ? Operation->GetDisplayOperationName() : FText::FromString( TEXT("Operation is Nullptr!") ) ;
}

TSharedRef<SWidget> SDataprepOperation::GetContentWidget() const
{
	return SNew( SDataprepDetailsView ).Object( Operation ).Class( UDataprepOperation::StaticClass() );
}

void SDataprepOperation::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject( Operation );
}
