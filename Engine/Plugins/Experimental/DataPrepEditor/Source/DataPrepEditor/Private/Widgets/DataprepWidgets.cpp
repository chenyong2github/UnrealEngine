// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DataprepWidgets.h"

#include "DataPrepAsset.h"
#include "DataprepAssetInstance.h"
#include "DataPrepContentConsumer.h"
#include "DataprepAssetView.h"
#include "DataprepEditorLogCategory.h"
#include "DataprepEditorUtils.h"
#include "DataprepParameterizableObject.h"
#include "Parameterization/DataprepParameterizationUtils.h"
#include "Widgets/Parameterization/SDataprepParameterizationLinkIcon.h"

#include "ContentBrowserModule.h"
#include "DetailLayoutBuilder.h"
#include "Dialogs/DlgPickPath.h"
#include "Editor.h"
#include "EditorFontGlyphs.h"
#include "EditorFontGlyphs.h"
#include "EditorStyleSet.h"
#include "Framework/Application/MenuStack.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "HAL/PlatformProcess.h"
#include "IContentBrowserSingleton.h"
#include "IDetailTreeNode.h"
#include "IPropertyRowGenerator.h"
#include "Input/Events.h"
#include "Layout/Geometry.h"
#include "Layout/WidgetPath.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "PropertyCustomizationHelpers.h"
#include "PropertyEditorModule.h"
#include "PropertyEditorModule.h"
#include "ScopedTransaction.h"
#include "Styling/CoreStyle.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Text/STextBlock.h"
#include "UObject/UObjectGlobals.h"

#define LOCTEXT_NAMESPACE "DataprepSlateHelper"

namespace DataprepWidgetUtils
{
	TSharedRef<SWidget> CreatePropertyWidget( TSharedPtr<SWidget> NameWidget, TSharedPtr<SWidget> ValueWidget, TSharedPtr< FDataprepDetailsViewColumnSizeData > ColumnSizeData, float Spacing, EHorizontalAlignment HAlign = EHorizontalAlignment::HAlign_Left, EVerticalAlignment VAlign = EVerticalAlignment::VAlign_Center )
	{
		return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.FillWidth(1.f)
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Left)
		.Padding(0.0f, 0.0f, 0.0f, Spacing)
		[
			SNew(SSplitter)
			.Style(FEditorStyle::Get(), "DetailsView.Splitter")
			.PhysicalSplitterHandleSize(1.0f)
			.HitDetectionSplitterHandleSize(5.0f)
			.ResizeMode( ESplitterResizeMode::Fill )
			+ SSplitter::Slot()
			.Value(ColumnSizeData->LeftColumnWidth)
			.OnSlotResized( SSplitter::FOnSlotResized::CreateLambda( [](float InNewWidth) -> void {} ) )
			[
				NameWidget.ToSharedRef()
			]
			+ SSplitter::Slot()
			.Value(ColumnSizeData->RightColumnWidth)
			.OnSlotResized(ColumnSizeData->OnWidthChanged)
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
					SNew(DataprepWidgetUtils::SConstrainedBox)
					[
						ValueWidget.ToSharedRef()
					]
				]
			]
		];
	}
}

void SDataprepCategoryWidget::ToggleExpansion()
{
	bIsExpanded = !bIsExpanded;
	CategoryContent->SetVisibility(bIsExpanded ? EVisibility::Visible : EVisibility::Collapsed);
}

const FSlateBrush* SDataprepCategoryWidget::GetBackgroundImage() const
{
	if (IsHovered())
	{
		return bIsExpanded ? FEditorStyle::GetBrush("DetailsView.CategoryTop_Hovered") : FEditorStyle::GetBrush("DetailsView.CollapsedCategory_Hovered");
	} 
	else
	{
		return bIsExpanded ? FEditorStyle::GetBrush("DetailsView.CategoryTop") : FEditorStyle::GetBrush("DetailsView.CollapsedCategory");
	}
}

