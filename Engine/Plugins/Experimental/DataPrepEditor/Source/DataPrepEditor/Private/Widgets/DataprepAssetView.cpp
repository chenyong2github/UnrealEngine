// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DataprepAssetView.h"

#include "DataPrepAsset.h"
#include "DataPrepEditor.h"

#include "DataPrepContentConsumer.h"
#include "DataprepEditorStyle.h"
#include "DataprepWidgets.h"
#include "SAssetsPreviewWidget.h"

#include "Engine/SCS_Node.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "IDetailTreeNode.h"
#include "K2Node_AddComponent.h"
#include "PropertyEditorModule.h"

#include "Modules/ModuleManager.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "DataprepAssetView"

const float IndentSize = 12;
const char* DataprepTabsFontName = "FontAwesome.11";

// Inspired from SKismetInspector::Construct
void SGraphNodeDetailsWidget::Construct(const FArguments& InArgs)
{
	FNotifyHook* NotifyHook = nullptr;

	// Create a property view
	FPropertyEditorModule& EditModule = FModuleManager::Get().GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

	FDetailsViewArgs::ENameAreaSettings NameAreaSettings = FDetailsViewArgs::ENameAreaSettings::HideNameArea;
	FDetailsViewArgs DetailsViewArgs( /*bUpdateFromSelection=*/ false, /*bLockable=*/ false, /*bAllowSearch=*/ true, NameAreaSettings, /*bHideSelectionTip=*/ true, /*InNotifyHook=*/ NotifyHook, /*InSearchInitialKeyFocus=*/ false, NAME_None);
	DetailsViewArgs.bShowOptions = false;
	DetailsViewArgs.bShowPropertyMatrixButton = false;

	PropertyView = EditModule.CreateDetailView(DetailsViewArgs);

	//PropertyView->OnFinishedChangingProperties().Add(FOnFinishedChangingProperties::FDelegate::CreateSP(this, &FBlueprintEditor::OnFinishedChangingProperties));

	PropertyView->GetIsPropertyEditingEnabledDelegate().BindSP(this, &SGraphNodeDetailsWidget::GetCanEditProperties);

	// Create the border that all of the content will get stuffed into
	ChildSlot
	[
		SNew(SVerticalBox)
		.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("BlueprintInspector")))
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SAssignNew(ContextualEditingBorderWidget, SBorder)
			.Padding(0)
			.BorderImage(FEditorStyle::GetBrush("NoBorder"))
		]
	];
}

void SGraphNodeDetailsWidget::ShowDetailsObjects(const TArray<UObject*>& Objects)
{
	bRefreshOnTick = true;
	RefreshPropertyObjects.Empty();

	RefreshPropertyObjects.Append(Objects);
}


void SGraphNodeDetailsWidget::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	if (bRefreshOnTick)
	{
		UpdateFromObjects(RefreshPropertyObjects);
		RefreshPropertyObjects.Empty();
		bRefreshOnTick = false;
	}
}

struct FKismetSelectionInfo
{
public:
	TArray<UActorComponent*> EditableComponentTemplates;
	TArray<UObject*> ObjectsForPropertyEditing;
};

void SGraphNodeDetailsWidget::AddPropertiesRecursive(UProperty* Property)
{
	if (Property != NULL)
	{
		// Add this property
		SelectedObjectProperties.Add(Property);

		// If this is a struct or an array of structs, recursively add the child properties
		UArrayProperty* ArrayProperty = Cast<UArrayProperty>(Property);
		UStructProperty* StructProperty = Cast<UStructProperty>(Property);
		if (StructProperty != NULL &&
			StructProperty->Struct != NULL)
		{
			for (TFieldIterator<UProperty> StructPropIt(StructProperty->Struct); StructPropIt; ++StructPropIt)
			{
				UProperty* InsideStructProperty = *StructPropIt;
				AddPropertiesRecursive(InsideStructProperty);
			}
		}
		else if (ArrayProperty && ArrayProperty->Inner->IsA<UStructProperty>())
		{
			AddPropertiesRecursive(ArrayProperty->Inner);
		}
	}
}

