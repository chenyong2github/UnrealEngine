// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/List/SObjectMixerEditorListRow.h"

#include "EditorClassUtils.h"
#include "ObjectMixerEditorStyle.h"
#include "Views/List/SObjectMixerEditorList.h"
#include "Views/Widgets/SHyperlinkWithTextHighlight.h"

#include "Customizations/ColorStructCustomization.h"
#include "Editor.h"
#include "EditorActorFolders.h"
#include "Engine/Blueprint.h"
#include "GameFramework/Actor.h"
#include "Input/DragAndDrop.h"
#include "ISinglePropertyView.h"
#include "ObjectMixerEditorLog.h"
#include "PropertyEditorModule.h"
#include "PropertyHandle.h"
#include "ScopedTransaction.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/ComponentEditorUtils.h"
#include "Styling/AppStyle.h"
#include "Styling/StyleColors.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"

#define LOCTEXT_NAMESPACE "ObjectMixerEditor"

const FText DropFormatText = LOCTEXT("DropFormatText", "{0} {1} {2} {3}");
const FText MultiDragFormatText = LOCTEXT("MultiDragFormatText", "{0} Items");

class SInlineEditableRowNameCellWidget : public SCompoundWidget
{
public:
	
	SLATE_BEGIN_ARGS(SObjectMixerEditorListRow)
	{}

	SLATE_END_ARGS()
	
	void Construct(const FArguments& InArgs, TSharedRef<FObjectMixerEditorListRow> InRow, FObjectMixerEditorListRowPtr InHybridChild)
	{
		Item = InRow;
		HybridChild = InHybridChild;

		InRow->OnRenameCommand().BindRaw(this, &SInlineEditableRowNameCellWidget::EnterEditingMode);
		
		TSharedRef<SHorizontalBox> HBox = SNew(SHorizontalBox);
		
		HBox->AddSlot()
		.AutoWidth()
		[
			SNew(SImage)
			.Image(InRow->GetObjectIconBrush())
			.ColorAndOpacity(FSlateColor::UseForeground())
		];

		bool bNeedsStandardTextBlock = true;
		const FText DisplayName = InRow->GetDisplayName(HybridChild.IsValid());
		const TObjectPtr<UObject> RowObject = InRow->GetObject();
		const bool bRowObjectIsValid = IsValid(RowObject);

		if (bRowObjectIsValid)
		{	
			if (const UClass* ActorClass = RowObject->GetClass())
			{
				if (UBlueprint* AsBlueprint = UBlueprint::GetBlueprintFromClass(ActorClass))
				{					
					bNeedsStandardTextBlock = false;
	
					HBox->AddSlot()
					.Padding(FMargin(10.0, 0, 0, 0))
					[
						SAssignNew(HyperlinkTextBlock, SHyperlinkWithTextHighlight)
						.Visibility(EVisibility::Visible)
						.Text(DisplayName)
						.ToolTipText(LOCTEXT("ClickToEditBlueprint", "Click to edit Blueprint"))
						.OnNavigate(this, &SInlineEditableRowNameCellWidget::OnClickBlueprintLink, AsBlueprint, RowObject.Get())
						.HighlightText(this, &SInlineEditableRowNameCellWidget::GetHighlightText)
						.IsSelected_Raw(this, &SInlineEditableRowNameCellWidget::GetIsSelectedExclusively)
						.OnTextCommitted(this, &SInlineEditableRowNameCellWidget::OnTextCommitted)
					];
				}
			}
		}

		if (bNeedsStandardTextBlock)
		{
			FText TooltipText = DisplayName;

			if (bRowObjectIsValid)
			{
				if (const UObjectMixerObjectFilter* Filter = InRow->GetMainObjectFilterInstance())
				{
					TooltipText = Filter->GetRowTooltipText(RowObject, HybridChild.IsValid());
				}
			}
			
			HBox->AddSlot()
			.Padding(FMargin(10.0, 0, 0, 0))
			[
				SAssignNew(EditableTextBlock, SInlineEditableTextBlock)
				.Visibility(EVisibility::Visible)
				.Justification(ETextJustify::Left)
				.Text(DisplayName)
				.ToolTipText(TooltipText)
				.IsReadOnly(false)
				.HighlightText(this, &SInlineEditableRowNameCellWidget::GetHighlightText)
				.IsSelected_Raw(this, &SInlineEditableRowNameCellWidget::GetIsSelectedExclusively)
				.OnTextCommitted(this, &SInlineEditableRowNameCellWidget::OnTextCommitted)
			];
		}
		
		ChildSlot
		[
			SNew(SBox)
			.Visibility(EVisibility::SelfHitTestInvisible)
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.Padding(FMargin(TextBlockLeftPadding, 0.f, 0.f, 0.f))
			[
				HBox
			]
		];
	}

