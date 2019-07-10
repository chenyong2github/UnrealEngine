// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/Action/SDataprepActionBlock.h"

#include "CoreMinimal.h"
#include "UObject/GCObject.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class UDataprepFilter;

struct FDataprepSchemaActionContext;

class SDataprepFilter : public SDataprepActionBlock, public FGCObject
{
	SLATE_BEGIN_ARGS(SDataprepFilter) {}
	SLATE_END_ARGS();

	void Construct(const FArguments& InArgs, UDataprepFilter& InFilter, const TSharedRef<FDataprepSchemaActionContext>& InDataprepActionContext);

protected:

	// SDataprepActionBlock interface
	virtual FText GetBlockTitle() const override;
	virtual TSharedRef<SWidget> GetTitleWidget() const override;
	virtual TSharedRef<SWidget> GetContentWidget() const override;
	virtual void PopulateMenuBuilder(class FMenuBuilder& MenuBuilder) const override;
	//~ end of SDataprepActionBlock interface

private:

	void InverseFilter();

	//~ FGCObject interface
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;

	UDataprepFilter* Filter;
};
