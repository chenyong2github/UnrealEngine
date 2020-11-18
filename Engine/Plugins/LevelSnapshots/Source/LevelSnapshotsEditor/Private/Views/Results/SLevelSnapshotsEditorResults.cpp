// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/Results/SLevelSnapshotsEditorResults.h"

#include "LevelSnapshot.h"
#include "PropertySnapshot.h"
#include "LevelSnapshotsEditorStyle.h"
#include "ILevelSnapshotsEditorView.h"
#include "LevelSnapshotsLog.h"
#include "Views/Results/LevelSnapshotsEditorResults.h"
#include "Widgets/SLevelSnapshotsEditorResultsGroup.h"
#include "LevelSnapshotsEditorData.h"
#include "LevelSnapshotsFunctionLibrary.h"

#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Views/SExpanderArrow.h"
#include "Widgets/Input/SComboButton.h"

#include "Types/ISlateMetaData.h"
#include "IPropertyRowGenerator.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "IDetailTreeNode.h"
#include "PropertyHandle.h"
#include "PropertyCustomizationHelpers.h"
#include "PropertyEditor/Private/PropertyEditorHelpers.h"
#include "EditorStyleSet.h"
#include "Components/SlateWrapperTypes.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

#include "DiffUtils.h"

#define LOCTEXT_NAMESPACE "LevelSnapshotsEditor"

namespace LevelSnapshotsEditorResultsUtil
{
	bool FindPropertyHandleRecursive(const TSharedPtr<IPropertyHandle>& PropertyHandle, const FString& QualifiedPropertyName, bool bRequiresMatchingPath)
	{
		if (PropertyHandle && PropertyHandle->IsValidHandle())
		{
			uint32 ChildrenCount = 0;
			PropertyHandle->GetNumChildren(ChildrenCount);
			for (uint32 Index = 0; Index < ChildrenCount; ++Index)
			{
				TSharedPtr<IPropertyHandle> ChildHandle = PropertyHandle->GetChildHandle(Index);
				if (FindPropertyHandleRecursive(ChildHandle, QualifiedPropertyName, bRequiresMatchingPath))
				{
					return true;
				}
			}

			if (PropertyHandle->GetProperty())
			{
				if (bRequiresMatchingPath)
				{
					if (PropertyHandle->GeneratePathToProperty() == QualifiedPropertyName)
					{
						return true;
					}
				}
				else if (PropertyHandle->GetProperty()->GetName() == QualifiedPropertyName)
				{
					return true;
				}
			}
		}

		return false;
	}

	TSharedPtr<IDetailTreeNode> FindTreeNodeRecursive(const TSharedRef<IDetailTreeNode>& RootNode, const FString& QualifiedPropertyName, bool bRequiresMatchingPath)
	{
		TArray<TSharedRef<IDetailTreeNode>> Children;
		RootNode->GetChildren(Children);
		for (TSharedRef<IDetailTreeNode>& Child : Children)
		{
			TSharedPtr<IDetailTreeNode> FoundNode = FindTreeNodeRecursive(Child, QualifiedPropertyName, bRequiresMatchingPath);
			if (FoundNode.IsValid())
			{
				return FoundNode;
			}
		}

		TSharedPtr<IPropertyHandle> Handle = RootNode->CreatePropertyHandle();
		if (FindPropertyHandleRecursive(Handle, QualifiedPropertyName, bRequiresMatchingPath))
		{
			return RootNode;
		}

		return nullptr;
	}

	/** Find a node by its name in a detail tree node hierarchy. */
	TSharedPtr<IDetailTreeNode> FindNode(const TArray<TSharedRef<IDetailTreeNode>>& RootNodes, const FString& QualifiedPropertyName, bool bRequiresMatchingPath)
	{
		for (const TSharedRef<IDetailTreeNode>& CategoryNode : RootNodes)
		{
			TSharedPtr<IDetailTreeNode> FoundNode = FindTreeNodeRecursive(CategoryNode, QualifiedPropertyName, bRequiresMatchingPath);
			if (FoundNode.IsValid())
			{
				return FoundNode;
			}
		}

		return nullptr;
	}
}