	virtual ~SInlineEditableRowNameCellWidget() override
	{
		if (const FObjectMixerEditorListRowPtr ItemPin = Item.Pin())
		{
			ItemPin->OnRenameCommand().Unbind();
		}
		
		Item.Reset();
		HybridChild.Reset();

		EditableTextBlock.Reset();
	}

	void EnterEditingMode() const
	{
		if (EditableTextBlock.IsValid())
		{
			EditableTextBlock->EnterEditingMode();
		}
		else if (HyperlinkTextBlock.IsValid() && HyperlinkTextBlock->EditableTextBlock.IsValid())
		{
			HyperlinkTextBlock->EditableTextBlock->EnterEditingMode();
		}
	}

private:

	bool IsValidComponentRename(const UActorComponent* ComponentInstance, const FText& InNewText) const
	{
		if (!ComponentInstance)
		{
			return false;
		}
		
		FText OutErrorMessage;
		const FString& NewTextStr = InNewText.ToString();

		if (IsValidRename(InNewText, ComponentInstance->GetName()))
		{
			AActor* Owner = ComponentInstance->GetOwner();

			if (!Owner)
			{
				return false;
			}

			UBlueprint* Blueprint = nullptr;

			if (const UClass* ActorClass = Owner->GetClass())
			{
				Blueprint = UBlueprint::GetBlueprintFromClass(ActorClass);
				if ( Blueprint )
				{
					// Subobject names must conform to UObject naming conventions.
					if (!FName::IsValidXName(NewTextStr, INVALID_OBJECTNAME_CHARACTERS, &OutErrorMessage))
					{
						UE_LOG(LogObjectMixerEditor, Warning, TEXT("%s"), *OutErrorMessage.ToString());
						return false;
					}
			
					AActor* ExistingNameSearchScope = ComponentInstance->GetOwner();
			
					if ((ExistingNameSearchScope == nullptr) && (Blueprint != nullptr))
					{
						ExistingNameSearchScope = Cast<AActor>(Blueprint->GeneratedClass->GetDefaultObject());
					}

					if (!FComponentEditorUtils::IsValidVariableNameString(ComponentInstance, NewTextStr))
					{
						OutErrorMessage = LOCTEXT("RenameFailed_EngineReservedName", "This name is reserved for engine use.");
						UE_LOG(LogObjectMixerEditor, Warning, TEXT("%hs: %s"), __FUNCTION__, *OutErrorMessage.ToString());
						return false;
					}
					else if (!FComponentEditorUtils::IsComponentNameAvailable(NewTextStr, ExistingNameSearchScope, ComponentInstance) 
							|| !FComponentEditorUtils::IsComponentNameAvailable(NewTextStr, ComponentInstance->GetOuter(), ComponentInstance ))
					{
						OutErrorMessage = LOCTEXT("RenameFailed_ExistingName", "Another component already has the same name.");
						UE_LOG(LogObjectMixerEditor, Warning, TEXT("%hs: %s"), __FUNCTION__, *OutErrorMessage.ToString());
						return false;
					}
				}
			}
		
			TSharedPtr<INameValidatorInterface> NameValidator;
			if (Blueprint != nullptr)
			{
				NameValidator = MakeShareable(new FKismetNameValidator(Blueprint, ComponentInstance->GetFName()));
			}
			else
			{
				NameValidator = MakeShareable(new FStringSetNameValidator(ComponentInstance->GetName()));
			}

			if(NameValidator)
			{
				EValidatorResult ValidatorResult = NameValidator->IsValid(NewTextStr);
				if (ValidatorResult == EValidatorResult::AlreadyInUse || ValidatorResult == EValidatorResult::LocallyInUse)
				{
					OutErrorMessage = FText::Format(LOCTEXT("RenameFailed_InUse", "'{0}' is in use by another variable or function!"), InNewText);
					UE_LOG(LogObjectMixerEditor, Warning, TEXT("%hs: %s"), __FUNCTION__, *OutErrorMessage.ToString());
				}
				else if (ValidatorResult == EValidatorResult::Ok)
				{
					return true;
				}
			}
		}

		return false;
	}