void SDataprepCategoryWidget::Construct( const FArguments& InArgs, TSharedRef< SWidget > InContent, const TSharedRef<STableViewBase>& InOwnerTableView )
{
	const float MyContentTopPadding = 2.0f;
	const float MyContentBottomPadding = 2.0f;

	const float ChildSlotPadding = 2.0f;
	const float BorderVerticalPadding = 3.0f;

	CategoryContent = InContent;

	TSharedPtr<SWidget> TitleDetail = InArgs._TitleDetail;
	if( !TitleDetail.IsValid() )
	{
		TitleDetail = SNullWidget::NullWidget;
	}

	TSharedPtr<SHorizontalBox> TitleHeader = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.VAlign( VAlign_Center )
		.Padding( 2.0f, MyContentTopPadding, 2.0f, MyContentBottomPadding )
		.AutoWidth()
		[
			SNew( SExpanderArrow, SharedThis(this) )
		]
		+ SHorizontalBox::Slot()
		.VAlign( VAlign_Center )
		.AutoWidth()
		.Padding( 0.0f, 8.0f )
		[
			SNew(STextBlock)
			.Text( InArgs._Title )
			.Font( FEditorStyle::GetFontStyle("DetailsView.CategoryFontStyle") )
			.ShadowOffset( FVector2D( 1.0f, 1.0f ) )
		];

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew( SBorder )
			.BorderImage( this, &SDataprepCategoryWidget::GetBackgroundImage )
			.Padding( FMargin( 0.0f, BorderVerticalPadding, 16.0f, BorderVerticalPadding ) )
			.BorderBackgroundColor( FLinearColor( .6, .6, .6, 1.0f ) )
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth( 0.5f )
				.HAlign( EHorizontalAlignment::HAlign_Left )
				[
					TitleHeader.ToSharedRef()
				]
				+ SHorizontalBox::Slot()
				.FillWidth( 0.5f )
				.HAlign( EHorizontalAlignment::HAlign_Right )
				[
					TitleDetail.ToSharedRef()
				]
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			CategoryContent.ToSharedRef()
		]
	];

	STableRow< TSharedPtr< EDataprepCategory > >::ConstructInternal(
		STableRow::FArguments()
		.Style( FEditorStyle::Get(), "DetailsView.TreeView.TableRow" )
		.ShowSelection( false ),
		InOwnerTableView
	);
}

void SDataprepConsumerWidget::OnLevelNameChanged( const FText &NewLevelName, ETextCommit::Type CommitType )
{
	const FScopedTransaction Transaction( LOCTEXT("Consumer_SetLevelName", "Set Level Name") );
	FText OutReason;
	if( !DataprepConsumerPtr->SetLevelName( NewLevelName.ToString(), OutReason ) )
	{
		UE_LOG( LogDataprepEditor, Error, TEXT("%s"), *OutReason.ToString() );
		LevelTextBox->SetText( FText::FromString( DataprepConsumerPtr->GetLevelName() ) );
	}
}

void SDataprepConsumerWidget::OnTextCommitted( const FText& NewText, ETextCommit::Type CommitType)
{
	if( UDataprepContentConsumer* DataprepConsumer = DataprepConsumerPtr.Get() )
	{
		FString NewContentFolder( NewText.ToString() );

		// Replace /Content/ with /Game/ since /Content is only used for display 
		if( NewContentFolder.StartsWith( TEXT("/Content") ) )
		{
			NewContentFolder = NewContentFolder.Replace( TEXT( "/Content" ), TEXT( "/Game" ) );
		}
		
		// Remove ending '/' if applicable
		if( !NewContentFolder.IsEmpty() && NewContentFolder[ NewContentFolder.Len()-1 ] == TEXT('/') )
	{
			NewContentFolder[ NewContentFolder.Len()-1 ] = 0;
			NewContentFolder = NewContentFolder.LeftChop(1);
	}

		const FScopedTransaction Transaction( LOCTEXT("Consumer_SetTargetContentFolder", "Set Target Content Folder") );

		FText ErrorReason;
		if( !DataprepConsumer->SetTargetContentFolder( NewContentFolder, ErrorReason ) )
	{
			UE_LOG( LogDataprepEditor, Error, TEXT("%s"), *ErrorReason.ToString() );
			UpdateContentFolderText();
		}
	}
}

void SDataprepConsumerWidget::OnBrowseContentFolder()
{
	FString Path = DataprepConsumerPtr->GetTargetContentFolder();
	if( Path.IsEmpty() )
	{
		Path = FPaths::GetPath( DataprepConsumerPtr->GetOutermost()->GetPathName() );
	}
	Path += TEXT("/"); // Trailing '/' is needed to set the default path

	//Ask the user for the root path where they want any content to be placed
	if( UDataprepContentConsumer* DataprepConsumer = DataprepConsumerPtr.Get() )
	{
		TSharedRef<SDlgPickPath> PickContentPathDlg =
			SNew(SDlgPickPath)
			.Title(LOCTEXT("DataprepSlateHelper_ChooseImportRootContentPath", "Choose Location for importing the Datasmith content"))
			.DefaultPath(FText::FromString(Path));

		if ( PickContentPathDlg->ShowModal() == EAppReturnType::Ok )
		{
			const FScopedTransaction Transaction( LOCTEXT("Consumer_SetTargetContentFolder", "Set Target Content Folder") );

			FText ErrorReason;
			if( DataprepConsumer->SetTargetContentFolder( PickContentPathDlg->GetPath().ToString(), ErrorReason ) )
			{
			UpdateContentFolderText();
		}
			else
			{
				UE_LOG( LogDataprepEditor, Error, TEXT("%s"), *ErrorReason.ToString() );
			}
		}
	}
}

