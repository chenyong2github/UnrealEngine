// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DataprepAssetView.h"

#include "DataPrepAsset.h"
#include "DataPrepEditor.h"

#include "DataPrepContentConsumer.h"
#include "DataprepEditorStyle.h"
#include "SAssetsPreviewWidget.h"
#include "SDataprepProducersWidget.h"

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

void SDataprepAssetView::Construct( const FArguments& InArgs, UDataprepAssetInterface* InDataprepAssetPtr, TSharedPtr<FUICommandList>& CommandList )
{
	check( InDataprepAssetPtr );

	DataprepAssetInterfacePtr = InDataprepAssetPtr;

	DataprepAssetInterfacePtr->GetOnChanged().AddRaw( this, &SDataprepAssetView::OnDataprepAssetChanged );

	bIsChecked = true;

	ColumnWidth = 0.7f;
	ColumnSizeData = MakeShared< FDataprepDetailsViewColumnSizeData >();
	ColumnSizeData->LeftColumnWidth = TAttribute<float>(this, &SDataprepAssetView::OnGetLeftColumnWidth);
	ColumnSizeData->RightColumnWidth = TAttribute<float>(this, &SDataprepAssetView::OnGetRightColumnWidth);
	ColumnSizeData->OnWidthChanged = SSplitter::FOnSlotResized::CreateSP(this, &SDataprepAssetView::OnSetColumnWidth);

	UDataprepAssetProducers* AssetProducers = DataprepAssetInterfacePtr->GetProducers();
	check( AssetProducers );

	for(int32 Index = 0; Index < AssetProducers->GetProducersCount(); ++Index)
	{
		bIsChecked &= AssetProducers->IsProducerEnabled( Index ) && !AssetProducers->IsProducerSuperseded( Index );
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

					if (DataprepAssetInterfacePtr->GetConsumer() != nullptr && DataprepAssetInterfacePtr->GetConsumer()->GetClass() == CurrentClass )
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


		ConsumerSelector = SNew( STextComboBox )
		.OptionsSource( &ConsumerDescriptionList )
		.OnSelectionChanged( this, &SDataprepAssetView::OnNewConsumerSelected )
		.InitiallySelectedItem( SelectedConsumerDescription );
	}
	else
	{
		ConsumerSelector = SNullWidget::NullWidget;
	}

	ProducersWidget = SNew(SDataprepProducersWidget, AssetProducers, CommandList)
		.ColumnSizeData(ColumnSizeData);

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
					+ SVerticalBox::Slot()
					.Padding( 5.0f )
					.AutoHeight()
					.MaxHeight( 400.f )
					[
						ProducersWidget.ToSharedRef()
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
						.Padding( FMargin( 5.0f, 5.0f, 0, 7.0f ) )
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
							ConsumerSelector.ToSharedRef()
						]
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SAssignNew( ConsumerWidget, SDataprepConsumerWidget )
						.DataprepConsumer( DataprepAssetInterfacePtr->GetConsumer() )
						.ColumnSizeData( ColumnSizeData )
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew( SDataprepDetailsView )
						.Object( DataprepAssetInterfacePtr->GetConsumer())
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
	if( UDataprepAssetInterface* DataprepAsset = DataprepAssetInterfacePtr.Get() )
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

	if( UDataprepAssetInterface* DataprepAsset = DataprepAssetInterfacePtr.Get() )
	{
		UClass** NewConsumerClassPtr = ConsumerDescriptionMap.Find(NewConsumerDescription);
		check(NewConsumerClassPtr);

		if( !DataprepAsset->SetConsumer( *NewConsumerClassPtr ) )
		{
			((STextComboBox*)ConsumerSelector.Get())->SetSelectedItem(SelectedConsumerDescription);
		}
		// Update SelectedConsumerDescription only, the widget displaying the consumer is updated thru notifications 
		else
		{
			SelectedConsumerDescription = NewConsumerDescription;
		}
	}
}

void SDataprepAssetView::OnDataprepAssetChanged(FDataprepAssetChangeType ChangeType)
{
	if( UDataprepAssetInterface* DataprepAsset = DataprepAssetInterfacePtr.Get() )
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
			ProducersWidget->Refresh();
		}
	}
}

#undef LOCTEXT_NAMESPACE