SLevelSnapshotsEditorResults::~SLevelSnapshotsEditorResults()
{
	ActorObjects.Empty();
}

void SLevelSnapshotsEditorResults::Construct(const FArguments& InArgs, const TSharedRef<FLevelSnapshotsEditorResults>& InEditorResults, const TSharedRef<FLevelSnapshotsEditorViewBuilder>& InBuilder)
{
	EditorResultsPtr = InEditorResults;
	BuilderPtr = InBuilder;

	InBuilder->OnSnapshotSelected.Add(FLevelSnapshotsEditorViewBuilder::FOnSnapshotSelectedDelegate::CreateSP(this, &SLevelSnapshotsEditorResults::OnSnapshotSelected));

	ChildSlot
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(5.f, 10.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2.f, 0.f)
				[
					SNew(SButton)
					.VAlign(VAlign_Center)
					.OnClicked(this, &SLevelSnapshotsEditorResults::SetAllGroupsCollapsed)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("CollapseAll", "Collapse All"))
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2.f, 0.f)
				[
					SNew(SButton)
					.VAlign(VAlign_Center)
					.OnClicked(this, &SLevelSnapshotsEditorResults::SetAllGroupsUnselected)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("UnselectAll", "Unselect All"))
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2.f, 0.f)
				[
					SNew(SButton)
					.VAlign(VAlign_Center)
					.OnClicked(this, &SLevelSnapshotsEditorResults::SetAllGroupsSelected)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("SelectAll", "Select All"))
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2.f, 0.f)
				[
					SNew( SComboButton )
					.ComboButtonStyle( FEditorStyle::Get(), "GenericFilters.ComboButtonStyle" )
					.ForegroundColor(FLinearColor::White)
					.ContentPadding(0)
					.ToolTipText( LOCTEXT( "AddFilterToolTip", "Result Filters" ) )
					.OnGetMenuContent( this, &SLevelSnapshotsEditorResults::MakeAddFilterMenu )
					.HasDownArrow( true )
					.ContentPadding( FMargin( 1, 0 ) )
					.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("ContentBrowserFiltersCombo")))
					.Visibility(EVisibility::Visible)
					.ButtonContent()
					[
						SNew(SHorizontalBox)

						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(STextBlock)
							.TextStyle(FEditorStyle::Get(), "GenericFilters.TextStyle")
							.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.9"))
							.Text(FText::FromString(FString(TEXT("\xf0b0"))) /*fa-filter*/)
						]

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(2,0,0,0)
						[
							SNew(STextBlock)
							.TextStyle(FEditorStyle::Get(), "GenericFilters.TextStyle")
							.Text(LOCTEXT("Filters", "Filters"))
						]
					]
				]
				+ SHorizontalBox::Slot()
				.HAlign(HAlign_Right)
				.Padding(2.f, 0.f)
				[
					SNew(SHorizontalBox)
					+SHorizontalBox::Slot()
					.Padding(2.f, 0.f)
					.AutoWidth()
					[
						SNew(SCheckBox)
						.IsChecked(false)
						.OnCheckStateChanged(this, &SLevelSnapshotsEditorResults::OnCheckedStateChange_ShowUnchangedSnapshotActors)
					]

					+SHorizontalBox::Slot()
					.Padding(2.f, 0.f)
					.AutoWidth()
					[
						SNew(SBorder)
						.Padding(FMargin(15.0f, 5.0f))
						.BorderImage(FLevelSnapshotsEditorStyle::GetBrush("LevelSnapshotsEditor.BrightBorder"))
						.HAlign(HAlign_Center)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("LevelSnapshotsEditorResults_ShowUnchanged", "Show Unchanged"))
						]
					]
				]
			]

			+ SVerticalBox::Slot()
			[
				SAssignNew(ResultList, STreeView<TSharedPtr<FLevelSnapshotsEditorResultsRow>>)
				.TreeItemsSource(reinterpret_cast<TArray<TSharedPtr<FLevelSnapshotsEditorResultsRow>>*>(&FieldGroups))
				.ItemHeight(24.0f)
				.OnGenerateRow(this, &SLevelSnapshotsEditorResults::OnGenerateRow)
				.OnGetChildren(this, &SLevelSnapshotsEditorResults::OnGetGroupChildren)
				.OnSelectionChanged(this, &SLevelSnapshotsEditorResults::OnSelectionChanged)
				.ClearSelectionOnClick(false)
			]
		];

	Refresh();
}

