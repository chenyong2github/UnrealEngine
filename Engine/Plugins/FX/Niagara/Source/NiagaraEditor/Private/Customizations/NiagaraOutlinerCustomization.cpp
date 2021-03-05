// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraOutlinerCustomization.h"

#include "Modules/ModuleManager.h"

//Customization
#include "IStructureDetailsView.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailCustomization.h"
#include "DetailCategoryBuilder.h"
//Widgets
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNiagaraDebugger.h"
#include "Widgets/Views/STreeView.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SScrollBorder.h"
#include "Widgets/Images/SImage.h"
///Niagara
#include "NiagaraEditorModule.h"
#include "NiagaraComponent.h"
#include "NiagaraEditorStyle.h"
#include "Widgets/SVerticalResizeBox.h"


#define LOCTEXT_NAMESPACE "NiagaraOutlinerCustomization"

//////////////////////////////////////////////////////////////////////////
// Outliner Row Widgets

class SNiagaraOutlinerTreeItem : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SNiagaraOutlinerTreeItem) { }
	SLATE_ATTRIBUTE(FText, HighlightText)
	SLATE_ARGUMENT(TSharedPtr<FNiagaraOutlinerTreeItem>, Item)
	SLATE_ARGUMENT(TSharedPtr<SNiagaraOutlinerTree>, Owner)
	SLATE_END_ARGS()

public:

	/**
	 * Constructs the widget.
	 *
	 * @param InArgs The construction arguments.
	 */
	//BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
	void Construct(const FArguments& InArgs)
	{
		Item = InArgs._Item;
		HighlightText = InArgs._HighlightText;
		Owner = InArgs._Owner;

		Item->Widget = SharedThis(this);

		RefreshContent();
	}
	//END_SLATE_FUNCTION_BUILD_OPTIMIZATION

	void RefreshContent()
	{
		if (!Item->bVisible)
		{
			ChildSlot
			.Padding(0.0f)
			[
				SNullWidget::NullWidget 
			];
			return;
		}

		TSharedRef<SWidget> ItemHeaderWidget = Item->GetHeaderWidget();
		ChildSlot
			.Padding(2, 2.0f, 2.0f, 2.0f)
			[
				SNew(SBorder)
				.BorderImage(FEditorStyle::GetBrush("NoBorder"))
				.Padding(FMargin(6.0, 3.0f, 6.0f, 3.0f))
				.ToolTipText(this, &SNiagaraOutlinerTreeItem::HandleBorderToolTipText)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text(this, &SNiagaraOutlinerTreeItem::HandleNameText)
							.Font(FEditorStyle::GetFontStyle("PropertyWindow.NormalFont"))
							.HighlightText(Owner->GetSearchText())
						]
						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.HAlign(HAlign_Right)
						[
							ItemHeaderWidget
						]
					]				
				]
			];
	}

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
		{
			Owner->ToggleItemExpansion(Item);

			return FReply::Unhandled();
		}
		else
		{
			return FReply::Unhandled();
		}
	}

	/** Callback for getting the name text */
	FText HandleNameText() const
	{
		return Item->GetShortNameText();
	}

	/** Callback for getting the text of the row border's tool tip. */
	FText HandleBorderToolTipText() const
	{
		return Item->GetFullNameText();
	}

	/** A reference to the tree item that is displayed in this row. */
	TSharedPtr<FNiagaraOutlinerTreeItem> Item;

	TAttribute<FText> HighlightText;
	
	TSharedPtr<SNiagaraOutlinerTree> Owner;
};

//////////////////////////////////////////////////////////////////////////

FString SNiagaraOutlinerTree::OutlinerItemToStringDebug(TSharedRef<FNiagaraOutlinerTreeItem> Item)
{
	FString Ret;
	
	switch(Item->GetType())
	{
	case ENiagaraOutlinerTreeItemType::World: Ret += TEXT("World: "); break;
	case ENiagaraOutlinerTreeItemType::System: Ret += TEXT("System: "); break;
	case ENiagaraOutlinerTreeItemType::Component: Ret += TEXT("Component: "); break;
	case ENiagaraOutlinerTreeItemType::Emitter: Ret += TEXT("Emitter: "); break;
	}

	Ret += Item->GetFullName();
	return Ret;
}


