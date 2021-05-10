// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "BindableProperty.h"
#include "CoreMinimal.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/Docking/TabManager.h"
#include "IAnimGraphSchematicView.h"
#include "RewindDebuggerModule.h"
#include "RewindDebuggerTimeSliderController.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"


class SRewindDebugger : public SCompoundWidget
{
	typedef TBindablePropertyInitializer<FString, BindingType_Out> DebugTargetInitializer;

public:
	DECLARE_DELEGATE_TwoParams( FOnScrubPositionChanged, float, bool )
	DECLARE_DELEGATE_OneParam( FOnDebugTargetChanged, TSharedPtr<FString> )

	SLATE_BEGIN_ARGS(SRewindDebugger) { }
		SLATE_ARGUMENT( TArray< TSharedPtr< FDebugObjectInfo > >*, DebugComponents );
		SLATE_ARGUMENT(DebugTargetInitializer, DebugTargetActor);
		SLATE_ARGUMENT(TBindablePropertyInitializer<double>, TraceTime);
		SLATE_ARGUMENT(TBindablePropertyInitializer<float>, RecordingDuration);
		SLATE_ATTRIBUTE(float, ScrubTime);
		SLATE_EVENT(FOnScrubPositionChanged, OnScrubPositionChanged);
	SLATE_END_ARGS()
	
public:

	/**
	* Default constructor.
	*/
	SRewindDebugger();
	virtual ~SRewindDebugger();

	/**
	* Constructs the application.
	*
	* @param InArgs The Slate argument list.
	* @param ConstructUnderMajorTab The major tab which will contain the session front-end.
	* @param ConstructUnderWindow The window in which this widget is being constructed.
	* @param InStyleSet The style set to use.
	*/
	void Construct(const FArguments& InArgs, TSharedRef<FUICommandList> CommandList, const TSharedRef<SDockTab>& ConstructUnderMajorTab, const TSharedPtr<SWindow>& ConstructUnderWindow);

	void TrackCursor(bool bReverse);
	void RefreshDebugComponents();
private:
	TSharedPtr<FTimeSliderController> TimeSliderController;

	TAttribute<float> ScrubTimeAttribute;
	TAttribute<bool> TrackScrubbingAttribute;
	FOnScrubPositionChanged OnScrubPositionChanged;

	// debug actor selector
	TSharedRef<SWidget> MakeSelectActorMenu();
	void SetDebugTargetActor(AActor* Actor);
	FReply OnSelectActorClicked();

	TBindableProperty<FString, BindingType_Out> DebugTargetActor;
	TBindableProperty<double> TraceTime;
	TBindableProperty<float> RecordingDuration;
	TBindableProperty<float> DebugTargetAnimInstanceId;

	// debug components list
	TArray<TSharedPtr<FDebugObjectInfo>>* DebugComponents;
    void OnComponentSelectionChanged(TSharedPtr<FDebugObjectInfo> SelectedItem, ESelectInfo::Type SelectInfo);
	TSharedPtr<SListView<TSharedPtr<FDebugObjectInfo>>> ComponentListView;
	TSharedRef<ITableRow> ComponentListViewGenerateRow(TSharedPtr<FDebugObjectInfo> InItem, const TSharedRef<STableViewBase>& OwnerTable);

	// anim graph view 
	TSharedPtr<IAnimGraphSchematicView> AnimGraphView;
};
