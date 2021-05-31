// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigEditor/SIKRigSolverStack.h"

#include "RigEditor/IKRigEditorStyle.h"
#include "RigEditor/IKRigToolkit.h"
#include "RigEditor/IKRigEditorController.h"
#include "IKRigSolver.h"

#include "Framework/Commands/UICommandList.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "IPersonaToolkit.h"
#include "SKismetInspector.h"
#include "Dialogs/Dialogs.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "SEditorHeaderButton.h"

#define LOCTEXT_NAMESPACE "SIKRigSolverStack"

TSharedRef<ITableRow> FSolverStackElement::MakeListRowWidget(
	const TSharedRef<STableViewBase>& InOwnerTable,
	TSharedRef<FSolverStackElement> InStackElement,
	TSharedPtr<SIKRigSolverStack> InSolverStack)
{
	return SNew(SIKRigSolverStackItem, InOwnerTable, InStackElement, InSolverStack);
}

void SIKRigSolverStackItem::Construct(
	const FArguments& InArgs,
	const TSharedRef<STableViewBase>& OwnerTable,
	TSharedRef<FSolverStackElement> InStackElement,
	TSharedPtr<SIKRigSolverStack> InSolverStack)
{
	STableRow<TSharedPtr<FSolverStackElement>>::Construct(
        STableRow<TSharedPtr<FSolverStackElement>>::FArguments()
        .OnDragDetected(InSolverStack.Get(), &SIKRigSolverStack::OnDragDetected)
        .OnCanAcceptDrop(InSolverStack.Get(), &SIKRigSolverStack::OnCanAcceptDrop)
        .OnAcceptDrop(InSolverStack.Get(), &SIKRigSolverStack::OnAcceptDrop)
        .Content()
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot()
            .MaxWidth(18)
			.FillWidth(1.0)
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
            [
                SNew(SImage)
                .Image(FIKRigEditorStyle::Get().GetBrush("IKRig.DragSolver"))
            ]
     
			+ SHorizontalBox::Slot()
            .VAlign(VAlign_Fill)
            .HAlign(HAlign_Fill)
            .Padding(3.0f, 1.0f)
            [
				SNew(STextBlock)
				.Text(InStackElement->DisplayName)
				.TextStyle(FEditorStyle::Get(), "NormalText.Important")
            ]
        ], OwnerTable);
}

TSharedRef<FIKRigSolverStackDragDropOp> FIKRigSolverStackDragDropOp::New(TWeakPtr<FSolverStackElement> InElement)
{
	TSharedRef<FIKRigSolverStackDragDropOp> Operation = MakeShared<FIKRigSolverStackDragDropOp>();
	Operation->Element = InElement;
	Operation->Construct();
	return Operation;
}

TSharedPtr<SWidget> FIKRigSolverStackDragDropOp::GetDefaultDecorator() const
{
	return SNew(SBorder)
        .Visibility(EVisibility::Visible)
        .BorderImage(FEditorStyle::GetBrush("Menu.Background"))
        [
            SNew(STextBlock)
            .Text(Element.Pin()->DisplayName)
        ];
}

SIKRigSolverStack::~SIKRigSolverStack()
{
	
}