// Inspired from SKismetInspector::UpdateFromObjects
void SGraphNodeDetailsWidget::UpdateFromObjects(const TArray<UObject*>& PropertyObjects)
{
	TSharedRef< SVerticalBox > ContextualEditingWidget = SNew(SVerticalBox);

	// DATAPREP_TODO: TO be revisited based on tpm's feedback. For the time being, simple view of properties
	SelectedObjects.Empty();
	FKismetSelectionInfo SelectionInfo;

	for (auto ObjectIt = PropertyObjects.CreateConstIterator(); ObjectIt; ++ObjectIt)
	{
		if (UObject* Object = *ObjectIt)
		{
			if (!Object->IsValidLowLevel())
			{
				ensureMsgf(false, TEXT("Object in KismetInspector is invalid, see TTP 281915"));
				continue;
			}

			SelectedObjects.Add(Object);

			if (USCS_Node* SCSNode = Cast<USCS_Node>(Object))
			{
				// Edit the component template
				UActorComponent* NodeComponent = SCSNode->ComponentTemplate;
				if (NodeComponent != NULL)
				{
					SelectionInfo.ObjectsForPropertyEditing.Add(NodeComponent);
					SelectionInfo.EditableComponentTemplates.Add(NodeComponent);
				}
			}
			else if (UK2Node* K2Node = Cast<UK2Node>(Object))
			{
				// Edit the component template if it exists
				if (UK2Node_AddComponent* ComponentNode = Cast<UK2Node_AddComponent>(K2Node))
				{
					if (UActorComponent* Template = ComponentNode->GetTemplateFromNode())
					{
						SelectionInfo.ObjectsForPropertyEditing.Add(Template);
						SelectionInfo.EditableComponentTemplates.Add(Template);
					}
				}

				// See if we should edit properties of the node
				if (K2Node->ShouldShowNodeProperties())
				{
					SelectionInfo.ObjectsForPropertyEditing.Add(Object);
				}
			}
			else if (UActorComponent* ActorComponent = Cast<UActorComponent>(Object))
			{
				AActor* Owner = ActorComponent->GetOwner();
				if (Owner != NULL && Owner->HasAnyFlags(RF_ClassDefaultObject))
				{
					// We're editing a component that's owned by a CDO, so set the CDO to the property editor (so that propagation works) and then filter to just the component property that we want to edit
					SelectionInfo.ObjectsForPropertyEditing.AddUnique(Owner);
					SelectionInfo.EditableComponentTemplates.Add(ActorComponent);
				}
				else
				{
					// We're editing a component that exists outside of a CDO, so just edit the component instance directly
					SelectionInfo.ObjectsForPropertyEditing.AddUnique(ActorComponent);
				}
			}
			else
			{
				// Editing any UObject*
				SelectionInfo.ObjectsForPropertyEditing.AddUnique(Object);
			}
		}
	}

	// By default, no property filtering
	SelectedObjectProperties.Empty();

	// Add to the property filter list for any editable component templates
	if (SelectionInfo.EditableComponentTemplates.Num())
	{
		for (auto CompIt = SelectionInfo.EditableComponentTemplates.CreateIterator(); CompIt; ++CompIt)
		{
			UActorComponent* EditableComponentTemplate = *CompIt;
			check(EditableComponentTemplate != NULL);

			// Add all properties belonging to the component template class
			for (TFieldIterator<UProperty> PropIt(EditableComponentTemplate->GetClass()); PropIt; ++PropIt)
			{
				UProperty* Property = *PropIt;
				check(Property != NULL);

				AddPropertiesRecursive(Property);
			}

			// Attempt to locate a matching property for the current component template
			for (auto ObjIt = SelectionInfo.ObjectsForPropertyEditing.CreateIterator(); ObjIt; ++ObjIt)
			{
				UObject* Object = *ObjIt;
				check(Object != NULL);

				if (Object != EditableComponentTemplate)
				{
					UObjectProperty* ObjectProperty = FindField<UObjectProperty>(Object->GetClass(), EditableComponentTemplate->GetFName());
					if (ObjectProperty != nullptr)
					{
						SelectedObjectProperties.Add(ObjectProperty);
					}
					else if (UActorComponent* Archetype = Cast<UActorComponent>(EditableComponentTemplate->GetArchetype()))
					{
						if (AActor* Owner = Archetype->GetOwner())
						{
							if (UClass* OwnerClass = Owner->GetClass())
							{
								AActor* OwnerCDO = CastChecked<AActor>(OwnerClass->GetDefaultObject());
								for (TFieldIterator<UObjectProperty> ObjPropIt(OwnerClass, EFieldIteratorFlags::IncludeSuper); ObjPropIt; ++ObjPropIt)
								{
									ObjectProperty = *ObjPropIt;
									check(ObjectProperty != nullptr);

									// If the property value matches the current archetype, add it as a selected property for filtering
									if (Archetype->GetClass()->IsChildOf(ObjectProperty->PropertyClass)
										&& Archetype == ObjectProperty->GetObjectPropertyValue_InContainer(OwnerCDO))
									{
										ObjectProperty = FindField<UObjectProperty>(Object->GetClass(), ObjectProperty->GetFName());
										if (ObjectProperty != nullptr)
										{
											SelectedObjectProperties.Add(ObjectProperty);
											break;
										}
									}
								}
							}
						}
					}
				}
			}
		}
	}

	PropertyView->SetObjects(SelectionInfo.ObjectsForPropertyEditing);

	if (SelectionInfo.ObjectsForPropertyEditing.Num() > 0)
	{
		ContextualEditingWidget->AddSlot()
		.FillHeight(0.9f)
		.VAlign(VAlign_Top)
		[
			SNew(SBox)
			//.Visibility(this, &SKismetInspector::GetPropertyViewVisibility)
			[
				SNew(SVerticalBox)
				//+ SVerticalBox::Slot()
				//.AutoHeight()
				//.Padding(FMargin(0, 0, 0, 1))
				//[
				//	SNew(SKismetInspectorUneditableComponentWarning)
				//	.Visibility(this, &SKismetInspector::GetInheritedBlueprintComponentWarningVisibility)
				//	.WarningText(NSLOCTEXT("SKismetInspector", "BlueprintUneditableInheritedComponentWarning", "Components flagged as not editable when inherited must be edited in the <a id=\"HyperlinkDecorator\" style=\"DetailsView.BPMessageHyperlinkStyle\">Parent Blueprint</>"))
				//	.OnHyperlinkClicked(this, &SKismetInspector::OnInheritedBlueprintComponentWarningHyperlinkClicked)
				//]
				+ SVerticalBox::Slot()
				[
					PropertyView.ToSharedRef()
				]
			]
		];
	}

	ContextualEditingBorderWidget->SetContent(ContextualEditingWidget);
}