TSharedRef<ITableRow> SLevelSnapshotsEditorResults::OnGenerateRow(TSharedPtr<FLevelSnapshotsEditorResultsRow> InResultsRow, const TSharedRef<STableViewBase>& OwnerTable)
{
	if (InResultsRow->GetType() == FLevelSnapshotsEditorResultsRow::Group)
	{
		if (TSharedPtr<FLevelSnapshotsEditorResultsRowGroup> RowGroup = StaticCastSharedPtr<FLevelSnapshotsEditorResultsRowGroup>(InResultsRow))
		{
			return SNew(SLevelSnapshotsEditorResultsRowGroup, OwnerTable, RowGroup.ToSharedRef(), SharedThis<SLevelSnapshotsEditorResults>(this));
		}
	}
	else if (InResultsRow->GetType() == FLevelSnapshotsEditorResultsRow::Field)
	{
		return SNew(STableRow<TSharedPtr<SWidget>>, OwnerTable)
			[
				InResultsRow->AsField().ToSharedRef()
			];
	}

	return SNew(STableRow<TSharedPtr<SWidget>>, OwnerTable)
		.Padding(FMargin(30.f, 0.f, 0.f, 0.f))
		.ShowSelection(false)
		[
			SNew(STextBlock).Text(LOCTEXT("Results", "Results"))
		];
}

void SLevelSnapshotsEditorResults::OnGetGroupChildren(TSharedPtr<FLevelSnapshotsEditorResultsRow> InResultsRow, TArray<TSharedPtr<FLevelSnapshotsEditorResultsRow>>& OutRows)
{
	if (InResultsRow.IsValid())
	{
		InResultsRow->GetNodeChildren(OutRows);
	}
}

void SLevelSnapshotsEditorResults::OnSelectionChanged(TSharedPtr<FLevelSnapshotsEditorResultsRow> InResultsRow, ESelectInfo::Type SelectInfo)
{
}

void SLevelSnapshotsEditorResults::Refresh()
{
	RefreshGroups();
}

void SLevelSnapshotsEditorResults::RefreshGroups()
{
	ResultList->RequestListRefresh();
}

#pragma optimize("", off)

/** Recursively create a property widget. */
static TSharedRef<SWidget> CreatePropertyWidget(TSharedPtr<FLevelSnapshotsEditorResultsRowGroup> InRowGroup, const TSharedPtr<IDetailTreeNode>& Node, bool bGenerateChildren = false)
{
	FNodeWidgets NodeWidgets = Node->CreateNodeWidgets();

	NodeWidgets.ValueWidget->SetEnabled(false);

	TSharedRef<SHorizontalBox> FieldWidget = SNew(SHorizontalBox);
	InRowGroup->FieldWidgetHBoxes.Add(FieldWidget);

	if (NodeWidgets.NameWidget && NodeWidgets.ValueWidget)
	{
		FieldWidget->AddSlot()
			.Padding(FMargin(3.0f, 2.0f))
			.AutoWidth()
			[
				NodeWidgets.NameWidget.ToSharedRef()
			];

		FieldWidget->AddSlot()
			.Padding(FMargin(3.0f, 2.0f))
			.HAlign(HAlign_Right)
			.AutoWidth()
			[
				NodeWidgets.ValueWidget.ToSharedRef()
			];
	}
	else if (NodeWidgets.WholeRowWidget)
	{
		FieldWidget->AddSlot()
			.Padding(FMargin(3.0f, 2.0f))
			.AutoWidth()
			[
				NodeWidgets.WholeRowWidget.ToSharedRef()
			];
	}

	InRowGroup->Fields.Add(SNew(SLevelSnapshotsEditorResultsField, FieldWidget));

	if (bGenerateChildren)
	{
		TArray<TSharedRef<IDetailTreeNode>> ChildNodes;
		Node->GetChildren(ChildNodes);

		for (const TSharedRef<IDetailTreeNode>& ChildNode : ChildNodes)
		{
			CreatePropertyWidget(InRowGroup, ChildNode);
		}
	}

	return FieldWidget;
}