	bool IsValidRename(const FText& NewName, const FString& OldName) const
	{
		FText OutErrorMessage;

		if (NewName.IsEmpty())
		{
			OutErrorMessage = LOCTEXT("RenameFailed_LeftBlank", "Names cannot be left blank");
			UE_LOG(LogObjectMixerEditor, Warning, TEXT("%hs: %s"), __FUNCTION__, *OutErrorMessage.ToString());
			return false;
		}

		if (NewName.ToString().Len() >= NAME_SIZE)
		{
			FFormatNamedArguments Arguments;
			Arguments.Add(TEXT("CharCount"), NAME_SIZE);
			OutErrorMessage = FText::Format(LOCTEXT("RenameFailed_TooLong", "Names must be less than {CharCount} characters long."), Arguments);
			UE_LOG(LogObjectMixerEditor, Warning, TEXT("%hs: %s"), __FUNCTION__, *OutErrorMessage.ToString());
			return false;
		}

		const FString LabelString = NewName.ToString();
		if (OldName.Equals(LabelString))
		{
			OutErrorMessage = LOCTEXT("RenameFailed_SameName", "Old and new names are the same.");
			UE_LOG(LogObjectMixerEditor, Warning, TEXT("%hs: %s"), __FUNCTION__, *OutErrorMessage.ToString());
			return false;
		}

		int32 Dummy = 0;
		if (LabelString.FindChar('/', Dummy) || LabelString.FindChar('\\', Dummy))
		{
			OutErrorMessage = LOCTEXT("RenameFailed_InvalidChar", "Names cannot contain / or \\.");
			UE_LOG(LogObjectMixerEditor, Warning, TEXT("%hs: %s"), __FUNCTION__, *OutErrorMessage.ToString());
			return false;
		}

		return true;
	}

	static void RenameFolder(const FObjectMixerEditorListRowPtr RowPtr, const FString& TextAsString)
	{
		const FFolder OldFolder = RowPtr->GetFolder();
		FName NewPath = OldFolder.GetParent().GetPath();
		if (NewPath.IsNone())
		{
			NewPath = FName(*TextAsString);
		}
		else
		{
			NewPath = FName(*(NewPath.ToString() / TextAsString));
		}
			
		const FFolder NewFolder(OldFolder.GetRootObject(), NewPath);
			
		// Transaction is built into the following method
		FActorFolders::Get().RenameFolderInWorld(*GEditor->GetEditorWorldContext().World(), OldFolder, NewFolder);

		if (const TSharedPtr<SObjectMixerEditorList> PinnedListView = RowPtr->GetListViewPtr().Pin())
		{
			PinnedListView->RequestRebuildList();
		}
	}

	void RenameActor(AActor* AsActor, const FText& TrimmedLabel) const
	{
		if (AsActor->IsActorLabelEditable() && IsValidRename(TrimmedLabel, AsActor->GetActorLabel()))
		{
			const FScopedTransaction Transaction(LOCTEXT("ObjectMixerRenameActorTransaction", "Rename Actor"));
			AsActor->Modify();
			FActorLabelUtilities::RenameExistingActor(AsActor, TrimmedLabel.ToString());
		}
	}

	bool RenameComponent(UActorComponent* AsComponent, const FText& TrimmedLabel) const
	{
		if (IsValidComponentRename(AsComponent, TrimmedLabel))
		{
			const FString TextAsString = TrimmedLabel.ToString();
			
			const ERenameFlags RenameFlags = REN_DontCreateRedirectors;
			if(!StaticFindObject(UObject::StaticClass(), AsComponent->GetOuter(), *TextAsString))
			{
				const FScopedTransaction Transaction(LOCTEXT("ObjectMixerRenameComponentTransaction", "Rename Component"));
				AsComponent->Modify();
				AsComponent->Rename(*TextAsString, nullptr, RenameFlags);

				return true;
			}
		}

		return false;
	}

	void OnTextCommitted(const FText& InText, ETextCommit::Type CommitType) const
	{
		const FObjectMixerEditorListRowPtr RowPtr = Item.Pin();
		check(RowPtr);
		
		const FText TrimmedLabel = FText::TrimPrecedingAndTrailing(InText);
		if (TrimmedLabel.IsEmpty())
		{
			return;
		}
		
		const FString TextAsString = TrimmedLabel.ToString();

		if (RowPtr->GetRowType() == FObjectMixerEditorListRow::Folder &&
			IsValidRename(TrimmedLabel, RowPtr->GetFolder().GetLeafName().ToString()))
		{
			RenameFolder(RowPtr, TextAsString);

			return;
		}
		
		UObject* RowObject = RowPtr->GetObject();

		if (!RowObject)
		{
			return;
		}

		if (AActor* AsActor = Cast<AActor>(RowObject))
		{
			RenameActor(AsActor, TrimmedLabel);
			
			if (const TSharedPtr<SObjectMixerEditorList> PinnedListView = RowPtr->GetListViewPtr().Pin())
			{
				PinnedListView->RequestRebuildList();
			}
		}
		else if (UActorComponent* AsComponent = Cast<UActorComponent>(RowObject))
		{
			if (RenameComponent(AsComponent, TrimmedLabel))
			{
				if (const TSharedPtr<SObjectMixerEditorList> PinnedListView = RowPtr->GetListViewPtr().Pin())
				{
					PinnedListView->RequestRebuildList();
				}
			}
		}
	}
	
