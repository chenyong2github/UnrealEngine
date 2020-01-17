// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/Action/SDataprepActionBlock.h"

#include "CoreMinimal.h"
#include "UObject/GCObject.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class UDataprepOperation;
struct FDataprepSchemaActionContext;

class SDataprepOperation : public SDataprepActionBlock, public FGCObject
{
	SLATE_BEGIN_ARGS(SDataprepOperation) {}
	SLATE_END_ARGS();

	void Construct(const FArguments& InArgs, UDataprepOperation* InOperation, const TSharedRef<FDataprepSchemaActionContext>& InDataprepActionContext);

protected:

	virtual FText GetBlockTitle() const override;
	virtual TSharedRef<SWidget> GetContentWidget() override;

private:

	FText GetTooltipText() const;

	//~ FGCObject interface
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	 
	UDataprepOperation* Operation;
};