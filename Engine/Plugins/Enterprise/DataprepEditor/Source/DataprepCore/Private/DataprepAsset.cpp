// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataprepAsset.h"

#include "Blueprint/K2Node_DataprepActionCore.h"
#include "Blueprint/K2Node_DataprepProducer.h"
#include "DataprepActionAsset.h"
#include "DataprepContentConsumer.h"
#include "DataprepContentProducer.h"
#include "DataprepCoreLogCategory.h"
#include "DataprepCorePrivateUtils.h"
#include "DataprepCoreUtils.h"
#include "DataprepParameterizableObject.h"
#include "DataprepRecipe.h"
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
#include "EdGraph/EdGraphSchema.h"
#include "EdGraphSchema_K2.h"
#include "EdGraph/EdGraphNode.h"

#define LOCTEXT_NAMESPACE "DataprepAsset"

// UDataprepAsset =================================================================

UDataprepAsset::UDataprepAsset()
{
#ifndef DP_NOBLUEPRINT
	DataprepRecipeBP = nullptr;
	StartNode = nullptr;
#endif
	CachedActionCount = 0;
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

#ifndef NO_BLUEPRINT
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
			if( StartNode == nullptr && ActionAssets.Num() == 0)
			{
				IBlueprintNodeBinder::FBindingSet Bindings;
				StartNode = UBlueprintNodeSpawner::Create<UK2Node_DataprepProducer>()->Invoke( PipelineGraph, Bindings, FVector2D(-100,0) );
				check(Cast<UK2Node_DataprepProducer>(StartNode));

				DataprepRecipeBP->MarkPackageDirty();
			}

			UpdateActions(false);
			bMarkDirty = true;
		}
#endif

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

		CachedActionCount = ActionAssets.Num();
	}
}

bool UDataprepAsset::Rename(const TCHAR* NewName/* =nullptr */, UObject* NewOuter/* =nullptr */, ERenameFlags Flags/* =REN_None */)
{
	bool bWasRename = Super::Rename( NewName, NewOuter, Flags );
	if ( bWasRename )
	{
		if ( Parameterization )
		{
			bWasRename &= Parameterization->OnAssetRename( Flags );
		}

		if ( DataprepRecipeBP && bWasRename )
		{
			// There shouldn't be a blueprint depending on us. Should be ok to just rename the generated class
			bWasRename &= DataprepRecipeBP->RenameGeneratedClasses( NewName, NewOuter, Flags );
		}
	}

	return bWasRename;
}

void UDataprepAsset::PostEditUndo()
{
	UDataprepAssetInterface::PostEditUndo();

	OnActionChanged.Broadcast(nullptr, (ActionAssets.Num() == CachedActionCount) ? FDataprepAssetChangeType::ActionMoved : FDataprepAssetChangeType::ActionRemoved);

	CachedActionCount = ActionAssets.Num();
}

const UDataprepActionAsset* UDataprepAsset::GetAction(int32 Index) const
{
	if ( ActionAssets.IsValidIndex( Index ) )
	{
		return ActionAssets[Index];
	}
	else
	{
		UE_LOG( LogDataprepCore
			, Error
			, TEXT("The action to retrieve is out of bound. (Passed index: %d, Number of actions: %d, Dataprepsset: %s)")
			, Index
			, ActionAssets.Num()
			, *GetPathName()
			);
	}

	return nullptr;
}

