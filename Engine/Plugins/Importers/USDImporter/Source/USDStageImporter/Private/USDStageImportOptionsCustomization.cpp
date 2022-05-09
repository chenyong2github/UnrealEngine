// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDStageImportOptionsCustomization.h"

#include "USDSchemasModule.h"
#include "USDSchemaTranslator.h"
#include "USDStageImportOptions.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Modules/ModuleManager.h"
#include "UObject/Object.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Styling/AppStyle.h"


#define LOCTEXT_NAMESPACE "UsdStageImportOptionsCustomization"

FUsdStageImportOptionsCustomization::FUsdStageImportOptionsCustomization()
{
}

TSharedRef<IDetailCustomization> FUsdStageImportOptionsCustomization::MakeInstance()
{
	return MakeShared<FUsdStageImportOptionsCustomization>();
}

void FUsdStageImportOptionsCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailLayoutBuilder)
{
	TArray<TWeakObjectPtr<UObject>> SelectedObjects = DetailLayoutBuilder.GetSelectedObjects();
	if ( SelectedObjects.Num() != 1 )
	{
		return;
	}

	TWeakObjectPtr<UObject> SelectedObject = SelectedObjects[ 0 ];
	if ( !SelectedObject.IsValid() )
	{
		return;
	}

	CurrentOptions = Cast<UUsdStageImportOptions>( SelectedObject.Get() );
	if ( !CurrentOptions )
	{
		return;
	}

	IUsdSchemasModule& UsdSchemasModule = FModuleManager::Get().LoadModuleChecked< IUsdSchemasModule >( TEXT( "USDSchemas" ) );

	ComboBoxItems.Reset();
	TSharedPtr<FString> InitiallySelectedContext;
	for ( const FName& Context : UsdSchemasModule.GetRenderContextRegistry().GetRenderContexts() )
	{
		TSharedPtr<FString> ContextStr;
		if ( Context == NAME_None )
		{
			ContextStr = MakeShared<FString>( TEXT( "universal" ) );
		}
		else
		{
			ContextStr = MakeShared<FString>( Context.ToString() );
		}

		if ( Context == CurrentOptions->RenderContextToImport )
		{
			InitiallySelectedContext = ContextStr;
		}

		ComboBoxItems.Add( ContextStr );
	}

	IDetailCategoryBuilder& CatBuilder = DetailLayoutBuilder.EditCategory( TEXT( "USD options" ) );

	if ( TSharedPtr<IPropertyHandle> RenderContextProperty = DetailLayoutBuilder.GetProperty( GET_MEMBER_NAME_CHECKED( UUsdStageImportOptions, RenderContextToImport ) ) )
	{
		DetailLayoutBuilder.HideProperty( RenderContextProperty );

		CatBuilder.AddCustomRow( FText::FromString( TEXT( "RenderContextCustomization" ) ) )
		.NameContent()
		[
			SNew( STextBlock )
			.Text( FText::FromString( TEXT( "Render Context to Import" ) ) )
			.Font( FAppStyle::GetFontStyle( TEXT( "PropertyWindow.NormalFont" ) ) )
			.ToolTipText( RenderContextProperty->GetToolTipText() )
		]
		.ValueContent()
		[
			SAssignNew( ComboBox, SComboBox<TSharedPtr<FString>> )
			.OptionsSource( &ComboBoxItems )
			.InitiallySelectedItem( InitiallySelectedContext )
			.OnSelectionChanged( this, &FUsdStageImportOptionsCustomization::OnComboBoxSelectionChanged )
			.OnGenerateWidget_Lambda( []( TSharedPtr<FString> Item )
			{
				return SNew( STextBlock )
					.Text( Item.IsValid() ? FText::FromString( *Item ) : FText::GetEmpty() )
					.Font(FAppStyle::GetFontStyle( TEXT( "PropertyWindow.NormalFont" ) ) );
			} )
			.Content()
			[
				SNew( STextBlock )
				.Text( this, &FUsdStageImportOptionsCustomization::GetComboBoxSelectedOptionText )
				.Font(FAppStyle::GetFontStyle( TEXT( "PropertyWindow.NormalFont" ) ) )
			]
		];
	}

	// Add/remove properties so that they retain their usual order
	if ( TSharedPtr<IPropertyHandle> OverrideStageOptionsProperty = DetailLayoutBuilder.GetProperty( GET_MEMBER_NAME_CHECKED( UUsdStageImportOptions, bOverrideStageOptions ) ) )
	{
		CatBuilder.AddProperty( OverrideStageOptionsProperty );
	}
	if ( TSharedPtr<IPropertyHandle> StageOptionsProperty = DetailLayoutBuilder.GetProperty( GET_MEMBER_NAME_CHECKED( UUsdStageImportOptions, StageOptions ) ) )
	{
		CatBuilder.AddProperty( StageOptionsProperty );
	}
}

void FUsdStageImportOptionsCustomization::CustomizeDetails( const TSharedPtr<IDetailLayoutBuilder>& DetailBuilder )
{
	CustomizeDetails( *DetailBuilder );
}

void FUsdStageImportOptionsCustomization::OnComboBoxSelectionChanged( TSharedPtr<FString> NewContext, ESelectInfo::Type SelectType )
{
	if ( CurrentOptions == nullptr || !NewContext.IsValid() )
	{
		return;
	}

	FName NewContextName = ( *NewContext ) == TEXT( "universal" ) ? NAME_None : FName( **NewContext );

	CurrentOptions->RenderContextToImport = NewContextName;
}

FText FUsdStageImportOptionsCustomization::GetComboBoxSelectedOptionText() const
{
	TSharedPtr<FString> SelectedItem = ComboBox->GetSelectedItem();
	if ( SelectedItem.IsValid() )
	{
		return FText::FromString( *SelectedItem );
	}

	return FText::GetEmpty();
}

#undef LOCTEXT_NAMESPACE
