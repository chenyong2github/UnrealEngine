// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Trace/Analyzer.h"

class FAnimationProvider;
namespace Trace { class IAnalysisSession; }

class FAnimationAnalyzer : public Trace::IAnalyzer
{
public:
	FAnimationAnalyzer(Trace::IAnalysisSession& InSession, FAnimationProvider& InAnimationProvider);

	virtual void OnAnalysisBegin(const FOnAnalysisContext& Context) override;
	virtual void OnAnalysisEnd() override {}
	virtual bool OnEvent(uint16 RouteId, const FOnEventContext& Context) override;

private:
	enum : uint16
	{
		RouteId_TickRecord,
		RouteId_SkeletalMesh,
		RouteId_SkeletalMeshComponent,
		RouteId_Name,
	};

	Trace::IAnalysisSession& Session;
	FAnimationProvider& AnimationProvider;
};