void SDataprepConsumerWidget::SetDataprepConsumer(UDataprepContentConsumer* InDataprepConsumer)
{
	if(InDataprepConsumer == nullptr)
	{
		return;
	}

	if(DataprepConsumerPtr != InDataprepConsumer)
	{
		if(DataprepConsumerPtr.IsValid())
		{
			DataprepConsumerPtr->GetOnChanged().Remove( OnConsumerChangedHandle );
		}

		DataprepConsumerPtr = InDataprepConsumer;

		OnConsumerChangedHandle = InDataprepConsumer->GetOnChanged().AddSP( this, &SDataprepConsumerWidget::OnConsumerChanged );

		OnConsumerChanged();
	}
}

void SDataprepConsumerWidget::Construct(const FArguments& InArgs )
{
	if (InArgs._ColumnSizeData.IsValid())
	{
		ColumnSizeData = InArgs._ColumnSizeData;
	}
	else
	{
		ColumnWidth = 0.7f;
		ColumnSizeData = MakeShared<FDataprepDetailsViewColumnSizeData>();
		ColumnSizeData->LeftColumnWidth = TAttribute<float>(this, &SDataprepConsumerWidget::OnGetLeftColumnWidth);
		ColumnSizeData->RightColumnWidth = TAttribute<float>(this, &SDataprepConsumerWidget::OnGetRightColumnWidth);
		ColumnSizeData->OnWidthChanged = SSplitter::FOnSlotResized::CreateSP(this, &SDataprepConsumerWidget::OnSetColumnWidth);
	}

	TSharedPtr<SWidget> InnerWidget = InArgs._DataprepConsumer ? BuildWidget() : BuildNullWidget();

	ChildSlot
	[
		InnerWidget.ToSharedRef()
	];

	if(InArgs._DataprepConsumer)
	{
		SetDataprepConsumer( InArgs._DataprepConsumer );
	}
}

SDataprepConsumerWidget::~SDataprepConsumerWidget()
{
	if ( UDataprepContentConsumer* DataprepConsumer = DataprepConsumerPtr.Get() )
	{
		DataprepConsumer->GetOnChanged().Remove( OnConsumerChangedHandle );
	}
}

TSharedRef<SWidget> SDataprepConsumerWidget::BuildWidget()
{
	TSharedRef<SWidget> BrowseButton = PropertyCustomizationHelpers::MakeBrowseButton( FSimpleDelegate::CreateSP( this, &SDataprepConsumerWidget::OnBrowseContentFolder ) );

	TSharedRef<SWidget> Widget = SNew(SBorder)
	.BorderImage( FCoreStyle::Get().GetBrush("NoBrush") )
	[
		SNew(SGridPanel)
		.FillColumn(0, 1.0f)
		+ SGridPanel::Slot(0, 0)
		.Padding(10.0f, 5.0f, 0.0f, 5.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Left)
			[
				SNew(SSplitter)
				.Style(FEditorStyle::Get(), "DetailsView.Splitter")
				.PhysicalSplitterHandleSize(1.0f)
				.HitDetectionSplitterHandleSize(5.0f)
				.ResizeMode(ESplitterResizeMode::Fill)
				+ SSplitter::Slot()
				.Value(ColumnSizeData->LeftColumnWidth)
				.OnSlotResized(SSplitter::FOnSlotResized::CreateSP(this, &SDataprepConsumerWidget::OnLeftColumnResized))
				[
					SNew(SHorizontalBox)
					.Clipping(EWidgetClipping::OnDemand)
					+ SHorizontalBox::Slot()
					.HAlign(HAlign_Left)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("DataprepSlateHelper_ContentFolderLabel", "Folder"))
						.Font(IDetailLayoutBuilder::GetDetailFont())
					]
				]
				+ SSplitter::Slot()
				.Value(ColumnSizeData->RightColumnWidth)
				.OnSlotResized(ColumnSizeData->OnWidthChanged)
				[
					SNew(SHorizontalBox)
					.Clipping(EWidgetClipping::OnDemand)
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.Padding(5.0f, 2.5f, 2.0f, 2.5f)
					[
						// Trick to force the splitter widget to fill up the space of its parent
						// Strongly inspired from SDetailSingleItemRow
						SNew(DataprepWidgetUtils::SConstrainedBox)
						[
							SAssignNew(ContentFolderTextBox, SEditableTextBox)
							.Font(IDetailLayoutBuilder::GetDetailFont())
							.HintText(LOCTEXT("DataprepSlateHelper_ContentFolderHintText", "Set the content folder to save in"))
							.IsReadOnly(false)
							.OnTextCommitted(FOnTextCommitted::CreateSP(this, &SDataprepConsumerWidget::OnTextCommitted))
						]
					]
				]
			]
		]
		+ SGridPanel::Slot(1, 0)
		.Padding(5.0f, 5.0f, 0.0f, 5.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Right)
			.AutoWidth()
			[
				BrowseButton
			]
		]
		// This column is required to align consumer section with producers' section which has 3 columns.
		+ SGridPanel::Slot(2, 0)
		.Padding(5.0f, 5.0f, 0.0f, 5.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Right)
			.AutoWidth()
			[
				SNew(SButton)
				.IsFocusable(false)
				.Visibility(EVisibility::Hidden)
				.IsEnabled(false)
				.VAlign(VAlign_Top)
				.Content()
				[
					SNew(STextBlock)
					.Font(FDataprepEditorUtils::GetGlyphFont())
					.ColorAndOpacity(FLinearColor::Transparent)
					.Text(FEditorFontGlyphs::Exclamation_Triangle)
				]
			]
		]
		+ SGridPanel::Slot(0, 1)
		.Padding(10.0f, 5.0f, 0.0f, 5.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Left)
			[
				SNew(SSplitter)
				.Style(FEditorStyle::Get(), "DetailsView.Splitter")
				.PhysicalSplitterHandleSize(1.0f)
				.HitDetectionSplitterHandleSize(5.0f)
				.ResizeMode(ESplitterResizeMode::Fill)
				+ SSplitter::Slot()
				.Value(ColumnSizeData->LeftColumnWidth)
				.OnSlotResized(SSplitter::FOnSlotResized::CreateSP(this, &SDataprepConsumerWidget::OnLeftColumnResized))
				[
					SNew(SHorizontalBox)
					.Clipping(EWidgetClipping::OnDemand)
					+ SHorizontalBox::Slot()
					.HAlign(HAlign_Left)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("DataprepSlateHelper_LevelNameLabel", "Sub-Level"))
						.Font(IDetailLayoutBuilder::GetDetailFont())
					]
				]
				+ SSplitter::Slot()
				.Value(ColumnSizeData->RightColumnWidth)
				.OnSlotResized(ColumnSizeData->OnWidthChanged)
				[
					SNew(SHorizontalBox)
					.Clipping(EWidgetClipping::OnDemand)
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.Padding(5.0f, 2.5f, 2.0f, 2.5f)
					[
						// Trick to force the splitter widget to fill up the space of its parent
						// Strongly inspired from SDetailSingleItemRow
						SNew(DataprepWidgetUtils::SConstrainedBox)
						[
							SAssignNew(LevelTextBox, SEditableTextBox)
							.Font(IDetailLayoutBuilder::GetDetailFont())
							.HintText(LOCTEXT("DataprepSlateHelper_LevelNameHintText", "Current will be used"))
							.OnTextCommitted(this, &SDataprepConsumerWidget::OnLevelNameChanged)
						]
					]
				]
			]
		]
	];

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
				.Text(LOCTEXT("DataprepSlateHelper_Error_InvalidConsumer", "Error: Not a valid consumer"))
				.Margin(FMargin( 5.0f, 5.0f, 0.0f, 0.0f ) )
				.ColorAndOpacity( FLinearColor(1,0,0,1) )
			]
		]
	];
}