void SProducerStackEntryTreeView::Construct(const FArguments& InArgs, SDataprepAssetView* InDataprepAssetView, FDataprepEditor* InDataprepEditorPtr)
{
	DataprepEditorPtr = InDataprepEditorPtr;
	DataprepEditorPtr->OnDataprepAssetProducerChanged().AddSP(this, &SProducerStackEntryTreeView::OnDataprepAssetProducerChanged);

	BuildProducerEntries();

	STreeView::Construct
	(
		STreeView::FArguments()
		.TreeItemsSource(&RootNodes)
		.OnGenerateRow(this, &SProducerStackEntryTreeView::OnGenerateRow)
		.OnGetChildren(this, &SProducerStackEntryTreeView::OnGetChildren)
	);
}

int32 SProducerStackEntryTreeView::GetDisplayIndexOfNode(FProducerStackEntryRef InNode)
{
	return LinearizedItems.Find(InNode);
}

void SProducerStackEntryTreeView::Refresh()
{
	BuildProducerEntries();
	RequestTreeRefresh();
}

void SProducerStackEntryTreeView::OnExpansionChanged(FProducerStackEntryRef InItem, bool bInIsExpanded)
{
}

TSharedRef<ITableRow> SProducerStackEntryTreeView::OnGenerateRow(FProducerStackEntryRef InDisplayNode, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SProducerStackEntryTableRow, OwnerTable, InDisplayNode);
}

void SProducerStackEntryTreeView::BuildProducerEntries()
{
	const TArray< FDataprepAssetProducer >& Producers = DataprepEditorPtr->GetDataprepAsset()->Producers;

	RootNodes.Empty( Producers.Num() );

	for ( const FDataprepAssetProducer& AssetProducer : Producers )
	{
		if (AssetProducer.Producer != nullptr)
		{
			TSharedRef<FProducerStackEntry> ProducerStackEntry = MakeShareable( new FProducerStackEntry( RootNodes.Num(), AssetProducer, DataprepEditorPtr ) );
			RootNodes.Add( ProducerStackEntry );
		}
	}
}

void SProducerStackEntryTreeView::OnDataprepAssetProducerChanged()
{
	BuildProducerEntries();
	RequestTreeRefresh();
}

