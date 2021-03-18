// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SWidget.h"
#include "Widgets/SCompoundWidget.h"
#include "Animation/CurveSequence.h"

class IToolTip;

/**  */
class UNREALED_API SDDCStatusIndicator : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SDDCStatusIndicator) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	EActiveTimerReturnType UpdateBusyIndicator(double InCurrentTime, float InDeltaTime);
	double GetDDCTime(bool bGet) const;
	EActiveTimerReturnType UpdateWarnings(double InCurrentTime, float InDeltaTime);

	double LastDDCGetTime = 0;
	double LastDDCPutTime = 0;

	FCurveSequence BusyPulseSequence;
	FCurveSequence FadeGetSequence;
	FCurveSequence FadePutSequence;
};