void SDataprepConsumerWidget::UpdateContentFolderText()
{
	if(UDataprepContentConsumer* Consumer = DataprepConsumerPtr.Get())
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
	else
	{
		ContentFolderTextBox->SetText( TAttribute<FText>() );
	}
}

void SDataprepConsumerWidget::OnConsumerChanged()
{
	if(DataprepConsumerPtr.IsValid())
	{
		UpdateContentFolderText();
		LevelTextBox->SetText( FText::FromString( DataprepConsumerPtr->GetLevelName() ) );
	}
	else
	{
		ContentFolderTextBox->SetText( TAttribute<FText>() );
		LevelTextBox->SetText( TAttribute<FText>() );
	}
}


void SDataprepDetailsView::CreateDefaultWidget( int32 Index, TSharedPtr< SWidget >& NameWidget, TSharedPtr< SWidget >& ValueWidget, float LeftPadding, EHorizontalAlignment HAlign, EVerticalAlignment VAlign, const FDataprepParameterizationContext& ParameterizationContext)
{
	TSharedRef<SHorizontalBox> NameColumn = SNew(SHorizontalBox)
		.Clipping(EWidgetClipping::OnDemand);

	NameWidget->SetClipping( EWidgetClipping::OnDemand );

	// Add the name widget
	NameColumn->AddSlot()
	.VAlign(VAlign_Center)
	.HAlign(HAlign_Left)
	.Padding(FMargin(LeftPadding, 0.f, 0.f, 0.f))
	[
		NameWidget.ToSharedRef()
	];

	if ( ParameterizationContext.State == EParametrizationState::IsParameterized )
	{
		NameColumn->AddSlot()
		.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
		.Padding(FMargin(5.f, 0.f, 5.f, 0.f))
			.AutoWidth()
			[
			SNew(SDataprepParameterizationLinkIcon, DataprepAssetForParameterization.Get(), DetailedObjectAsParameterizable, ParameterizationContext.PropertyChain)
		];
	}

	FOnContextMenuOpening OnContextMenuOpening;

	if ( ParameterizationContext.State == EParametrizationState::IsParameterized || ParameterizationContext.State == EParametrizationState::CanBeParameterized )
	{
		OnContextMenuOpening.BindLambda( [this, InPropertyChain = ParameterizationContext.PropertyChain] () -> TSharedPtr<SWidget>
			{
				if ( UDataprepAsset * DataprepAsset = DataprepAssetForParameterization.Get() )
					{
					if ( DetailedObjectAsParameterizable )
						{
						FMenuBuilder MenuBuilder(true, nullptr);
						FDataprepEditorUtils::PopulateMenuForParameterization(MenuBuilder, *DataprepAsset, *DetailedObjectAsParameterizable, InPropertyChain);
						return MenuBuilder.MakeWidget();
						}
				}
				return TSharedPtr<SWidget>();
			});
	}

	GridPanel->AddSlot(0, Index)
	[
		SNew(SDataprepContextMenuOverride)
		.OnContextMenuOpening(OnContextMenuOpening)
		[
			DataprepWidgetUtils::CreatePropertyWidget( NameColumn, ValueWidget, ColumnSizeData, Spacing, HAlign, VAlign )
		]
		];

	if(bColumnPadding)
	{
		// Add two more columns to align parameter widget
		GridPanel->AddSlot(1, Index)
		.Padding(5.0f, 5.0f, 0.0f, 5.0f)
		[
			SNew(SHorizontalBox)
	+ SHorizontalBox::Slot()
	.VAlign(VAlign_Center)
			.HAlign(HAlign_Right)
			.AutoWidth()
	[
				SNew(SButton)
				.IsFocusable(false)
				.Visibility(EVisibility::Hidden)
				.IsEnabled(false)
				.VAlign(VAlign_Top)
				.Content()
		[
					SNew(STextBlock)
					.Font(FDataprepEditorUtils::GetGlyphFont())
					.ColorAndOpacity(FLinearColor::Transparent)
					.Text(FEditorFontGlyphs::Exclamation_Triangle)
				]
		]
		];

		GridPanel->AddSlot(2, Index)
		.Padding(5.0f, 5.0f, 0.0f, 5.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Right)
			.AutoWidth()
			[
				SNew(SButton)
				.IsFocusable(false)
				.Visibility(EVisibility::Hidden)
				.IsEnabled(false)
				.VAlign(VAlign_Top)
				.Content()
				[
					SNew(STextBlock)
					.Font(FDataprepEditorUtils::GetGlyphFont())
					.ColorAndOpacity(FLinearColor::Transparent)
					.Text(FEditorFontGlyphs::Exclamation_Triangle)
			]
		]
	];
	}
}

