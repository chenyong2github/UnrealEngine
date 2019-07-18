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

#include "DetailLayoutBuilder.h"
#include "EditorFontGlyphs.h"
#include "Modules/ModuleManager.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SScrollBar.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "DataprepAssetView"

const float IndentSize = 12;

namespace DataprepEditorUtils
{
	FSlateFontInfo GetGlyphFont()
	{
		return FEditorStyle::Get().GetFontStyle( "FontAwesome.11" );
	}
}

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
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				[
					PropertyView.ToSharedRef()
				]
			]
		];
	}

	ContextualEditingBorderWidget->SetContent(ContextualEditingWidget);
}

void SProducerStackEntryTreeView::Construct(const FArguments& InArgs, SDataprepAssetView* InDataprepAssetView, UDataprepAsset* InDataprepAssetPtr)
{
	DataprepAssetPtr = InDataprepAssetPtr;
	check( DataprepAssetPtr.IsValid() );

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
	if( UDataprepAsset* DataprepAsset = DataprepAssetPtr.Get() )
	{
		int32 ProducersCount = DataprepAsset->GetProducersCount();

		RootNodes.Empty( ProducersCount );

		for( int32 Index = 0; Index < ProducersCount; ++Index )
		{
			TSharedRef<FProducerStackEntry> ProducerStackEntry = MakeShareable( new FProducerStackEntry( Index, DataprepAsset ) );
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

	if ( !ProducerStackEntry.IsValid() )
	{
		return SNullWidget::NullWidget;
	}

	auto DeleteEntry = [ProducerStackEntry]()
	{
		ProducerStackEntry->RemoveProducer();
		return FReply::Handled();
	};

	// Padding for check and delete buttons to center them on the first line of the detail view
	const FMargin ButtonPadding( 0.0f, 10.0f, 0.0f, 0.0f );

	TSharedPtr<STextBlock> StatusText;

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
			SAssignNew(StatusText, STextBlock)
			.Font( DataprepEditorUtils::GetGlyphFont() )
			.ColorAndOpacity( this, &SProducerStackEntryTableRow::GetStatusColorAndOpacity )
			.Text( FEditorFontGlyphs::Exclamation_Triangle )
		]
		// Input entry label
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		[
			SNew( SDataprepDetailsView )
			.Object( ProducerStackEntry->GetProducer() )
			.Class( UDataprepContentProducer::StaticClass() )
		]
		// Delete button
		+ SHorizontalBox::Slot()
		.HAlign(HAlign_Center)
		.Padding( ButtonPadding )
		.AutoWidth()
		[
			SNew(SButton)
			.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
			.ToolTipText(LOCTEXT("ProducerStackEntryTableRow_DeleteToolTip", "Delete this producer"))
			.IsFocusable(false)
			.OnClicked_Lambda( DeleteEntry )
			.VAlign( VAlign_Top )
			.Content()
			[
				SNew(STextBlock)
				.Font( DataprepEditorUtils::GetGlyphFont() )
				.ColorAndOpacity( FLinearColor::White )
				.Text( FEditorFontGlyphs::Trash )
			]
		]
	];

	StatusText->SetToolTipText( TAttribute<FText>( this, &SProducerStackEntryTableRow::GetStatusTooltipText ) );

	return Widget.ToSharedRef();
}

FSlateColor SProducerStackEntryTableRow::GetStatusColorAndOpacity() const
{
	FProducerStackEntryPtr ProducerStackEntry = Node.Pin();
	return  ( ProducerStackEntry.IsValid() && ProducerStackEntry->WillBeRun() ) ? FLinearColor::Transparent : FLinearColor::Red;
}

FText SProducerStackEntryTableRow::GetStatusTooltipText() const
{
	FProducerStackEntryPtr ProducerStackEntry = Node.Pin();
	if( !ProducerStackEntry.IsValid() )
	{
		return LOCTEXT( "ProducerStackEntryTableRow_StatusTextTooltip_Invalid", "The producer is not valid");
	}

	return  ProducerStackEntry->WillBeRun() ? FText() : LOCTEXT( "ProducerStackEntryTableRow_StatusTextTooltip_Superseded", "This producer is superseded by another one and will be skipped when run.");
}

