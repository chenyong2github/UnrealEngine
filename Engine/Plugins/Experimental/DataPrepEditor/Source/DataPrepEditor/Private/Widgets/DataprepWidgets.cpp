// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DataprepWidgets.h"

#include "DataPrepAsset.h"
#include "DataPrepContentConsumer.h"
#include "DataprepAssetView.h"
#include "DataprepEditorUtils.h"
#include "Parameterization/DataprepParameterizationUtils.h"

#include "ContentBrowserModule.h"
#include "DetailLayoutBuilder.h"
#include "Dialogs/DlgPickPath.h"
#include "Editor.h"
#include "EditorFontGlyphs.h"
#include "EditorFontGlyphs.h"
#include "EditorStyleSet.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "HAL/PlatformProcess.h"
#include "IContentBrowserSingleton.h"
#include "IDetailTreeNode.h"
#include "IPropertyRowGenerator.h"
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
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "DataprepSlateHelper"

/** Helper class to force a widget to fill in a space. Copied from SDetailSingleItemRow.cpp */
class SConstrainedBox : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SConstrainedBox)
	{
	}
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
		return FVector2D(FMath::Max(MinWidthVal, ChildSize.X), ChildSize.Y);
	}
};

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

void SDataprepConsumerWidget::Construct(const FArguments& InArgs )
{
	DataprepConsumer = InArgs._DataprepConsumer;

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
						SNew(SConstrainedBox)
						[
							SAssignNew(ContentFolderTextBox, SEditableTextBox)
							.Font(IDetailLayoutBuilder::GetDetailFont())
							.HintText(LOCTEXT("DataprepSlateHelper_ContentFolderHintText", "Set the content folder to save in"))
							.IsReadOnly(false)
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
						SNew(SConstrainedBox)
						[
							SAssignNew(LevelTextBox, SEditableTextBox)
							.Text(FText::FromString(DataprepConsumer->GetLevelName()))
							.Font(IDetailLayoutBuilder::GetDetailFont())
							.HintText(LOCTEXT("DataprepSlateHelper_LevelNameHintText", "Current will be used"))
							.OnTextCommitted(this, &SDataprepConsumerWidget::OnLevelNameChanged)
						]
					]
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
				.Text(LOCTEXT("DataprepSlateHelper_Error_InvalidConsumer", "Error: Not a valid consumer"))
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


TSharedRef< SWidget > SDataprepDetailsView::CreateDefaultWidget( TSharedPtr< SWidget >& NameWidget, TSharedPtr< SWidget >& ValueWidget, float LeftPadding, EHorizontalAlignment HAlign, EVerticalAlignment VAlign, const FDataprepParameterizationContext& ParameterizationContext)
{
	TSharedRef<SHorizontalBox> NameColumn = SNew(SHorizontalBox)
		.Clipping(EWidgetClipping::OnDemand);

	// Optionally add the parameterization widget
	if ( DataprepAssetForParameterization.IsValid() )
	{
		if ( ParameterizationContext.State == EParametrizationState::CanBeParameterized )
		{
			NameColumn->AddSlot()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				.AutoWidth()
				[

					SNew(STextBlock)
					.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.11"))
					.Text(FEditorFontGlyphs::Plus_Circle)
					.Margin(FMargin(5.0f, 5.0f, 0.0f, 0.0f))
					.ColorAndOpacity(FColor::Blue)
					.OnDoubleClicked_Lambda([this, PropertyChain = ParameterizationContext.PropertyChain](const FGeometry&,const FPointerEvent&) -> FReply
						{
							if ( UDataprepAsset* DataprepAsset = DataprepAssetForParameterization.Get() )
							{
								FScopedTransaction Transaction( LOCTEXT("DataprepBindingToParameterization","Adding Parameter") );
								DataprepAssetForParameterization->BindObjectPropertyToParameterization( DetailedObject, PropertyChain, *PropertyChain.Last().CachedProperty->GetDisplayNameText().ToString() );
								bRefreshObjectToDisplay = true;
							}

							return FReply::Handled();
						})
				];
		}
		else if ( ParameterizationContext.State == EParametrizationState::IsParameterized )
		{
			NameColumn->AddSlot()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				.AutoWidth()
				[

					SNew(STextBlock)
					.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.11"))
					.Text(FEditorFontGlyphs::Minus_Circle)
					.Margin(FMargin(5.0f, 5.0f, 0.0f, 0.0f))
					.ColorAndOpacity(FColor::Red)
					.OnDoubleClicked_Lambda([this, PropertyChain = ParameterizationContext.PropertyChain](const FGeometry&, const FPointerEvent&)->FReply
					{
						if (UDataprepAsset * DataprepAsset = DataprepAssetForParameterization.Get())
						{
							FScopedTransaction Transaction( LOCTEXT("DataprepRemoveBindingFromParameterization","Removing Parameter") );
							DataprepAssetForParameterization->RemoveObjectPropertyFromParameterization( DetailedObject, PropertyChain );
							bRefreshObjectToDisplay = true;
						}

						return FReply::Handled();
					})
				];
		}
	}

	// Add the name widget
	NameColumn->AddSlot()
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Left)
		.Padding(FMargin(LeftPadding, 0.f, 0.f, 0.f))
		[
			NameWidget.ToSharedRef()
		];


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
		.Value(ColumnSizeData->LeftColumnWidth)
		.OnSlotResized( SSplitter::FOnSlotResized::CreateSP( this, &SDataprepDetailsView::OnLeftColumnResized ) )
		[
			NameColumn
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
		bRefreshObjectToDisplay = true;
	}
}

