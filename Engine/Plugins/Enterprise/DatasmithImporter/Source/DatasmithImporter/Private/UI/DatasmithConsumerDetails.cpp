// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/DatasmithConsumerDetails.h"

#include "DatasmithConsumer.h"
#include "Utility/DatasmithImporterUtils.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Dialogs/DlgPickPath.h"
#include "EditorLevelUtils.h"
#include "Engine/Level.h"
#include "Engine/LevelStreamingAlwaysLoaded.h"
#include "Engine/World.h"
#include "Factories/LevelFactory.h"
#include "Misc/MessageDialog.h"
#include "Misc/PackageName.h"
#include "PropertyCustomizationHelpers.h"
#include "ScopedTransaction.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "DatasmithConsumer"

namespace DatasmithConsumerDetailsUtil
{
	/** Helper class to force a widget to fill in a space. Copied from SDetailSingleItemRow.cpp */
	class SConstrainedBox : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SConstrainedBox) {}
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

	class SFolderProperty : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SFolderProperty) {}
		SLATE_END_ARGS()

	public:
		void Construct(const FArguments& InArgs, UDatasmithConsumer* InConsumer)
		{
			ConsumerPtr = InConsumer;
			FSlateFontInfo FontInfo = IDetailLayoutBuilder::GetDetailFont();

			TSharedRef<SWidget> BrowseButton = PropertyCustomizationHelpers::MakeBrowseButton( FSimpleDelegate::CreateSP( this, &SFolderProperty::OnBrowseContentFolder ) );

			ChildSlot
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.HAlign(EHorizontalAlignment::HAlign_Fill)
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
						.OnTextCommitted(FOnTextCommitted::CreateSP(this, &SFolderProperty::OnTextCommitted))
					]
				]
				+ SHorizontalBox::Slot()
				.HAlign(EHorizontalAlignment::HAlign_Right)
				[
					BrowseButton
				]
			];

			UpdateContentFolderText();
		}

	private:
		void OnBrowseContentFolder()
		{
							   //Ask the user for the root path where they want any content to be placed
			if( UDataprepContentConsumer* DataprepConsumer = ConsumerPtr.Get() )
			{
				FString Path = DataprepConsumer->GetTargetContentFolder();
				if( Path.IsEmpty() )
				{
					Path = FPaths::GetPath( DataprepConsumer->GetOutermost()->GetPathName() );
				}
				Path += TEXT("/"); // Trailing '/' is needed to set the default path

				TSharedRef<SDlgPickPath> PickContentPathDlg =
					SNew(SDlgPickPath)
					.Title(LOCTEXT("DataprepSlateHelper_ChooseImportRootContentPath", "Choose Location for importing the Datasmith content"))
					.DefaultPath(FText::FromString(Path));

				if ( PickContentPathDlg->ShowModal() == EAppReturnType::Ok )
				{
					FScopedTransaction Transaction( LOCTEXT("Consumer_SetTargetContentFolder", "Set Target Content Folder") );

					FText ErrorReason;
					if( DataprepConsumer->SetTargetContentFolder( PickContentPathDlg->GetPath().ToString(), ErrorReason ) )
					{
						UpdateContentFolderText();
					}
					else
					{
						Transaction.Cancel();
						UE_LOG( LogDatasmithImport, Error, TEXT("%s"), *ErrorReason.ToString() );
					}
				}
			}
		}

		void UpdateContentFolderText()
		{
			if(UDataprepContentConsumer* Consumer = ConsumerPtr.Get())
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

		void OnTextCommitted( const FText& NewText, ETextCommit::Type CommitType)
		{
			if( UDataprepContentConsumer* DataprepConsumer = ConsumerPtr.Get() )
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

				if(NewContentFolder != DataprepConsumer->GetTargetContentFolder())
				{
					FScopedTransaction Transaction( LOCTEXT("Consumer_SetTargetContentFolder", "Set Target Content Folder") );

					FText ErrorReason;
					if( !DataprepConsumer->SetTargetContentFolder( NewContentFolder, ErrorReason ) )
					{
						Transaction.Cancel();
						UE_LOG( LogDatasmithImport, Error, TEXT("%s"), *ErrorReason.ToString() );
						UpdateContentFolderText();
					}
				}
			}
		}

	private:
		TWeakObjectPtr< UDatasmithConsumer > ConsumerPtr;
		TSharedPtr< SEditableTextBox > ContentFolderTextBox;
	};

	class SLevelProperty : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SLevelProperty) {}
		SLATE_END_ARGS()

	public:
		void Construct(const FArguments& InArgs, UDatasmithConsumer* InConsumer)
		{
			ConsumerPtr = InConsumer;
			FSlateFontInfo FontInfo = IDetailLayoutBuilder::GetDetailFont();

			ChildSlot
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.HAlign(EHorizontalAlignment::HAlign_Fill)
				.Padding(5.0f, 2.5f, 2.0f, 2.5f)
				[
					// Trick to force the splitter widget to fill up the space of its parent
					// Strongly inspired from SDetailSingleItemRow
					//SNew(SConstrainedBox)
					//[
						SAssignNew(LevelTextBox, SEditableTextBox)
						.Font(IDetailLayoutBuilder::GetDetailFont())
						.HintText(LOCTEXT("DataprepLevelProperty_HintText", "Set the name of the level to save in"))
						.IsReadOnly(false)
						.OnTextCommitted(FOnTextCommitted::CreateSP(this, &SLevelProperty::OnTextCommitted))
					//]
				]
			];

			UpdateLevelText();
		}

	private:
		void UpdateLevelText()
		{
			if(UDatasmithConsumer* Consumer = ConsumerPtr.Get())
			{
				LevelTextBox->SetText( FText::FromString( Consumer->GetLevelName() ) );
			}
			else
			{
				LevelTextBox->SetText( TAttribute<FText>() );
			}
		}

		void OnTextCommitted( const FText& NewText, ETextCommit::Type CommitType)
		{
			if( UDatasmithConsumer* DataprepConsumer = ConsumerPtr.Get() )
			{
				FString NewLevelName( NewText.ToString() );

				if(NewLevelName != DataprepConsumer->GetLevelName())
				{
					FScopedTransaction Transaction( LOCTEXT("Consumer_SetLevelName", "Set Output Level") );

					FText OutReason;
					if( !DataprepConsumer->SetLevelName( NewLevelName, OutReason ) )
					{
						Transaction.Cancel();

						UpdateLevelText();

						UE_LOG( LogDatasmithImport, Error, TEXT("Cannot create a level named %s - %s"), *NewLevelName, *OutReason.ToString() );
					}
				}
			}
		}

	private:
		TWeakObjectPtr< UDatasmithConsumer > ConsumerPtr;
		TSharedPtr< SEditableTextBox > LevelTextBox;
	};
}

void FDatasmithConsumerDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray< TWeakObjectPtr< UObject > > Objects;
	DetailBuilder.GetObjectsBeingCustomized( Objects );
	check( Objects.Num() > 0 );

	UDatasmithConsumer* Consumer = Cast< UDatasmithConsumer >(Objects[0].Get());
	check( Consumer );

	//FName CategoryName( TEXT("DatasmithFileProducerCustom") );
	IDetailCategoryBuilder& ImportSettingsCategoryBuilder = DetailBuilder.EditCategory( NAME_None, FText::GetEmpty(), ECategoryPriority::Important );

	TSharedRef<IPropertyHandle> TargetFolderHandle = DetailBuilder.GetProperty( TEXT("TargetContentFolder"), UDataprepContentConsumer::StaticClass() );
	TargetFolderHandle->MarkHiddenByCustomization();

	TSharedRef<IPropertyHandle> LevelNameHandle = DetailBuilder.GetProperty( TEXT("LevelName"), UDataprepContentConsumer::StaticClass() );
	LevelNameHandle->MarkHiddenByCustomization();

	TSharedRef<IPropertyHandle> LevelPathHandle = DetailBuilder.GetProperty( TEXT("OutputLevelPath"), UDatasmithConsumer::StaticClass() );
	LevelPathHandle->MarkHiddenByCustomization();

	{
		using namespace DatasmithConsumerDetailsUtil;

		FText PropertyName = FText::FromString( TEXT( "Folder" ) );
		FDetailWidgetRow& CustomRow = ImportSettingsCategoryBuilder.AddCustomRow( PropertyName );

		CustomRow.NameContent()
		[
			SNew( STextBlock )
			.Text( PropertyName )
			.Font( IDetailLayoutBuilder::GetDetailFont() )
		];

		CustomRow.ValueContent()
		.MinDesiredWidth( 2000.0f )
		[
			SNew( SFolderProperty, Consumer )
		];
	}

	{
		using namespace DatasmithConsumerDetailsUtil;

		FText PropertyName = FText::FromString( TEXT( "Level Name" ) );
		FDetailWidgetRow& CustomRow = ImportSettingsCategoryBuilder.AddCustomRow( PropertyName );

		CustomRow.NameContent()
		[
			SNew( STextBlock )
			.Text( PropertyName )
			.Font( IDetailLayoutBuilder::GetDetailFont() )
		];

		CustomRow.ValueContent()
		.MinDesiredWidth( 2000.0f )
		[
			SNew( SLevelProperty, Consumer )
		];
	}
}

#undef LOCTEXT_NAMESPACE