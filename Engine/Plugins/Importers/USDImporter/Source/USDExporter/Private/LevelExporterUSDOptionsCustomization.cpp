// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelExporterUSDOptionsCustomization.h"

#include "LevelExporterUSDOptions.h"
#include "LevelSequenceExporterUSDOptions.h"

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

				void Construct( const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTableView, TWeakPtr<FString> InEntry, FLevelExporterUSDOptionsInner* Inner )
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
							.IsChecked_Lambda( [ LevelName, Inner ]()
							{
								if ( Inner )
								{
									return Inner->LevelsToIgnore.Contains( LevelName )
										? ECheckBoxState::Unchecked
										: ECheckBoxState::Checked;
								}

								return ECheckBoxState::Undetermined;
							})
							.OnCheckStateChanged_Lambda( [ LevelName, Inner ]( ECheckBoxState State )
							{
								if ( Inner )
								{
									if ( State == ECheckBoxState::Checked )
									{
										Inner->LevelsToIgnore.Remove( LevelName );
									}
									else if ( State == ECheckBoxState::Unchecked )
									{
										Inner->LevelsToIgnore.Add( LevelName );
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
				void Construct( const FArguments& InArgs, FLevelExporterUSDOptionsInner* Inner )
				{
					if ( Inner )
					{
						Inner->LevelsToIgnore.Reset();

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
									Inner->LevelsToIgnore.Add( LevelName );
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
											Inner->LevelsToIgnore.Add( LevelName );
										}
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
						.OnGenerateRow( this, &SLevelPickerList::OnGenerateRow, Inner )
					);
				}

			private:
				TSharedRef< ITableRow > OnGenerateRow( TSharedRef<FString> InEntry, const TSharedRef<STableViewBase>& OwnerTable, FLevelExporterUSDOptionsInner* Inner ) const
				{
					return SNew( SLevelPickerRow, OwnerTable, InEntry, Inner );
				}

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

	TSharedPtr< LevelExporterUSDImpl::SLevelPickerList > PickerTree = nullptr;
	TStrongObjectPtr<UObject> OptionsPtr;
	FName LevelFilterPropName;
	FName ExportSublayersPropName;
	if ( ULevelExporterUSDOptions* Options = Cast< ULevelExporterUSDOptions>( SelectedObjects[ 0 ].Get() ) )
	{
		OptionsPtr.Reset( Options );

		PickerTree = SNew( LevelExporterUSDImpl::SLevelPickerList, &Options->Inner );
		LevelFilterPropName = TEXT( "Inner.LevelsToIgnore" );
		ExportSublayersPropName = TEXT( "Inner.bExportSublayers" );
	}
	else if ( ULevelSequenceExporterUsdOptions* LevelSequenceOptions = Cast< ULevelSequenceExporterUsdOptions>( SelectedObjects[ 0 ].Get() ) )
	{
		OptionsPtr.Reset( LevelSequenceOptions );

		// For now there is no easy way of fetching the level to export from a ULevelSequence... we could potentially try to guess what it is
		// by looking at the soft object paths, but even those aren't exposed, so here we just default to using the current level as the export level.
		LevelSequenceOptions->Level = GWorld;

		PickerTree = SNew( LevelExporterUSDImpl::SLevelPickerList, &LevelSequenceOptions->LevelExportOptions );
		LevelFilterPropName = TEXT( "LevelExportOptions.LevelsToIgnore" );
		ExportSublayersPropName = TEXT( "LevelExportOptions.bExportSublayers" );
	}
	else
	{
		return;
	}

	TSharedRef<IPropertyHandle> LevelFilterProp = DetailLayoutBuilder.GetProperty( LevelFilterPropName );
	TSharedRef<IPropertyHandle> ExportSublayersProp = DetailLayoutBuilder.GetProperty( ExportSublayersPropName );

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
