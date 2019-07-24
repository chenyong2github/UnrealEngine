// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DataprepWidgets.h"

#include "DataPrepContentConsumer.h"
#include "DataprepEditorUtils.h"

#include "ContentBrowserModule.h"
#include "DetailLayoutBuilder.h"
#include "Dialogs/DlgPickPath.h"
#include "EditorStyleSet.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "PropertyEditorModule.h"
#include "HAL/PlatformProcess.h"
#include "IContentBrowserSingleton.h"
#include "IDetailTreeNode.h"
#include "IPropertyRowGenerator.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "PropertyCustomizationHelpers.h"
#include "PropertyEditorModule.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"

#define LOCTEXT_NAMESPACE "DataprepSlateHelper"

void SDataprepConsumerWidget::OnLevelNameChanged( const FText &NewLevelName, ETextCommit::Type CommitType )
{
	FText OutReason;
	if( !DataprepConsumer->SetLevelName( NewLevelName.ToString(), OutReason ) )
	{
		// #ueent_todo: Warn user name is wrong
		LevelTextBox->SetText( FText::FromString( DataprepConsumer->GetLevelName() ) );
	}
}

void SDataprepConsumerWidget::OnBrowseContentFolder()
{
	FString Path = DataprepConsumer->GetTargetContentFolder();
	if( Path.IsEmpty() )
	{
		Path = FPaths::GetPath( DataprepConsumer->GetOutermost()->GetPathName() );
	}
	Path += TEXT("/"); // Trailing '/' is needed to set the default path

	//Ask the user for the root path where they want any content to be placed
	{
		TSharedRef<SDlgPickPath> PickContentPathDlg =
			SNew(SDlgPickPath)
			.Title(LOCTEXT("DataprepSlateHelper_ChooseImportRootContentPath", "Choose Location for importing the Datasmith content"))
			.DefaultPath(FText::FromString(Path));

		if ( PickContentPathDlg->ShowModal() == EAppReturnType::Ok )
		{
			DataprepConsumer->SetTargetContentFolder( PickContentPathDlg->GetPath().ToString() );
			UpdateContentFolderText();
		}
	}
}

void SDataprepConsumerWidget::SetDataprepConsumer(UDataprepContentConsumer* InDataprepConsumer)
{
	if(InDataprepConsumer == nullptr)
	{
		return;
	}

	DataprepConsumer = InDataprepConsumer;

	UpdateContentFolderText();
	LevelTextBox->SetText( FText::FromString( DataprepConsumer->GetLevelName() ) );
}

void SDataprepConsumerWidget::Construct(const FArguments& InArgs)
{
	DataprepConsumer = InArgs._DataprepConsumer;

	ChildSlot
	[
		BuildWidget()
	];
}

TSharedRef<SWidget> SDataprepConsumerWidget::BuildWidget()
{
	if(DataprepConsumer == nullptr)
	{
		return BuildNullWidget();
	}

	TSharedRef<SWidget> BrowseButton = PropertyCustomizationHelpers::MakeBrowseButton( FSimpleDelegate::CreateSP( this, &SDataprepConsumerWidget::OnBrowseContentFolder ) );

	TSharedRef<SWidget> Widget = SNew(SBorder)
	.BorderImage( FCoreStyle::Get().GetBrush("NoBrush") )
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.Padding( FMargin( 20, 0, 0, 0 ) )
		[
			SNew(SGridPanel)
			.FillColumn(0, 0.25f)
			.FillColumn(1, 0.75f)
			+ SGridPanel::Slot(0, 0)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				.FillWidth(1.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("DataprepSlateHelper_ContentFolderLabel", "Folder"))
					.Font( IDetailLayoutBuilder::GetDetailFont() )
				]
			]
			+ SGridPanel::Slot(1, 0)
			.Padding(0.0f, 5.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.FillWidth(1.0f)
				[
					SAssignNew(ContentFolderTextBox, SEditableTextBox)
					.Font( IDetailLayoutBuilder::GetDetailFont() )
					.HintText( LOCTEXT("DataprepSlateHelper_ContentFolderHintText", "Set the content folder to save in") )
					.IsReadOnly( true )
				]
				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Center)
				[
					BrowseButton
				]
			]
			+ SGridPanel::Slot(0, 1)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				.FillWidth(1.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("DataprepSlateHelper_LevelNameLabel", "Sub-Level"))
					.Font( IDetailLayoutBuilder::GetDetailFont() )
				]
			]
			+ SGridPanel::Slot(1, 1)
			.Padding(0.0f, 5.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				.FillWidth(1.0f)
				[
					SAssignNew(LevelTextBox, SEditableTextBox)
					.Text( FText::FromString( DataprepConsumer->GetLevelName() ) )
					.Font( IDetailLayoutBuilder::GetDetailFont() )
					.HintText( LOCTEXT("DataprepSlateHelper_LevelNameHintText", "Current will be used") )
					.OnTextCommitted(  this, &SDataprepConsumerWidget::OnLevelNameChanged  )
				]
			]
		]
	];

	UpdateContentFolderText();

	return Widget;
}