void SDataprepDetailsView::OnPropertyChanged(const FPropertyChangedEvent& InEvent)
{
	FDataprepEditorUtils::NotifySystemOfChangeInPipeline( DetailedObject );

	if( TrackedProperties.Contains( InEvent.Property ) )
	{
		ForceRefresh();
	}
}

void SDataprepDetailsView::OnObjectReplaced(const TMap<UObject*, UObject*>& ReplacementObjectMap)
{
	if ( UObject * const* ObjectPtr = ReplacementObjectMap.Find( DetailedObject ) )
	{
		DetailedObject = *ObjectPtr;
		if ( DetailedObject->IsA<UDataprepParameterizableObject>() )
		{
			DetailedObjectAsParameterizable = static_cast<UDataprepParameterizableObject*>( DetailedObject );
		}

		ForceRefresh();
	}
}

void SDataprepDetailsView::ForceRefresh()
{
	// ueent_hotfix Hotfix for 4.24 (Remove the ui flickering)
	InvalidatePrepass();
	bRefreshObjectToDisplay = true;
}

void SDataprepDetailsView::OnDataprepParameterizationStatusForObjectsChanged(const TSet<UObject*>* Objects)
{
	if ( !Objects || Objects->Contains( DetailedObjectAsParameterizable ) )
	{
		ForceRefresh();
	}
}

void SDataprepDetailsView::OnObjectTransacted(UObject* Object, const class FTransactionObjectEvent& TransactionObjectEvent)
{
	// Hack to support refresh the parameterization display of a dataprep instance
	if ( Object == DetailedObject || ( DetailedObject && DetailedObject->GetOuter() == Object ) )
	{
		ForceRefresh();
	}
}

