// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/Action/SDataprepActionBlock.h"

#include "DataprepActionAsset.h"
#include "DataprepEditorStyle.h"
#include "SchemaActions/DataprepDragDropOp.h"
#include "SchemaActions/DataprepSchemaAction.h"

#include "Editor.h"
#include "Editor/TransBuffer.h"
#include "Framework/Application/IMenu.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Commands/GenericCommands.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Text/TextLayout.h"
#include "Layout/WidgetPath.h"
#include "ScopedTransaction.h"
#include "Styling/ISlateStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Layout/Anchors.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SDataprepActionBlock"

void SDataprepActionBlock::Construct(const FArguments& InArgs, const TSharedRef<FDataprepSchemaActionContext>& InDataprepActionContext)
{
	DataprepActionContext = InDataprepActionContext;
	const ISlateStyle* DataprepEditorStyle = FSlateStyleRegistry::FindSlateStyle( FDataprepEditorStyle::GetStyleSetName() );
	check( DataprepEditorStyle );

	const float DefaultPadding = DataprepEditorStyle->GetFloat( "DataprepAction.Padding" );

	ChildSlot
	[
		SNew( SConstraintCanvas )

		// The outline. This is done by a background image
		+ SConstraintCanvas::Slot()
		.Anchors( FAnchors( 0.f, 0.f, 1.f, 1.f ) )
		.Offset( FMargin() )
		[
			SNew( SColorBlock )
			.Color( DataprepEditorStyle->GetColor( "DataprepAction.OutlineColor" ) )
		]

		+ SConstraintCanvas::Slot()
		.Anchors( FAnchors( 0.f, 0.f, 1.f, 1.f ) )
		.Offset( FMargin() )
		.AutoSize( true )
		[
			SNew( SVerticalBox )

			//The title of the block
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding( FMargin( DefaultPadding, DefaultPadding, DefaultPadding, 0.f ) )
			[
				SNew( SConstraintCanvas )

				// The background of the title
				+ SConstraintCanvas::Slot()
				.Anchors( FAnchors( 0.f, 0.f, 1.f, 1.f ) )
				.Offset( FMargin() )
				[
					GetTitleBackgroundWidget()
				]

				// The title of the block
				+ SConstraintCanvas::Slot()
				.Anchors( FAnchors( 0.5f, 0.5f, 0.5f, 0.5f ) )
				.Offset( FMargin() )
				.AutoSize( true )
				[
					GetTitleWidget()
				]
			]

			// The content zone of the action block
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding( FMargin( DefaultPadding ) )
			[
				SNew( SConstraintCanvas )

				// The background of the content zone
				+ SConstraintCanvas::Slot()
				.Anchors( FAnchors( 0.f, 0.f, 1.f, 1.f ) )
				.Offset( FMargin() )
				[
					GetContentBackgroundWidget()
				]

				// The content of the content zone
				+ SConstraintCanvas::Slot()
				.Anchors(FAnchors( 0.f, 0.f, 1.f, 1.f ) )
				.Offset( FMargin() )
				.AutoSize( true )
				[
					GetContentWidget()
				]
			]
		]
	];
}

FReply SDataprepActionBlock::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if ( MouseEvent.GetEffectingButton() ==  EKeys::LeftMouseButton )
	{
		return FReply::Handled().DetectDrag( AsShared(), EKeys::LeftMouseButton );
	}
	return FReply::Unhandled();
}

FReply SDataprepActionBlock::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if ( MouseEvent.GetEffectingButton() == EKeys::RightMouseButton )
	{
		FMenuBuilder MenuBuilder( true, nullptr );
		PopulateMenuBuilder( MenuBuilder );

		FWidgetPath WidgetPath = MouseEvent.GetEventPath() ? *MouseEvent.GetEventPath() : FWidgetPath();
		FSlateApplication::Get().PushMenu(
			AsShared(),
			WidgetPath,
			MenuBuilder.MakeWidget(),
			MouseEvent.GetScreenSpacePosition(),
			FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu)
		);

		return FReply::Handled();
	}

	return FReply::Unhandled();
}