void SDataprepAssetView::Construct( const FArguments& InArgs, UDataprepAsset* InDataprepAssetPtr, TSharedPtr<FUICommandList>& CommandList )
{
	check( InDataprepAssetPtr );

	DataprepAssetPtr = InDataprepAssetPtr;

	DataprepAssetPtr->GetOnChanged().AddRaw( this, &SDataprepAssetView::OnDataprepAssetChanged );

	bIsChecked = true;

	for(int32 Index = 0; Index < DataprepAssetPtr->GetProducersCount(); ++Index)
	{
		bIsChecked &= DataprepAssetPtr->IsProducerEnabled( Index ) && !DataprepAssetPtr->IsProducerSuperseded( Index );
	}

	for( TObjectIterator< UClass > It ; It ; ++It )
	{
		UClass* CurrentClass = (*It);

		if ( !CurrentClass->HasAnyClassFlags( CLASS_Abstract ) )
		{
			if( CurrentClass->IsChildOf( UDataprepContentConsumer::StaticClass() ) )
			{
				if( UDataprepContentConsumer* Consumer = Cast< UDataprepContentConsumer >( CurrentClass->GetDefaultObject() ) )
				{
					TSharedPtr< FString >& ConsumerDescriptionLabel = ConsumerDescriptionList.Emplace_GetRef( new FString( Consumer->GetLabel().ToString() ) );
					ConsumerDescriptionMap.Add( ConsumerDescriptionLabel, CurrentClass );

					if (DataprepAssetPtr->GetConsumer() != nullptr && DataprepAssetPtr->GetConsumer()->GetClass() == CurrentClass )
					{
						SelectedConsumerDescription = ConsumerDescriptionLabel;
					}
				}
			}
		}
	}

	// Display a combo-box if there are more than one type of consumers
	if( ConsumerDescriptionMap.Num() > 1 )
	{
		if ( !SelectedConsumerDescription.IsValid() )
		{
			SelectedConsumerDescription = MakeShared<FString>( FString() );
		}


		ProducerSelector = SNew( STextComboBox )
		.OptionsSource( &ConsumerDescriptionList )
		.OnSelectionChanged( this, &SDataprepAssetView::OnNewConsumerSelected )
		.InitiallySelectedItem( SelectedConsumerDescription );
	}
	else
	{
		ProducerSelector = SNullWidget::NullWidget;
	}

	TSharedPtr<SWidget> AddNewMenu = SNew(SComboButton)
	.ComboButtonStyle(FEditorStyle::Get(), "ToolbarComboButton")
	.ForegroundColor(FLinearColor::White)
	.ToolTipText(LOCTEXT("AddNewToolTip", "Add a new producer."))
	.OnGetMenuContent(this, &SDataprepAssetView::CreateAddProducerMenuWidget, CommandList)
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
			.Font( DataprepEditorUtils::GetGlyphFont() )
			.ColorAndOpacity( FLinearColor::White )
			.Text( FEditorFontGlyphs::Plus_Circle )
		]
	];

	TreeView = SNew(SProducerStackEntryTreeView, this, DataprepAssetPtr.Get() );
	TSharedRef<SScrollBar> ScrollBar = SNew(SScrollBar);

	// #ueent_todo: Look at changing the border brushes to add color to this stuff
	ChildSlot
	[
		SNew(SBorder)
		.Padding(4.0f)
		.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
		[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.FillWidth(1)
			[

				SNew(SScrollBox)
				.ExternalScrollbar(ScrollBar)
				+ SScrollBox::Slot()
				[

					SNew(SVerticalBox)
					// Section for producers
					+ SVerticalBox::Slot()
					.Padding( 5.0f )
					.AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.HAlign(EHorizontalAlignment::HAlign_Left)
						.VAlign(VAlign_Center)
						.Padding( FMargin( 10, 0, 0, 0 ) )
						[
							SNew(STextBlock)
							.Font( IDetailLayoutBuilder::GetDetailFontBold() )
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
						TreeView.ToSharedRef()
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
						[
							SNew(SSpacer)
							.Size( FVector2D( 200, 10 ) )		
						]
					]
					// Section for consumer
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.HAlign(EHorizontalAlignment::HAlign_Left)
						.VAlign(VAlign_Center)
						.Padding( FMargin( 10, 0, 0, 0 ) )
						[
							SNew(STextBlock)
							.Text(LOCTEXT("DataprepAssetView_Consumer_label", "Output"))
							.MinDesiredWidth( 200 )
							.Font( IDetailLayoutBuilder::GetDetailFontBold() )
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.HAlign(EHorizontalAlignment::HAlign_Right)
						.Padding(0, 0, 2, 0)
						[
							ProducerSelector.ToSharedRef()
						]
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SAssignNew( ConsumerWidget, SDataprepConsumerWidget )
						.DataprepConsumer( DataprepAssetPtr->GetConsumer() )
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						[
							SNew(SSpacer)
							.Size( FVector2D( 200, 10 ) )		
						]
					]
					// Section for consumer
				]
			]
			+SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew( SBox )
				.WidthOverride( FOptionalSize( 16 ) )
				[
					ScrollBar
				]
			]
		]
	];
}