void SProducerStackEntryTreeView::OnGetChildren(FProducerStackEntryRef InParent, TArray<FProducerStackEntryRef>& OutChildren) const
{
	OutChildren.Reset();
}

/** Construct function for this widget */
void SProducerStackEntryTableRow::Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTableView, const TSharedRef<FProducerStackEntry>& InNode)
{
	Node = InNode;
	STableRow::Construct(STableRow::FArguments(), OwnerTableView);

	FProducerStackEntryPtr ProducerStackEntry = Node.Pin();

	if (!ProducerStackEntry.IsValid())
	{
		SetContent( SNullWidget::NullWidget );
	}
	else
	{
		SetContent( GetInputMainWidget() );
	}
}

TSharedRef<SWidget> SProducerStackEntryTableRow::GetInputMainWidget()
{
	FProducerStackEntryPtr ProducerStackEntry = Node.Pin();

	if ( !ProducerStackEntry.IsValid() || !ProducerStackEntry->HasValidData() )
	{
		return SNullWidget::NullWidget;
	}

	auto CheckEntry = [this, ProducerStackEntry]()
	{
		ProducerStackEntry->bIsEnabled = !ProducerStackEntry->bIsEnabled;
		CheckBox->SetText( FText::FromString( ProducerStackEntry->bIsEnabled ? TEXT( "\xf14a" ) : TEXT( "\xf0c8" ) ) );

		ProducerStackEntry->GetDataprepAssetProducer().bIsEnabled = ProducerStackEntry->bIsEnabled;

		return FReply::Handled();
	};

	auto DeleteEntry = [ProducerStackEntry]()
	{
		ProducerStackEntry->DataprepEditorPtr->OnRemoveProducer( ProducerStackEntry->ProducerIndex );
		return FReply::Handled();
	};

	// Padding for check and delete buttons to center them on the first line of the detail view
	const FMargin ButtonPadding( 0.0f, 10.0f, 0.0f, 0.0f );

	TSharedPtr<SWidget> Widget = SNew(SBorder)
	.BorderImage(FEditorStyle::GetBrush("NoBrush"))
	.Padding(5.0f)
	[
		SNew(SHorizontalBox)
		// Check button
		+ SHorizontalBox::Slot()
		.HAlign(HAlign_Center)
		.Padding( ButtonPadding )
		.AutoWidth()
		[
			SNew(SButton)
			.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
			.ToolTipText(LOCTEXT("CheckToolTip", "Include or exclude this producer to the creation of the world "))
			.IsFocusable(false)
			.OnClicked_Lambda( CheckEntry )
			.VAlign( VAlign_Top )
			.Content()
			[
				SAssignNew(CheckBox, STextBlock)
				.Font(FEditorStyle::Get().GetFontStyle(DataprepTabsFontName))
				.ColorAndOpacity( FLinearColor::White )
				.Text(FText::FromString(FString( ProducerStackEntry->bIsEnabled ? TEXT( "\xf14a" ) : TEXT( "\xf0c8" ) ) ) )
			]
		]
		// Input entry label
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		[
			SNew( SDataprepDetailsView )
			.Object( ProducerStackEntry->GetProducer() )
		]
		// Delete button
		+ SHorizontalBox::Slot()
		.HAlign(HAlign_Center)
		.Padding( ButtonPadding )
		.AutoWidth()
		[
			SNew(SButton)
			.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
			.ToolTipText(LOCTEXT("DeleteToolTip", "Delete this producer"))
			.IsFocusable(false)
			.OnClicked_Lambda( DeleteEntry )
			.VAlign( VAlign_Top )
			.Content()
			[
				SNew(STextBlock)
				.Font(FEditorStyle::Get().GetFontStyle(DataprepTabsFontName))
				.ColorAndOpacity( FLinearColor::White )
				.Text(FText::FromString(FString(TEXT("\xf1f8"))))
			]
		]
	];

	return Widget.ToSharedRef();
}

