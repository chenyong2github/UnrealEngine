// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DataPrepAsset.h"

#include "Blueprint/K2Node_DataprepActionCore.h"
#include "Blueprint/K2Node_DataprepProducer.h"
#include "DataPrepContentConsumer.h"
#include "DataPrepContentProducer.h"
#include "DataPrepRecipe.h"
#include "DataprepActionAsset.h"
#include "DataprepCoreLogCategory.h"
#include "DataprepCorePrivateUtils.h"
#include "DataprepCoreUtils.h"
#include "Parameterization/DataprepParameterization.h"

#include "AssetRegistryModule.h"
#include "BlueprintNodeBinder.h"
#include "BlueprintNodeSpawner.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UObject/UObjectGlobals.h"

#ifdef WITH_EDITOR
#include "Editor.h"
#endif //WITH_EDITOR

#define LOCTEXT_NAMESPACE "DataprepAsset"

// UDataprepAsset =================================================================

UDataprepAsset::UDataprepAsset()
{
	// Temp code for the nodes development
	DataprepRecipeBP = nullptr;
	StartNode = nullptr;
	// end of temp code for nodes development
}

void UDataprepAsset::PostLoad()
{
	UDataprepAssetInterface::PostLoad();

	check(DataprepRecipeBP);
	DataprepRecipeBP->OnChanged().AddUObject( this, &UDataprepAsset::OnDataprepBlueprintChanged );

	// Move content of deprecated properties to the corresponding new ones.
	if(HasAnyFlags(RF_WasLoaded))
	{
		bool bMarkDirty = false;
		if(Producers_DEPRECATED.Num() > 0)
		{
			Inputs->AssetProducers.Reserve(Producers_DEPRECATED.Num());

			while(Producers_DEPRECATED.Num() > 0)
			{
				if(Inputs->AddAssetProducer( Producers_DEPRECATED.Pop(false) ) == INDEX_NONE)
				{
					// #ueent_todo Log message a producer was not properly restored
				}
			}

			Producers_DEPRECATED.Empty();
			bMarkDirty = true;
		}

		if(Consumer_DEPRECATED)
		{
			Output = Consumer_DEPRECATED;
			Consumer_DEPRECATED = nullptr;
			bMarkDirty = true;
		}

		// Most likely a Dataprep asset from 4.23
		if(StartNode == nullptr)
		{
			UEdGraph* PipelineGraph = FBlueprintEditorUtils::FindEventGraph(DataprepRecipeBP);
			check( PipelineGraph );

			for( UEdGraphNode* GraphNode : PipelineGraph->Nodes )
			{
				StartNode = Cast<UK2Node_DataprepProducer>(GraphNode);
				if( StartNode )
				{
					break;
				}
			}

			// This Dataprep asset was never opened in the editor
			if( StartNode == nullptr )
			{
				IBlueprintNodeBinder::FBindingSet Bindings;
				StartNode = UBlueprintNodeSpawner::Create<UK2Node_DataprepProducer>()->Invoke( PipelineGraph, Bindings, FVector2D(-100,0) );
				check(Cast<UK2Node_DataprepProducer>(StartNode));

				DataprepRecipeBP->MarkPackageDirty();
			}

			UpdateActions();
			bMarkDirty = true;
		}

		if ( !Parameterization )
		{
			Parameterization = NewObject<UDataprepParameterization>( this, FName(), RF_Public | RF_Transactional );
			bMarkDirty = true;
		}

		// Mark the asset as dirty to indicate asset's properties have changed
		if(bMarkDirty)
		{
			const FText AssetName = FText::FromString( GetName() );
			const FText WarningMessage = FText::Format( LOCTEXT( "DataprepAssetOldVersion", "{0} is from an old version and has been updated. Please save asset to complete update."), AssetName );
			const FText NotificationText = FText::Format( LOCTEXT( "DataprepAssetOldVersionNotif", "{0} is from an old version and has been updated."), AssetName );
			DataprepCorePrivateUtils::LogMessage( EMessageSeverity::Warning, WarningMessage, NotificationText );

			GetOutermost()->SetDirtyFlag(true);
		}
	}
}

bool UDataprepAsset::CreateBlueprint()
{
	// Begin: Temp code for the nodes development
	const FString DesiredName = GetName() + TEXT("_Recipe");
	FName BlueprintName = MakeUniqueObjectName( GetOutermost(), UBlueprint::StaticClass(), *DesiredName );

	DataprepRecipeBP = FKismetEditorUtilities::CreateBlueprint( UDataprepRecipe::StaticClass(), this, BlueprintName, BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass() );
	check( DataprepRecipeBP );

	// This blueprint is not the asset of the package
	DataprepRecipeBP->ClearFlags( RF_Standalone );

	FAssetRegistryModule::AssetCreated( DataprepRecipeBP );

	// Create the start node of the Blueprint
	UEdGraph* PipelineGraph = FBlueprintEditorUtils::FindEventGraph(DataprepRecipeBP);
	check( PipelineGraph );

	UEdGraph* EventGraph = FBlueprintEditorUtils::FindEventGraph(DataprepRecipeBP);
	IBlueprintNodeBinder::FBindingSet Bindings;

	StartNode = UBlueprintNodeSpawner::Create<UK2Node_DataprepProducer>()->Invoke( EventGraph, Bindings, FVector2D(-100,0) );
	check(Cast<UK2Node_DataprepProducer>(StartNode));

	DataprepRecipeBP->MarkPackageDirty();

	DataprepRecipeBP->OnChanged().AddUObject( this, &UDataprepAsset::OnDataprepBlueprintChanged );
	// End: Temp code for the nodes development

	MarkPackageDirty();

	return true;
}