void SLevelSnapshotsEditorResults::OnSnapshotSelected(ULevelSnapshot* InLevelSnapshot)
{
	FieldGroups.Empty();
	ActorSnapshotCounterpartMap.Empty();

	if (InLevelSnapshot != nullptr)
	{
		// Saving a reference to the selected LevelSnapshot for diffing
		SelectedLevelSnapshotPtr = InLevelSnapshot;

		// TODO: Get World from world selection combobox
		ULevelSnapshot* CurrentLevelSnapshot = ULevelSnapshotsFunctionLibrary::TakeLevelSnapshot(GEditor->GetEditorWorldContext().World());
		ProvisionSnapshotActors(CurrentLevelSnapshot, true);
		FieldGroups.Empty();
		ProvisionSnapshotActors(InLevelSnapshot, false);

		RefreshGroups();

		// TODO: Mark the CurrentLevelSnapshot for destruction
	}
}

void SLevelSnapshotsEditorResults::ProvisionSnapshotActors(ULevelSnapshot* InLevelSnapshot, bool bFromCurrentLevel)
{
	TSharedPtr<FLevelSnapshotsEditorViewBuilder> Builder = BuilderPtr.Pin();
	FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	
	for (const TPair<FString, FLevelSnapshot_Actor>& ActorSnapshotPair : InLevelSnapshot->ActorSnapshots)
	{
		FString ActorPath = ActorSnapshotPair.Key;
		FSoftObjectPath ActorSoftPath = ActorSnapshotPair.Value.Base.SoftObjectPath;
		
		TSharedPtr<FLevelSnapshotsEditorResultsRowGroup> NewGroup = MakeShared<FLevelSnapshotsEditorResultsRowGroup>(ActorPath, ActorSnapshotPair.Value);
		{
			AActor* ActorObject = ActorSnapshotPair.Value.GetDeserializedActor();

			if (!ActorObject)
			{
				UE_LOG(LogLevelSnapshots, Error, TEXT("Couldn't deserialize actor from %s"), *ActorPath);
				continue; // Skip this line if ActorObject is not available
			}
			
			TStrongObjectPtr<UObject> ActorObjectPtr = TStrongObjectPtr<UObject>(ActorObject);
			ActorObjects.Add(ActorObjectPtr);

			FPropertyRowGeneratorArgs GeneratorArgs;
			TSharedPtr<IPropertyRowGenerator> RowGenerator = PropertyEditorModule.CreatePropertyRowGenerator(GeneratorArgs);
			Generators.Add(RowGenerator);

			TArray<UObject*> Objects;
			Objects.Add(ActorObjectPtr.Get());
			RowGenerator->SetObjects(Objects);

			for (const TPair<FName, FLevelSnapshot_Property>& PropertyPair : ActorSnapshotPair.Value.Base.Properties)
			{
				FString PropertyString = PropertyPair.Key.ToString();

				// Check should we expose this property from filtered list
				if (TSharedPtr<IDetailTreeNode> Node = LevelSnapshotsEditorResultsUtil::FindNode(RowGenerator->GetRootTreeNodes(), PropertyString, true))
				{
					/*ExposedFieldWidgets.Add(SAssignNew(ExposedFieldWidget, SRCPanelExposedField, RCProperty, RowGenerator, WeakPanel)
							.EditMode_Raw(this, &SRemoteControlTarget::GetPanelEditMode));*/

					TSharedRef<SWidget> PropertyWidget = CreatePropertyWidget(NewGroup, Node, true);

					TArray<FSoftObjectPath> ActorSnapshotCounterpartKeys;
					ActorSnapshotCounterpartMap.GetKeys(ActorSnapshotCounterpartKeys);
					
					if (bFromCurrentLevel && Node.Get()->GetRow().IsValid() && Node.Get()->GetRow()->GetPropertyHandle().IsValid())
					{
						// If we already have this actor in the TMap
						if (ActorSnapshotCounterpartKeys.Num() > 0 && ActorSnapshotCounterpartKeys.Contains(ActorSoftPath))
						{
							ActorSnapshotCounterpartMap[ActorSoftPath].CurrentLevelProperties.Add(PropertyString, Node.Get()->GetRow()->GetPropertyHandle());
						}
						else // Otherwise add a new entry entirely
						{
							ActorSnapshotCounterpartMap.Add(ActorSoftPath, FActorSnapshotCounterpartInfo(PropertyString, Node.Get()->GetRow()->GetPropertyHandle()));
						}
					}
					else
					{
						if (ActorSnapshotCounterpartKeys.Contains(ActorSoftPath))
						{
							NewGroup->AddCurrentPropertyWidget(ActorSnapshotCounterpartMap[ActorSoftPath].CurrentLevelProperties[PropertyString]);
						}
						else
						{
							UE_LOG(LogLevelSnapshots, Warning, TEXT("Actor's UniqueID from path '%s' not in ActorSnapshotCounterpartKeys and therefore not in current level"), *ActorPath);
						}
					}
				}
				else
				{
					UE_LOG(LogLevelSnapshots, Warning, TEXT("Node not in the list; PropertyString is %s"), *PropertyString);
				}
			}
		}

		FieldGroups.Add(NewGroup);
	}
}

