// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_EDITOR

#include "USDStageActorCustomization.h"

#include "USDSchemasModule.h"
#include "USDSchemaTranslator.h"
#include "USDStageActor.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Modules/ModuleManager.h"
#include "ScopedTransaction.h"
#include "UObject/Object.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Styling/AppStyle.h"


#define LOCTEXT_NAMESPACE "UsdStageActorCustomization"

FUsdStageActorCustomization::FUsdStageActorCustomization()
{
}

TSharedRef<IDetailCustomization> FUsdStageActorCustomization::MakeInstance()
{
	return MakeShared<FUsdStageActorCustomization>();
}

void FUsdStageActorCustomization::CustomizeDetails( IDetailLayoutBuilder& DetailLayoutBuilder )
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

	CurrentActor = Cast<AUsdStageActor>( SelectedObject.Get() );
	if ( !CurrentActor )
	{
		return;
	}

	CurrentActor->OnStageChanged.AddSP( this, &FUsdStageActorCustomization::ForceRefreshDetails );

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

		if ( Context == CurrentActor->RenderContext )
		{
			InitiallySelectedContext = ContextStr;
		}

		ComboBoxItems.Add( ContextStr );
	}

	IDetailCategoryBuilder& CatBuilder = DetailLayoutBuilder.EditCategory( TEXT( "USD" ) );

	if ( TSharedPtr<IPropertyHandle> RenderContextProperty = DetailLayoutBuilder.GetProperty( GET_MEMBER_NAME_CHECKED( AUsdStageActor, RenderContext ) ) )
	{
		DetailLayoutBuilder.HideProperty( RenderContextProperty );

		CatBuilder.AddCustomRow( FText::FromString( TEXT( "RenderContextCustomization" ) ) )
		.NameContent()
		[
			SNew( STextBlock )
			.Text( FText::FromString( TEXT( "Render Context" ) ) )
			.Font( FAppStyle::GetFontStyle( TEXT( "PropertyWindow.NormalFont" ) ) )
			.ToolTipText( RenderContextProperty->GetToolTipText() )
		]
		.ValueContent()
		[
			SAssignNew( ComboBox, SComboBox<TSharedPtr<FString>> )
			.OptionsSource( &ComboBoxItems )
			.InitiallySelectedItem( InitiallySelectedContext )
			.OnSelectionChanged( this, &FUsdStageActorCustomization::OnComboBoxSelectionChanged )
			.OnGenerateWidget_Lambda( []( TSharedPtr<FString> Item )
			{
				return SNew( STextBlock )
					.Text( Item.IsValid() ? FText::FromString( *Item ) : FText::GetEmpty() )
					.Font(FAppStyle::GetFontStyle( TEXT( "PropertyWindow.NormalFont" ) ) );
			} )
			.Content()
			[
				SNew( STextBlock )
				.Text( this, &FUsdStageActorCustomization::GetComboBoxSelectedOptionText )
				.Font(FAppStyle::GetFontStyle( TEXT( "PropertyWindow.NormalFont" ) ) )
			]
		];
	}

	// Add/remove properties so that they retain their usual order
	if ( TSharedPtr<IPropertyHandle> TimeProperty = DetailLayoutBuilder.GetProperty( TEXT( "Time" ) ) )
	{
		CatBuilder.AddProperty( TimeProperty );
	}
	if ( TSharedPtr<IPropertyHandle> LevelSequenceProperty = DetailLayoutBuilder.GetProperty( TEXT( "LevelSequence" ) ) )
	{
		CatBuilder.AddProperty( LevelSequenceProperty );
	}
}

void FUsdStageActorCustomization::CustomizeDetails( const TSharedPtr<IDetailLayoutBuilder>& DetailBuilder )
{
	DetailBuilderWeakPtr = DetailBuilder;
	CustomizeDetails( *DetailBuilder );
}

void FUsdStageActorCustomization::OnComboBoxSelectionChanged( TSharedPtr<FString> NewContext, ESelectInfo::Type SelectType )
{
	if ( CurrentActor == nullptr || !NewContext.IsValid() )
	{
		return;
	}

	FScopedTransaction Transaction(FText::Format(
		LOCTEXT("RenderContextChangedTransaction", "Changed the UsdStageActor {0}'s RenderContext to '{1}'"),
		FText::FromString( CurrentActor->GetActorLabel() ),
		FText::FromString( NewContext.IsValid() ? *NewContext : TEXT( "None" ) ) )
	);

	FName NewContextName = ( *NewContext ) == TEXT( "universal" ) ? NAME_None : FName( **NewContext );

	CurrentActor->SetRenderContext( NewContextName );
}

FText FUsdStageActorCustomization::GetComboBoxSelectedOptionText() const
{
	TSharedPtr<FString> SelectedItem = ComboBox->GetSelectedItem();
	if ( SelectedItem.IsValid() )
	{
		return FText::FromString( *SelectedItem );
	}

	return FText::GetEmpty();
}

void FUsdStageActorCustomization::ForceRefreshDetails()
{
	// Raw because we don't want to keep alive the details builder when calling the force refresh details
	IDetailLayoutBuilder* DetailLayoutBuilder = DetailBuilderWeakPtr.Pin().Get();
	if ( DetailLayoutBuilder )
	{
		DetailLayoutBuilder->ForceRefreshDetails();
	}
}

#undef LOCTEXT_NAMESPACE

#endif // WITH_EDITOR