void SNiagaraOutlinerTree::Construct(const FArguments& InArgs, TSharedPtr<FNiagaraDebugger> InDebugger)
{
	Debugger = InDebugger;

	if (UNiagaraOutliner* Outliner = Debugger->GetOutliner())
	{
		FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

		FDetailsViewArgs ViewArgs;
		ViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
		ViewArgs.bAllowSearch = false;
		ViewArgs.bHideSelectionTip = true;
		SelectedItemDetails = PropertyEditorModule.CreateStructureDetailView(
			ViewArgs,
			FStructureDetailsViewArgs(),
			nullptr);

		Outliner->OnDataChangedDelegate.AddSP(this, &SNiagaraOutlinerTree::RequestRefresh);

		TreeView = SNew(STreeView<TSharedRef<FNiagaraOutlinerTreeItem>>)
			.ItemHeight(20.0f)
			.SelectionMode(ESelectionMode::Single)
			.TreeItemsSource(&RootEntries)
			.OnGenerateRow(this, &SNiagaraOutlinerTree::OnGenerateRow)
			.OnGetChildren(this, &SNiagaraOutlinerTree::OnGetChildren)
			.OnExpansionChanged(this, &SNiagaraOutlinerTree::HandleExpansionChanged)
			.OnSelectionChanged(this, &SNiagaraOutlinerTree::HandleSelectionChanged)
			.OnItemToString_Debug(this, &SNiagaraOutlinerTree::OutlinerItemToStringDebug);

		ChildSlot
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(0.65f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					// Search box allows for filtering
					SAssignNew(SearchBox, SSearchBox)
					.OnTextChanged_Lambda([this](const FText& InText) { SearchText = InText; RefreshTree(); })
				]
				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				[
					SNew(SScrollBorder, TreeView.ToSharedRef())
					[
						TreeView.ToSharedRef()
					]
				]
			]
			+ SHorizontalBox::Slot()
			.FillWidth(0.35f)
			[
				SelectedItemDetails->GetWidget().ToSharedRef()//TODO: Maybe shunt this out intot he main outliner details somehow?
			]
		];

		// Set focus to the search box on creation
		FSlateApplication::Get().SetKeyboardFocus(SearchBox);
		FSlateApplication::Get().SetUserFocus(0, SearchBox);

		RefreshTree();
	}
}

void SNiagaraOutlinerTree::ToggleItemExpansion(TSharedPtr<FNiagaraOutlinerTreeItem>& Item)
{
	TreeView->SetItemExpansion(Item.ToSharedRef(), Item->Expansion != ENiagaraOutlinerSystemExpansionState::Expanded);
}

void SNiagaraOutlinerTree::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	if(bNeedsRefresh)
	{
		bNeedsRefresh = false;
		RefreshTree();
	}
}

TSharedRef<ITableRow> SNiagaraOutlinerTree::OnGenerateRow(TSharedRef<FNiagaraOutlinerTreeItem> InItem, const TSharedRef<STableViewBase>& InOwnerTable)
{
	static const char* ItemStyles[] =
	{
		"NiagaraEditor.Outliner.WorldItem",
		"NiagaraEditor.Outliner.SystemItem",
		"NiagaraEditor.Outliner.ComponentItem",
		"NiagaraEditor.Outliner.EmitterItem",
	};

	ENiagaraOutlinerTreeItemType StyleType = InItem->GetType();

	return SNew(STableRow<TSharedRef<FNiagaraOutlinerTreeItem>>, InOwnerTable)
		.Style(FNiagaraEditorStyle::Get(), ItemStyles[(int32)StyleType])
		[
			SNew(SNiagaraOutlinerTreeItem)
			.Item(InItem)
			.HighlightText(SearchText)
			.Owner(SharedThis(this))
		];
}

void SNiagaraOutlinerTree::OnGetChildren(TSharedRef<FNiagaraOutlinerTreeItem> InItem, TArray<TSharedRef<FNiagaraOutlinerTreeItem>>& OutChildren)
{
	for(const TSharedRef<FNiagaraOutlinerTreeItem>& ChildItem : InItem->Children)
	{
		if(ChildItem->bVisible)
		{
			OutChildren.Add(ChildItem);
		}
	}
}

void SNiagaraOutlinerTree::HandleExpansionChanged(TSharedRef<FNiagaraOutlinerTreeItem> InItem, bool bExpanded)
{
	InItem->Expansion = bExpanded ? ENiagaraOutlinerSystemExpansionState::Expanded : ENiagaraOutlinerSystemExpansionState::Collapsed;
}