#ifndef NO_BLUEPRINT
void UDataprepAsset::RemoveActionUsingBP(int32 Index)
{
	if ( ActionAssets.IsValidIndex( Index ) )
	{
		if ( UDataprepActionAsset* DataprepActionAsset = ActionAssets[Index] )
		{
			// Note this code will need to be updated with the new graph (also performance wise it's not really good (to many events) )
			if ( UK2Node_DataprepActionCore* DataprepActionNode = Cast<UK2Node_DataprepActionCore>( DataprepActionAsset->GetOuter() ) )
			{
				UEdGraphPin* OuputPin = DataprepActionNode->FindPin( UEdGraphSchema_K2::PN_Then, EGPD_Output );
				UEdGraphPin* InputPin = DataprepActionNode->FindPin( UEdGraphSchema_K2::PN_Execute, EGPD_Input );

				// Reconnects the input of the node to it's output
				if ( OuputPin && InputPin && OuputPin->LinkedTo.Num() > 0 && InputPin->LinkedTo.Num() )
				{
					if ( const UEdGraphSchema* GraphSchema = DataprepActionNode->GetSchema() )
					{
						TArray<UEdGraphPin*> Froms = InputPin->LinkedTo;
						UEdGraphPin* To = OuputPin->LinkedTo[0];

						// Notification will be send latter for the froms and to (modification are still recorded if there is a transaction)
						constexpr bool bSendNotification = false;
						GraphSchema->BreakPinLinks( *InputPin, bSendNotification );
						GraphSchema->BreakPinLinks( *OuputPin, bSendNotification );

						for ( UEdGraphPin* From : Froms )
						{
							GraphSchema->TryCreateConnection( From, To );
						}
					}
				}

				DataprepActionNode->DestroyNode();
				UpdateActions();
			}
		}
	}
	else
	{
		UE_LOG( LogDataprepCore
			, Error
			,TEXT("The action to remove is out of bound. (Passed index: %d, Number of actions: %d, Dataprepsset: %s)")
			, Index
			, ActionAssets.Num()
			, *GetPathName()
			);
	}

}

UDataprepActionAsset* UDataprepAsset::AddActionUsingBP(UEdGraphNode* NewActionNode)
{
	if ( ActionAssets.Num() > 0 )
	{
		UDataprepActionAsset* LastDataprepAction = ActionAssets.Last();
		if ( UEdGraphNode* LastActionNode = Cast<UEdGraphNode>( LastDataprepAction->GetOuter() ) )
		{
			NewActionNode->AutowireNewNode( LastActionNode->FindPin( UEdGraphSchema_K2::PN_Then, EGPD_Output ) );
		}
	}
	else
	{
		NewActionNode->AutowireNewNode( StartNode->FindPin( UEdGraphSchema_K2::PN_Then, EGPD_Output ) );
	}
	UpdateActions();

	//Todo return the action
	return nullptr;
}

