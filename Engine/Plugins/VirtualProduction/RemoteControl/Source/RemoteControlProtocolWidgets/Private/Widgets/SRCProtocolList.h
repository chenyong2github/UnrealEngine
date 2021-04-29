// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class ITableRow;
class SComboButton;
class STableViewBase;
template <typename ItemType> class SListView;

/** A dropdown list of available protocols */
class REMOTECONTROLPROTOCOLWIDGETS_API SRCProtocolList : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRCProtocolList)
	{}
	SLATE_END_ARGS()

	/** Constructs this widget with InArgs */
	void Construct(const FArguments& InArgs);

	/** Currently selected protocol name */
	const TSharedPtr<FName>& GetSelectedProtocolName() const { return SelectedProtocolName; }

private:
	TSharedRef<ITableRow> ConstructListItem(TSharedPtr<FName> InItem, const TSharedRef<STableViewBase>& InOwnerTable) const;
	void OnSelectionChanged(TSharedPtr<FName> InNewSelection, ESelectInfo::Type InSelectInfo);
	void Refresh();

private:
	/** List of all available protocol names */
	TArray<TSharedPtr<FName>> AvailableProtocolNames;

	/** Currently selected protocol name */ 
	TSharedPtr<FName> SelectedProtocolName;
	
	/** ComboButton */
	TSharedPtr<SComboButton> SelectionButton;

	/** ComboButton menu content */
	TSharedPtr<SListView<TSharedPtr<FName>>> ListView;
};