	void OnClickBlueprintLink(UBlueprint* AsBlueprint, UObject* Object) const
	{
		if (AsBlueprint)
		{
			if (Object)
			{
				if (ensure(Object->GetClass()->ClassGeneratedBy == AsBlueprint))
				{
					AsBlueprint->SetObjectBeingDebugged(Object);
				}
			}
			// Open the blueprint
			GEditor->EditObject(AsBlueprint);
		}
	}

	FText GetHighlightText() const
	{
		const FObjectMixerEditorListRowPtr RowPtr = HybridChild.IsValid() ? HybridChild.Pin() : Item.Pin();

		if (const TSharedPtr<SObjectMixerEditorList> PinnedListView = RowPtr->GetListViewPtr().Pin())
		{
			return PinnedListView->GetSearchTextFromSearchInputField();
		}

		return FText::GetEmpty();
	}

	bool GetIsSelectedExclusively() const
	{
		return Item.Pin()->GetIsSelected() && Item.Pin()->GetSelectedTreeViewItems().Num() == 1;
	}

	TWeakPtr<FObjectMixerEditorListRow> Item;
	TWeakPtr<FObjectMixerEditorListRow> HybridChild;

	TSharedPtr<SInlineEditableTextBlock> EditableTextBlock;
	TSharedPtr<SHyperlinkWithTextHighlight> HyperlinkTextBlock;

	/** The offset applied to text widgets so that the text aligns with the column header text */
	float TextBlockLeftPadding = 3.0f;
};

void SObjectMixerEditorListRow::Construct(
	const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTable,
	const TWeakPtr<FObjectMixerEditorListRow> InRow)
{
	check(InRow.IsValid());

	Item = InRow;
	HybridRowIndex = Item.Pin()->GetOrFindHybridRowIndex();

	SMultiColumnTableRow<FObjectMixerEditorListRowPtr>::Construct(
		FSuperRowType::FArguments()
		.Padding(1.0f)
		.OnCanAcceptDrop(this, &SObjectMixerEditorListRow::HandleCanAcceptDrop)
		.OnAcceptDrop(this, &SObjectMixerEditorListRow::HandleAcceptDrop)
		.OnDragDetected(this, &SObjectMixerEditorListRow::HandleDragDetected)
		.OnDragLeave(this, &SObjectMixerEditorListRow::HandleDragLeave)
	 	, InOwnerTable
	);

	const FName VisibleHoveredBrushName = TEXT("Level.VisibleHighlightIcon16x");
	const FName VisibleNotHoveredBrushName = TEXT("Level.VisibleIcon16x");
	const FName NotVisibleHoveredBrushName = TEXT("Level.NotVisibleHighlightIcon16x");
	const FName NotVisibleNotHoveredBrushName = TEXT("Level.NotVisibleIcon16x");

	VisibleHoveredBrush = FAppStyle::Get().GetBrush(VisibleHoveredBrushName);
	VisibleNotHoveredBrush = FAppStyle::Get().GetBrush(VisibleNotHoveredBrushName);
	NotVisibleHoveredBrush = FAppStyle::Get().GetBrush(NotVisibleHoveredBrushName);
	NotVisibleNotHoveredBrush = FAppStyle::Get().GetBrush(NotVisibleNotHoveredBrushName);
	
	SoloOnBrush = FObjectMixerEditorStyle::Get().GetBrush("ObjectMixer.Solo");
	SoloOffHoveredBrush = FObjectMixerEditorStyle::Get().GetBrush("ObjectMixer.SoloHoverOff");
}

TSharedRef<SWidget> SObjectMixerEditorListRow::GenerateWidgetForColumn(const FName& InColumnName)
{
	check(Item.IsValid());
	const FObjectMixerEditorListRowPtr RowPtr = GetHybridChildOrRowItemIfNull();

	if (const TSharedPtr<SWidget> CellWidget = GenerateCells(InColumnName, RowPtr))
	{
		if (InColumnName == SObjectMixerEditorList::ItemNameColumnName)
		{			
			// The first column gets the tree expansion arrow for this row
			return SNew(SBox)
				.MinDesiredHeight(20)
				[
					SNew( SHorizontalBox )

					+SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(6, 0, 0, 0)
					[
						SNew( SExpanderArrow, SharedThis(this) ).IndentAmount(12)
					]

					+SHorizontalBox::Slot()
					.FillWidth(1.0f)
					[
						CellWidget.ToSharedRef()
					]
				];
		}
		
		return SNew(SBox)
				   .HAlign(HAlign_Fill)
				   .VAlign(VAlign_Center)
			   [
				   CellWidget.ToSharedRef()
			   ];
	}

	return SNullWidget::NullWidget;
}