TSharedRef<SWidget> SDataprepConsumerWidget::BuildNullWidget()
{
	return SNew(SBorder)
	.BorderImage( FCoreStyle::Get().GetBrush("ToolPanel.GroupBorder") )
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.Padding(5.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.FillWidth(1.0f)
			[
				SNew(STextBlock)
				.Font( IDetailLayoutBuilder::GetDetailFont() )
				.Text(LOCTEXT("DataprepSlateHelper_ContentFolderLabel", "Error: Not a valid consumer"))
				.Margin(FMargin( 5.0f, 5.0f, 0.0f, 0.0f ) )
				.ColorAndOpacity( FLinearColor(1,0,0,1) )
			]
		]
	];
}

void SDataprepConsumerWidget::UpdateContentFolderText()
{
	if(UDataprepContentConsumer* Consumer = DataprepConsumer.Get())
	{
		FString TargetContentFolder( Consumer->GetTargetContentFolder() );

		if( TargetContentFolder.IsEmpty() )
		{
			TargetContentFolder = TEXT("/Content");
		}
		else if( TargetContentFolder.StartsWith( TEXT( "/Game" ) ) )
		{
			TargetContentFolder = TargetContentFolder.Replace( TEXT( "/Game" ), TEXT( "/Content" ) );
		}

		ContentFolderTextBox->SetText( FText::FromString( TargetContentFolder + TEXT( "/" ) ) );
	}
}

TSharedRef< SWidget > SDataprepDetailsView::CreateDefaultWidget( TSharedPtr< SWidget >& NameWidget, TSharedPtr< SWidget >& ValueWidget, float LeftPadding, EHorizontalAlignment HAlign, EVerticalAlignment VAlign )
{
	/** Helper class to force a widget to fill in a space. Copied from SDetailSingleItemRow.cpp */
	class SConstrainedBox : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SConstrainedBox)
		{}
		SLATE_DEFAULT_SLOT(FArguments, Content)
			SLATE_END_ARGS()

			void Construct(const FArguments& InArgs)
		{
			ChildSlot
			[
				InArgs._Content.Widget
			];
		}

		virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override
		{
			// Voluntarily ridiculously large value to force the child widget to fill up the available space
			const float MinWidthVal = 2000;
			const FVector2D ChildSize = ChildSlot.GetWidget()->GetDesiredSize();
			return FVector2D( FMath::Max( MinWidthVal, ChildSize.X ), ChildSize.Y );
		}
	};

	return SNew(SHorizontalBox)
	+ SHorizontalBox::Slot()
	.FillWidth(1.f)
	.VAlign(VAlign_Center)
	.HAlign(HAlign_Left)
	[
		SNew(SSplitter)
		.Style(FEditorStyle::Get(), "DetailsView.Splitter")
		.PhysicalSplitterHandleSize(1.0f)
		.HitDetectionSplitterHandleSize(5.0f)
		.ResizeMode( ESplitterResizeMode::Fill )
		+ SSplitter::Slot()
		.Value(ColumnSizeData.LeftColumnWidth)
		.OnSlotResized( SSplitter::FOnSlotResized::CreateSP( this, &SDataprepDetailsView::OnLeftColumnResized ) )
		[
			SNew(SHorizontalBox)
			.Clipping(EWidgetClipping::OnDemand)
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Left)
			.Padding( FMargin( LeftPadding, 0.f, 0.f, 0.f ) )
			[
				NameWidget.ToSharedRef()
			]
		]
		+ SSplitter::Slot()
		.Value(ColumnSizeData.RightColumnWidth)
		.OnSlotResized(ColumnSizeData.OnWidthChanged)
		[
			SNew(SHorizontalBox)
			.Clipping(EWidgetClipping::OnDemand)
			+ SHorizontalBox::Slot()
			.Padding( 5.0f, 2.5f, 2.0f, 2.5f )
			.HAlign( HAlign )
			.VAlign( VAlign )
			[
				// Trick to force the splitter widget to fill up the space of its parent
				// Strongly inspired from SDetailSingleItemRow
				SNew(SConstrainedBox)
				[
					ValueWidget.ToSharedRef()
				]
			]
		]
	];
}

