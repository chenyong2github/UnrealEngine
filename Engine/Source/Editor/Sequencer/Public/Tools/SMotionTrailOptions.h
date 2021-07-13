// Copyright Epic Games, Inc. All Rights Reserved.
/**
* Hold the View for the Motion Trail Options Widget
*/
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "IDetailsView.h"

class SEQUENCER_API SMotionTrailOptions : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SMotionTrailOptions) {}

	SLATE_END_ARGS()
	~SMotionTrailOptions()
	{
	}

	void Construct(const FArguments& InArgs);

	TSharedPtr<IDetailsView> DetailsView;
};