void SObjectMixerEditorListRow::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	bIsHovered = true;
	
	SMultiColumnTableRow<FObjectMixerEditorListRowPtr>::OnMouseEnter(MyGeometry, MouseEvent);
}

void SObjectMixerEditorListRow::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	bIsHovered = false;

	SMultiColumnTableRow<FObjectMixerEditorListRowPtr>::OnMouseLeave(MouseEvent);
}

SObjectMixerEditorListRow::~SObjectMixerEditorListRow()
{
	Item.Reset();
}

FReply SObjectMixerEditorListRow::HandleDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	TArray<FObjectMixerEditorListRowPtr> DraggedItems = Item.Pin()->GetSelectedTreeViewItems();
	TSharedRef<FObjectMixerListRowDragDropOp> Operation =
		FObjectMixerListRowDragDropOp::New(DraggedItems);

	return FReply::Handled().BeginDragDrop(Operation);
}

void SObjectMixerEditorListRow::HandleDragLeave(const FDragDropEvent& DragDropEvent)
{
	if (TSharedPtr<FObjectMixerListRowDragDropOp> Operation =
		DragDropEvent.GetOperationAs<FObjectMixerListRowDragDropOp>())
	{
		Operation->ResetToDefaultToolTip();
	}
}

TOptional<EItemDropZone> SObjectMixerEditorListRow::HandleCanAcceptDrop(
	const FDragDropEvent& DragDropEvent, EItemDropZone DropZone, FObjectMixerEditorListRowPtr TargetItem)
{
	TSharedPtr<FObjectMixerListRowDragDropOp> Operation =
		DragDropEvent.GetOperationAs<FObjectMixerListRowDragDropOp>();

	if (!Operation.IsValid())
	{
		return TOptional<EItemDropZone>();
	}

	const UObject* DropOnObject = TargetItem->GetObject();
	const bool bIsDroppingOnFolderRow = TargetItem->GetRowType() == FObjectMixerEditorListRow::Folder;

	const bool bIsDropDenied =
		(!DropOnObject && !bIsDroppingOnFolderRow) ||
		(DropOnObject && DropOnObject->IsA(UActorComponent::StaticClass())) ||
		(
			Operation->DraggedItems.Num() == 1 &&
			Operation->DraggedItems[0]->GetObject() &&
			Operation->DraggedItems[0]->GetObject()->IsA(UActorComponent::StaticClass())
		)
	;

	if (bIsDropDenied)
	{
		Operation->SetToolTip(
			LOCTEXT("ObjectMixerDragDropWarning", "Drop an actor row onto another actor row or folder to set attach parent or folder.\nDrop any row onto a collection button to assign a collection to the row."),
			FAppStyle::Get().GetBrush("Graph.ConnectorFeedback.Error")
		);

		return TOptional<EItemDropZone>();
	}

	FText ItemNameText = FText::GetEmpty();

	if (Operation->DraggedItems.Num() == 1)
	{
		ItemNameText = Operation->DraggedItems[0]->GetDisplayName();
	}
	else
	{
		ItemNameText = FText::Format(MultiDragFormatText, FText::AsNumber(Operation->DraggedItems.Num()));
	}

	const FText DropPermittedText =
		FText::Format(DropFormatText,
			bIsDroppingOnFolderRow ? LOCTEXT("DragDropMoveToFolderPrefix", "Move") : LOCTEXT("DragDropSetAttachParentPrefix", "Set"),
			ItemNameText,
			bIsDroppingOnFolderRow ? LOCTEXT("DragDropMoveToFolderMidfix", "into") : LOCTEXT("DragDropSetAttachParentMidfix", "AttachParent as"),
			TargetItem->GetDisplayName()
		);

	Operation->SetToolTip(
		DropPermittedText,
		FAppStyle::Get().GetBrush("Graph.ConnectorFeedback.OK")
	);

	// We have no behaviour yet for dropping one item onto another, so we'll treat it like we dropped it above
	return EItemDropZone::OntoItem;
}

