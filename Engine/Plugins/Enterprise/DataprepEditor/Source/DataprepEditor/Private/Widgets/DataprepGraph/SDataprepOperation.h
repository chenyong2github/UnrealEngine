// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/DataprepGraph/SDataprepActionBlock.h"

#include "CoreMinimal.h"
#include "UObject/GCObject.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class UDataprepOperation;
struct FDataprepSchemaActionContext;

class SDataprepOperation : public SDataprepActionBlock, public FGCObject
{
#ifndef NO_BLUEPRINT
	SLATE_BEGIN_ARGS(SDataprepOperation)
	: _IsSimplified(false)
	{}
		SLATE_ARGUMENT( bool, IsSimplified )
#else
	SLATE_BEGIN_ARGS(SDataprepOperation) {}
#endif
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UDataprepOperation* InOperation, const TSharedRef<FDataprepSchemaActionContext>& InDataprepActionContext);

protected:

	// SDataprepActionBlock interface
	virtual FSlateColor GetOutlineColor() const override;
	virtual FText GetBlockTitle() const override;
	virtual TSharedRef<SWidget> GetContentWidget() override;
	//~ end of SDataprepActionBlock interface

private:

	FText GetTooltipText() const;

	//~ FGCObject interface
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	 
	UDataprepOperation* Operation;
};