void SNiagaraOutlinerTree::HandleSelectionChanged(TSharedPtr<FNiagaraOutlinerTreeItem> SelectedItem, ESelectInfo::Type SelectInfo)
{
	if (SelectedItemDetails.IsValid())
	{
		if (SelectedItem.IsValid())
		{
			SelectedItemDetails->SetStructureData(SelectedItem->GetDetailsViewContent());
		}
		else
		{
			SelectedItemDetails->SetStructureData(nullptr);
		}
	}
}

template<typename ChildItemType>
TSharedPtr<FNiagaraOutlinerTreeItem> SNiagaraOutlinerTree::AddChildItemToEntry(TArray<TSharedRef<FNiagaraOutlinerTreeItem>>& ExistingEntries, const TSharedPtr<FNiagaraOutlinerTreeItem>& InItem, FString ChildName, bool DefaultVisibility, ENiagaraOutlinerSystemExpansionState DefaultExpansion)
{
	TSharedPtr<FNiagaraOutlinerTreeItem> NewTreeEntry;
	if (TSharedRef<FNiagaraOutlinerTreeItem>* ExistingTreeEntry = ExistingEntries.FindByPredicate([&](const auto& Existing) { return Existing->Name == ChildName; }))
	{
		NewTreeEntry = *ExistingTreeEntry;
	}
	else
	{
		NewTreeEntry = MakeShared<ChildItemType>();
		NewTreeEntry->Name = ChildName;
		NewTreeEntry->Expansion = DefaultExpansion;
		NewTreeEntry->bVisible = DefaultVisibility;
	}

	NewTreeEntry->Parent = InItem;

	NewTreeEntry->bMatchesSearch = InItem.IsValid() && InItem->bMatchesSearch;
	if (SearchText.IsEmpty() == false)
	{
		NewTreeEntry->bMatchesSearch |= NewTreeEntry->GetFullName().Contains(*SearchText.ToString());
	}

	RefreshTree_Helper(NewTreeEntry);
	
	NewTreeEntry->RefreshWidget();

	if (InItem.IsValid())
	{
		InItem->Children.Add(NewTreeEntry.ToSharedRef());
		InItem->bAnyChildrenVisible |= NewTreeEntry->bVisible;
		//InItem->Expansion = FMath::Max(NewTreeEntry->Expansion, InItem->Expansion);
	}

	return NewTreeEntry;
};

