// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Attribute.h"
#include "SlateGlobals.h"
#include "Input/Reply.h"
#include "Styling/SlateWidgetStyleAsset.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

#include "IDetailsView.h"

class URemoteControlPreset;
class SVerticalBox;
struct FExposedProperty;
class FPropertyEditorModule;

class STableViewBase;
class ITableRow;
template <typename ItemType>
class SListView;

struct FPropertyChangedEvent;

class SRemoteControlProtocolWidgetExtension : public SCompoundWidget
{
public:
	using OnFinishedChangingPropertiesCallback = TFunction<void(const FPropertyChangedEvent&)>;

private:
	struct FBindingItem : public TSharedFromThis<FBindingItem>
	{
		FBindingItem() = default;
		FBindingItem(const TSharedRef<SRemoteControlProtocolWidgetExtension>& InWidgetExtension, const FGuid& InBindingId)
			: WidgetExtensionPtr(InWidgetExtension)
			, BindingId(InBindingId)
		{

		}

		const FGuid& GetBindingId() const { return BindingId; }

		TSharedRef<ITableRow> MakeRangeItem(TSharedPtr<FGuid> InItem, const TSharedRef<STableViewBase>& OwnerTable);

		FReply OnRemoveRangeMapping(const FGuid InRangeId);

		TWeakPtr<SRemoteControlProtocolWidgetExtension> WidgetExtensionPtr;

		FGuid BindingId;

		TSharedPtr<SListView<TSharedPtr<FGuid>>> RangesListView;
		
		TArray<TSharedPtr<FGuid>> RangesList;
	};

public:
	SLATE_BEGIN_ARGS(SRemoteControlProtocolWidgetExtension)
		: _Preset(nullptr)
		, _PropertyLabel()
	{
	}

	SLATE_ARGUMENT(URemoteControlPreset*, Preset)

	SLATE_ARGUMENT(FName, PropertyLabel)

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

public:
	bool DeleteMappingFromListByID(const FGuid& InID);

	const FProperty* GetProperty() const { return Property; }

	const FGuid& GetPropertyId() const { return PropertyId; }

	URemoteControlPreset* GetPresetPtr() const { return PresetPtr.Get(); }

public:
	static TSharedRef<SWidget> CreateStructureDetailView(TSharedPtr<FStructOnScope> StructData, const OnFinishedChangingPropertiesCallback& OnFinishedChangingProperties);

private:
	TSharedRef<ITableRow> MakeBindingListItem(TSharedPtr<FBindingItem> InItem, const TSharedRef<STableViewBase>& OwnerTable);

	FReply OnAddRangeMapping(TSharedRef<FBindingItem> InItem);

	FReply OnRemoveMapping(TSharedRef<FBindingItem> InItem);

	TSharedRef<ITableRow> MakeProtocolSelectionWidget(TSharedPtr<FName> InItem, const TSharedRef<STableViewBase>& OwnerTable);

	FReply OnAddProtocol();

	void OnProtocolSelectionChanged(TSharedPtr<FName> NewSelection, ESelectInfo::Type InSelectInfo);

	void RefreshUnselectedProtocolsList();

	void RefreshProtocolBinding();

private:
	TWeakObjectPtr<URemoteControlPreset> PresetPtr;

	FName PropertyLabel;

	FGuid PropertyId;

	TOptional<FExposedProperty> ExposedProperty;

	FProperty* Property;

	TSharedPtr<FName> SelectedProtocol;

	TArray<TSharedPtr<FName>> UnselectedProtocols;

	TSharedPtr<SComboButton> ProtocolSelectionButton;

	TSharedPtr<SListView<TSharedPtr<FName>>> ProtocolListView;

	TSharedPtr<SListView<TSharedPtr<FBindingItem>>> BindingListView;

	TArray<TSharedPtr<FBindingItem>> BindingList;
};