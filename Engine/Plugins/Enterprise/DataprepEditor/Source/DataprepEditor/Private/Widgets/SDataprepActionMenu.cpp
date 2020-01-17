// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SDataprepActionMenu.h"

#include "BlueprintNodes/K2Node_DataprepAction.h"
#include "DataprepActionAsset.h"
#include "SGraphActionMenu.h"
#include "SchemaActions/DataprepSchemaAction.h"
#include "SchemaActions/DataprepSchemaActionUtils.h"
#include "SchemaActions/IDataprepMenuActionCollector.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphSchema.h"
#include "EditorStyleSet.h"
#include "Framework/Application/SlateApplication.h"
#include "ScopedTransaction.h"
#include "Widgets/Views/SExpanderArrow.h"

void SDataprepActionMenu::Construct(const FArguments& InArgs, TUniquePtr<IDataprepMenuActionCollector> InMenuActionCollector)
{
	MenuActionCollector = MoveTemp( InMenuActionCollector );
	
	Context = InArgs._DataprepActionContext;
	TransactionTextGetter = InArgs._TransactionText;
	GraphObj = InArgs._GraphObj;
	NewNodePosition = InArgs._NewNodePosition;
	DraggedFromPins = InArgs._DraggedFromPins;
	OnClosedCallback = InArgs._OnClosedCallback;

	ChildSlot
	[
		SNew( SBorder )
		.BorderImage( FEditorStyle::GetBrush("Menu.Background") )
		.Padding( 5 )
		[
			SNew( SBox )
			.WidthOverride( 300 )
			.HeightOverride( 400 )
			[
				SAssignNew( ActionMenu, SGraphActionMenu )
				.OnActionSelected( this, &SDataprepActionMenu::OnActionSelected )
				.OnCollectAllActions( this, &SDataprepActionMenu::CollectActions )
				.OnCreateCustomRowExpander( this, &SDataprepActionMenu::OnCreateCustomRowExpander )
				.AutoExpandActionMenu( MenuActionCollector->ShouldAutoExpand() )
				.ShowFilterTextBox( true )
			]
		]
	];
}

TSharedPtr<class SEditableTextBox> SDataprepActionMenu::GetFilterTextBox()
{
	return ActionMenu->GetFilterTextBox();
}

SDataprepActionMenu::~SDataprepActionMenu()
{
	OnClosedCallback.ExecuteIfBound();
}

void SDataprepActionMenu::CollectActions(FGraphActionListBuilderBase& OutActions)
{
	for ( TSharedPtr<FDataprepSchemaAction> Action : MenuActionCollector->CollectActions() )
	{
		OutActions.AddAction( StaticCastSharedPtr<FEdGraphSchemaAction>( Action ) );
	}
}

void SDataprepActionMenu::OnActionSelected(const TArray< TSharedPtr<FEdGraphSchemaAction> >& SelectedActions, ESelectInfo::Type InSelectionType)
{
	/**
	 * Create a node if needed and execute the action
	 * return true if the action was properly executed
	 */
	auto ExcuteAction = [this](FDataprepSchemaAction& DataprepAction) -> bool
		{
			UK2Node_DataprepAction* ActionNode = nullptr;

			if ( ShouldCreateNewNode() )
			{
				ActionNode = DataprepSchemaActionUtils::SpawnEdGraphNode<UK2Node_DataprepAction>( *GraphObj, NewNodePosition );
				ActionNode->CreateDataprepActionAsset();
				Context.DataprepActionPtr = MakeWeakObjectPtr<UDataprepActionAsset>( ActionNode->GetDataprepAction() );

				if ( DraggedFromPins.Num() > 0 )
				{
					ActionNode->AutowireNewNode( DraggedFromPins[0] );
				}
			}

			if ( !DataprepAction.ExecuteAction( Context ) )
			{
				if ( ActionNode )
				{
					// Remove the created node
					GraphObj->RemoveNode( ActionNode );
				}
				return false;
			}

			return true;
		};

	for ( const TSharedPtr<FEdGraphSchemaAction>& Action : SelectedActions )
	{
		FDataprepSchemaAction* DataprepAction = static_cast< FDataprepSchemaAction* >( Action.Get() );
		if ( DataprepAction )
		{
			if ( TransactionTextGetter.IsSet() )
			{
				FScopedTransaction Transaction( TransactionTextGetter.Get() );

				if ( !ExcuteAction( *DataprepAction ) )
				{
					Transaction.Cancel();
				}
			}
			else
			{
				ExcuteAction( *DataprepAction );
			}
		}
	}

	if ( SelectedActions.Num() > 0 )
	{
		FSlateApplication::Get().DismissAllMenus();
	}
}

TSharedRef<SExpanderArrow> SDataprepActionMenu::OnCreateCustomRowExpander(const FCustomExpanderData& InCustomExpanderData) const
{
	return SNew( SExpanderArrow, InCustomExpanderData.TableRow );
}

bool SDataprepActionMenu::ShouldCreateNewNode() const
{
	if ( GraphObj && !Context.DataprepActionPtr.Get() )
	{
		return true;
	}

	return false;
}
