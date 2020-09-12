// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SDMXEntityInspector.h"

#include "CoreMinimal.h"


/** DMX fixture confiturations inspector */
class SDMXFixturePatchInspector
	: public SDMXEntityInspector
{
public:
	SLATE_BEGIN_ARGS(SDMXFixturePatchInspector)
	{}

	SLATE_ARGUMENT(TWeakPtr<FDMXEditor>, DMXEditor)

		SLATE_EVENT(FOnFinishedChangingProperties::FDelegate, OnFinishedChangingProperties)

	SLATE_END_ARGS()

	/** Constructs the widget */
	void Construct(const FArguments& InArgs);

private:
	void OnFixturePatchesSelected();
};