FReply SLevelSnapshotsEditorResults::SetAllGroupsSelected()
{
	for (TSharedPtr<FLevelSnapshotsEditorResultsRowGroup, ESPMode::Fast> Group : FieldGroups)
	{
		if (Group->GetType() == FLevelSnapshotsEditorResultsRow::Group)
		{
			TSharedPtr<SLevelSnapshotsEditorResultsRowGroup> GroupWidget = StaticCastSharedPtr<
				SLevelSnapshotsEditorResultsRowGroup>(ResultList->WidgetFromItem(Group));
			if (GroupWidget->CheckboxPtr.IsValid())
			{
				GroupWidget->CheckboxPtr->SetIsChecked(false);
			}
		}
	}

	return FReply::Handled();
}

FReply SLevelSnapshotsEditorResults::SetAllGroupsUnselected()
{
	for (TSharedPtr<FLevelSnapshotsEditorResultsRowGroup, ESPMode::Fast> Group : FieldGroups)
	{
		if (Group->GetType() == FLevelSnapshotsEditorResultsRow::Group)
		{
			TSharedPtr<SLevelSnapshotsEditorResultsRowGroup> GroupWidget = StaticCastSharedPtr<
				SLevelSnapshotsEditorResultsRowGroup>(ResultList->WidgetFromItem(Group));
			if (GroupWidget->CheckboxPtr.IsValid())
			{
				GroupWidget->CheckboxPtr->SetIsChecked(false);
			}
		}
	}

	return FReply::Handled();
}

FReply SLevelSnapshotsEditorResults::SetAllGroupsCollapsed()
{
	ResultList->ClearExpandedItems();

	return FReply::Handled();
}

void SLevelSnapshotsEditorResults::OnCheckedStateChange_ShowUnchangedSnapshotActors(ECheckBoxState NewState)
{

	if (NewState == ECheckBoxState::Checked)
	{
		SetShowUnchangedSnapshotGroups(true);
	}
	else
	{
		SetShowUnchangedSnapshotGroups(false);
	}
}