void UDataprepAsset::SwapActionsUsingBP(int32 FirstActionIndex, int32 SecondActionIndex)
{
	if ( ActionAssets.Num() > 0 )
	{
		if ( !ActionAssets.IsValidIndex( FirstActionIndex ) || !ActionAssets.IsValidIndex( SecondActionIndex ) )
		{
			UE_LOG( LogDataprepCore
				, Error
				, TEXT("Can swap the dataprep actions a index is out of range. (First Index : %d, Second Index: %d, Number of Actions: %d, DataprepAction: %s)")
				, FirstActionIndex
				, SecondActionIndex
				, ActionAssets.Num()
				, *GetPathName()
				);
		}

		// Note this code will need to be updated with the new graph (also performance wise it's not really good (to many events) )
		auto GetOutput = [](UEdGraphPin& OuputPin) -> UEdGraphPin*
		{
			if (OuputPin.LinkedTo.Num() > 0)
			{
				return OuputPin.LinkedTo[0];
			}
			return nullptr;
		};

		// Grab the in/out of the first action
		UDataprepActionAsset* FirstDataprepActionAsset = ActionAssets[FirstActionIndex];
		check(FirstDataprepActionAsset);
		UK2Node_DataprepActionCore* FirstDataprepActionNode = Cast<UK2Node_DataprepActionCore>( FirstDataprepActionAsset->GetOuter() );
		check(FirstDataprepActionNode);

		UEdGraphPin* FirstOuputPin = FirstDataprepActionNode->FindPin( UEdGraphSchema_K2::PN_Then, EGPD_Output );
		check(FirstOuputPin);
		UEdGraphPin* FirstOuput = GetOutput( *FirstOuputPin );

		UEdGraphPin* FirstInputPin = FirstDataprepActionNode->FindPin( UEdGraphSchema_K2::PN_Execute, EGPD_Input );
		check(FirstInputPin);
		TArray<UEdGraphPin*> FirstInputs = FirstInputPin->LinkedTo;

		// Grab the in/out of the second action
		UDataprepActionAsset* SecondDataprepActionAsset = ActionAssets[SecondActionIndex];
		check(SecondDataprepActionAsset);
		UK2Node_DataprepActionCore* SecondDataprepActionNode = Cast<UK2Node_DataprepActionCore>( SecondDataprepActionAsset->GetOuter() );
		check(SecondDataprepActionNode);

		UEdGraphPin* SecondOuputPin = SecondDataprepActionNode->FindPin( UEdGraphSchema_K2::PN_Then, EGPD_Output );
		check(SecondOuputPin);
		UEdGraphPin* SecondOuput = GetOutput( *SecondOuputPin );

		UEdGraphPin* SecondInputPin = SecondDataprepActionNode->FindPin( UEdGraphSchema_K2::PN_Execute, EGPD_Input );
		check(SecondInputPin);
		TArray<UEdGraphPin*> SecondInputs = SecondInputPin->LinkedTo;

		// Reconnect the nodes
		// Notification will be send latter for the froms and to (modification are still recorded if there is a transaction)
		constexpr bool bSendNotification = false;
		FirstOuputPin->BreakAllPinLinks( bSendNotification );
		FirstInputPin->BreakAllPinLinks( bSendNotification );
		SecondOuputPin->BreakAllPinLinks( bSendNotification );
		SecondInputPin->BreakAllPinLinks( bSendNotification );

		const UEdGraphSchema* GraphSchema = FirstDataprepActionNode->GetSchema();
		check(GraphSchema);
		if ( FMath::Abs( FirstActionIndex - SecondActionIndex ) == 1 )
		{
			for (UEdGraphPin* FirstInput : FirstInputs)
			{
				GraphSchema->TryCreateConnection( FirstInput, SecondInputPin );
			}
			GraphSchema->TryCreateConnection( FirstOuputPin, SecondOuput );
			GraphSchema->TryCreateConnection( SecondOuputPin, FirstInputPin );
		}
		else
		{
			GraphSchema->TryCreateConnection(FirstOuputPin, SecondOuput);
			for (UEdGraphPin* SecondInput : SecondInputs)
			{
				GraphSchema->TryCreateConnection(SecondInput, FirstInputPin);
			}
			GraphSchema->TryCreateConnection(SecondOuputPin, FirstOuput);
			for (UEdGraphPin* FirstInput : FirstInputs)
			{
				GraphSchema->TryCreateConnection(FirstInput, SecondInputPin);
			}
		}

		UpdateActions();
	}
	else
	{
		UE_LOG( LogDataprepCore
			, Error
			, TEXT("Can't swap the actions of a DataprepAsset without actions. (DataprepAsset: %s)")
			, *GetPathName()
			);
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
#endif

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
		CopyOfAction->SetFlags(EObjectFlags::RF_Transactional);
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

void UDataprepAsset::BindObjectPropertyToParameterization(UDataprepParameterizableObject* Object, const TArray<struct FDataprepPropertyLink>& InPropertyChain, const FName& Name)
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

bool UDataprepAsset::IsObjectPropertyBinded(UDataprepParameterizableObject* Object, const TArray<struct FDataprepPropertyLink>& InPropertyChain) const
{
	return Parameterization->IsObjectPropertyBinded( Object, InPropertyChain );
}

FName UDataprepAsset::GetNameOfParameterForObjectProperty(UDataprepParameterizableObject* Object, const TArray<struct FDataprepPropertyLink>& InPropertyChain) const
{
	return Parameterization->GetNameOfParameterForObjectProperty( Object, InPropertyChain );
}

void UDataprepAsset::RemoveObjectPropertyFromParameterization(UDataprepParameterizableObject* Object, const TArray<struct FDataprepPropertyLink>& InPropertyChain)
{
	Parameterization->RemoveBindedObjectProperty( Object, InPropertyChain );
}

void UDataprepAsset::GetExistingParameterNamesForType(FProperty* Property, bool bIsDescribingFullProperty, TSet<FString>& OutValidExistingNames, TSet<FString>& OutInvalidNames) const
{
	Parameterization->GetExistingParameterNamesForType( Property, bIsDescribingFullProperty, OutValidExistingNames, OutInvalidNames );
}

#ifndef NO_BLUEPRINT
void UDataprepAsset::OnDataprepBlueprintChanged( UBlueprint* InBlueprint )
{
	if(InBlueprint == DataprepRecipeBP)
	{
		UpdateActions();
		OnChanged.Broadcast( FDataprepAssetChangeType::RecipeModified );
	}
}

void UDataprepAsset::UpdateActions(bool bNotify)
{
	ActionAssets.Empty(ActionAssets.Num());

	UEdGraphPin* NodeOutPin= StartNode->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output);
	if( NodeOutPin && NodeOutPin->LinkedTo.Num() > 0 )
	{
		TSet<UEdGraphNode*> ActionNodesVisited;

		for( UEdGraphPin* NextNodeInPin = NodeOutPin->LinkedTo[0]; NextNodeInPin != nullptr ; )
		{
			UEdGraphNode* NextNode = NextNodeInPin->GetOwningNode();

			uint32 NodeHash = GetTypeHash( NextNode );
			// Break the loop if the node had already been visited
			if( ActionNodesVisited.FindByHash( NodeHash, NextNode ) )
			{
				break;
			}
			else
			{
				ActionNodesVisited.AddByHash( NodeHash, NextNode );
			}

			if(UK2Node_DataprepActionCore* ActionNode = Cast<UK2Node_DataprepActionCore>(NextNode))
			{
				if( UDataprepActionAsset* DataprepAction = ActionNode->GetDataprepAction() )
				{
					DataprepAction->Rename(nullptr, this, REN_DoNotDirty | REN_DontCreateRedirectors | REN_NonTransactional);
					ActionAssets.Add( DataprepAction );
				}
			}

			// Look for the next node
			NodeOutPin = NextNode->FindPin( UEdGraphSchema_K2::PN_Then, EGPD_Output );

			if ( !NodeOutPin )
			{
				// If we couldn't find a then pin try to get the first output pin as a fallback
				for ( UEdGraphPin* Pin : NextNode->Pins )
				{
					if ( Pin && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec && Pin->Direction == EGPD_Output )
					{
						NodeOutPin = Pin;
						break;
					}
				}
			}

			NextNodeInPin = NodeOutPin ? ( NodeOutPin->LinkedTo.Num() > 0 ? NodeOutPin->LinkedTo[0] : nullptr ) : nullptr;
		}
	}

	if(bNotify)
	{
		OnActionChanged.Broadcast(ActionAssets.Num() > 0 ? ActionAssets[0] : nullptr, FDataprepAssetChangeType::ActionAdded);
	}

	CachedActionCount = ActionAssets.Num();
}
#endif

int32 UDataprepAsset::AddAction(const UDataprepActionAsset* InAction)
{
	UDataprepActionAsset* Action = InAction ? DuplicateObject<UDataprepActionAsset>( InAction, this) : NewObject<UDataprepActionAsset>( this, UDataprepActionAsset::StaticClass(), NAME_None, RF_Transactional );

	if ( Action )
	{
		Modify();

		Action->SetFlags(EObjectFlags::RF_Transactional);
		Action->SetLabel( InAction ? InAction->GetLabel() : TEXT("New Action") );

		ActionAssets.Add( Action );
		OnActionChanged.Broadcast(Action, FDataprepAssetChangeType::ActionAdded);

		CachedActionCount = ActionAssets.Num();

		return ActionAssets.Num() - 1;
	}

	UE_LOG( LogDataprepCore, Error, TEXT("UDataprepAsset::AddAction: The action is invalid") );
	ensure(false);

	// Invalid
	return INDEX_NONE;
}

int32 UDataprepAsset::AddActions(const TArray<const UDataprepActionAsset*>& InActions)
{
	if ( InActions.Num() > 0 && InActions[0] != nullptr )
	{
		Modify();

		int32 PreviousActionCount = ActionAssets.Num();

		for(const UDataprepActionAsset* InAction : InActions)
		{
			if(InAction)
			{
				UDataprepActionAsset* Action = DuplicateObject<UDataprepActionAsset>( InAction, this);
				Action->SetFlags(EObjectFlags::RF_Transactional);
				Action->SetLabel( InAction->GetLabel() );

				ActionAssets.Add( Action );
			}
		}

		CachedActionCount = ActionAssets.Num();

		if(PreviousActionCount != CachedActionCount)
		{
			OnActionChanged.Broadcast(ActionAssets.Last(), FDataprepAssetChangeType::ActionAdded);

			return ActionAssets.Num() - 1;
		}
	}

	UE_LOG( LogDataprepCore, Error, TEXT("UDataprepAsset::AddActions: None of the action steps is invalid") );
	ensure(false);

	// Invalid
	return INDEX_NONE;
}

int32 UDataprepAsset::AddActions(const TArray<const UDataprepActionStep*>& InActionSteps, bool bCreateOne)
{
	if ( InActionSteps.Num() > 0 && InActionSteps[0] != nullptr )
	{
		Modify();

		int32 PreviousActionCount = ActionAssets.Num();

		if(bCreateOne == true)
		{
			UDataprepActionAsset* Action = NewObject<UDataprepActionAsset>( this, UDataprepActionAsset::StaticClass(), NAME_None, RF_Transactional );
			Action->SetLabel( TEXT("New Action") );

			ActionAssets.Add( Action );

			Action->AddSteps(InActionSteps);
		}
		else
		{

			for(const UDataprepActionStep* InActionStep : InActionSteps)
			{
				if(InActionStep)
				{
					UDataprepActionAsset* Action = NewObject<UDataprepActionAsset>( this, UDataprepActionAsset::StaticClass(), NAME_None, RF_Transactional );
					Action->SetLabel( TEXT("New Action") );

					ActionAssets.Add( Action );

					Action->AddStep(InActionStep);
				}
			}
		}

		CachedActionCount = ActionAssets.Num();

		if(PreviousActionCount != CachedActionCount)
		{
			OnActionChanged.Broadcast(ActionAssets.Last(), FDataprepAssetChangeType::ActionAdded);

			return ActionAssets.Num() - 1;
		}
	}

	UE_LOG( LogDataprepCore, Error, TEXT("UDataprepAsset::AddActionSteps: None of the action steps is invalid") );
	ensure(false);

	// Invalid
	return INDEX_NONE;
}

bool UDataprepAsset::InsertAction(const UDataprepActionAsset* InAction, int32 Index)
{
	if(!ActionAssets.IsValidIndex(Index))
	{
		UE_LOG( LogDataprepCore, Error, TEXT("UDataprepAsset::InsertAction: The index is invalid") );
		return false;
	}

	if ( InAction )
	{
		Modify();

		UDataprepActionAsset* Action = DuplicateObject<UDataprepActionAsset>( InAction, this);
		Action->SetFlags(EObjectFlags::RF_Transactional);
		Action->SetLabel( InAction->GetLabel());

		ActionAssets.Insert( Action, Index );

		OnActionChanged.Broadcast(Action, FDataprepAssetChangeType::ActionAdded);

		CachedActionCount = ActionAssets.Num();

		return true;
	}

	UE_LOG( LogDataprepCore, Error, TEXT("UDataprepAsset::InsertAction: The action is invalid") );
	ensure(false);

	// Invalid
	return false;
}

bool UDataprepAsset::InsertActions(const TArray<const UDataprepActionAsset*>& InActions, int32 Index)
{
	if(!ActionAssets.IsValidIndex(Index))
	{
		UE_LOG( LogDataprepCore, Error, TEXT("UDataprepAsset::InsertActions: The index is invalid") );
		return false;
	}

	if ( InActions.Num() > 0 && InActions[0] != nullptr )
	{
		Modify();

		int32 PreviousActionCount = ActionAssets.Num();

		int32 InsertIndex = Index;

		for(const UDataprepActionAsset* InAction : InActions)
		{
			if(InAction)
			{
				UDataprepActionAsset* Action = DuplicateObject<UDataprepActionAsset>( InAction, this);
				Action->SetFlags(EObjectFlags::RF_Transactional);
				Action->SetLabel( InAction->GetLabel() );

				ActionAssets.Insert( Action, InsertIndex );

				++InsertIndex;
			}
		}

		CachedActionCount = ActionAssets.Num();

		if(PreviousActionCount != CachedActionCount)
		{
			OnActionChanged.Broadcast(ActionAssets.Last(), FDataprepAssetChangeType::ActionAdded);

			return true;
		}
	}

	UE_LOG( LogDataprepCore, Error, TEXT("UDataprepAsset::InsertActions: None of the actions is invalid") );
	ensure(false);

	// Invalid
	return false;
}

bool UDataprepAsset::InsertActions(const TArray<const UDataprepActionStep*>& InActionSteps, int32 Index, bool bCreateOne)
{
	if ( InActionSteps.Num() > 0 && InActionSteps[0] != nullptr )
	{
		Modify();

		int32 PreviousActionCount = ActionAssets.Num();

		if(bCreateOne == true)
		{
			UDataprepActionAsset* Action = NewObject<UDataprepActionAsset>( this, UDataprepActionAsset::StaticClass(), NAME_None, RF_Transactional );
			Action->SetLabel( TEXT("New Action") );

			ActionAssets.Insert( Action, Index );

			Action->AddSteps(InActionSteps);
		}
		else
		{
			int32 InsertIndex = Index;

			for(const UDataprepActionStep* InActionStep : InActionSteps)
			{
				if(InActionStep)
				{
					UDataprepActionAsset* Action = NewObject<UDataprepActionAsset>( this, UDataprepActionAsset::StaticClass(), NAME_None, RF_Transactional );
					Action->SetLabel( TEXT("New Action") );

					ActionAssets.Insert( Action, InsertIndex );
					++InsertIndex;

					Action->AddStep(InActionStep);
				}
			}
		}

		CachedActionCount = ActionAssets.Num();

		if(CachedActionCount != PreviousActionCount)
		{
			OnActionChanged.Broadcast(ActionAssets.Last(), FDataprepAssetChangeType::ActionAdded);

			return true;
		}
	}

	UE_LOG( LogDataprepCore, Error, TEXT("UDataprepAsset::AddAction: None of the action steps is invalid") );
	ensure(false);

	// Invalid
	return false;
}

bool UDataprepAsset::MoveAction(int32 SourceIndex, int32 DestinationIndex)
{
	if ( SourceIndex == DestinationIndex )
	{
		UE_LOG( LogDataprepCore, Error, TEXT("UDataprepAsset::MoveAction: Nothing done. Moving to current location") );
		return true;
	}

	if ( !ActionAssets.IsValidIndex( SourceIndex ) || !ActionAssets.IsValidIndex( DestinationIndex ) )
	{
		if ( !ActionAssets.IsValidIndex( SourceIndex ) )
		{
			UE_LOG( LogDataprepCore, Error, TEXT("UDataprepAsset::MoveAction: The Step Index is out of range") );
		}

		if ( !ActionAssets.IsValidIndex( DestinationIndex ) )
		{
			UE_LOG( LogDataprepCore, Error, TEXT("UDataprepAsset::MoveAction: The Destination Index is out of range") );
		}
		return false;
	}

	Modify();

	if ( DataprepCorePrivateUtils::MoveArrayElement( ActionAssets, SourceIndex, DestinationIndex ) )
	{
		OnActionChanged.Broadcast(ActionAssets[DestinationIndex], FDataprepAssetChangeType::ActionMoved);
		return true;
	}

	ensure( false );
	return false;
}

bool UDataprepAsset::RemoveAction(int32 Index)
{
	if ( ActionAssets.IsValidIndex( Index ) )
	{
		Modify();

		UDataprepActionAsset* ActionAsset = ActionAssets[Index];
		if(ActionAsset)
		{
			ActionAsset->NotifyDataprepSystemsOfRemoval();
		}

		ActionAssets.RemoveAt( Index );

		CachedActionCount = ActionAssets.Num();

		OnActionChanged.Broadcast(ActionAsset, FDataprepAssetChangeType::ActionRemoved);

		return true;
	}

	ensure( false );
	UE_LOG( LogDataprepCore, Error, TEXT("UDataprepAsset::RemoveAction: The Index is out of range") );

	return false;
}

bool UDataprepAsset::RemoveActions(const TArray<int32>& Indices)
{
	bool bHasValidIndices = false;
	for(int32 Index : Indices)
	{
		if(ActionAssets.IsValidIndex( Index ))
		{
			bHasValidIndices = true;
			break;
		}
	}

	if ( bHasValidIndices )
	{
		Modify();

		// Used to cache last action removed
		UDataprepActionAsset* ActionAsset = nullptr;

		// Sort array in reverse order before removal
		TArray<int32> LocalIndices = Indices;
		LocalIndices.Sort(TGreater<int32>());

		// Now safe to use TArray::RemoveAt
		for(int32 Index : LocalIndices)
		{
			if(ActionAssets.IsValidIndex( Index ))
			{
				ActionAsset = ActionAssets[Index];
				if(ActionAsset)
				{
					ActionAsset->NotifyDataprepSystemsOfRemoval();
				}

				ActionAssets.RemoveAt(Index);
			}
		}

		CachedActionCount = ActionAssets.Num();

		// Notify on last action removed
		OnActionChanged.Broadcast(ActionAsset, FDataprepAssetChangeType::ActionRemoved);

		return true;
	}

	ensure( false );
	UE_LOG( LogDataprepCore, Error, TEXT("UDataprepAsset::RemoveActions: None of the indices are in range") );

	return false;
}

#undef LOCTEXT_NAMESPACE
