// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SCompoundWidget.h"
#include "Misc/NotifyHook.h"

class UCSVtoSVGArugments;
class IDetailsView;
class SStatList;

class SCSVtoSVG : public SCompoundWidget, public FNotifyHook
{
	SLATE_BEGIN_ARGS(SCSVtoSVG)
	{
	}

	SLATE_END_ARGS()

	void Construct(const FArguments& Args);

	/** SCSVtoSVG destructor */
	~SCSVtoSVG();

private:

	FReply OnGenerateGraphClicked();

	void StatListSelectionChanged(const TArray<FString>& Stats);

	//~ Begin FNotifyHook Interface
	virtual void NotifyPostChange(const FPropertyChangedEvent& PropertyChangedEvent, FProperty* PropertyThatChanged) override;
	// End of FNotifyHook

	void LoadCSVFile(const FString& Filepath);

	/** This is the Detail View for the currently selected UActorRecording or an item from an Extender */
	TSharedPtr<IDetailsView> CSVtoSVGArgumentsDetailsView = nullptr;

	/** The list of stats parsed from the CSV file. */
	TSharedPtr<SStatList> StatListView = nullptr;

	/** The current arguments. */
	TStrongObjectPtr<UCSVtoSVGArugments> Arguments = nullptr;
};