void SLevelSnapshotsEditorResults::SetShowUnchangedSnapshotGroups(const bool bShowGroups)
{
	TArray<TSharedPtr<FLevelSnapshotsEditorResultsRowGroup>> UnchangedGroups;

	if (!bShowGroups)
	{
		// If we want to hide unchanged groups we first have to determine what they are
		UnchangedGroups = DetermineUnchangedGroupsFromLevelSnapshot(SelectedLevelSnapshotPtr.Get());
	}
	
	for (TSharedPtr<FLevelSnapshotsEditorResultsRowGroup, ESPMode::Fast> Group : FieldGroups)
	{
		if (Group->GetType() == FLevelSnapshotsEditorResultsRow::Group)
		{
			TSharedPtr<SLevelSnapshotsEditorResultsRowGroup> GroupWidget = StaticCastSharedPtr<
				SLevelSnapshotsEditorResultsRowGroup>(ResultList->WidgetFromItem(Group));

			EVisibility NewGroupVisibility = EVisibility::SelfHitTestInvisible;

			if (!bShowGroups)
			{
				if (UnchangedGroups.Num() > 0 && UnchangedGroups.ContainsByPredicate(
					[Group](const TSharedPtr<FLevelSnapshotsEditorResultsRowGroup> G)
					{
						return G->ObjectPath.Equals(Group->ObjectPath);
				}))
				{
					NewGroupVisibility = EVisibility::Collapsed;
				}
			}
			
			GroupWidget->SetVisibility(NewGroupVisibility);
		}
	}
}

TArray<TSharedPtr<FLevelSnapshotsEditorResultsRowGroup>> SLevelSnapshotsEditorResults::DetermineUnchangedGroupsFromLevelSnapshot(
	ULevelSnapshot* InLevelSnapshot)
{
	TArray<TSharedPtr<FLevelSnapshotsEditorResultsRowGroup>> UnchangedGroups;

	// Create transient ULevelSnapshot to compare against InLevelSnapshot
	ULevelSnapshot* TempLevelSnapshot = ULevelSnapshotsFunctionLibrary::TakeLevelSnapshot(GEditor->GetEditorWorldContext().World());

	if (!TempLevelSnapshot || !InLevelSnapshot)
	{
		UE_LOG(LogLevelSnapshots, Warning, TEXT("Unable to Diff snapshots as at least one snapshot was invalid"));
		return UnchangedGroups;
	}

	for (const TPair<FString, FLevelSnapshot_Actor>& SnapshotPair : TempLevelSnapshot->ActorSnapshots)
	{
		const FString& FirstSnapshotPathName = SnapshotPair.Key;
		const FLevelSnapshot_Actor& FirstActorSnapshot = SnapshotPair.Value;

		if (const FLevelSnapshot_Actor* SecondActorSnapshot = InLevelSnapshot->ActorSnapshots.Find(FirstSnapshotPathName))
		{
			// Actor paths match, so let's compare properties

			AActor* FirstActor = FirstActorSnapshot.GetDeserializedActor();
			AActor* SecondActor = SecondActorSnapshot->GetDeserializedActor();

			if (FirstActor && SecondActor)
			{
				TArray<FSingleObjectDiffEntry> DifferingProperties;
				DiffUtils::CompareUnrelatedObjects(FirstActor, SecondActor, DifferingProperties);

				if (DifferingProperties.Num() < 1)
				{
					// This means there is no difference between the matching actors. It's unchanged since the last snapshot.
					// We add it to the TArray by finding a candidate from FieldGroups whose ObjectPath matches the path of this ActorSnapshot

					TSharedPtr<FLevelSnapshotsEditorResultsRowGroup> Group = FieldGroups[FieldGroups.IndexOfByPredicate(
						[FirstSnapshotPathName] (const TSharedPtr<FLevelSnapshotsEditorResultsRowGroup> G)
					{
							return G->ObjectPath.Equals(FirstSnapshotPathName);
					})];

					UnchangedGroups.Add(Group);
				}
			}
		}
		else
		{
			UE_LOG(LogLevelSnapshots, Warning, TEXT("%s exists in the First snapshot but not the Second."), *FirstSnapshotPathName);
		}
	}
	
	return UnchangedGroups;
}

TSharedRef<SWidget> SLevelSnapshotsEditorResults::MakeAddFilterMenu()
{
	FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection=*/true, nullptr, nullptr, /*bCloseSelfOnly=*/true);

	MenuBuilder.BeginSection("BasicFilters");
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("Filter1", "Filter1"),
			LOCTEXT("FilterTooltipPrefix", "FilterTooltipPrefix"),
			FSlateIcon(),
			FUIAction()
			);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("Filter2", "Filter2"),
			LOCTEXT("FilterTooltipPrefix", "FilterTooltipPrefix"),
			FSlateIcon(),
			FUIAction()
			);
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