void SNiagaraOutlinerTree::RefreshTree_Helper(const TSharedPtr<FNiagaraOutlinerTreeItem>& InTreeEntry)
{
	if (UNiagaraOutliner* Outliner = Debugger->GetOutliner())
	{
		if (InTreeEntry.IsValid() == false)
		{
			TArray<TSharedRef<FNiagaraOutlinerTreeItem>> ExistingItems = MoveTemp(RootEntries);
			
			//Create the root nodes.
			for (TPair<FString, FNiagaraOutlinerWorldData>& WorldData : Outliner->Data.WorldData)
			{
				FString WorldName = WorldData.Key;
				TSharedPtr<FNiagaraOutlinerTreeItem> NewRootEntry = AddChildItemToEntry<FNiagaraOutlinerTreeWorldItem>(ExistingItems, InTreeEntry, WorldName, true, ENiagaraOutlinerSystemExpansionState::Collapsed);
				RootEntries.Add(NewRootEntry.ToSharedRef());
			}
		}
		else
		{
			//Store off any existing items for us to pull from but remove them from the actual tree child list so any items that are no longer in the data are removed.
			TArray<TSharedRef<FNiagaraOutlinerTreeItem>> ExistingItems = MoveTemp(InTreeEntry->Children);
			ENiagaraOutlinerTreeItemType Type = InTreeEntry->GetType();

			InTreeEntry->bVisible = false;
			InTreeEntry->bAnyChildrenVisible = false;
			bool bFiltered = false;

			if (Type == ENiagaraOutlinerTreeItemType::World)
			{
				//Add all systems in use in the world to the tree
				if (FNiagaraOutlinerWorldData* WorldData = (FNiagaraOutlinerWorldData*)InTreeEntry->GetData())
				{
					for (TPair<FString, FNiagaraOutlinerSystemData>& SystemData : WorldData->Systems)
					{
						FString SystemName = SystemData.Key;
						AddChildItemToEntry<FNiagaraOutlinerTreeSystemItem>(ExistingItems, InTreeEntry, SystemName, true, ENiagaraOutlinerSystemExpansionState::Collapsed);
					}
				}

				//Worlds are filtered if they have no visible contents.
				bFiltered = InTreeEntry->bAnyChildrenVisible == false;
			}
			else if (Type == ENiagaraOutlinerTreeItemType::System)
			{				
				//Add any child instances of this system to the tree
				if (FNiagaraOutlinerSystemData* SystemData = (FNiagaraOutlinerSystemData*)InTreeEntry->GetData())
				{
					for (FNiagaraOutlinerSystemInstanceData& InstData : SystemData->SystemInstances)
					{
						FString ComponentName = InstData.ComponentName;
						AddChildItemToEntry<FNiagaraOutlinerTreeComponentItem>(ExistingItems, InTreeEntry, ComponentName, true, ENiagaraOutlinerSystemExpansionState::Collapsed);
					}
				}

				//Systems are filtered if they have no visible contents.
				bFiltered = InTreeEntry->bAnyChildrenVisible == false;
			}
			else if(Type == ENiagaraOutlinerTreeItemType::Component)
			{
				//Add any child emitters to the tree
				if (FNiagaraOutlinerSystemInstanceData* InstData = (FNiagaraOutlinerSystemInstanceData*)InTreeEntry->GetData())
				{
					for (FNiagaraOutlinerEmitterInstanceData& EmtitterData : InstData->Emitters)
					{
						FString EmitterName = EmtitterData.EmitterName;
						AddChildItemToEntry<FNiagaraOutlinerTreeEmitterItem>(ExistingItems, InTreeEntry, EmitterName, true, ENiagaraOutlinerSystemExpansionState::Collapsed);
					}

					//Apply any system instance filters. No need to generate and check children with these filters.
					if (Outliner->Filters.bFilterBySystemExecutionState && Outliner->Filters.SystemExecutionState != InstData->ActualExecutionState)
					{
						bFiltered = true;
					}
				}
			}
			else if(Type == ENiagaraOutlinerTreeItemType::Emitter)
			{
				//Should this emitter be filtered out.
				if (FNiagaraOutlinerEmitterInstanceData* EmitterData = (FNiagaraOutlinerEmitterInstanceData*)InTreeEntry->GetData())
				{
					bFiltered = (Outliner->Filters.bFilterByEmitterExecutionState && Outliner->Filters.EmitterExecutionState != EmitterData->ExecState) ||
						(Outliner->Filters.bFilterByEmitterSimTarget && Outliner->Filters.EmitterSimTarget != EmitterData->SimTarget);			
				}
			}

			bool bIsLeaf = (Type == ENiagaraOutlinerTreeItemType::Emitter);
			bool bAnyFiltersActive = SearchText.IsEmpty() == false || Outliner->Filters.bFilterByEmitterExecutionState || Outliner->Filters.bFilterByEmitterSimTarget || Outliner->Filters.bFilterBySystemExecutionState;

			bool bVisible = false;
			if (bFiltered)
			{
				bVisible = false;
			}
			else
			{
				if (bIsLeaf)
				{
					bVisible = SearchText.IsEmpty() || InTreeEntry->bMatchesSearch;
				}
				else
				{
					bVisible = InTreeEntry->bMatchesSearch || (!bAnyFiltersActive || InTreeEntry->bAnyChildrenVisible);
				}
			}
			InTreeEntry->bVisible = bVisible;
			TreeView->SetItemExpansion(InTreeEntry.ToSharedRef(), InTreeEntry->Expansion == ENiagaraOutlinerSystemExpansionState::Expanded);
		}
	}
}

void SNiagaraOutlinerTree::RefreshTree()
{
	RefreshTree_Helper(nullptr);
	TreeView->RequestTreeRefresh();
}

//////////////////////////////////////////////////////////////////////////

template<typename T>
class SNiagaraOutlinerTreeItemHeaderDataWidget : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SNiagaraOutlinerTreeItemHeaderDataWidget) 
	: _MinDesiredWidth(FOptionalSize()) 
	{
	}

	SLATE_ARGUMENT(FText, ToolTipText)
	SLATE_ARGUMENT(T, Data)
	SLATE_ATTRIBUTE(FOptionalSize, MinDesiredWidth)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		T Data = InArgs._Data;
		FText ValueText = FText::AsNumber(Data);

		ChildSlot
		[
			SNew(SBox)
			.MinDesiredWidth(InArgs._MinDesiredWidth)
			[
				SNew(SBorder)
				.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
				[
					SNew(STextBlock)
					.Text(ValueText)
					.ToolTipText(InArgs._ToolTipText)
					.Justification(ETextJustify::Center)
				]
			]
		];
	}
};

