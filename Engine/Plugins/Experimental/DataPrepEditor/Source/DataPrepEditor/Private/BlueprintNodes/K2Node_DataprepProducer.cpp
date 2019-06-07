// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "K2Node_DataprepProducer.h"
#include "DataPrepContentProducer.h"

#include "AssetRegistryModule.h"
#include "BlueprintActionDatabaseRegistrar.h"
#include "BlueprintNodeSpawner.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "KismetCompiler.h"
#include "ScopedTransaction.h"

#include "EditorStyleSet.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "PropertyEditorModule.h"
#include "HAL/PlatformProcess.h"
#include "Modules/ModuleManager.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "SGraphNode.h"
#include "Widgets/Layout/SBox.h"

#define LOCTEXT_NAMESPACE "UK2Node_DataprepProducer"

const char* DataprepProducerFontName = "FontAwesome.11";

// #ueent_todo: Temporarily disabling details view in producer K2Node
static bool bShowVisualWidget = false;

void UK2Node_DataprepProducer::AllocateDefaultPins()
{
	// The immediate continue pin
	CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Exec, UEdGraphSchema_K2::PN_Then);

	Super::AllocateDefaultPins();
}

void UK2Node_DataprepProducer::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	UClass* ActionKey = GetClass();
	if (ActionRegistrar.IsOpenForRegistration(ActionKey))
	{
		UBlueprintNodeSpawner* NodeSpawner = UBlueprintNodeSpawner::Create(GetClass());
		check(NodeSpawner != nullptr);
		ActionRegistrar.AddBlueprintAction(ActionKey, NodeSpawner);
	}
}

FText UK2Node_DataprepProducer::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("NodeTitle", "Start");
}

FText UK2Node_DataprepProducer::GetTooltipText() const
{
	return LOCTEXT("NodeTooltip", "Hold onto all the producers associated to a Dataprep asset");
}

bool UK2Node_DataprepProducer::CanDuplicateNode() const
{
	return false;
}

bool UK2Node_DataprepProducer::CanUserDeleteNode() const
{
	return false;
}

void UK2Node_DataprepProducer::SetDataprepAsset( UDataprepAsset* InDataprepAsset )
{
	// #ueent_todo: Should we allow changing the Dataprep asset associated with a UK2Node_DataprepProducer?
	check(DataprepAsset == nullptr);
	DataprepAsset = InDataprepAsset;
	DataprepAssetPath = FSoftObjectPath( InDataprepAsset );
}

UDataprepAsset* UK2Node_DataprepProducer::GetDataprepAsset()
{
	return DataprepAsset;
}

void UK2Node_DataprepProducer::Serialize(FArchive & Ar)
{
	Super::Serialize( Ar );

	if(Ar.IsLoading())
	{
		DataprepAsset = Cast<UDataprepAsset>(DataprepAssetPath.TryLoad());
	}
}

#if WITH_EDITOR