void SDataprepDetailsView::OnPropertyChanged(const FPropertyChangedEvent& InEvent)
{
	FDataprepEditorUtils::NotifySystemOfChangeInPipeline( DetailedObject );

	if( TrackedProperties.Contains( InEvent.Property ) )
	{
		TArray< UObject* > Objects;
		Generator->SetObjects( Objects );

		TrackedProperties.Empty();

		ChildSlot.DetachWidget();

		Objects.Add( DetailedObject );
		Generator->SetObjects( Objects );

		Construct();
	}
}

void SDataprepDetailsView::AddWidgets( const TArray< TSharedRef< IDetailTreeNode > >& DetailTree, TSharedPtr<SGridPanel>& GridPanel, int32& Index, float LeftPadding )
{
	auto IsDetailNodeDisplayable = []( const TSharedPtr< IPropertyHandle >& PropertyHandle)
	{
		if(PropertyHandle.IsValid() && PropertyHandle->IsValidHandle() && PropertyHandle->IsEditable())
		{
			return PropertyHandle->GetProperty() != nullptr && !PropertyHandle->GetProperty()->HasAnyPropertyFlags( CPF_DisableEditOnInstance );
		}

		return true;
	};

	auto IsDetailNodeAPropertyArray = []( const TSharedPtr< IPropertyHandle >& PropertyHandle)
	{
		if( PropertyHandle.IsValid() && PropertyHandle->IsValidHandle() && PropertyHandle->IsEditable() )
		{
			return Cast<UArrayProperty>( PropertyHandle->GetProperty() ) != nullptr;
		}

		return false;
	};

	for( const TSharedRef< IDetailTreeNode >& ChildNode : DetailTree )
	{
		TSharedPtr< IPropertyHandle > PropertyHandle = ChildNode->CreatePropertyHandle();

		if( ChildNode->GetNodeType() == EDetailNodeType::Category )
		{
			if( Index > 0 )
			{
				GridPanel->AddSlot( 0, Index )
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.VAlign(VAlign_Center)
					.HAlign(HAlign_Left)
					[
						SNew(SSpacer)
						.Size( FVector2D( 0.f, 10.f ) )
					]
				];
				Index++;
			}

			TArray< TSharedRef< IDetailTreeNode > > Children;
			ChildNode->GetChildren( Children );
			AddWidgets( Children, GridPanel, Index, LeftPadding );
		}
		else if( IsDetailNodeAPropertyArray( PropertyHandle ) )
		{
			TSharedPtr< IDetailPropertyRow > DetailPropertyRow = ChildNode->GetRow();
			if( DetailPropertyRow.IsValid() )
			{
				FDetailWidgetRow Row;
				TSharedPtr< SWidget > NameWidget;
				TSharedPtr< SWidget > ValueWidget;
				DetailPropertyRow->GetDefaultWidgets( NameWidget, ValueWidget, Row, true );
				GridPanel->AddSlot( 0, Index )
				[
					CreateDefaultWidget( NameWidget, ValueWidget, LeftPadding, Row.ValueWidget.HorizontalAlignment, Row.ValueWidget.VerticalAlignment )
				];
				Index++;

				TArray< TSharedRef< IDetailTreeNode > > Children;
				ChildNode->GetChildren( Children );
				if( Children.Num() > 0 )
				{
					// #ueent_todo: Find a way to add collapse/expand capability for property with children
					AddWidgets( Children, GridPanel, Index, LeftPadding + 10.f );
				}

				TrackedProperties.Add( PropertyHandle->GetProperty() );
			}
		}
		else if( IsDetailNodeDisplayable( PropertyHandle ) )
		{
			TSharedPtr< SWidget > NameWidget;
			TSharedPtr< SWidget > ValueWidget;
			EHorizontalAlignment HAlign;
			EVerticalAlignment VAlign;

			TSharedPtr< IDetailPropertyRow > DetailPropertyRow = ChildNode->GetRow();
			if( DetailPropertyRow.IsValid() )
			{
				FDetailWidgetRow Row;
				DetailPropertyRow->GetDefaultWidgets( NameWidget, ValueWidget, Row, true );
				HAlign = Row.ValueWidget.HorizontalAlignment;
				VAlign = Row.ValueWidget.VerticalAlignment;
			}
			else
			{
				FNodeWidgets NodeWidgets = ChildNode->CreateNodeWidgets();

				NameWidget = NodeWidgets.NameWidget;
				ValueWidget = NodeWidgets.ValueWidget;
				HAlign = NodeWidgets.ValueWidgetLayoutData.HorizontalAlignment;
				VAlign = NodeWidgets.ValueWidgetLayoutData.VerticalAlignment;
			}

			if( NameWidget.IsValid() && ValueWidget.IsValid() )
			{
				GridPanel->AddSlot(0, Index)
				[
					CreateDefaultWidget( NameWidget, ValueWidget, LeftPadding, HAlign, VAlign )
				];
				Index++;

				bool bDisplayChildren = true;

				// Do not display children if the property is a FVector or FVector2D
				if( PropertyHandle.IsValid() )
				{
					FVector DummyVec;
					FVector2D DummyVec2D;

					bDisplayChildren &= PropertyHandle->GetValue( DummyVec ) == FPropertyAccess::Fail;
					bDisplayChildren &= PropertyHandle->GetValue( DummyVec2D ) == FPropertyAccess::Fail;
				}

				TArray< TSharedRef< IDetailTreeNode > > Children;
				ChildNode->GetChildren( Children );
				if( bDisplayChildren && Children.Num() > 0 )
				{
					// #ueent_todo: Find a way to add collapse/expand capability for property with children
					AddWidgets( Children, GridPanel, Index, LeftPadding + 10.f );
				}
			}
		}
	}
}

