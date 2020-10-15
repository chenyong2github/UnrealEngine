// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/Results/SLevelSnapshotsEditorResults.h"

#include "ActorSnapshot.h"
#include "PropertySnapshot.h"
#include "ILevelSnapshotsEditorView.h"
#include "Views/Results/LevelSnapshotsEditorResults.h"
#include "Widgets/SLevelSnapshotsEditorResultsGroup.h"
#include "LevelSnapshotsEditorData.h"

#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"

#include "IPropertyRowGenerator.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "IDetailTreeNode.h"
#include "PropertyHandle.h"
#include "ISinglePropertyView.h"

#define LOCTEXT_NAMESPACE "LevelSnapshotsEditor"

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
			return SNew(STableRow<TSharedPtr<SWidget>>, OwnerTable)
				.Padding(FMargin(30.f, 0.f, 0.f, 0.f))
				.ShowSelection(false)
				[
					SNew(SLevelSnapshotsEditorResultsGroup, RowGroup->ObjectPath, RowGroup->ActorSnapshot)
				];
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

	TSharedRef<SHorizontalBox> FieldWidget = SNew(SHorizontalBox);

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

	//VerticalWrapper->AddSlot()
	//	.AutoHeight()
	//	[
	//		FieldWidget
	//	];

	InRowGroup->Fields.Add(SNew(SLevelSnapshotsEditorResultsField, FieldWidget));

	if (bGenerateChildren)
	{
		TArray<TSharedRef<IDetailTreeNode>> ChildNodes;
		Node->GetChildren(ChildNodes);

		for (const TSharedRef<IDetailTreeNode>& ChildNode : ChildNodes)
		{
			//VerticalWrapper->AddSlot()
			//	.AutoHeight()
			//	.Padding(5.0f, 0.0f)
			//	[
			CreatePropertyWidget(InRowGroup, ChildNode);
			//	];
		}
	}

	//return VerticalWrapper;
	return FieldWidget;
}

void SLevelSnapshotsEditorResults::OnSnapshotSelected(ULevelSnapshot* InLevelSnapshot)
{
	FieldGroups.Empty();

	TSharedPtr<FLevelSnapshotsEditorViewBuilder> Builder = BuilderPtr.Pin();
	FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");


	if (InLevelSnapshot != nullptr)
	{
		for (const TPair<FString, FActorSnapshot>& ActorSnapshotPair : InLevelSnapshot->ActorSnapshots)
		{
			TSharedPtr<FLevelSnapshotsEditorResultsRowGroup> NewGroup = MakeShared<FLevelSnapshotsEditorResultsRowGroup>(ActorSnapshotPair.Key, ActorSnapshotPair.Value);

			{
				// That showld be in parent tree item
				//UObject* ActorObject = FindObject<UObject>(Builder->EditorContextPtr.Pin()->Get(), *ActorSnapshotPair.Key);
				//UObject* ActorObject = FindObject<UObject>(nullptr, *ActorSnapshotPair.Key);
				//if (ActorObject == nullptr)
				//{
				//	ActorObject = LoadObject<UObject>(nullptr, *ActorSnapshotPair.Key);
				//}


				UObject* ActorObject = ActorSnapshotPair.Value.GetDeserializedActor();

				TStrongObjectPtr<UObject> ActorObjectPtr = TStrongObjectPtr<UObject>(ActorObject);

				TStrongObjectPtr<ULevelSnapshotsEditorDataResults> TestDataResults = 
					TStrongObjectPtr<ULevelSnapshotsEditorDataResults>(NewObject<ULevelSnapshotsEditorDataResults>(Builder->EditorContextPtr.Pin()->Get(), "MyMagicObject", RF_Transactional | RF_DefaultSubObject | RF_WasLoaded | RF_LoadCompleted));
				ActorObjects.Add(TestDataResults);
				ActorObjects.Add(ActorObjectPtr);


				// Test details view
				//{
				//	// Create a property view
				//	FPropertyEditorModule& EditModule = FModuleManager::Get().GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

				//	FDetailsViewArgs DetailsViewArgs(
				//		/*bUpdateFromSelection=*/ false,
				//		/*bLockable=*/ false,
				//		/*bAllowSearch=*/ true,
				//		FDetailsViewArgs::HideNameArea,
				//		/*bHideSelectionTip=*/ true,
				//		/*InNotifyHook=*/ nullptr,
				//		/*InSearchInitialKeyFocus=*/ false,
				//		/*InViewIdentifier=*/ NAME_None);
				//	DetailsViewArgs.DefaultsOnlyVisibility = EEditDefaultsOnlyNodeVisibility::Automatic;

				//	TSharedRef<IDetailsView> PropertyView = EditModule.CreateDetailView(DetailsViewArgs);
				//	PropertyView->SetObject(ActorObject);
				//	NewGroup->Fields.Add(SNew(SLevelSnapshotsEditorResultsField, PropertyView));
				//}


				//for (const TPair<FName, FInternalPropertySnapshot>& PropertyPair : ActorSnapshotPair.Value.Properties)
				//{
				//	TSharedPtr<ISinglePropertyView> SinglePropertyView = PropertyEditorModule.CreateSingleProperty(ActorObject, PropertyPair.Key, FSinglePropertyParams());
				//	if (SinglePropertyView.IsValid())
				//	{
				//		NewGroup->Fields.Add(SNew(SLevelSnapshotsEditorResultsField, SinglePropertyView.ToSharedRef()));
				//	}
				//}


				FPropertyRowGeneratorArgs GeneratorArgs;
				TSharedPtr<IPropertyRowGenerator> RowGenerator = PropertyEditorModule.CreatePropertyRowGenerator(GeneratorArgs);
				Generators.Add(RowGenerator);

				TArray<UObject*> Objects;
				Objects.Add(ActorObjectPtr.Get());
				RowGenerator->SetObjects(Objects);

				const TArray<TSharedRef<IDetailTreeNode>>& RootNodes = RowGenerator->GetRootTreeNodes();

				for (const TSharedRef<IDetailTreeNode>& CategoryNode : RootNodes)
				{
					TArray<TSharedRef<IDetailTreeNode>> ChildNodes;
					CategoryNode->GetChildren(ChildNodes);

					for (const TSharedRef<IDetailTreeNode>& ChildNode : ChildNodes)
					{
						CreatePropertyWidget(NewGroup, ChildNode, true);
					}
				}
			}

			FieldGroups.Add(NewGroup);
		}

		RefreshGroups();
	}
}
void SLevelSnapshotsEditorResultsRowGroup::Tick(const FGeometry&, const double, const float)
{
}

void SLevelSnapshotsEditorResultsRowGroup::Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView, const TSharedPtr<FLevelSnapshotsEditorResultsRowGroup>& FieldGroup, const TSharedPtr<SLevelSnapshotsEditorResults>& OwnerPanel)
{
}

void FLevelSnapshotsEditorResultsRowGroup::GetNodeChildren(TArray<TSharedPtr<FLevelSnapshotsEditorResultsRow>>& OutChildren)
{
	OutChildren.Append(Fields);
}

void SLevelSnapshotsEditorResultsField::Construct(const FArguments& InArgs, const TSharedRef<SWidget>& InContent)
{
	ChildSlot
		[
			InContent
		];
}

TSharedPtr<SLevelSnapshotsEditorResultsField> SLevelSnapshotsEditorResultsField::AsField()
{
	return SharedThis(this);
}

#pragma optimize("", on)

#undef LOCTEXT_NAMESPACE