void SDataprepDetailsView::AddWidgets( const TArray< TSharedRef< IDetailTreeNode > >& DetailTree, int32& Index, float LeftPadding, const FDataprepParameterizationContext& InParameterizationContext)
{
	auto IsDetailNodeDisplayable = []( const TSharedPtr< IPropertyHandle >& PropertyHandle)
	{
		if(PropertyHandle.IsValid() && PropertyHandle->IsValidHandle() && PropertyHandle->IsEditable())
		{
			FProperty* Property = PropertyHandle->GetProperty();

			if ( Property )
			{
				if ( FFieldVariant Outer = Property->GetOwnerVariant() )
				{
					// if the outer is a container property (array,set or map) it's editable even without the proper flags.
					//UClass* OuterClass = Outer->GetClass();
					if (Outer.IsA<FArrayProperty>() || Outer.IsA<FSetProperty>() || Outer.IsA<FMapProperty>() )
					{
						return true;
					}
				}

				return Property && !Property->HasAnyPropertyFlags(CPF_DisableEditOnInstance) && Property->HasAnyPropertyFlags(CPF_Edit);
			}
		}

		// Ok to display DetailNode without property because at this stage the parent property was displayable
		return true;
	};

	auto IsDetailNodeDisplayableContainerProperty = []( const TSharedPtr< IPropertyHandle >& PropertyHandle)
	{
		if( PropertyHandle.IsValid() && PropertyHandle->IsValidHandle() && PropertyHandle->IsEditable() )
		{
			if ( FProperty* Property = PropertyHandle->GetProperty() )
			{ 
				FFieldClass* PropertyClass = Property->GetClass();
				if ( PropertyClass == FArrayProperty::StaticClass() || PropertyClass == FSetProperty::StaticClass() || PropertyClass == FMapProperty::StaticClass() )
				{
					return !Property->HasAnyPropertyFlags(CPF_DisableEditOnInstance) && Property->HasAnyPropertyFlags(CPF_Edit);
				}
			}
		}

		return false;
	};

	for( const TSharedRef< IDetailTreeNode >& ChildNode : DetailTree )
	{
		TSharedPtr< IPropertyHandle > PropertyHandle = ChildNode->CreatePropertyHandle();
		FDataprepParameterizationContext CurrentParameterizationContext = FDataprepParameterizationUtils::CreateContext( PropertyHandle, InParameterizationContext );
		if ( CurrentParameterizationContext.State == EParametrizationState::CanBeParameterized )
		{
			if ( UDataprepAsset* DataprepAsset = DataprepAssetForParameterization.Get() )
			{
				if ( DataprepAsset->IsObjectPropertyBinded( DetailedObjectAsParameterizable, CurrentParameterizationContext.PropertyChain ) )
				{
					CurrentParameterizationContext.State = EParametrizationState::IsParameterized;
				}
			}
		}


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
			AddWidgets( Children, Index, LeftPadding, CurrentParameterizationContext );
		}
		else if( IsDetailNodeDisplayableContainerProperty( PropertyHandle ) )
		{
			TSharedPtr< IDetailPropertyRow > DetailPropertyRow = ChildNode->GetRow();
			if( DetailPropertyRow.IsValid() )
			{
				FDetailWidgetRow Row;
				TSharedPtr< SWidget > NameWidget;
				TSharedPtr< SWidget > ValueWidget;
				DetailPropertyRow->GetDefaultWidgets( NameWidget, ValueWidget, Row, true );

				CreateDefaultWidget( Index, NameWidget, ValueWidget, LeftPadding, Row.ValueWidget.HorizontalAlignment, Row.ValueWidget.VerticalAlignment, CurrentParameterizationContext );
				Index++;

				TArray< TSharedRef< IDetailTreeNode > > Children;
				ChildNode->GetChildren( Children );
				if( Children.Num() > 0 )
				{
					// #ueent_todo: Find a way to add collapse/expand capability for property with children
					AddWidgets( Children, Index, LeftPadding + 10.f, CurrentParameterizationContext );
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
				CreateDefaultWidget( Index, NameWidget, ValueWidget, LeftPadding, HAlign, VAlign, CurrentParameterizationContext );
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
					AddWidgets( Children, Index, LeftPadding + 10.f, CurrentParameterizationContext );
				}
			}
		}
	}
}

void SDataprepDetailsView::Construct(const FArguments& InArgs)
{
	bRefreshObjectToDisplay = false;
	DetailedObject = InArgs._Object;
	Spacing = InArgs._Spacing;
	bColumnPadding = InArgs._ColumnPadding;

	if (InArgs._ColumnSizeData.IsValid())
	{
		ColumnSizeData = InArgs._ColumnSizeData;
	}
	else
	{
		ColumnWidth = 0.7f;
		ColumnSizeData = MakeShared<FDataprepDetailsViewColumnSizeData>();
		ColumnSizeData->LeftColumnWidth = TAttribute<float>(this, &SDataprepDetailsView::OnGetLeftColumnWidth);
		ColumnSizeData->RightColumnWidth = TAttribute<float>(this, &SDataprepDetailsView::OnGetRightColumnWidth);
		ColumnSizeData->OnWidthChanged = SSplitter::FOnSlotResized::CreateSP(this, &SDataprepDetailsView::OnSetColumnWidth);
	}
	
	FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

	FPropertyRowGeneratorArgs Args;
	Generator = PropertyEditorModule.CreatePropertyRowGenerator(Args);

	if( DetailedObject != nullptr )
	{
		TArray< UObject* > Objects;
		Objects.Add( DetailedObject );
		Generator->SetObjects( Objects );
	}

	OnPropertyChangedHandle = Generator->OnFinishedChangingProperties().AddSP( this, &SDataprepDetailsView::OnPropertyChanged );

	if ( GEditor )
	{
		OnObjectReplacedHandle = GEditor->OnObjectsReplaced().AddSP(this, &SDataprepDetailsView::OnObjectReplaced);
		OnObjectTransactedHandle = FCoreUObjectDelegates::OnObjectTransacted.AddSP( this, &SDataprepDetailsView::OnObjectTransacted );
	}

	Construct();
}

