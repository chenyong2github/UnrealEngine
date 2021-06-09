// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IGameplayInsightsDebugView.h"
#include "IGameplayInsightsDebugViewCreator.h"
#include "SPropertiesDebugViewBase.h"

namespace TraceServices { class IAnalysisSession; }

class SNotifiesView : public SPropertiesDebugViewBase
{
public:
	virtual void GetVariantsAtFrame(const TraceServices::FFrame& InFrame, TArray<TSharedRef<FVariantTreeNode>>& OutVariants) const override;
	virtual FName GetName() const override;
};

class FNotifiesViewCreator : public ICreateGameplayInsightsDebugView
{
	public:
		virtual FName GetName() const override;
		virtual FText GetTitle() const override;
		virtual FSlateIcon GetIcon() const override;
		virtual TSharedPtr<IGameplayInsightsDebugView> CreateDebugView(uint64 ObjectId, double CurrentTime, const TraceServices::IAnalysisSession& InAnalysisSession) const override;
};