bool UDataprepAsset::CreateParameterization()
{
	if( !Parameterization )
	{
		Parameterization = NewObject<UDataprepParameterization>( this, FName(), RF_Public | RF_Transactional );
		MarkPackageDirty();
		return true;
	}

	return false;
}

void UDataprepAsset::ExecuteRecipe(const TSharedPtr<FDataprepActionContext>& InActionsContext)
{
	ExecuteRecipe_Internal(	InActionsContext, ActionAssets );
}

TArray<UDataprepActionAsset*> UDataprepAsset::GetCopyOfActions(TMap<UObject*,UObject*>& OutOriginalToCopy) const
{
	TArray<UDataprepActionAsset*> CopyOfActionAssets;
	CopyOfActionAssets.Reserve( ActionAssets.Num() );
	for ( UDataprepActionAsset* ActionAsset : ActionAssets )
	{
		FObjectDuplicationParameters DuplicationParameter( ActionAsset, GetTransientPackage() );
		DuplicationParameter.CreatedObjects = &OutOriginalToCopy;

		UDataprepActionAsset* CopyOfAction = static_cast<UDataprepActionAsset*>( StaticDuplicateObjectEx( DuplicationParameter ) );
		check( CopyOfAction );

		OutOriginalToCopy.Add( ActionAsset, CopyOfAction );
		CopyOfActionAssets.Add( CopyOfAction );
	}

	return CopyOfActionAssets;
}

UObject* UDataprepAsset::GetParameterizationObject()
{
	return Parameterization->GetDefaultObject();
}

void UDataprepAsset::BindObjectPropertyToParameterization(UObject* Object, const TArray<FDataprepPropertyLink>& InPropertyChain, FName Name)
{
	bool bPassConditionCheck = false;

	if ( InPropertyChain.Num() > 0 )
	{
		// Validate that the object is part of this asset
		UObject* Outer = Object;
		while ( Outer && !bPassConditionCheck )
		{
			Outer =  Outer->GetOuter();
			bPassConditionCheck = Outer == this;
		}
	}

	if ( bPassConditionCheck )
	{
		Parameterization->BindObjectProperty( Object, InPropertyChain, Name );
	}

}

void UDataprepAsset::OnDataprepBlueprintChanged( UBlueprint* InBlueprint )
{
	if(InBlueprint == DataprepRecipeBP)
	{
		UpdateActions();
		OnChanged.Broadcast( FDataprepAssetChangeType::RecipeModified );
	}
}

void UDataprepAsset::UpdateActions()
{
	ActionAssets.Empty(ActionAssets.Num());

	UEdGraphPin* StartNodePin = StartNode->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output);
	if( StartNodePin && StartNodePin->LinkedTo.Num() > 0 )
	{
		TSet<UK2Node_DataprepActionCore*> ActionNodesExecuted;

		for( UEdGraphPin* NextNodeInPin = StartNodePin->LinkedTo[0]; NextNodeInPin != nullptr ; )
		{
			UEdGraphNode* NextNode = NextNodeInPin->GetOwningNode();

			if(UK2Node_DataprepActionCore* ActionNode = Cast<UK2Node_DataprepActionCore>(NextNode))
			{
				// Break the loop if the node had already been executed
				if( ActionNodesExecuted.Find( ActionNode ) )
				{
					break;
				}

				if(UDataprepActionAsset* DataprepAction = ActionNode->GetDataprepAction())
				{
					ActionAssets.Add( DataprepAction );
				}
			}

			UEdGraphPin* NextNodeOutPin = NextNode->FindPin( UEdGraphSchema_K2::PN_Then, EGPD_Output );

			if ( !NextNodeOutPin )
			{
				// If we couldn't find a then pin try to get the first output pin as a fallback
				for ( UEdGraphPin* Pin : NextNode->Pins )
				{
					if ( Pin && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec && Pin->Direction == EGPD_Output )
					{
						NextNodeOutPin = Pin;
						break;
					}
				}
			}

			NextNodeInPin = NextNodeOutPin ? ( NextNodeOutPin->LinkedTo.Num() > 0 ? NextNodeOutPin->LinkedTo[0] : nullptr ) : nullptr;
		}
	}
}

#undef LOCTEXT_NAMESPACE
