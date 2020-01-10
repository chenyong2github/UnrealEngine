// Copyright Epic Games, Inc. All Rights Reserved.

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
public:
	SLATE_BEGIN_ARGS(SDataprepFilter) {}
	SLATE_END_ARGS();

	void Construct(const FArguments& InArgs, UDataprepFilter& InFilter, const TSharedRef<FDataprepSchemaActionContext>& InDataprepActionContext);

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;	

protected:

	// SDataprepActionBlock interface
	virtual FText GetBlockTitle() const override;
	virtual TSharedRef<SWidget> GetTitleWidget() override;
	virtual TSharedRef<SWidget> GetContentWidget() override;
	virtual void PopulateMenuBuilder(class FMenuBuilder& MenuBuilder) override;
	//~ end of SDataprepActionBlock interface

private:

	void InverseFilter();

	FText GetTooltipText() const;

	//~ FGCObject interface
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;

	TSharedPtr<class SDataprepDetailsView> DetailsView;

	UDataprepFilter* Filter = nullptr;
};