template<>
class SNiagaraOutlinerTreeItemHeaderDataWidget<FText> : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SNiagaraOutlinerTreeItemHeaderDataWidget)
		: _MinDesiredWidth(FOptionalSize())
	{
	}

	SLATE_ARGUMENT(FText, ToolTipText)
	SLATE_ARGUMENT(FText, Data)
	SLATE_ATTRIBUTE(FOptionalSize, MinDesiredWidth)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		ChildSlot
		[
			SNew(SBox)
			.MinDesiredWidth(InArgs._MinDesiredWidth)
			[			
				SNew(SBorder)
				.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
				[
					SNew(STextBlock)
					.Text(InArgs._Data)
					.ToolTipText(InArgs._ToolTipText)
					.Justification(ETextJustify::Center)
				]
			]
		];
	}
};

const float FNiagaraOutlinerTreeItem::HeaderPadding = 6.0f;

void FNiagaraOutlinerTreeItem::RefreshWidget()
{
	if (TSharedPtr<SNiagaraOutlinerTreeItem> PinnedWidget = Widget.Pin())
	{
		PinnedWidget->RefreshContent();
	}
}

TSharedRef<SWidget> FNiagaraOutlinerTreeItem::GetHeaderWidget()
{
	return SNullWidget::NullWidget;
}

TSharedPtr<FStructOnScope>& FNiagaraOutlinerTreeItem::GetDetailsViewContent()
{
	if (DetailsViewData == nullptr)
	{
		DetailsViewData = MakeShared<FStructOnScope>();

		UScriptStruct* Struct = nullptr;
		ENiagaraOutlinerTreeItemType Type = GetType();
		switch (Type)
		{
		case ENiagaraOutlinerTreeItemType::World: Struct = FNiagaraOutlinerWorldData::StaticStruct(); break;
		case ENiagaraOutlinerTreeItemType::System: Struct = FNiagaraOutlinerSystemData::StaticStruct(); break;
		case ENiagaraOutlinerTreeItemType::Component: Struct = FNiagaraOutlinerSystemInstanceData::StaticStruct(); break;
		case ENiagaraOutlinerTreeItemType::Emitter: Struct = FNiagaraOutlinerEmitterInstanceData::StaticStruct(); break;
		}

		const void* Data = GetData();
		check(Struct && Data);

		DetailsViewData->Initialize(Struct);
		Struct->CopyScriptStruct(DetailsViewData->GetStructMemory(), Data);
	}
	return DetailsViewData;
}

const void* FNiagaraOutlinerTreeWorldItem::GetData()const
{
	FNiagaraEditorModule& NiagaraEditorModule = FModuleManager::GetModuleChecked<FNiagaraEditorModule>("NiagaraEditor");

	if (UNiagaraOutliner* Outliner = NiagaraEditorModule.GetDebugger()->GetOutliner())
	{
		const FString& WorldName = GetFullName();
		return Outliner->FindWorldData(WorldName);
	}
	return nullptr;
}

