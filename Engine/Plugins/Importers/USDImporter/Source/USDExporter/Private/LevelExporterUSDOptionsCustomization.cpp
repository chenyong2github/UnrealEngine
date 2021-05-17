// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelExporterUSDOptionsCustomization.h"

#include "LevelExporterUSDOptions.h"

#include "CoreMinimal.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Editor.h"
#include "Engine/LevelStreaming.h"
#include "Modules/ModuleManager.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SScrollBorder.h"
#include "Widgets/Views/SListView.h"

#define LOCTEXT_NAMESPACE "LevelExporterUSDOptionsCustomization"

namespace UE
{
	namespace LevelExporterOptions
	{
		namespace Private
		{
			class SLevelPickerRow : public STableRow< TSharedRef<FString> >
			{
			public:
				SLATE_BEGIN_ARGS(SLevelPickerRow) {}
				SLATE_END_ARGS()

				void Construct( const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTableView, TWeakPtr<FString> InEntry, ULevelExporterUSDOptions* Options )
				{
					STableRow::Construct( STableRow::FArguments(), OwnerTableView );

					FString LevelName;
					if ( TSharedPtr<FString> PinnedEntry = InEntry.Pin() )
					{
						LevelName = *PinnedEntry;
					}

					SetRowContent(
						SNew( SHorizontalBox )

						+ SHorizontalBox::Slot()
						.HAlign( HAlign_Left )
						.VAlign( VAlign_Center )
						.MaxWidth(20)
						[
							SNew( SCheckBox )
							.IsChecked_Lambda( [ LevelName, Options ]()
							{
								if ( Options )
								{
									return Options->LevelsToIgnore.Contains( LevelName )
										? ECheckBoxState::Unchecked
										: ECheckBoxState::Checked;
								}

								return ECheckBoxState::Undetermined;
							})
							.OnCheckStateChanged_Lambda( [ LevelName, Options ]( ECheckBoxState State )
							{
								if ( Options )
								{
									if ( State == ECheckBoxState::Checked )
									{
										Options->LevelsToIgnore.Remove( LevelName );
									}
									else if ( State == ECheckBoxState::Unchecked )
									{
										Options->LevelsToIgnore.Add( LevelName );
									}
								}
							})
						]

						+ SHorizontalBox::Slot()
						.HAlign( HAlign_Left )
						.VAlign( VAlign_Center )
						[
							SNew( STextBlock )
							.Text( FText::FromString( LevelName ) )
							.Font( FEditorStyle::GetFontStyle( TEXT( "PropertyWindow.NormalFont" ) ) )
						]
					);
				}
			};

			class SLevelPickerList : public SListView<TSharedRef<FString>>
			{
			public:
				void Construct( const FArguments& InArgs, ULevelExporterUSDOptions* Options )
				{
					// We'll be writing directly to its LevelsToIgnore, so keep it alive while we're alive
					OptionsPtr.Reset( Options );

					Options->LevelsToIgnore.Reset();

					if ( UWorld* EditorWorld = GEditor->GetEditorWorldContext().World() )
					{
						// Make sure all streamed levels are loaded so we can query their names and export them
						const bool bForce = true;
						EditorWorld->LoadSecondaryLevels( bForce );

						if ( ULevel* PersistentLevel = EditorWorld->PersistentLevel )
						{
							const FString LevelName = TEXT( "Persistent Level" );
							RootItems.Add( MakeShared< FString >( LevelName ) );

							if ( !PersistentLevel->bIsVisible )
							{
								Options->LevelsToIgnore.Add( LevelName );
							}
						}

						for ( ULevelStreaming* StreamingLevel : EditorWorld->GetStreamingLevels() )
						{
							if ( StreamingLevel )
							{
								if ( ULevel* Level = StreamingLevel->GetLoadedLevel() )
								{
									const FString LevelName = Level->GetTypedOuter<UWorld>()->GetName();
									RootItems.Add( MakeShared< FString >( *LevelName ) );

									if ( !Level->bIsVisible )
									{
										Options->LevelsToIgnore.Add( LevelName );
									}
								}
							}
						}
					}

					SListView::Construct
					(
						SListView::FArguments()
						.ListItemsSource( &RootItems )
						.SelectionMode( ESelectionMode::None )
						.OnGenerateRow( this, &SLevelPickerList::OnGenerateRow, Options )
					);
				}

			private:
				TSharedRef< ITableRow > OnGenerateRow( TSharedRef<FString> InEntry, const TSharedRef<STableViewBase>& OwnerTable, ULevelExporterUSDOptions* Options ) const
				{
					return SNew( SLevelPickerRow, OwnerTable, InEntry, Options );
				}

				TStrongObjectPtr<ULevelExporterUSDOptions> OptionsPtr;
				TArray< TSharedRef<FString> > RootItems;
			};
		}
	}
}
namespace LevelExporterUSDImpl = UE::LevelExporterOptions::Private;

TSharedRef<IDetailCustomization> FLevelExporterUSDOptionsCustomization::MakeInstance()
{
	return MakeShared<FLevelExporterUSDOptionsCustomization>();
}

void FLevelExporterUSDOptionsCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailLayoutBuilder)
{
	TArray<TWeakObjectPtr<UObject>> SelectedObjects = DetailLayoutBuilder.GetSelectedObjects();
	if ( SelectedObjects.Num() != 1 )
	{
		return;
	}

	ULevelExporterUSDOptions* Options = Cast< ULevelExporterUSDOptions>( SelectedObjects[ 0 ].Get() );
	if ( !Options )
	{
		return;
	}

	TSharedPtr< LevelExporterUSDImpl::SLevelPickerList > PickerTree = SNew( LevelExporterUSDImpl::SLevelPickerList, Options );

	TSharedRef<IPropertyHandle> LevelFilterProp = DetailLayoutBuilder.GetProperty( GET_MEMBER_NAME_CHECKED( ULevelExporterUSDOptions, LevelsToIgnore ) );
	TSharedRef<IPropertyHandle> ExportSublayersProp = DetailLayoutBuilder.GetProperty( GET_MEMBER_NAME_CHECKED( ULevelExporterUSDOptions, bExportSublayers ) );

	// Touch these properties and categories to enforce this ordering
	DetailLayoutBuilder.EditCategory( TEXT( "Stage options" ) );
	DetailLayoutBuilder.EditCategory( TEXT( "Export settings" ) );
	DetailLayoutBuilder.EditCategory( TEXT( "Sublayers" ) );
	DetailLayoutBuilder.AddPropertyToCategory( ExportSublayersProp );

	DetailLayoutBuilder.HideProperty( LevelFilterProp );
	DetailLayoutBuilder.AddCustomRowToCategory( LevelFilterProp, LevelFilterProp->GetPropertyDisplayName() )
	.NameContent()
	[
		SNew( STextBlock )
		.Text( FText::FromString( TEXT( "Levels to export" ) ) )
		.Font( FEditorStyle::GetFontStyle( TEXT( "PropertyWindow.NormalFont" ) ) )
	]
	.ValueContent()
	.MinDesiredWidth(300)
	[
		SNew(SScrollBorder, PickerTree.ToSharedRef())
		[
			SNew(SBox) // Prevent the list from expanding freely
			.MaxDesiredHeight(200)
			[
				PickerTree.ToSharedRef()
			]
		]
	];
}

#undef LOCTEXT_NAMESPACE