void SLevelSnapshotsEditorResultsRowGroup::Tick(const FGeometry&, const double, const float)
{
}

void SLevelSnapshotsEditorResultsRowGroup::Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView, const TSharedPtr<FLevelSnapshotsEditorResultsRowGroup>& FieldGroup, const TSharedPtr<SLevelSnapshotsEditorResults>& OwnerPanel)
{
	ChildSlot
		[
			SNew(SBox)
			.Padding(FMargin(0.0f, 2.0f))
			[
				SNew(SBorder)
				.Padding(FMargin(5.0f, 8.0f))
				.BorderImage(FLevelSnapshotsEditorStyle::GetBrush("LevelSnapshotsEditor.GroupBorder"))
				.VAlign(VAlign_Fill)
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot()
					.VAlign(VAlign_Fill)
					.HAlign(HAlign_Center)
					.Padding(FMargin(4.0f, 0.0f))
					.AutoWidth()
					[
						SAssignNew(ExpanderArrowPtr, SExpanderArrow, SharedThis(this))
					]

					+ SHorizontalBox::Slot()
					.Padding(FMargin(4.0f, 0.0f))
					.AutoWidth()
					[
						SAssignNew(CheckboxPtr, SCheckBox)
						.IsChecked(true)
					]

					+ SHorizontalBox::Slot()
					.Padding(FMargin(10.0f, 0.0f))
					[
						SNew(SLevelSnapshotsEditorResultsGroup, FieldGroup->ActorSnapshot.Base.ObjectName.ToString(), FieldGroup->ActorSnapshot)
					]
				]
			]
		];


	STableRow<TSharedPtr<FLevelSnapshotsEditorResultsRowGroup>>::ConstructInternal(
		STableRow::FArguments()
		.ShowSelection(false),
		InOwnerTableView
	);
}

void FLevelSnapshotsEditorResultsRowGroup::GetNodeChildren(TArray<TSharedPtr<FLevelSnapshotsEditorResultsRow>>& OutChildren)
{
	OutChildren.Append(Fields);
}

void FLevelSnapshotsEditorResultsRowGroup::AddCurrentPropertyWidget(const TSharedPtr<IPropertyHandle> InPropertyHandle)
{
	const int32 FieldWidgetIndex = FieldWidgetHBoxes.Num() - 1;
	
	if (FieldWidgetIndex < 0)
	{
		UE_LOG(LogLevelSnapshots, Error, TEXT("SLevelSnapshotsEditorResults::AddCurrentPropertyWidget: Length of FieldWidgetHBoxes is 0."))
		return;
	}
	
	TSharedRef<SHorizontalBox> FieldWidget = FieldWidgetHBoxes[FieldWidgetIndex];
	TSharedPtr<SProperty> CurrentPropertyWidget;

	// Add separation between snapshot value and current value
	FieldWidget->AddSlot()
		.Padding(FMargin(3.0f, 2.0f))
		.AutoWidth()
		[
			SNew(STextBlock)
			.Text(INVTEXT("| "))
		];
	FieldWidget->AddSlot()
		.Padding(FMargin(3.0f, 2.0f))
		.AutoWidth()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("CurrentValue", "Current: "))
		];
	FieldWidget->AddSlot()
		.Padding(FMargin(3.0f, 2.0f))
		.AutoWidth()
		[
			SAssignNew(CurrentPropertyWidget, SProperty, InPropertyHandle)
			.ShouldDisplayName(false)
			.IsEnabled(false)
		];
}

void SLevelSnapshotsEditorResultsField::Construct(const FArguments& InArgs, const TSharedRef<SWidget>& InContent)
{
	ChildSlot
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SAssignNew(CheckboxPtr, SCheckBox)
				.IsChecked(true)
			]

			+ SHorizontalBox::Slot()
			[
				InContent
			]
		];
}

TSharedPtr<SLevelSnapshotsEditorResultsField> SLevelSnapshotsEditorResultsField::AsField()
{
	return SharedThis(this);
}

#pragma optimize("", on)

#undef LOCTEXT_NAMESPACE