void SDataprepDetailsView::OnObjectReplaced(const TMap<UObject*, UObject*>& ReplacementObjectMap)
{
	if ( UObject * const* ObjectPtr = ReplacementObjectMap.Find( DetailedObject ) )
	{
		DetailedObject = *ObjectPtr;
		bRefreshObjectToDisplay = true;
	}
}

void SDataprepDetailsView::ForceRefresh()
{
	bRefreshObjectToDisplay = true;
}

void SDataprepDetailsView::AddWidgets( const TArray< TSharedRef< IDetailTreeNode > >& DetailTree, TSharedPtr<SGridPanel>& GridPanel, int32& Index, float LeftPadding, const FDataprepParameterizationContext& InParameterizationContext)
{
	auto IsDetailNodeDisplayable = []( const TSharedPtr< IPropertyHandle >& PropertyHandle)
	{
		if(PropertyHandle.IsValid() && PropertyHandle->IsValidHandle() && PropertyHandle->IsEditable())
		{
			UProperty* Property = PropertyHandle->GetProperty();

			if ( Property )
			{
				if ( UObject * Outer = Property->GetOuter() )
				{
					// if the outer is a container property (array,set or map) it's editable even without the proper flags.
					UClass* OuterClass = Outer->GetClass();
					if ( OuterClass == UArrayProperty::StaticClass() || OuterClass == USetProperty::StaticClass() || OuterClass == UMapProperty::StaticClass() )
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
			if ( UProperty* Property = PropertyHandle->GetProperty() )
			{ 
				UClass* PropertyClass = Property->GetClass();
				if ( PropertyClass == UArrayProperty::StaticClass() || PropertyClass == USetProperty::StaticClass() || PropertyClass == UMapProperty::StaticClass() )
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
				if ( DataprepAsset->IsObjectPropertyBinded( DetailedObject, CurrentParameterizationContext.PropertyChain ) )
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
			AddWidgets( Children, GridPanel, Index, LeftPadding, CurrentParameterizationContext );
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
				GridPanel->AddSlot(0, Index)
				[
					CreateDefaultWidget( NameWidget, ValueWidget, LeftPadding, Row.ValueWidget.HorizontalAlignment, Row.ValueWidget.VerticalAlignment, CurrentParameterizationContext )
				];

				Index++;

				TArray< TSharedRef< IDetailTreeNode > > Children;
				ChildNode->GetChildren( Children );
				if( Children.Num() > 0 )
				{
					// #ueent_todo: Find a way to add collapse/expand capability for property with children
					AddWidgets( Children, GridPanel, Index, LeftPadding + 10.f, CurrentParameterizationContext );
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
					CreateDefaultWidget( NameWidget, ValueWidget, LeftPadding, HAlign, VAlign, CurrentParameterizationContext )
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
					AddWidgets( Children, GridPanel, Index, LeftPadding + 10.f, CurrentParameterizationContext );
				}
			}
		}
	}
}

void SDataprepDetailsView::Construct(const FArguments& InArgs)
{
	bRefreshObjectToDisplay = false;
	DetailedObject = InArgs._Object;

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
	}

	Construct();
}

void SDataprepDetailsView::Construct()
{
	DataprepAssetForParameterization.Reset();

	if ( DetailedObject )
	{
		UDataprepAsset* DataprepAsset = FDataprepParameterizationUtils::GetDataprepAssetForParameterization( DetailedObject );
		if ( DataprepAsset )
		{
			OnDataprepParameterizationWasModifiedHandle = DataprepAsset->OnParameterizationWasModified.AddSP( this, &SDataprepDetailsView::ForceRefresh );
		}

		FDataprepParameterizationContext ParameterizationContext;
		ParameterizationContext.State = DataprepAsset ? EParametrizationState::CanBeParameterized : EParametrizationState::InvalidForParameterization;
		DataprepAssetForParameterization = DataprepAsset;

		TSharedPtr<SGridPanel> GridPanel = SNew(SGridPanel).FillColumn( 0.0f, 1.0f );
		TArray< TSharedRef< IDetailTreeNode > > RootNodes = Generator->GetRootTreeNodes();

		int32 Index = 0;
		AddWidgets(RootNodes, GridPanel, Index, 0.f, ParameterizationContext);

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
	}

	if ( UDataprepAsset* DataprepAsset = DataprepAssetForParameterization.Get() )
	{
		DataprepAsset->OnParameterizationWasModified.Remove( OnDataprepParameterizationWasModifiedHandle );
	}
}

void SDataprepDetailsView::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	Super::Tick( AllottedGeometry, InCurrentTime, InDeltaTime );

	if ( bRefreshObjectToDisplay )
	{
		TArray< UObject* > Objects;
		Objects.Add( DetailedObject );
		Generator->SetObjects( Objects );
		 
		Construct();
		bRefreshObjectToDisplay = false;
	}
}

void SDataprepDetailsView::SetObjectToDisplay(UObject& Object)
{
	UObject* NewObjectToDisplay = &Object;
	if ( DetailedObject != NewObjectToDisplay )
	{
		DetailedObject = NewObjectToDisplay;
		bRefreshObjectToDisplay = true;
	}
}

#undef LOCTEXT_NAMESPACE
