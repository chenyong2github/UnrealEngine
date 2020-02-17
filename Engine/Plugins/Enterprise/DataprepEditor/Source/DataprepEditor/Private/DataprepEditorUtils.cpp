// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataprepEditorUtils.h"

#include "DataprepAsset.h"
#include "DataprepParameterizableObject.h"
#include "Parameterization/DataprepParameterizationUtils.h"
#include "Widgets/Parameterization/SDataprepLinkToParameter.h"

#include "EditorStyleSet.h"
#include "Engine/Blueprint.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "ScopedTransaction.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "DataprepEditorUtils"

void FDataprepParametrizationActionData::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(DataprepAsset);
	Collector.AddReferencedObject(Object);
}

bool FDataprepParametrizationActionData::IsValid() const
{
	return DataprepAsset && Object && PropertyChain.Num() > 0;
}

void FDataprepEditorUtils::PopulateMenuForParameterization(FMenuBuilder& MenuBuilder, UDataprepAsset& DataprepAsset, UDataprepParameterizableObject& Object, const TArray<FDataprepPropertyLink>& PropertyChain)
{
	TSharedRef<FDataprepParametrizationActionData> ActionData = MakeShared<FDataprepParametrizationActionData>( DataprepAsset, Object, PropertyChain );

	MenuBuilder.BeginSection( NAME_None, LOCTEXT("ParametrizationMenuSection", "Parameterization") );
	{
		FName ParameterName = DataprepAsset.GetNameOfParameterForObjectProperty( &Object, PropertyChain );

		FNewMenuDelegate BindToParamerizationDelegate;
		

		BindToParamerizationDelegate.BindLambda( [ActionData, ParameterName](FMenuBuilder& BindToParamerizationDelegateMenu)
			{
				FUIAction DoNothing;
				BindToParamerizationDelegateMenu.AddWidget( SNew(SDataprepLinkToParameter, ActionData), FText(), true, false );
			});


		MenuBuilder.AddSubMenu( LOCTEXT("LinkToParameterLabel", "Link To Parameter"), LOCTEXT("LinkToParameterTooltip", "Link this property to a existing parameter or a new one")
			, BindToParamerizationDelegate, true, FSlateIcon(), false );

		if ( !ParameterName.IsNone() )
		{ 
			
			FUIAction RemoveBinding;
			FText UnlinkFromParameterLabel = LOCTEXT("UnlinkFromParameterLabel", "Remove Link To Parameter");
			RemoveBinding.ExecuteAction.BindLambda( [ActionData, UnlinkFromParameterLabel]()
				{
					if ( ActionData->IsValid() )
					{
						FScopedTransaction ScopedTransaction( UnlinkFromParameterLabel );
						ActionData->DataprepAsset->RemoveObjectPropertyFromParameterization( ActionData->Object, ActionData->PropertyChain );
					}
				});

			MenuBuilder.AddMenuEntry( UnlinkFromParameterLabel, TAttribute<FText>(), FSlateIcon(), RemoveBinding );
		}
	}
	MenuBuilder.EndSection();
}

FSlateFontInfo FDataprepEditorUtils::GetGlyphFont()
{
	return FEditorStyle::Get().GetFontStyle("FontAwesome.11");
}

TSharedPtr<SWidget> FDataprepEditorUtils::MakeContextMenu(const TSharedPtr<FDataprepParametrizationActionData>& ParameterizationActionData)
{
	if ( ParameterizationActionData && ParameterizationActionData->IsValid() )
	{
		FMenuBuilder MenuBuilder( true, nullptr );
		PopulateMenuForParameterization( MenuBuilder, *ParameterizationActionData->DataprepAsset,
			*ParameterizationActionData->Object, ParameterizationActionData->PropertyChain );
		return MenuBuilder.MakeWidget();
	}

	return {};
}

#undef LOCTEXT_NAMESPACE