TSharedRef<SWidget> FNiagaraOutlinerTreeWorldItem::GetHeaderWidget()
{
	if (FNiagaraOutlinerWorldData* Data = (FNiagaraOutlinerWorldData*)GetData())
	{
		TSharedRef<SHorizontalBox> Box = SNew(SHorizontalBox);

		//Count system instances matching filters.
		int32 NumMatchingInstances = 0;
		for (const TSharedRef<FNiagaraOutlinerTreeItem>& SystemItem : Children)
		{
			if (SystemItem->GetType() == ENiagaraOutlinerTreeItemType::System && SystemItem->bVisible)
			{
				for (const TSharedRef<FNiagaraOutlinerTreeItem>& InstItem : SystemItem->Children)
				{
					if (InstItem->GetType() == ENiagaraOutlinerTreeItemType::Component && InstItem->bVisible)
					{
						++NumMatchingInstances;
					}
				}
			}
		}
		
		Box->AddSlot()
			.AutoWidth()
			.Padding(FMargin(HeaderPadding, 0.0f, HeaderPadding, 0.0f))
			[
				SNew(SNiagaraOutlinerTreeItemHeaderDataWidget<FText>)
				.ToolTipText(LOCTEXT("WorldHeaderTooltip_WorldType", "World Type"))
				.Data(FText::FromString(ToString((EWorldType::Type)Data->WorldType)))
			];

		Box->AddSlot()
			.AutoWidth()
			.Padding(FMargin(HeaderPadding, 0.0f, HeaderPadding, 0.0f))
			[
				SNew(SNiagaraOutlinerTreeItemHeaderDataWidget<FText>)
				.ToolTipText(LOCTEXT("WorldHeaderTooltip_NetMode", "Net Mode"))
				.Data(FText::FromString(ToString((ENetMode)Data->NetMode)))
			];

		Box->AddSlot()
			.AutoWidth()
			.Padding(FMargin(HeaderPadding, 0.0f, HeaderPadding, 0.0f))
			[
				SNew(SNiagaraOutlinerTreeItemHeaderDataWidget<FText>)
				.ToolTipText(LOCTEXT("WorldHeaderTooltip_BegunPlay", "Has Begun Play"))
				.Data(Data->bHasBegunPlay ? FText(LOCTEXT("True", "True")) : FText(LOCTEXT("False", "False")))
				.MinDesiredWidth(50.0f)
			];

		Box->AddSlot()
			.AutoWidth()
			.Padding(FMargin(HeaderPadding, 0.0f, HeaderPadding, 0.0f))
			[
				SNew(SNiagaraOutlinerTreeItemHeaderDataWidget<int32>)
				.ToolTipText(LOCTEXT("VisibleSystemsHeaderName", "Num instances matching current search and filters."))
				.Data(NumMatchingInstances)
			];
		return Box;
	}
	return SNullWidget::NullWidget;
}

const void* FNiagaraOutlinerTreeSystemItem::GetData()const
{
	FNiagaraEditorModule& NiagaraEditorModule = FModuleManager::GetModuleChecked<FNiagaraEditorModule>("NiagaraEditor");

	if (UNiagaraOutliner* Outliner = NiagaraEditorModule.GetDebugger()->GetOutliner())
	{
		TSharedPtr<FNiagaraOutlinerTreeItem> WorldItem = GetParent();

		const FString& SystemName = GetFullName();
		const FString& WorldName = WorldItem->GetFullName();
		return Outliner->FindSystemData(WorldName, SystemName);
	}
	return nullptr;
}

TSharedRef<SWidget> FNiagaraOutlinerTreeSystemItem::GetHeaderWidget()
{
	if (FNiagaraOutlinerSystemData* Data = (FNiagaraOutlinerSystemData*)GetData())
	{
		TSharedRef<SHorizontalBox> Box = SNew(SHorizontalBox);

		int32 NumMatchingSystems = 0;
		for (const TSharedRef<FNiagaraOutlinerTreeItem>& Item : Children)
		{
			if (Item->GetType() == ENiagaraOutlinerTreeItemType::Component && Item->bVisible)
			{
				++NumMatchingSystems;
			}
		}

		Box->AddSlot()
			.AutoWidth()
			.Padding(FMargin(HeaderPadding, 0.0f, HeaderPadding, 0.0f))
			[
				SNew(SNiagaraOutlinerTreeItemHeaderDataWidget<int32>)
				.ToolTipText(LOCTEXT("VisibleInstancesHeaderName", "Num system instances matching current search and filters."))
				.Data(NumMatchingSystems)
			];

		return Box;
	}
	return SNullWidget::NullWidget;
}

const void* FNiagaraOutlinerTreeComponentItem::GetData()const
{
	FNiagaraEditorModule& NiagaraEditorModule = FModuleManager::GetModuleChecked<FNiagaraEditorModule>("NiagaraEditor");

	if (UNiagaraOutliner* Outliner = NiagaraEditorModule.GetDebugger()->GetOutliner())
	{
		TSharedPtr<FNiagaraOutlinerTreeItem> SystemItem = GetParent();
		TSharedPtr<FNiagaraOutlinerTreeItem> WorldItem = SystemItem->GetParent();

		const FString& ComponentName = GetFullName();
		const FString& SystemName = SystemItem->GetFullName();
		const FString& WorldName = WorldItem->GetFullName();
		return Outliner->FindComponentData(WorldName, SystemName, ComponentName);
	}
	return nullptr;
}