FReply SObjectMixerEditorListRow::HandleAcceptDrop(const FDragDropEvent& DragDropEvent, EItemDropZone DropZone,
                                                        FObjectMixerEditorListRowPtr TargetItem)
{
	TSharedPtr<FObjectMixerListRowDragDropOp> Operation =
		DragDropEvent.GetOperationAs<FObjectMixerListRowDragDropOp>();

	if (!Operation.IsValid())
	{
		return FReply::Unhandled();
	}

	UObject* DropOnObject = TargetItem->GetObject();
	const bool bIsDroppingOnFolderRow = TargetItem->GetRowType() == FObjectMixerEditorListRow::Folder && TargetItem->GetFolderPath() != NAME_None;
	FAttachmentTransformRules Rules(EAttachmentRule::KeepWorld, false);

	FScopedTransaction DragDropTransaction(LOCTEXT("ObjectMixerDragDropTransaction","Object Mixer Drag & Drop"));

	for (const FObjectMixerEditorListRowPtr& DraggedItem : Operation->DraggedItems)
	{
		if (DraggedItem->GetRowType() == FObjectMixerEditorListRow::Folder)
		{
			if (bIsDroppingOnFolderRow)
			{
				if (const TSharedPtr<SObjectMixerEditorList> PinnedList = DraggedItem->GetListViewPtr().Pin())
				{
						PinnedList->OnRequestMoveFolder(DraggedItem->GetFolder(), TargetItem->GetFolder());
				}
			}
		}
		else if (AActor* ObjectAsActor = Cast<AActor>(DraggedItem->GetObject()))
		{
			if (bIsDroppingOnFolderRow)
			{
				ObjectAsActor->Modify();
				ObjectAsActor->SetFolderPath(TargetItem->GetFolderPath());

				if (AActor* AttachParent = ObjectAsActor->GetAttachParentActor())
				{
					if (const FObjectMixerEditorListRowPtr ParentRow = DraggedItem->GetDirectParentRow().Pin(); ParentRow.IsValid())
					{
						if (!Operation->DraggedItems.Contains(ParentRow))
						{
							AttachParent->Modify();
							FDetachmentTransformRules DetachmentRules(EDetachmentRule::KeepWorld, false);
							ObjectAsActor->DetachFromActor(DetachmentRules);
						}
					}
				}
			}
			else if (AActor* DropOnObjectAsActor = Cast<AActor>(DropOnObject))
			{
				ObjectAsActor->Modify();
				ObjectAsActor->AttachToActor(DropOnObjectAsActor, Rules);
			}
		}
	}

	return FReply::Handled();
}

FObjectMixerEditorListRowPtr SObjectMixerEditorListRow::GetHybridChildOrRowItemIfNull() const
{
	if (const TSharedPtr<FObjectMixerEditorListRow> PinnedItem = Item.Pin())
	{			
		return PinnedItem->GetHybridChildOrRowItemIfNull();
	}

	return nullptr;
}

bool SObjectMixerEditorListRow::GetIsItemOrHybridChildSelected() const
{
	if (const TSharedPtr<FObjectMixerEditorListRow> PinnedItem = Item.Pin())
	{			
		return PinnedItem->GetIsItemOrHybridChildSelected();
	}

	return false;
}

bool SObjectMixerEditorListRow::IsVisible() const
{
	if (const TSharedPtr<FObjectMixerEditorListRow> PinnedItem = Item.Pin())
	{
		return PinnedItem->GetCurrentEditorObjectVisibility();
	}

	return false;
}

FSlateColor SObjectMixerEditorListRow::GetVisibilityIconForegroundColor() const
{
	check(Item.IsValid());
	
	const bool bIsSelected = Item.Pin()->GetIsSelected();

	// make the foreground brush transparent if it is not selected and it is visible
	if (IsVisible() && !bIsHovered && !bIsSelected)
	{
		return FLinearColor::Transparent;
	}
	else if (bIsHovered && !bIsSelected)
	{
		return FStyleColors::ForegroundHover;
	}

	return FSlateColor::UseForeground();
}

FSlateColor SObjectMixerEditorListRow::GetSoloIconForegroundColor() const
{
	check(Item.IsValid());
	const FObjectMixerEditorListRowPtr RowPtr = GetHybridChildOrRowItemIfNull();

	const bool bIsSelected = RowPtr->GetIsSelected();

	// make the foreground brush transparent if it is not selected, hovered or solo
	if (!RowPtr->GetRowSoloState() && !bIsHovered && !bIsSelected)
	{
		return FLinearColor::Transparent;
	}
	else if (bIsHovered && !bIsSelected)
	{
		return FStyleColors::ForegroundHover;
	}

	return FSlateColor::UseForeground();
}

