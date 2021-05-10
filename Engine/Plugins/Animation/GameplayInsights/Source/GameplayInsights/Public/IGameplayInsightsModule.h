// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IAnimGraphSchematicView.h"

namespace GameplayInsightsTabs
{
	extern GAMEPLAYINSIGHTS_API const FName DocumentTab;

};

namespace TraceServices
{
	class IAnalysisSession;
}

class IGameplayInsightsModule
	: public IModuleInterface
{
public:
	/** Creates a widget for displaying the anim graph schematic */
	virtual TSharedRef<IAnimGraphSchematicView> CreateAnimGraphSchematicView(uint64 InAnimInstanceId, double InTimeMarker, const TraceServices::IAnalysisSession& InAnalysisSession) = 0;
};