TSharedRef<SWidget> FNiagaraOutlinerTreeComponentItem::GetHeaderWidget()
{
	if (FNiagaraOutlinerSystemInstanceData* Data = (FNiagaraOutlinerSystemInstanceData*)GetData())
	{
		TSharedRef<SHorizontalBox> Box = SNew(SHorizontalBox);

		UEnum* ExecStateEnum = StaticEnum<ENiagaraExecutionState>();

		if (Data->ActualExecutionState == ENiagaraExecutionState::Num)
		{
			//System instance is not initialized	
			Box->AddSlot()
				.AutoWidth()
				.Padding(FMargin(HeaderPadding, 0.0f, HeaderPadding, 0.0f))
				[
					SNew(SNiagaraOutlinerTreeItemHeaderDataWidget<FText>)
					.ToolTipText(LOCTEXT("UninitializedSystemInstanceTooltip", "Internal data for component is uninitialized. Likely as it has yet to be activated."))
					.Data(LOCTEXT("UninitializedSystemInstanceValue", "Uninitialized"))
				];
		}
		else
		{
			int32 NumMatchingEmitters = 0;
			for (const TSharedRef<FNiagaraOutlinerTreeItem>& Item : Children)
			{
				if (Item->GetType() == ENiagaraOutlinerTreeItemType::Emitter && Item->bVisible)
				{
					++NumMatchingEmitters;
				}
			}

			if (Data->ScalabilityState.bCulled)
			{
				Box->AddSlot()
					.AutoWidth()
					.Padding(FMargin(HeaderPadding, 0.0f, HeaderPadding, 0.0f))
					[
						SNew(SNiagaraOutlinerTreeItemHeaderDataWidget<FText>)
						.ToolTipText(LOCTEXT("CulledEmittersHeaderTooltip", "State"))
						.Data(LOCTEXT("CulledEmittersHeaderValue", "Culled"))
					];
			}
			else
			{
				Box->AddSlot()
					.AutoWidth()
					.Padding(FMargin(HeaderPadding, 0.0f, HeaderPadding, 0.0f))
					[
						SNew(SNiagaraOutlinerTreeItemHeaderDataWidget<FText>)
						.ToolTipText(LOCTEXT("ComponentHeaderTooltip_ExecutionState", "Execution State"))
						.Data(ExecStateEnum->GetDisplayNameTextByValue((int32)Data->ActualExecutionState))
						.MinDesiredWidth(50.0f)
					];
			}

			Box->AddSlot()
				.AutoWidth()
				.Padding(FMargin(HeaderPadding, 0.0f, HeaderPadding, 0.0f))
				[
					SNew(SNiagaraOutlinerTreeItemHeaderDataWidget<int32>)
					.ToolTipText(LOCTEXT("VisibleEmittersHeaderTooltip", "Num emitters matching current search and filters."))
					.Data(NumMatchingEmitters)
				];

		}

		return Box;
	}
	return SNullWidget::NullWidget;
}

const void* FNiagaraOutlinerTreeEmitterItem::GetData()const
{
	FNiagaraEditorModule& NiagaraEditorModule = FModuleManager::GetModuleChecked<FNiagaraEditorModule>("NiagaraEditor");

	if (UNiagaraOutliner* Outliner = NiagaraEditorModule.GetDebugger()->GetOutliner())
	{
		TSharedPtr<FNiagaraOutlinerTreeItem> CompItem = GetParent();
		TSharedPtr<FNiagaraOutlinerTreeItem> SystemItem = CompItem->GetParent();
		TSharedPtr<FNiagaraOutlinerTreeItem> WorldItem = SystemItem->GetParent();

		const FString& EmitterName = GetFullName();
		const FString& ComponentName = CompItem->GetFullName();
		const FString& SystemName = SystemItem->GetFullName();
		const FString& WorldName = WorldItem->GetFullName();
		return Outliner->FindEmitterData(WorldName, SystemName, ComponentName, EmitterName);
	}
	return nullptr;
}