void SIKRigSolverStack::Construct(const FArguments& InArgs, TSharedRef<FIKRigEditorController> InEditorController)
{
	EditorController = InEditorController;
	EditorController.Pin()->SolverStackView = SharedThis(this);
	
	CommandList = MakeShared<FUICommandList>();

	ChildSlot
    [
        SNew(SVerticalBox)
        +SVerticalBox::Slot()
        .AutoHeight()
        .VAlign(VAlign_Top)
        .Padding(0.0f)
        [
            SNew(SBorder)
            .Padding(0.0f)
            .BorderImage(FEditorStyle::GetBrush("DetailsView.CategoryTop"))
            .BorderBackgroundColor(FLinearColor(0.6f, 0.6f, 0.6f, 1.0f))
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot()
                .AutoHeight()
                .VAlign(VAlign_Top)
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot()
                    .VAlign(VAlign_Center)
                    .HAlign(HAlign_Left)
                    .FillWidth(1.f)
                    .Padding(3.0f, 1.0f)
                    [
	                    SNew(SEditorHeaderButton)
				        .Icon(FAppStyle::Get().GetBrush("Icons.Plus"))
				        .Text(LOCTEXT("AddNewSolverLabel", "Add New Solver"))
				        .ToolTipText(LOCTEXT("AddNewToolTip", "Add a new IK solver to the rig."))
				        .IsEnabled(this, &SIKRigSolverStack::IsAddSolverEnabled)
				        .OnGetMenuContent(this, &SIKRigSolverStack::CreateAddNewMenuWidget)
                    ]
                ]
            ]
        ]

        +SVerticalBox::Slot()
        .Padding(0.0f, 0.0f)
        [
			SAssignNew( ListView, SSolverStackListViewType )
			.SelectionMode(ESelectionMode::Multi)
			.IsEnabled(this, &SIKRigSolverStack::IsAddSolverEnabled)
			.ListItemsSource( &ListViewItems )
			.OnGenerateRow( this, &SIKRigSolverStack::MakeListRowWidget )
			.OnMouseButtonClick(this, &SIKRigSolverStack::OnItemClicked)
        ]
    ];

	RefreshStackView();
}

TSharedRef<SWidget> SIKRigSolverStack::CreateAddNewMenuWidget()
{
	const bool bShouldCloseWindowAfterMenuSelection = true;
	FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, CommandList);

	BuildAddNewMenu(MenuBuilder);

	return MenuBuilder.MakeWidget();
}

void SIKRigSolverStack::BuildAddNewMenu(FMenuBuilder& MenuBuilder)
{
	MenuBuilder.BeginSection("AddNewSolver", LOCTEXT("AddOperations", "Add New Solver"));
	
	// add menu option to create each solver type
	TArray<UClass*> SolverClasses;
	for(TObjectIterator<UClass> ClassIt; ClassIt; ++ClassIt)
	{
		UClass* Class = *ClassIt;

		if(Class->IsChildOf(UIKRigSolver::StaticClass()) && !Class->HasAnyClassFlags(CLASS_Abstract))
		{
			FUIAction Action = FUIAction( FExecuteAction::CreateSP(this, &SIKRigSolverStack::AddNewSolver, Class));
			MenuBuilder.AddMenuEntry(FText::FromString(Class->GetName()), FText::GetEmpty(), FSlateIcon(), Action);
		}
	}
	
	MenuBuilder.EndSection();
}

bool SIKRigSolverStack::IsAddSolverEnabled() const
{
	if (UIKRigController* Controller = EditorController.Pin()->AssetController)
	{
		if (Controller->GetSkeleton().BoneNames.Num() > 0)
		{
			return true;
		}
	}
	
	return false;
}

void SIKRigSolverStack::AddNewSolver(UClass* Class)
{
	if (UIKRigController* Controller = EditorController.Pin()->AssetController)
	{
		// add the solver
		const int32 NewSolverIndex = Controller->AddSolver(Class);
	}

	RefreshStackView();
	EditorController.Pin()->SkeletonView->RefreshTreeView(); // update solver indices in effector items
}

void SIKRigSolverStack::RefreshStackView()
{
	ListViewItems.Reset();
	UIKRigController* Controller = EditorController.Pin()->AssetController;
	const int32 NumSolvers = Controller->GetNumSolvers();
	for (int32 i=0; i<NumSolvers; ++i)
	{
		UIKRigSolver* Solver = Controller->GetSolver(i);
		const FText DisplayName = Solver ? FText::FromString(Solver->GetName()) : FText::FromString("Unknown Solver");
		TSharedPtr<FSolverStackElement> SolverItem = FSolverStackElement::Make(DisplayName, i);
		ListViewItems.Add(SolverItem);
	}

	// select first item if none others selected
	if (ListViewItems.Num() > 0 && ListView->GetNumItemsSelected() == 0)
	{
		ListView->SetSelection(ListViewItems[0]);
	}

	ListView->RequestListRefresh();
}

