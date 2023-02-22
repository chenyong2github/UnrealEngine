// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/Input/SButton.h"
#include "Widgets/SCompoundWidget.h"

struct FGraphActionListBuilderBase;

/**
 * Contents of the "Members" tab in the graph asset editor.
 */
class SMovieGraphMembersTabContent : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMovieGraphMembersTabContent) {}
	SLATE_END_ARGS();
	
	void Construct(const FArguments& InArgs);

private:
	/** The section identifier in the action widget. */
	enum class EActionSection : uint8
	{
		Invalid,
		
		Inputs,
		Outputs,
		Variables,

		COUNT
	};

	/** The names of the sections in the action widget. */
	static const TArray<FText> ActionMenuSectionNames;
	
	void CollectAllActions(FGraphActionListBuilderBase& OutAllActions);
	void CollectStaticSections(TArray<int32>& StaticSectionIDs);
	FText GetSectionTitle(int32 InSectionID);
	TSharedRef<SWidget> GetSectionWidget(TSharedRef<SWidget> RowWidget, int32 InSectionID);
};
