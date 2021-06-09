// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IGameplayInsightsDebugView.h"
#include "IGameplayInsightsDebugViewCreator.h"
#include "SVariantValueView.h"

namespace TraceServices { class IAnalysisSession; }

class SPropertiesDebugViewBase : public IGameplayInsightsDebugView
{
	SLATE_BEGIN_ARGS(SPropertiesDebugViewBase) {}
	SLATE_END_ARGS()
public:
	void Construct(const FArguments& InArgs, uint64 InObjectId, double InTimeMarker, const TraceServices::IAnalysisSession& InAnalysisSession);

	virtual void SetTimeMarker(double InTimeMarker) override;
	virtual uint64 GetObjectId() const override { return ObjectId; }

	virtual void GetVariantsAtFrame(const TraceServices::FFrame& InFrame, TArray<TSharedRef<FVariantTreeNode>>& OutVariants) const = 0;

protected:
	TSharedPtr<SVariantValueView> View;

	uint64 ObjectId;
	double TimeMarker;
	const TraceServices::IAnalysisSession* AnalysisSession;
};