void SDataprepAssetView::Construct( const FArguments& InArgs, FDataprepEditor* InDataprepEditor )
{
	DataprepEditorPtr = InDataprepEditor;

	const UDataprepAsset* DataprepAsset = DataprepEditorPtr->GetDataprepAsset();

	bIsChecked = true;
	const TArray< FDataprepAssetProducer >& Producers = DataprepAsset->Producers;
	for (const FDataprepAssetProducer& Producer : Producers)
	{
		bIsChecked &= Producer.bIsEnabled;
	}

	const TArray< DataprepEditorClassDescription >& Consumers = DataprepEditorPtr->GetConsumerDescriptions();

	for (const DataprepEditorClassDescription& Consumer : Consumers)
	{
		TSharedPtr< FString >& ConsumerLabel = ConsumerList.Emplace_GetRef( new FString( Consumer.Get<1>().ToString() ) );
		if (DataprepAsset->Consumer != nullptr && DataprepAsset->Consumer->GetClass() == Consumer.Get<0>() )
		{
			SelectedProducer = ConsumerLabel;
		}
	}

	if ( !SelectedProducer.IsValid() && ConsumerList.Num() > 0)
	{
		SelectedProducer = MakeShared<FString>( FString() );
	}

	auto CheckEntry = [ this ]()
	{
		bIsChecked = !bIsChecked;

		CheckBox->SetText(FText::FromString( FString( bIsChecked ? TEXT( "\xf14a" ) : TEXT( "\xf0c8" ) ) ) );

		TArray< FDataprepAssetProducer >& Producers = DataprepEditorPtr->GetDataprepAsset()->Producers;
		for ( FDataprepAssetProducer& Producer : Producers )
		{
			Producer.bIsEnabled = bIsChecked;
		}

		// #ueent_todo: Revisit how changes are propagated from UI to editor to asset and reverse
		// Propagate dataprep asset has changed
		DataprepEditorPtr->OnDataprepAssetProducerChanged().Broadcast();

		TreeView->Refresh();

		return FReply::Handled();
	};

	TSharedPtr<SWidget> AddNewMenu = SNew(SComboButton)
	.ComboButtonStyle(FEditorStyle::Get(), "ToolbarComboButton")
	.ForegroundColor(FLinearColor::White)
	.ToolTipText(LOCTEXT("AddNewToolTip", "Add a new producer."))
	.OnGetMenuContent(this, &SDataprepAssetView::CreateAddProducerMenuWidget)
	.HasDownArrow(false)
	.ButtonContent()
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(FMargin(0, 1))
		.HAlign(HAlign_Center)
		[
			SAssignNew(CheckBox, STextBlock)
			.Font(FEditorStyle::Get().GetFontStyle(DataprepTabsFontName))
			.ColorAndOpacity( FLinearColor::White )
			.Text(FText::FromString(FString( TEXT( "\xf055" ) ) ) )
		]
	];

	TreeView = SNew(SProducerStackEntryTreeView, this, DataprepEditorPtr);

	// #ueent_todo: Look at changing the border brushes to add color to this stuff
	ChildSlot
	[
		SNew(SBorder)
		.Padding(4.0f)
		.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SSpacer)
				.Size( FVector2D( 200, 100 ) )		
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.Padding( 0, 10, 0, 0 )
				.HAlign( EHorizontalAlignment::HAlign_Center )
				[
					// #ueent_todo: make color block's width vary with parent widget
					SNew(SColorBlock)
					.Color( FLinearColor( 0.9f, 0.9f, 0.9f ) )
					.Size( FVector2D( 3000, 1 ) )
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.HAlign(EHorizontalAlignment::HAlign_Left)
				.VAlign(VAlign_Center)
				.Padding( 5.0f )
				[
					SNew(STextBlock)
					.Font(FEditorStyle::Get().GetFontStyle(DataprepTabsFontName))
					.Text(LOCTEXT("DataprepAssetView_Consumer_label", "Output"))
					.MinDesiredWidth( 200 )
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.HAlign(EHorizontalAlignment::HAlign_Left)
				.VAlign(VAlign_Center)
				[
					SAssignNew( ProducerSelector, STextComboBox )
					.OptionsSource( &ConsumerList )
					.OnSelectionChanged( this, &SDataprepAssetView::OnConsumerChanged )
					.InitiallySelectedItem( SelectedProducer )
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SAssignNew( ConsumerWidget, SDataprepConsumerWidget )
				.DataprepConsumer( DataprepAsset->Consumer )
			]
			// Section for producers
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				[
					SNew(SSpacer)
					.Size( FVector2D( 200, 20 ) )		
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.Padding( 0, 10, 0, 0 )
				.HAlign( EHorizontalAlignment::HAlign_Center )
				[
					SNew(SColorBlock)
					.Color( FLinearColor( 0.9f, 0.9f, 0.9f ) )
					.Size( FVector2D( 2000, 1 ) )
				]
			]
			+ SVerticalBox::Slot()
			.Padding( 5.0f )
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				// Check button
				+ SHorizontalBox::Slot()
				.HAlign(HAlign_Center)
				.AutoWidth()
				[
					SNew(SButton)
					.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
					.ToolTipText(LOCTEXT("CheckToolTip", "Include or exclude this producer to the creation of the world "))
					.IsFocusable(false)
					.OnClicked_Lambda( CheckEntry )
					.VAlign(VAlign_Center)
					.Content()
					[
						SAssignNew(CheckBox, STextBlock)
						.Font(FEditorStyle::Get().GetFontStyle(DataprepTabsFontName))
						.ColorAndOpacity( FLinearColor::White )
						.Text(FText::FromString(FString( bIsChecked ? TEXT( "\xf14a" ) : TEXT( "\xf0c8" ) ) ) )
					]
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.HAlign(EHorizontalAlignment::HAlign_Left)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Font(FEditorStyle::Get().GetFontStyle(DataprepTabsFontName))
					.Text(LOCTEXT("DataprepAssetView_Producers_label", "Inputs"))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.HAlign(EHorizontalAlignment::HAlign_Right)
				.Padding(0, 0, 2, 0)
				[
					AddNewMenu.ToSharedRef()
				]
			]
			+ SVerticalBox::Slot()
			.Padding( 5.0f )
			.AutoHeight()
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				.Padding(0, 3, 0, 3)
				[
					TreeView.ToSharedRef()
				]
			]
		]
	];
}