FReply SDataprepActionBlock::OnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	check( DataprepActionContext );

	// Is the step moved or copied
	TSharedRef<bool> IsMoved = MakeShared<bool>( true );

	// Callback that move or copy the operation
	FDataprepGraphOperation Operation = FDataprepGraphOperation::CreateLambda( [InitialContext = *DataprepActionContext.Get(), IsMoved] (const FDataprepSchemaActionContext& Context)
		{

			UDataprepActionAsset* InitialAction = InitialContext.DataprepActionPtr.Get();
			UDataprepActionAsset* DroppedOnAction = Context.DataprepActionPtr.Get();
			if ( InitialAction && DroppedOnAction )
			{
				if ( InitialAction == DroppedOnAction && IsMoved.Get() )
				{
					// Move the position of the step in the action
					if ( InitialContext.StepIndex != INDEX_NONE && InitialContext.StepIndex != Context.StepIndex )
					{
						if ( Context.StepIndex == INDEX_NONE )
						{
							if ( InitialContext.StepIndex != DroppedOnAction->GetStepsCount() - 1 )
							{
								// The step will be move at the end of the array
								DroppedOnAction->MoveStep( InitialContext.StepIndex, DroppedOnAction->GetStepsCount() - 1 );
							}
							else
							{
								return false;
							}
						}
						else
						{
							DroppedOnAction->MoveStep( InitialContext.StepIndex, Context.StepIndex );
						}
						return true;
					}
				}
				else
				{
					// Move the step to a another action or duplicate the step
					UDataprepActionStep* ActionStep = InitialContext.DataprepActionStepPtr.Get();
					if ( ActionStep )
					{
						uint32 NewStepIndex = DroppedOnAction->AddStep( ActionStep );
						if ( Context.StepIndex != INDEX_NONE )
						{
							DroppedOnAction->MoveStep( NewStepIndex, Context.StepIndex );
						}
						
						if ( IsMoved.Get() )
						{
							InitialAction->RemoveStep( InitialContext.StepIndex );
						}
						return true;
					}
				}
			}

			return false;
		});

	TSharedRef<FDataprepDragDropOp> DragDropOperation = FDataprepDragDropOp::New( MoveTemp( Operation ) );

	// The pre drop pop the contextual menu allowing the user to select between copy, move and simply cancel the drag and drop.
	FDataprepPreDropConfirmation PreDropConfirmation = FDataprepPreDropConfirmation::CreateLambda( [ SourceWidget = AsShared(), IsMoved ] (const FDataprepSchemaActionContext& Context, TFunction<void ()> ConfirmationCallback)
		{
			auto CopyDraggedItems = [ConfirmationCallback, IsMoved] ()
				{
					IsMoved.Get() = false;
					ConfirmationCallback();
				};

			auto MoveDraggedItems = [ConfirmationCallback, IsMoved] ()
				{
					IsMoved.Get() = true;
					ConfirmationCallback();
				};

			FModifierKeysState ModifierKeyState = FSlateApplication::Get().GetModifierKeys();

			bool bCopyKeyDown = ModifierKeyState.IsControlDown()  || ModifierKeyState.IsCommandDown();
			bool bMoveKeyDown = ModifierKeyState.IsAltDown();

			if ( bCopyKeyDown && !bMoveKeyDown )
			{
				// The user is using the shortcut to copy the dragged items
				IsMoved.Get() = false;
				ConfirmationCallback();
			}
			else if ( bMoveKeyDown && !bCopyKeyDown )
			{
				// The user using the shortcut to move the dragged items
				IsMoved.Get() = true;
				ConfirmationCallback();
			}
			else
			{
				FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection=*/true, nullptr);
				const FText MoveCopyHeaderString = LOCTEXT("AssetViewDropMenuHeading", "Move or Copy");
				MenuBuilder.BeginSection("PathAssetMoveCopy", MoveCopyHeaderString);
				{
					MenuBuilder.AddMenuEntry(
						LOCTEXT("DragDropMove", "Move Here"),
						LOCTEXT("DragDropMoveTooltip", "Move the dragged items."),
						FSlateIcon(),
						FUIAction( FExecuteAction::CreateLambda( MoveDraggedItems ) )
					);
					MenuBuilder.AddMenuEntry(
						LOCTEXT("DragDropCopy", "Copy Here"),
						LOCTEXT("DragDropCopyTooltip", "Copy the dragged items."),
						FSlateIcon(),
						FUIAction( FExecuteAction::CreateLambda( CopyDraggedItems ) )
					);
				}
				MenuBuilder.EndSection();

				TSharedPtr<IMenu> Menu = FSlateApplication::Get().PushMenu(
					FSlateApplication::Get().GetActiveTopLevelWindow().ToSharedRef(),
					FWidgetPath(),
					MenuBuilder.MakeWidget(),
					FSlateApplication::Get().GetCursorPos(),
					FPopupTransitionEffect( FPopupTransitionEffect::ContextMenu )
					);

				if ( GEditor && GEditor->Trans && Menu )
				{
					UTransBuffer* TransBuffer = CastChecked<UTransBuffer>( GEditor->Trans );
					FDelegateHandle Handle = TransBuffer->OnBeforeRedoUndo().AddLambda( [Menu] (const struct FTransactionContext&)
						{
							FSlateApplication::Get().DismissMenu( Menu );
						});

					Menu->GetOnMenuDismissed().AddLambda( [Handle] (TSharedRef<IMenu>)
						{
							if ( GEditor && GEditor->Trans )
							{
								UTransBuffer* TransBuffer = CastChecked<UTransBuffer>( GEditor->Trans );
								TransBuffer->OnBeforeRedoUndo().Remove( Handle );
							}
						} );
				}
			}
		} );

	DragDropOperation->SetPreDropConfirmation( MoveTemp( PreDropConfirmation ) );

	return FReply::Handled().BeginDragDrop( MoveTemp( DragDropOperation ) );
}