TSharedRef<SWidget> FNiagaraOutlinerTreeEmitterItem::GetHeaderWidget()
{
	if(FNiagaraOutlinerEmitterInstanceData* Data = (FNiagaraOutlinerEmitterInstanceData*) GetData())	
	{
		TSharedRef<SHorizontalBox> Box = SNew(SHorizontalBox);

		const FSlateBrush* SimTargetBrush = Data->SimTarget == ENiagaraSimTarget::CPUSim ? FNiagaraEditorStyle::Get().GetBrush("NiagaraEditor.Stack.CPUIcon") : FNiagaraEditorStyle::Get().GetBrush("NiagaraEditor.Stack.GPUIcon");

		UEnum* ExecStateEnum = StaticEnum<ENiagaraExecutionState>();

		Box->AddSlot()
			.AutoWidth()
			.Padding(FMargin(HeaderPadding, 0.0f, HeaderPadding, 0.0f))
			[
				SNew(SNiagaraOutlinerTreeItemHeaderDataWidget<FText>)
				.ToolTipText(LOCTEXT("EmitterHeaderTooltip_ExecState", "Execution State"))
				.Data(ExecStateEnum->GetDisplayNameTextByValue((int32)Data->ExecState))
				.MinDesiredWidth(50.0f)
			];
		
		Box->AddSlot()
			.AutoWidth()
			.Padding(FMargin(HeaderPadding, 0.0f, HeaderPadding, 0.0f))
			[
				SNew(SImage)
				.Image(SimTargetBrush)
				.ToolTipText(LOCTEXT("EmitterHeaderTooltip_SimTarget", "Sim Target"))
			];

		Box->AddSlot()
			.AutoWidth()
			.Padding(FMargin(HeaderPadding, 0.0f, HeaderPadding, 0.0f))
			[
				SNew(SNiagaraOutlinerTreeItemHeaderDataWidget<int32>)
				.ToolTipText(LOCTEXT("EmitterHeaderTooltip_NumParticles", "Num Particles"))
				.Data(Data->NumParticles)
				.MinDesiredWidth(50.0f)
			];
		return Box;
	}
	return SNullWidget::NullWidget;
}


//////////////////////////////////////////////////////////////////////////

void FNiagaraOutlinerCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	FNiagaraEditorModule& NiagaraEditorModule = FModuleManager::GetModuleChecked<FNiagaraEditorModule>("NiagaraEditor");
	TSharedPtr<FNiagaraDebugger> Debugger = NiagaraEditorModule.GetDebugger();
	if (UNiagaraOutliner * Outliner = Debugger->GetOutliner())
	{
		//Hide this property as it this data is displayed in the outliner tree.
		DetailBuilder.HideProperty(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UNiagaraOutliner, Data)));
		DetailBuilder.HideProperty(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UNiagaraOutliner, Settings)));
		DetailBuilder.HideProperty(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UNiagaraOutliner, Filters)));

		//DetailBuilder.HideCategory(TEXT("Outliner"));
		DetailBuilder.HideCategory(TEXT("Settings"));
		DetailBuilder.HideCategory(TEXT("Filters"));

		TSharedRef<IPropertyHandle> OutlinerDataProperty = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UNiagaraOutliner, Data));
		IDetailCategoryBuilder& OutlinerCategory = DetailBuilder.EditCategory("Outliner", FText::GetEmpty(), ECategoryPriority::Important);
		OutlinerCategory.AddProperty(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UNiagaraOutliner, Settings)));
		OutlinerCategory.AddProperty(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UNiagaraOutliner, Filters)));
	}
}

#undef LOCTEXT_NAMESPACE

void FNiagaraOutlinerWorldDetailsCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
// 	HeaderRow
// 		.NameContent()
// 		[
// 			StructPropertyHandle->CreatePropertyNameWidget()
// 		]
// 	.ValueContent()
// 		[
// 			StructPropertyHandle->CreatePropertyValueWidget()
// 		];
}

void FNiagaraOutlinerWorldDetailsCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, class IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	FNiagaraEditorModule& NiagaraEditorModule = FModuleManager::GetModuleChecked<FNiagaraEditorModule>("NiagaraEditor");

	uint32 NumChildren = 0;
	StructPropertyHandle->GetNumChildren(NumChildren);

	for (uint32 ChildNum = 0; ChildNum < NumChildren; ++ChildNum)
	{
		TSharedPtr<IPropertyHandle> ChildProperty = StructPropertyHandle->GetChildHandle(ChildNum);

		if (ChildProperty->GetProperty()->GetFName() == GET_MEMBER_NAME_CHECKED(FNiagaraOutlinerWorldData, Systems))
		{
			ChildBuilder.AddProperty(ChildProperty.ToSharedRef());
		}
	}
}