void SDataprepDetailsView::Construct()
{
	DataprepAssetForParameterization.Reset();
	bHasCustomPrepass = true;

	if ( DetailedObject )
	{
		UDataprepAsset* DataprepAsset = FDataprepParameterizationUtils::GetDataprepAssetForParameterization( DetailedObject );
		if ( DataprepAsset )
		{
			OnDataprepParameterizationStatusForObjectsChangedHandle = DataprepAsset->OnParameterizedObjectsChanged.AddSP( this, &SDataprepDetailsView::OnDataprepParameterizationStatusForObjectsChanged );
		}

		if ( DetailedObject->IsA<UDataprepParameterizableObject>() )
		{
			DetailedObjectAsParameterizable = static_cast<UDataprepParameterizableObject*>(DetailedObject);
		}

		FDataprepParameterizationContext ParameterizationContext;
		ParameterizationContext.State = DataprepAsset && DetailedObjectAsParameterizable ? EParametrizationState::CanBeParameterized : EParametrizationState::InvalidForParameterization;
		DataprepAssetForParameterization = DataprepAsset;

		GridPanel = SNew(SGridPanel).FillColumn( 0.0f, 1.0f );
		TArray< TSharedRef< IDetailTreeNode > > RootNodes = Generator->GetRootTreeNodes();

		int32 Index = 0;
		AddWidgets(RootNodes, Index, 0.f, ParameterizationContext);

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
		FText ErrorText = LOCTEXT( "DataprepSlateHelper_InvalidDetailedObject", "Error: Not a valid Object" );

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

	if ( GEditor )
	{
		GEditor->OnObjectsReplaced().Remove( OnObjectReplacedHandle );
		FCoreUObjectDelegates::OnObjectTransacted.Remove( OnObjectTransactedHandle );
	}

	if ( UDataprepAsset* DataprepAsset = DataprepAssetForParameterization.Get() )
	{
		DataprepAsset->OnParameterizedObjectsChanged.Remove( OnDataprepParameterizationStatusForObjectsChangedHandle );
	}
}

void SDataprepDetailsView::SetObjectToDisplay(UObject& Object)
{
	UObject* NewObjectToDisplay = &Object;
	if ( DetailedObject != NewObjectToDisplay )
	{
		DetailedObject = NewObjectToDisplay;
		if ( DetailedObject->IsA<UDataprepParameterizableObject>() )
		{
			DetailedObjectAsParameterizable = static_cast<UDataprepParameterizableObject*>( DetailedObject );
		}
		ForceRefresh();
	}
}

void SDataprepDetailsView::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject( DetailedObject );
	Collector.AddReferencedObject( DetailedObjectAsParameterizable );
	for (FProperty* Property : TrackedProperties)
	{
		if (Property)
		{
			Property->AddReferencedObjects(Collector);
		}
	}
}

bool SDataprepDetailsView::CustomPrepass(float LayoutScaleMultiplier)
{
	if ( bRefreshObjectToDisplay )
	{
		TArray< UObject* > Objects;
		Objects.Add( DetailedObject );
		Generator->SetObjects( Objects );
		Construct();
		bRefreshObjectToDisplay = false;
	}

	return true;
}

void SDataprepContextMenuOverride::Construct(const FArguments& InArgs)
{
	OnContextMenuOpening = InArgs._OnContextMenuOpening;
	
	ChildSlot
	[
		InArgs._DefaultSlot.Widget
	];
}

FReply SDataprepContextMenuOverride::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if ( MouseEvent.GetEffectingButton() == EKeys::RightMouseButton && OnContextMenuOpening.IsBound() )
	{
		TSharedPtr<SWidget> ContextMenu = OnContextMenuOpening.Execute();
		if ( ContextMenu )
		{ 
			FWidgetPath WidgetPath = MouseEvent.GetEventPath() ? *MouseEvent.GetEventPath() : FWidgetPath();
			FSlateApplication::Get().PushMenu(
				AsShared(),
				WidgetPath,
				ContextMenu.ToSharedRef(),
				MouseEvent.GetScreenSpacePosition(),
				FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu)
				);

			return FReply::Handled();
		}
	}

	return FReply::Unhandled();
}