SDataprepAssetView::~SDataprepAssetView()
{
	if( UDataprepAsset* DataprepAsset = DataprepAssetPtr.Get() )
	{
		DataprepAsset->GetOnChanged().RemoveAll( this );
	}
}

void SDataprepAssetView::OnNewConsumerSelected( TSharedPtr<FString> NewConsumerDescription, ESelectInfo::Type SelectInfo)
{
	if ( !NewConsumerDescription.IsValid() || SelectedConsumerDescription == NewConsumerDescription )
	{
		return;
	}

	if( UDataprepAsset* DataprepAsset = DataprepAssetPtr.Get() )
	{
		UClass** NewConsumerClassPtr = ConsumerDescriptionMap.Find(NewConsumerDescription);
		check(NewConsumerClassPtr);

		if( !DataprepAsset->ReplaceConsumer( *NewConsumerClassPtr ) )
		{
			((STextComboBox*)ProducerSelector.Get())->SetSelectedItem(SelectedConsumerDescription);
		}
		// Update SelectedConsumerDescription only, the widget displaying the consumer is updated thru notifications 
		else
		{
			SelectedConsumerDescription = NewConsumerDescription;
		}
	}
}

void SDataprepAssetView::OnDataprepAssetChanged(FDataprepAssetChangeType ChangeType, int32 Index)
{
	if( UDataprepAsset* DataprepAsset = DataprepAssetPtr.Get() )
	{
		if(ChangeType == FDataprepAssetChangeType::ConsumerModified)
		{
			// Update the widget holding the consumer
			ConsumerWidget->SetDataprepConsumer( DataprepAsset->GetConsumer() );
		}
		else if( ChangeType == FDataprepAssetChangeType::ProducerModified ||
			ChangeType == FDataprepAssetChangeType::ProducerAdded ||
			ChangeType == FDataprepAssetChangeType::ProducerRemoved )
		{
			// Brute force : Regenerate the whole tree view
			TreeView->Refresh();
		}
	}
}

TSharedRef<SWidget> SDataprepAssetView::CreateAddProducerMenuWidget(TSharedPtr<FUICommandList> CommandList)
{
	const bool bShouldCloseWindowAfterMenuSelection = true;
	FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, CommandList);

	MenuBuilder.BeginSection("AddNewProducer", LOCTEXT("DataprepEditorViews_AddImports", "Add Producer"));
	{
		FUIAction MenuAction;
		int32 Index = 0;

		// Find content producers the user could use for his/her data preparation
		for( TObjectIterator< UClass > It ; It ; ++It )
		{
			UClass* CurrentClass = (*It);

			if ( !CurrentClass->HasAnyClassFlags( CLASS_Abstract ) )
			{
				if( CurrentClass->IsChildOf( UDataprepContentProducer::StaticClass() ) )
				{
					MenuAction.ExecuteAction.BindSP(this, &SDataprepAssetView::OnAddProducer, CurrentClass);

					UDataprepContentProducer* DefaultProducer = CurrentClass->GetDefaultObject<UDataprepContentProducer>();
					check( DefaultProducer );

					MenuBuilder.AddMenuEntry(
						DefaultProducer->GetLabel(),
						DefaultProducer->GetDescription(),
						FSlateIcon( FDataprepEditorStyle::GetStyleSetName(), TEXT("DataprepEditor.Producer") ),
						MenuAction,
						NAME_None,
						EUserInterfaceActionType::Button
					);

					++Index;
				}
			}
		}
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

void SDataprepAssetView::OnAddProducer( UClass* ProducerClass )
{
	if( UDataprepAsset* DataprepAsset = DataprepAssetPtr.Get() )
	{
		DataprepAsset->AddProducer(ProducerClass);
	}
}

void SDataprepAssetView::OnSelectionChanged( TSharedPtr< FProducerStackEntry > InItem, ESelectInfo::Type InSeletionInfo )
{
	// An entry is selected
	if ( InItem.IsValid() && InItem->HasValidData() )
	{
		// Take hold on the selected entry
		SelectedEntry = InItem;
	}
	// An entry is deselected
	else
	{
		// Release hold on selected entry
		SelectedEntry.Reset();
	}
}

#undef LOCTEXT_NAMESPACE