void SDataprepDetailsView::Construct(const FArguments& InArgs)
{
	ObjectAttribute =  InArgs._Object;
	if( !ObjectAttribute.IsSet() )
	{
		ChildSlot
		[
			SNullWidget::NullWidget
		];

		return;
	}

	DetailedObject = InArgs._Object.Get();
	DetailedClass = InArgs._Class.Get();

	FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

	FPropertyRowGeneratorArgs Args;
	Generator = PropertyEditorModule.CreatePropertyRowGenerator(Args);

	if( DetailedObject != nullptr )
	{
		TArray< UObject* > Objects;
		Objects.Add( DetailedObject );
		Generator->SetObjects( Objects );
	}

	ColumnWidth = 0.7f;
	ColumnSizeData.LeftColumnWidth = TAttribute<float>( this, &SDataprepDetailsView::OnGetLeftColumnWidth );
	ColumnSizeData.RightColumnWidth = TAttribute<float>( this, &SDataprepDetailsView::OnGetRightColumnWidth );
	ColumnSizeData.OnWidthChanged = SSplitter::FOnSlotResized::CreateSP( this, &SDataprepDetailsView::OnSetColumnWidth );
	OnPropertyChangedHandle = Generator->OnFinishedChangingProperties().AddSP( this, &SDataprepDetailsView::OnPropertyChanged );

	Construct();
}

void SDataprepDetailsView::Construct()
{
	if ( DetailedObject )
	{
		TSharedPtr<SGridPanel> GridPanel = SNew(SGridPanel).FillColumn(0.0f, 1.0f);

		TArray< TSharedRef< IDetailTreeNode > > RootNodes = Generator->GetRootTreeNodes();

		int32 Index = 0;
		AddWidgets(RootNodes, GridPanel, Index, 0.f);

		ChildSlot
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.Padding(5.0f)
			[
				GridPanel.ToSharedRef()
			]
		];
	}
	else
	{
		FText ErrorText = FText::Format( LOCTEXT( "DataprepSlateHelper_InvalidDetailedObject", "Error: Not a valid {0}" ), FText::FromString( DetailedClass ? DetailedClass->GetName() : UObject::StaticClass()->GetName() ) );

		ChildSlot
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.Padding(5.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				.FillWidth(1.0f)
				[
					SNew(STextBlock)
					.Font( IDetailLayoutBuilder::GetDetailFontBold() )
					.Text( ErrorText )
					.Margin(FMargin( 5.0f, 5.0f, 0.0f, 0.0f ) )
					.ColorAndOpacity( FLinearColor(1,0,0,1) )
				]
			]
		];
	}
}

SDataprepDetailsView::~SDataprepDetailsView()
{
	Generator->OnFinishedChangingProperties().Remove( OnPropertyChangedHandle );
}

void SDataprepDetailsView::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	Super::Tick( AllottedGeometry, InCurrentTime, InDeltaTime );

	UObject* Object = ObjectAttribute.Get();
	if ( DetailedObject != Object )
	{
		DetailedObject = Object;
		if ( DetailedObject )
		{
			TArray< UObject* > Objects;
			Objects.Add(DetailedObject);
			Generator->SetObjects(Objects);
		}
		Construct();
	}
}

#undef LOCTEXT_NAMESPACE