void SObjectMixerEditorListRow::OnClickSoloIcon(const FObjectMixerEditorListRowPtr& RowPtr)
{
	check (RowPtr.IsValid());
							
	if (const TSharedPtr<SObjectMixerEditorList> PinnedListView = RowPtr->GetListViewPtr().Pin();
		PinnedListView->GetTreeViewItemCount() > 0)
	{
		const bool bNewSolo = !RowPtr->GetRowSoloState();
								
		using LambdaType = void(*)(const FObjectMixerEditorListRowPtr&, const bool);
		
		static LambdaType SetSoloPerRowRecursively =
			[](const FObjectMixerEditorListRowPtr& RowPtr, const bool bNewSolo)
		{
			if (bNewSolo)
			{
				RowPtr->SetUserHiddenInEditor(false);
			}
			
			RowPtr->SetRowSoloState(bNewSolo);
								
			for (const FObjectMixerEditorListRowPtr& SelectedItem : RowPtr->GetChildRows())
			{
				SetSoloPerRowRecursively(SelectedItem, bNewSolo);
			}
		};
								
		if (PinnedListView->GetSelectedTreeViewItemCount() > 0 && RowPtr->GetIsSelected())
		{
			for (const FObjectMixerEditorListRowPtr& SelectedItem : PinnedListView->GetSelectedTreeViewItems())
			{
				SetSoloPerRowRecursively(SelectedItem, bNewSolo);
			}
		}
		else
		{
			SetSoloPerRowRecursively(RowPtr, bNewSolo);
		}
								
		PinnedListView->EvaluateAndSetEditorVisibilityPerRow();
	}
}

const FSlateBrush* SObjectMixerEditorListRow::GetVisibilityBrush() const
{
	if (IsVisible())
	{
		return bIsHovered ? VisibleHoveredBrush : VisibleNotHoveredBrush;
	}
	
	return bIsHovered ? NotVisibleHoveredBrush : NotVisibleNotHoveredBrush;
}

const FSlateBrush* SObjectMixerEditorListRow::GetSoloBrush() const
{
	check(Item.IsValid());
	const FObjectMixerEditorListRowPtr RowPtr = GetHybridChildOrRowItemIfNull();

	if (RowPtr->GetRowType() == FObjectMixerEditorListRow::Folder)
	{
		if (RowPtr->HasAtLeastOneChildThatIsNotSolo())
		{
			return SoloOffHoveredBrush;
		}

		return SoloOnBrush;
	}
	
	if (RowPtr->GetRowSoloState())
	{
		return SoloOnBrush;
	}

	return SoloOffHoveredBrush;
}

void SObjectMixerEditorListRow::OnClickVisibilityIcon(const FObjectMixerEditorListRowPtr& RowPtr)
{
	check (RowPtr.IsValid());
							
	if (const TSharedPtr<SObjectMixerEditorList> PinnedListView = RowPtr->GetListViewPtr().Pin();
		PinnedListView->GetTreeViewItemCount() > 0)
	{
		const bool bNewHidden = !RowPtr->IsUserSetHiddenInEditor();
		const bool bIsListInSoloState = PinnedListView->IsListInSoloState();
								
		using LambdaType = void(*)(const FObjectMixerEditorListRowPtr&, const bool, const bool);
		
		static LambdaType SetVisibilityPerRowRecursively = [](
			const FObjectMixerEditorListRowPtr& RowPtr, const bool bNewHidden, const bool bIsListInSoloState)
		{
			if (bIsListInSoloState)
			{
				RowPtr->SetRowSoloState(!RowPtr->GetRowSoloState());
			}
			else
			{
				RowPtr->SetUserHiddenInEditor(bNewHidden);
			}
								
			for (const FObjectMixerEditorListRowPtr& SelectedItem : RowPtr->GetChildRows())
			{
				SetVisibilityPerRowRecursively(SelectedItem, bNewHidden, bIsListInSoloState);
			}
		};
								
		if (PinnedListView->GetSelectedTreeViewItemCount() > 0 && RowPtr->GetIsSelected())
		{
			for (const FObjectMixerEditorListRowPtr& SelectedItem : PinnedListView->GetSelectedTreeViewItems())
			{
				SetVisibilityPerRowRecursively(SelectedItem, bNewHidden, bIsListInSoloState);
			}
		}
		else
		{
			SetVisibilityPerRowRecursively(RowPtr, bNewHidden, bIsListInSoloState);
		}
								
		PinnedListView->EvaluateAndSetEditorVisibilityPerRow();
	}
}