FText SDataprepActionBlock::GetBlockTitle() const
{
	return FText::FromString( TEXT("Default Action Block Title") );
}

TSharedRef<SWidget> SDataprepActionBlock::GetTitleWidget()
{
	const ISlateStyle* DataprepEditorStyle = FSlateStyleRegistry::FindSlateStyle(FDataprepEditorStyle::GetStyleSetName());
	check( DataprepEditorStyle );
	const float DefaultPadding = DataprepEditorStyle->GetFloat( "DataprepAction.Padding" );

	return SNew( STextBlock )
		.Text( GetBlockTitle() )
		.TextStyle( &DataprepEditorStyle->GetWidgetStyle<FTextBlockStyle>( "DataprepActionBlock.TitleTextBlockStyle" ) )
		.ColorAndOpacity( FLinearColor( 1.f, 1.f, 1.f ) )
		.Margin( FMargin( DefaultPadding ) )
		.Justification( ETextJustify::Center );
}

TSharedRef<SWidget> SDataprepActionBlock::GetTitleBackgroundWidget()
{
	const ISlateStyle* DataprepEditorStyle = FSlateStyleRegistry::FindSlateStyle( FDataprepEditorStyle::GetStyleSetName() );
	check( DataprepEditorStyle );

	return SNew( SColorBlock )
		.Color( DataprepEditorStyle->GetColor( "DataprepActionBlock.TitleBackgroundColor" ) );
}

TSharedRef<SWidget> SDataprepActionBlock::GetContentWidget()
{
	return SNullWidget::NullWidget;
}

TSharedRef<SWidget> SDataprepActionBlock::GetContentBackgroundWidget()
{
	const ISlateStyle* DataprepEditorStyle = FSlateStyleRegistry::FindSlateStyle( FDataprepEditorStyle::GetStyleSetName() );
	check( DataprepEditorStyle );

	return SNew( SColorBlock )
		.Color( DataprepEditorStyle->GetColor( "DataprepActionBlock.ContentBackgroundColor" ) );
}

void SDataprepActionBlock::PopulateMenuBuilder(FMenuBuilder& MenuBuilder)
{
	FUIAction DeleteAction;
	DeleteAction.ExecuteAction.BindSP( this, &SDataprepActionBlock::DeleteStep );

	TSharedPtr<FUICommandInfo> DeleteCommand = FGenericCommands::Get().Delete;
	MenuBuilder.AddMenuEntry( DeleteCommand->GetLabel(),
		DeleteCommand->GetDescription(),
		DeleteCommand->GetIcon(),
		DeleteAction );
}

void SDataprepActionBlock::DeleteStep()
{
	if ( FDataprepSchemaActionContext* ActionContext = DataprepActionContext.Get() )
	{
		if ( UDataprepActionAsset* ActionAsset = ActionContext->DataprepActionPtr.Get() )
		{
			FScopedTransaction Transaction( LOCTEXT("DeleteStepTransaction", "Remove step from action") );
			ActionAsset->RemoveStep( ActionContext->StepIndex );
		}
	}
}

#undef LOCTEXT_NAMESPACE
