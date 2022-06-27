// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ConcertMessageData.h"
#include "Session/History/SSessionHistory.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

namespace UE
{
	namespace ConcertSyncCore
	{
		struct FHistoryAnalysisResult;
	}
}

/** A view for displaying activity dependencies when deleting a dependency. */
class SActivityDependencyView : public SCompoundWidget
{
public:

	DECLARE_DELEGATE_RetVal_OneParam(TSharedRef<SSessionHistory>, FCreateSessionHistory, const SSessionHistory::FArguments& /*InArgs*/)
	DECLARE_DELEGATE_RetVal_OneParam(TSharedRef<SWidget>, FCreateActivityColumnWidget, FActivityID /*ActivityID*/);

    SLATE_BEGIN_ARGS(SActivityDependencyView)
    {}
		/** Required. A function that will create a session history widget. */
		SLATE_EVENT(FCreateSessionHistory, CreateSessionHistory)
		/** Optional. If specified, this will create a widget in the first column using this function. */
		SLATE_EVENT(FCreateActivityColumnWidget, CreateActivityColumnWidget)
		SLATE_ARGUMENT(FText, CustomActivityColumnLabel)
    SLATE_END_ARGS()
	
	void Construct(const FArguments& InArgs, const UE::ConcertSyncCore::FHistoryAnalysisResult& DeletionRequirements);
};
