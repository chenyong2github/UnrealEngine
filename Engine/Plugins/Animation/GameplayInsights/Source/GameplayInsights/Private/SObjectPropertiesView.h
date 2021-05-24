// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IGameplayInsightsDebugView.h"
#include "SVariantValueView.h"

namespace TraceServices { class IAnalysisSession; }

class SObjectPropertiesView : public IGameplayInsightsDebugView
{
	SLATE_BEGIN_ARGS(SObjectPropertiesView) {}
	SLATE_END_ARGS()
public:
	void Construct(const FArguments& InArgs, uint64 InObjectId, double InTimeMarker, const TraceServices::IAnalysisSession& InAnalysisSession);

	virtual void SetTimeMarker(double InTimeMarker) override;
	virtual FText GetTitle() override;

	void GetVariantsAtFrame(const TraceServices::FFrame& InFrame, TArray<TSharedRef<FVariantTreeNode>>& OutVariants) const;
private:
	TSharedPtr<SVariantValueView> View;

	uint64 ObjectId;
	double TimeMarker;
	const TraceServices::IAnalysisSession* AnalysisSession;
};