void SDataprepAssetView::OnConsumerChanged( TSharedPtr<FString> NewConsumer, ESelectInfo::Type SelectInfo)
{
	if ( !NewConsumer.IsValid() )
	{
		return;
	}

	if ( SelectedProducer == NewConsumer )
	{
		return;
	}

	// #ueent_todo: Provide an index instead of a FString
	if ( !DataprepEditorPtr->OnChangeConsumer( NewConsumer ) )
	{
		ProducerSelector->SetSelectedItem( SelectedProducer );
	}
	else
	{
		SelectedProducer = NewConsumer;
		ConsumerWidget->SetDataprepConsumer( DataprepEditorPtr->GetDataprepAsset()->Consumer );
	}
}

TSharedRef<SWidget> SDataprepAssetView::CreateAddProducerMenuWidget()
{
	const bool bShouldCloseWindowAfterMenuSelection = true;
	FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, DataprepEditorPtr->GetToolkitCommands());

	MenuBuilder.BeginSection("AddNewProducer", LOCTEXT("DataprepEditorViews_AddImports", "Add Producer"));
	{
		FUIAction MenuAction;
		int32 Index = 0;

		for (const DataprepEditorClassDescription& ProducerDescription : DataprepEditorPtr->GetProducerDescriptions())
		{
			MenuAction.ExecuteAction.BindSP(DataprepEditorPtr, &FDataprepEditor::OnAddProducer, Index);

			MenuBuilder.AddMenuEntry(
				ProducerDescription.Get<1>(),
				ProducerDescription.Get<2>(),
				FSlateIcon( FDataprepEditorStyle::GetStyleSetName(), TEXT("DataprepEditor.Producer") ),
				MenuAction,
				NAME_None,
				EUserInterfaceActionType::Button
			);

			++Index;
		}
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

void SDataprepAssetView::OnSelectionChanged( TSharedPtr< FProducerStackEntry > InItem, ESelectInfo::Type InSeletionInfo, FDataprepEditor* DataprepEditor )
{
	// An entry is selected
	if ( InItem.IsValid() )
	{
		const TArray< FDataprepAssetProducer >& Producers = DataprepEditorPtr->GetDataprepAsset()->Producers;

		if (Producers.IsValidIndex(InItem->ProducerIndex))
		{
			// Take hold on the selected entry
			SelectedEntry = InItem;
			// Show properties of associated producer
			DetailsView->SetObject( Producers[InItem->ProducerIndex].Producer, true );
		}
	}
	// An entry is deselected
	else
	{
		// Release hold on selected entry
		SelectedEntry.Reset();
		// Clear details section
		DetailsView->SetObject( nullptr, true );
	}
}

#undef LOCTEXT_NAMESPACE
