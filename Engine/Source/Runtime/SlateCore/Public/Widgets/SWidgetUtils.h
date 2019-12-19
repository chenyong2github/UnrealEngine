// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SWidget.h"

#if STATS

/** Structure used to track time spent by a SWidget */
struct FScopeCycleCounterSWidget : public FCycleCounter
{
	/**
	 * Constructor, starts timing
	 */
	FORCEINLINE FScopeCycleCounterSWidget(const SWidget* Widget)
	{
		if (Widget)
		{
			TStatId WidgetStatId = Widget->GetStatID();
			if (FThreadStats::IsCollectingData(WidgetStatId))
			{
				Start(WidgetStatId);
			}
		}
	}

	/**
	 * Updates the stat with the time spent
	 */
	FORCEINLINE ~FScopeCycleCounterSWidget()
	{
		Stop();
	}
};

#define SCOPE_CYCLE_SWIDGET(Name, Object) \
	FScopeCycleCounterSWidget ObjCycleCount_##Name(Object);

#elif ENABLE_STATNAMEDEVENTS

struct FScopeCycleCounterSWidget
{
	FScopeCycleCounter ScopeCycleCounter;
	FORCEINLINE FScopeCycleCounterSWidget(const SWidget* Widget)
		: ScopeCycleCounter(Widget ? Widget->GetStatID().StatString : nullptr)
	{
	}
};

#define SCOPE_CYCLE_SWIDGET(Name, Object) \
	FScopeCycleCounterSWidget ObjCycleCount_##Name(Object);

#else

#define SCOPE_CYCLE_SWIDGET(Name, Object)

#endif