void SDataprepInstanceParentWidget::Construct(const FArguments& InArgs)
{
	DataprepInstancePtr = InArgs._DataprepInstance;
	if(!DataprepInstancePtr.IsValid())
	{
		return;
	}

	if (InArgs._ColumnSizeData.IsValid())
	{
		ColumnSizeData = InArgs._ColumnSizeData;
	}
	else
	{
		ColumnWidth = 0.7f;
		ColumnSizeData = MakeShared<FDataprepDetailsViewColumnSizeData>();
		ColumnSizeData->LeftColumnWidth = TAttribute<float>(this, &SDataprepInstanceParentWidget::OnGetLeftColumnWidth);
		ColumnSizeData->RightColumnWidth = TAttribute<float>(this, &SDataprepInstanceParentWidget::OnGetRightColumnWidth);
		ColumnSizeData->OnWidthChanged = SSplitter::FOnSlotResized::CreateSP(this, &SDataprepInstanceParentWidget::OnSetColumnWidth);
	}


	TSharedRef<SWidget> NameWidget = SNew(SHorizontalBox)
	.Clipping(EWidgetClipping::OnDemand)
	+ SHorizontalBox::Slot()
	.VAlign(VAlign_Center)
	.HAlign(HAlign_Left)
	.Padding(FMargin(0.f, 0.f, 0.f, 0.f))
	[
		SNew( STextBlock )
		.Text( LOCTEXT("DataprepInstanceParentWidget_Parent_Label", "Parent") )
		.Font( IDetailLayoutBuilder::GetDetailFont() )
	];

	TSharedRef<SWidget> ValueWidget = SNew( SObjectPropertyEntryBox )
	.AllowedClass( UDataprepAssetInterface::StaticClass() )
	.OnObjectChanged( this, &SDataprepInstanceParentWidget::SetDataprepInstanceParent )
	.OnShouldFilterAsset( this, &SDataprepInstanceParentWidget::ShouldFilterAsset )
	.ObjectPath( this, &SDataprepInstanceParentWidget::GetDataprepInstanceParent );

	// The widget is disabled as the workflow to change the parent asset of a Dataprep instance is refined
	ValueWidget->SetEnabled( false );

	ChildSlot
	[
		SNew(SHorizontalBox)
		+SHorizontalBox::Slot()
		.FillWidth(1)
		[
			SNew(SVerticalBox)
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
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				DataprepWidgetUtils::CreatePropertyWidget( NameWidget, ValueWidget, ColumnSizeData, 0.0f )
			]
		]
	];
}

void SDataprepInstanceParentWidget::SetDataprepInstanceParent(const FAssetData& InAssetData)
{
	if(UDataprepAssetInstance* DataprepInstance = DataprepInstancePtr.Get())
	{
		if ( UDataprepAsset* DataprepAsset = Cast<UDataprepAsset>( InAssetData.GetAsset() ) )
		{
			const FScopedTransaction Transaction( LOCTEXT("DataprepInstance_SetParent", "Set Parent") );
			DataprepInstance->SetParent( DataprepAsset );
		}
	}
}

FString SDataprepInstanceParentWidget::GetDataprepInstanceParent() const
{
	FString PathName;

	if(UDataprepAssetInstance* DataprepInstance = DataprepInstancePtr.Get())
	{
		if(DataprepInstance->GetParent())
		{
			PathName = DataprepInstance->GetParent()->GetPathName();
		}
	}

	return PathName;
}

bool SDataprepInstanceParentWidget::ShouldFilterAsset(const FAssetData& InAssetData)
{
	if(UDataprepAssetInstance* DataprepInstance = DataprepInstancePtr.Get())
	{
		if ( InAssetData.GetClass() == UDataprepAssetInterface::StaticClass() )
		{
			FAssetData CurrentAssetData(DataprepInstance->GetParent());
			return CurrentAssetData != InAssetData;
		}
	}

	return false;
}

TSharedRef<SWidget> DataprepWidgetUtils::CreateParameterRow( TSharedPtr<SWidget> ParameterWidget )
{
	return  SNew(SGridPanel)
	.FillColumn(0, 1.0f)
	+ SGridPanel::Slot(0, 0)
	.Padding(10.0f, 5.0f, 0.0f, 5.0f)
	[
		ParameterWidget.ToSharedRef()
	]
	// Add two more columns to align parameter widget
	+ SGridPanel::Slot(1, 0)
	.Padding(5.0f, 5.0f, 0.0f, 5.0f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Right)
		.AutoWidth()
		[
			SNew(SButton)
			.IsFocusable(false)
			.Visibility(EVisibility::Hidden)
			.IsEnabled(false)
			.VAlign(VAlign_Top)
			.Content()
			[
				SNew(STextBlock)
				.Font(FDataprepEditorUtils::GetGlyphFont())
				.ColorAndOpacity(FLinearColor::Transparent)
				.Text(FEditorFontGlyphs::Exclamation_Triangle)
			]
		]
	]
	+ SGridPanel::Slot(2, 0)
	.Padding(5.0f, 5.0f, 0.0f, 5.0f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Right)
		.AutoWidth()
		[
			SNew(SButton)
			.IsFocusable(false)
			.Visibility(EVisibility::Hidden)
			.IsEnabled(false)
			.VAlign(VAlign_Top)
			.Content()
			[
				SNew(STextBlock)
				.Font(FDataprepEditorUtils::GetGlyphFont())
				.ColorAndOpacity(FLinearColor::Transparent)
				.Text(FEditorFontGlyphs::Exclamation_Triangle)
			]
		]
	];
}

#undef LOCTEXT_NAMESPACE