TSharedPtr<SWidget> SObjectMixerEditorListRow::GenerateCells(
	const FName& InColumnName, const TSharedPtr<FObjectMixerEditorListRow> RowPtr)
{
	check(RowPtr.IsValid());
	
	if (RowPtr->GetRowType() == FObjectMixerEditorListRow::None)
	{
		return SNullWidget::NullWidget;
	}
	
	const bool bIsHybridRow = HybridRowIndex != INDEX_NONE;
	
	if (InColumnName.IsEqual(SObjectMixerEditorList::ItemNameColumnName))
	{
		// Pass in actual row item even if hybrid row
		return SNew(SInlineEditableRowNameCellWidget,
			Item.Pin().ToSharedRef(), HybridRowIndex != INDEX_NONE ? GetHybridChildOrRowItemIfNull() : nullptr);
	}

	if (InColumnName.IsEqual(SObjectMixerEditorList::EditorVisibilityColumnName))
	{
		if (!bIsHybridRow && RowPtr->GetObject() && !RowPtr->GetObject()->IsA(AActor::StaticClass()))
		{
			return nullptr;
		}
		
		return SNew(SBox)
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				.Visibility(EVisibility::SelfHitTestInvisible)
				.Padding(FMargin(2,0,0,0))
				[
					SNew(SImage)
					.ColorAndOpacity(this, &SObjectMixerEditorListRow::GetVisibilityIconForegroundColor)
					.Image_Raw(this, &SObjectMixerEditorListRow::GetVisibilityBrush)
					.OnMouseButtonDown_Lambda(
						[this] (const FGeometry&, const FPointerEvent&)
						{
							// Pass in actual row item even if hybrid row
							OnClickVisibilityIcon(Item.Pin());

							return FReply::Handled();
						}
					)
				]
			;
	}

	if (InColumnName.IsEqual(SObjectMixerEditorList::EditorVisibilitySoloColumnName))
	{
		if (!bIsHybridRow && RowPtr->GetObject() && !RowPtr->GetObject()->IsA(AActor::StaticClass()))
		{
			return nullptr;
		}
		
		return SNew(SBox)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.Visibility(EVisibility::SelfHitTestInvisible)
				.Padding(0.f)
				[
					SNew(SImage)
					.ColorAndOpacity(this, &SObjectMixerEditorListRow::GetSoloIconForegroundColor)
					.Image(this, &SObjectMixerEditorListRow::GetSoloBrush)
					.OnMouseButtonDown_Lambda(
						[this] (const FGeometry&, const FPointerEvent&)
						{
							// Pass in actual row item even if hybrid row
							OnClickSoloIcon(Item.Pin());

							return FReply::Handled();
						}
					)
				]
			;
	}
	
	if (UObject* ObjectRef = RowPtr->GetObject())
	{
		FPropertyEditorModule& PropertyEditorModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
		{
			FSinglePropertyParams Params;
			Params.NamePlacement = EPropertyNamePlacement::Hidden;
			
			const TSharedPtr<ISinglePropertyView> SinglePropertyView =
				PropertyEditorModule.CreateSingleProperty(ObjectRef, InColumnName, Params
			);

			if (SinglePropertyView)
			{
				if (const TSharedPtr<IPropertyHandle> Handle = SinglePropertyView->GetPropertyHandle())
				{
					if (const FProperty* Property = Handle->GetProperty())
					{
						const FName PropertyName = Property->GetFName();
						RowPtr->PropertyNamesToHandles.Add(PropertyName, Handle);
						
						// Simultaneously edit all selected rows with a similar property
						const TDelegate<void(const FPropertyChangedEvent&)> OnPropertyValueChanged =
							TDelegate<void(const FPropertyChangedEvent&)>::CreateRaw(
								this,
								&SObjectMixerEditorListRow::OnPropertyChanged, PropertyName);
					
						Handle->SetOnPropertyValueChangedWithData(OnPropertyValueChanged);
						Handle->SetOnChildPropertyValueChangedWithData(OnPropertyValueChanged);

						return SNew(SBox)
								.Visibility(EVisibility::SelfHitTestInvisible)
								.HAlign(HAlign_Fill)
								.VAlign(VAlign_Center)
								[
									SinglePropertyView.ToSharedRef()
								];
					}
				}
			}
		}
	}

	return nullptr;
}

void SObjectMixerEditorListRow::OnPropertyChanged(const FPropertyChangedEvent& Event, const FName PropertyName) const
{
	if (const TSharedPtr<FObjectMixerEditorListRow> PinnedItem = Item.Pin())
	{
		const EPropertyValueSetFlags::Type Flag =
			 Event.ChangeType == EPropertyChangeType::Interactive ?
			 	EPropertyValueSetFlags::InteractiveChange : EPropertyValueSetFlags::DefaultFlags;

		const FObjectMixerEditorListRow::FPropertyPropagationInfo PropagationInfo(
			{PinnedItem->GetUniqueIdentifier(), PropertyName, Flag});
		
		if (Flag == EPropertyValueSetFlags::InteractiveChange)
		{
			PinnedItem->PropagateChangesToSimilarSelectedRowProperties(PropagationInfo);
		}
		else
		{
			// If not an interactive change, schedule property propagation on next frame
			if (const TSharedPtr<SObjectMixerEditorList> PinnedListView = PinnedItem->GetListViewPtr().Pin())
			{
				PinnedListView->AddToPendingPropertyPropagations(PropagationInfo);
				PinnedListView->RequestRebuildList();
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