TSharedPtr<SGraphNode> UK2Node_DataprepProducer::CreateVisualWidget()
{
	typedef TTuple< UClass*, FText, FText > DataprepProducerDescription;

	class SGraphNodeDataprepProducer : public SGraphNode
	{
		struct ProducerWidget
		{
			FDataprepAssetProducer* ProducerPtr;
			TSharedPtr< STextBlock > CheckBox;

			ProducerWidget() : ProducerPtr(nullptr) {}
		};

	public:
		SLATE_BEGIN_ARGS(SGraphNodeDataprepProducer){}
		SLATE_END_ARGS()

		~SGraphNodeDataprepProducer()
		{
			FCoreUObjectDelegates::OnObjectPropertyChanged.RemoveAll(this);
		}

		void Construct(const FArguments& InArgs, UK2Node_DataprepProducer* InNode)
		{
			GraphNode = InNode;
			Initialize();
			UpdateGraphNode();
		}

		virtual void Tick( const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime ) override
		{
			SGraphNode::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
		}

		virtual void CreateStandardPinWidget(UEdGraphPin* Pin) override
		{
			SGraphNode::CreateStandardPinWidget(Pin);
		}

		void OnPostPropertyChanged( UObject* InObject, FPropertyChangedEvent& InPropertyChangedEvent)
		{
			const FName MemberPropertyName = InPropertyChangedEvent.MemberProperty != nullptr ? InPropertyChangedEvent.MemberProperty->GetFName() : NAME_None;
			if (InObject && MemberPropertyName == GET_MEMBER_NAME_CHECKED(UDataprepAsset, Producers))
			{
				UpdateGraphNode();
			}
		}

		void Initialize()
		{
			bNeedsUpdate = false;

			DataprepAsset = CastChecked<UK2Node_DataprepProducer>(GraphNode)->GetDataprepAsset();
			check( DataprepAsset );

			FCoreUObjectDelegates::OnObjectPropertyChanged.AddRaw(this, &SGraphNodeDataprepProducer::OnPostPropertyChanged);

			// #ueent_todo: Move this code where it would capture new classes of producer created at runtime, .i.e BP based producers
			for( TObjectIterator< UClass > It ; It ; ++It )
			{
				UClass* CurrentClass = (*It);

				if ( !CurrentClass->HasAnyClassFlags( CLASS_Abstract ) && CurrentClass->IsChildOf( UDataprepContentProducer::StaticClass() ))
				{
					UDataprepContentProducer* Producer = Cast< UDataprepContentProducer >( CurrentClass->GetDefaultObject() );
					ProducerDescriptions.Emplace( CurrentClass, Producer->GetLabel(), Producer->GetDescription() );
				}
			}
		}

		void OnAddProducer( int32 Index )
		{
			if (DataprepAsset == nullptr || !ProducerDescriptions.IsValidIndex( Index ) )
			{
				return;
			}

			UClass* ProducerClass = ProducerDescriptions[Index].Get<0>();
			check( ProducerClass );

			UDataprepContentProducer* Producer = NewObject< UDataprepContentProducer >( DataprepAsset->GetOutermost(), ProducerClass, NAME_None, RF_Transactional );
			FAssetRegistryModule::AssetCreated( Producer );
			Producer->MarkPackageDirty();

			FScopedTransaction Transaction(LOCTEXT("DataprepAsset_AddProducer", "AddProducer"));

			DataprepAsset->Producers.Emplace( Producer, true );

			UProperty* Property = FindField<UProperty>(UDataprepAsset::StaticClass(), GET_MEMBER_NAME_CHECKED(UDataprepAsset, Producers));
			FPropertyChangedEvent PropertyChangedEvent( Property, EPropertyChangeType::ArrayAdd );
			DataprepAsset->PostEditChangeProperty( PropertyChangedEvent );
		}

		TSharedRef<SWidget> CreateAddProducerMenuWidget()
		{
			FMenuBuilder MenuBuilder(true, nullptr);

			MenuBuilder.BeginSection("AddNewProducer", LOCTEXT("DataprepEditorViews_AddImports", "Add Producer"));
			{
				FUIAction MenuAction;
				int32 Index = 0;

				for (const DataprepProducerDescription& ProducerDescription : ProducerDescriptions)
				{
					MenuAction.ExecuteAction.BindSP(this, &SGraphNodeDataprepProducer::OnAddProducer, Index);

					MenuBuilder.AddMenuEntry(
						ProducerDescription.Get<1>(),
						ProducerDescription.Get<2>(),
						FSlateIcon(),
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

		void DeleteRegisteredAsset(UObject* Asset)
		{
			if(Asset != nullptr)
			{
				FName AssetName = MakeUniqueObjectName( GetTransientPackage(), Asset->GetClass(), *( Asset->GetName() + TEXT( "_Deleted" ) ) );
				Asset->Rename( *AssetName.ToString(), GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional );

				Asset->ClearFlags(RF_Standalone | RF_Public);
				Asset->RemoveFromRoot();
				Asset->MarkPendingKill();

				FAssetRegistryModule::AssetDeleted( Asset ) ;
			}
		}

		TSharedRef< SWidget > CreateProducerWidget( ProducerWidget* ProducerWidgetPtr, int32 Index )
		{
			auto CheckEntry = [this, ProducerWidgetPtr]()
			{
				ProducerWidgetPtr->ProducerPtr->bIsEnabled = !ProducerWidgetPtr->ProducerPtr->bIsEnabled;
				ProducerWidgetPtr->CheckBox->SetText( FText::FromString( ProducerWidgetPtr->ProducerPtr->bIsEnabled ? TEXT( "\xf14a" ) : TEXT( "\xf0c8" ) ) );

				UProperty* Property = FindField<UProperty>(UDataprepAsset::StaticClass(), GET_MEMBER_NAME_CHECKED(UDataprepAsset, Producers));
				FPropertyChangedEvent PropertyChangedEvent( Property, EPropertyChangeType::ValueSet );
				DataprepAsset->PostEditChangeProperty( PropertyChangedEvent );

				return FReply::Handled();
			};

			auto DeleteEntry = [this, ProducerWidgetPtr, Index]()
			{
				FScopedTransaction Transaction(LOCTEXT("DataprepAsset_AddProducer", "AddProducer"));

				DeleteRegisteredAsset( ProducerWidgetPtr->ProducerPtr->Producer );
				DataprepAsset->Producers.RemoveAt( Index );

				UProperty* Property = FindField<UProperty>(UDataprepAsset::StaticClass(), GET_MEMBER_NAME_CHECKED(UDataprepAsset, Producers));
				FPropertyChangedEvent PropertyChangedEvent( Property, EPropertyChangeType::ArrayRemove );
				DataprepAsset->PostEditChangeProperty( PropertyChangedEvent );

				return FReply::Handled();
			};

			FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

			FDetailsViewArgs DetailsViewArgs;
			DetailsViewArgs.bAllowSearch = false;
			DetailsViewArgs.bAllowMultipleTopLevelObjects = true;
			DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;

			TSharedPtr<SBox> DetailsViewBox;

			TSharedPtr<SWidget> Widget = SNew(SBorder)
			.BorderImage( FEditorStyle::GetBrush("NoBrush") )
			.Padding(5.0f)
			[
				SNew(SHorizontalBox)
				// Check button
				+ SHorizontalBox::Slot()
				.HAlign(HAlign_Center)
				.AutoWidth()
				[
					SNew(SButton)
					.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
					.ToolTipText( LOCTEXT( "CheckToolTip", "Include or exclude this producer to the creation of the world " ) )
					.IsFocusable(false)
					.OnClicked_Lambda( CheckEntry )
					.VAlign(VAlign_Center)
					.Content()
					[
						SAssignNew( ProducerWidgetPtr->CheckBox, STextBlock )
						.Font( FEditorStyle::Get().GetFontStyle( DataprepProducerFontName ) )
						.ColorAndOpacity( FLinearColor::White )
						.Text(FText::FromString( FString( ProducerWidgetPtr->ProducerPtr->bIsEnabled ? TEXT( "\xf14a" ) : TEXT( "\xf0c8" ) ) ) )
					]
				]
				// Input entry label
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SAssignNew( DetailsViewBox, SBox )
				]
				// Delete button
				+ SHorizontalBox::Slot()
				.HAlign(HAlign_Center)
				.AutoWidth()
				[
					SNew(SButton)
					.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
					.ToolTipText( LOCTEXT( "DeleteToolTip", "Delete this producer" ) )
					.IsFocusable(false)
					.OnClicked_Lambda( DeleteEntry )
					.VAlign(VAlign_Center)
					.Content()
					[
						SNew( STextBlock )
						.Font( FEditorStyle::Get().GetFontStyle( DataprepProducerFontName ) )
						.ColorAndOpacity( FLinearColor::White )
						.Text( FText::FromString( FString( TEXT("\xf1f8") ) ) )
					]
				]
			];

			TSharedPtr<IDetailsView> DetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);

			DetailsViewBox->SetContent(DetailsView.ToSharedRef());

			DetailsView->SetObject( ProducerWidgetPtr->ProducerPtr->Producer );

			return Widget.ToSharedRef();
		}

		virtual void CreateBelowPinControls(TSharedPtr<SVerticalBox> MainBox) override
		{
			if(!bShowVisualWidget)
			{
				return;
			}

			TSharedPtr<SWidget> AddNewMenu = SNew(SComboButton)
			.ComboButtonStyle(FEditorStyle::Get(), "ToolbarComboButton")
			.ButtonStyle(FEditorStyle::Get(), "FlatButton.Success")
			.ForegroundColor(FLinearColor::White)
			.ToolTipText(LOCTEXT("AddNewToolTip", "Add a new producer."))
			.OnGetMenuContent(this, &SGraphNodeDataprepProducer::CreateAddProducerMenuWidget)
			.HasDownArrow(false)
			//.ContentPadding(FMargin(1, 0, 2, 0))
			.ButtonContent()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				//.AutoWidth()
				.MaxWidth(12)
				//.Padding(FMargin(0, 1))
				.HAlign(HAlign_Center)
				[
					// #ueent_todo: Replace image with plus sign from fontawesome
					SNew(SImage)
					.Image(FEditorStyle::GetBrush("Plus"))
				]
			];

			MainBox->AddSlot()
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					//.AutoWidth()
					.HAlign(EHorizontalAlignment::HAlign_Center)
					//.Padding(FMargin(10.f, 5.f, 10.f, 10.f))
					[
						AddNewMenu.ToSharedRef()
					]
				]
			];

			ProducerWidgets.SetNum( DataprepAsset->Producers.Num() );

			int32 Index = 0;
			for( FDataprepAssetProducer& Producer : DataprepAsset->Producers )
			{
				ProducerWidgets[ Index ].ProducerPtr = &Producer;

				MainBox->AddSlot()[
					CreateProducerWidget( &ProducerWidgets[ Index ], Index )
				];
				++Index;
			}
		}

	private:
		bool bNeedsUpdate;
		TArray< DataprepProducerDescription > ProducerDescriptions;
		UDataprepAsset* DataprepAsset;
		TArray< ProducerWidget > ProducerWidgets;
	};

	return SNew(SGraphNodeDataprepProducer, this);
}
#endif

#undef LOCTEXT_NAMESPACE
