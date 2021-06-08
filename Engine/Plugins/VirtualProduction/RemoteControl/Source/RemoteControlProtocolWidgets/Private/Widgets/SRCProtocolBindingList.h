// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "IRCProtocolBindingList.h"
#include "SRCProtocolShared.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class FProtocolBindingViewModel;
class FProtocolEntityViewModel;
class IRCTreeNodeViewModel;
class ITableRow;
class SRCProtocolBinding;
class SRCProtocolList;
class STableViewBase;
class URemoteControlProtocolWidgetsSettings;

/** The root view for a given entity. A (vertical) list of bindings, where each binding has a protocol. */
class REMOTECONTROLPROTOCOLWIDGETS_API SRCProtocolBindingList final : public SCompoundWidget, public IRCProtocolBindingList
{
public:
	SLATE_BEGIN_ARGS(SRCProtocolBindingList)
	{ }
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedRef<FProtocolEntityViewModel> InViewModel);
	virtual ~SRCProtocolBindingList() override;

	//~ Begin IRCProtocolBindingList Interface
	/** Get the set of entities which is awaiting state and waiting for binding. */
	virtual FRemoteControlProtocolEntitySet GetAwaitingProtocolEntities() const override
	{
		return AwaitingProtocolEntities;
	}
	//~ End IRCProtocolBindingList Interface

private:
	/** Constructs a single item in the list. */
	TSharedRef<ITableRow> ConstructBindingWidget(TSharedPtr<IRCTreeNodeViewModel> InViewModel, const TSharedRef<STableViewBase>& InOwnerTable);

	/** Check if the bound entity type is supported by Protocol. */
	bool CanAddProtocol();

	/** Toggles visibility of the given protocol. */
	void ToggleShowProtocol(const FName& InProtocolName);

	/** Check if the given protocol name is being displayed. */
	bool IsProtocolShown(const FName& InProtocolName);

	/** Called to get the visibility of the scrollbar based on options, needs to be dynamic to avoid layout changing on expansion */
	EVisibility GetScrollBarVisibility() const;

	/** Primary Column is between Input and Output values. */
	float OnGetPrimaryLeftColumnWidth() const
	{
		return 1.0f - PrimaryColumnWidth;
	}

	float OnGetPrimaryRightColumnWidth() const
	{
		return PrimaryColumnWidth;
	}

	void OnSetPrimaryColumnWidth(float InWidth)
	{
		PrimaryColumnWidth = InWidth;
	}

	/** Secondary Column is for output struct labels - doesn't apply when the type is drawn in a single row. */
	float OnGetSecondaryLeftColumnWidth() const
	{
		return 1.0f - SecondaryColumnWidth;
	}

	float OnGetSecondaryRightColumnWidth() const
	{
		return SecondaryColumnWidth;
	}

	void OnSetSecondaryColumnWidth(float InWidth)
	{
		SecondaryColumnWidth = InWidth;
	}

	/** Start recording incoming protocol message handler */
	void OnStartRecording(TSharedPtr<TStructOnScope<FRemoteControlProtocolEntity>> InEntity);

	/** Stop recording incoming protocol message handler */
	void OnStopRecording(TSharedPtr<TStructOnScope<FRemoteControlProtocolEntity>> InEntity);

	/** Get (mutable) module settings */
	URemoteControlProtocolWidgetsSettings* GetSettings();

	/** Refresh protocol binding list. Returns false if the backing widget wasn't valid. */
	bool Refresh(bool bNavigateToEnd = false);

private:
	/** ViewModel for the Protocol Entity. */
	TSharedPtr<FProtocolEntityViewModel> ViewModel;

	/** Current status message, if any */
	FText StatusMessage;

	/** List of all available protocol names */
	TArray<FName> ProtocolNames;

	/** Dropdown list of available protocol names to add */
	TSharedPtr<SRCProtocolList> ProtocolList;

	/** ListView widget for each protocol binding */
	TSharedPtr<SListView<TSharedPtr<IRCTreeNodeViewModel>>> BindingList;

	/** Binding view models, filtered by the current HiddenProtocolTypeNames list */
	TArray<TSharedPtr<IRCTreeNodeViewModel>> FilteredBindings;

	/** Container used by all primary splitters in the details view, so that they move in sync */
	TSharedPtr<RemoteControlProtocolWidgetUtils::FPropertyViewColumnSizeData> PrimaryColumnSizeData;

	/** Relative width to control primary splitters */
	float PrimaryColumnWidth = 0.5f;

	/** Container used by all primary splitters in the details view, so that they move in sync */
	TSharedPtr<RemoteControlProtocolWidgetUtils::FPropertyViewColumnSizeData> SecondaryColumnSizeData;

	/** Relative width to control secondary splitters */
	float SecondaryColumnWidth = 0.5f;

	/** Reference to (mutable) settings class for this module */
	TWeakObjectPtr<URemoteControlProtocolWidgetsSettings> Settings;

	/** Set of protocol entities with awaiting state and waiting for binding */
	FRemoteControlProtocolEntitySet AwaitingProtocolEntities;
};