TSharedRef<ITableRow> SIKRigSolverStack::MakeListRowWidget(
	TSharedPtr<FSolverStackElement> InElement,
    const TSharedRef<STableViewBase>& OwnerTable)
{
	return InElement->MakeListRowWidget(
		OwnerTable,
		InElement.ToSharedRef(),
		SharedThis(this));
}

FReply SIKRigSolverStack::OnDragDetected(
	const FGeometry& MyGeometry,
	const FPointerEvent& MouseEvent)
{
	const TArray<TSharedPtr<FSolverStackElement>> SelectedItems = ListView.Get()->GetSelectedItems();
	if (SelectedItems.Num() != 1)
	{
		return FReply::Unhandled();
	}
	
	if (MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton))
	{
		const TSharedPtr<FSolverStackElement> DraggedElement = SelectedItems[0];
		const TSharedRef<FIKRigSolverStackDragDropOp> DragDropOp = FIKRigSolverStackDragDropOp::New(DraggedElement);
		return FReply::Handled().BeginDragDrop(DragDropOp);
	}

	return FReply::Unhandled();
}

void SIKRigSolverStack::OnItemClicked(TSharedPtr<FSolverStackElement> InItem)
{
	EditorController.Pin()->ShowDetailsForSolver(InItem.Get()->IndexInStack);
	EditorController.Pin()->SkeletonView->RefreshTreeView(); // update filter showing which bones are affected
}

TOptional<EItemDropZone> SIKRigSolverStack::OnCanAcceptDrop(
	const FDragDropEvent& DragDropEvent,
	EItemDropZone DropZone,
	TSharedPtr<FSolverStackElement> TargetItem)
{
	TOptional<EItemDropZone> ReturnedDropZone;
	
	const TSharedPtr<FIKRigSolverStackDragDropOp> DragDropOp = DragDropEvent.GetOperationAs<FIKRigSolverStackDragDropOp>();
	if (DragDropOp.IsValid())
	{
		ReturnedDropZone = EItemDropZone::BelowItem;	
	}
	
	return ReturnedDropZone;
}

FReply SIKRigSolverStack::OnAcceptDrop(
	const FDragDropEvent& DragDropEvent,
	EItemDropZone DropZone,
	TSharedPtr<FSolverStackElement> TargetItem)
{
	const TSharedPtr<FIKRigSolverStackDragDropOp> DragDropOp = DragDropEvent.GetOperationAs<FIKRigSolverStackDragDropOp>();
	if (!DragDropOp.IsValid())
	{
		return FReply::Unhandled();
	}

	const FSolverStackElement& DraggedElement = *DragDropOp.Get()->Element.Pin().Get();
	UIKRigController* Controller = EditorController.Pin()->AssetController;
	const bool bWasReparented = Controller->MoveSolverInStack(DraggedElement.IndexInStack, TargetItem.Get()->IndexInStack);
	if (bWasReparented)
	{
		RefreshStackView();
		EditorController.Pin()->SkeletonView->RefreshTreeView(); // update solver indices in effector items
	}
	
	return FReply::Handled();
}

FReply SIKRigSolverStack::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();

	// handle deleting selected solver
	if (Key == EKeys::Delete)
	{
		TArray<TSharedPtr<FSolverStackElement>> SelectedItems = ListView->GetSelectedItems();
		if (SelectedItems.IsEmpty())
		{
			return FReply::Unhandled();
		}

		UIKRigController* Controller = EditorController.Pin()->AssetController;
		UIKRigSolver* SolverToRemove = Controller->GetSolver(SelectedItems[0]->IndexInStack);
		Controller->RemoveSolver(SolverToRemove);

		RefreshStackView();
		EditorController.Pin()->SkeletonView->RefreshTreeView(); // update solver indices in effector items
		
		return FReply::Handled();
	}
	
	return FReply::Unhandled();
}

#undef LOCTEXT_NAMESPACE
