// Copyright Epic Games, Inc. All Rights Reserved.

#include "ViewModels/HierarchyEditor/NiagaraHierarchyViewModelBase.h"
#include "NiagaraEditorUtilities.h"
#include "NiagaraEmitterEditorData.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraScriptVariable.h"
#include "NiagaraSystemEditorData.h"
#include "SDropTarget.h"
#include "ViewModels/NiagaraSystemViewModel.h"
#include "ViewModels/Stack/NiagaraStackViewModel.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/SNiagaraHierarchy.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/SToolTip.h"
#include "Widgets/Input/SCheckBox.h"
#include "Editor.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "NiagaraHierarchyEditor"

const TArray<UNiagaraHierarchyItemBase*>& UNiagaraHierarchyItemBase::GetChildren() const
{
	if(CanHaveChildren())
	{
		return Children;
	}

	static TArray<UNiagaraHierarchyItemBase*> EmptyChildren;
	return EmptyChildren;
}

void UNiagaraHierarchyItemBase::Refresh()
{
	TArray<UNiagaraHierarchyItemBase*> OldChildren = Children;

	RefreshDataInternal();

	Children.Empty();
	for(UNiagaraHierarchyItemBase* Item : OldChildren)
	{
		if(Item->IsFinalized())
		{
			continue;
		}

		Item->Refresh();
		Children.Add(Item);
	}
}

void UNiagaraHierarchyItemBase::Finalize()
{	
	for(UNiagaraHierarchyItemBase* Child : Children)
	{
		Child->Finalize();
	}
	
	bFinalized = true;
}

bool UNiagaraHierarchyItemBase::Modify(bool bAlwaysMarkDirty)
{
	bool bSavedToTransactionBuffer = true;

	for(UNiagaraHierarchyItemBase* Child : Children)
	{
		bSavedToTransactionBuffer &= Child->Modify(bAlwaysMarkDirty);
	}
	
	bSavedToTransactionBuffer &= UObject::Modify(bAlwaysMarkDirty);

	return bSavedToTransactionBuffer;
}

void UNiagaraHierarchyRoot::RefreshDataInternal()
{
	// in addition, we update our sections
	TArray<UNiagaraHierarchySection*> OldSections = Sections;

	Sections.Empty();
	for(UNiagaraHierarchySection* Section : OldSections)
	{
		// if a section was marked finalized, we don't add it back
		if(Section->IsFinalized())
		{
			continue;
		}

		Sections.Add(Section);
	}
}

UNiagaraHierarchySection* UNiagaraHierarchyRoot::AddSection(FText InNewSectionName)
{
	TSet<FName> ExistingSectionNames;
	
	for(FName& SectionName : GetSections())
	{
		ExistingSectionNames.Add(SectionName);
	}
	
	FName NewName = FNiagaraUtilities::GetUniqueName(FName(InNewSectionName.ToString()), ExistingSectionNames);
	UNiagaraHierarchySection* NewSectionItem = NewObject<UNiagaraHierarchySection>(this);
	NewSectionItem->SetSectionName(NewName);
	NewSectionItem->SetFlags(RF_Transactional);
	Sections.Add(NewSectionItem);
	return NewSectionItem;
}

void UNiagaraHierarchyRoot::RemoveSection(FText SectionName)
{
	if(ensure(Sections.ContainsByPredicate([SectionName](UNiagaraHierarchySection* Section)
	{
		return Section->GetSectionNameAsText().EqualTo(SectionName);
	})))
	{
		Sections.RemoveAll([SectionName](UNiagaraHierarchySection* Section)
		{
			return Section->GetSectionNameAsText().EqualTo(SectionName);
		});
	}
}

TSet<FName> UNiagaraHierarchyRoot::GetSections() const
{
	TSet<FName> OutSections;
	for(UNiagaraHierarchySection* Section : Sections)
	{
		OutSections.Add(Section->GetSectionName());
	}

	return OutSections;
}

int32 UNiagaraHierarchyRoot::GetSectionIndex(UNiagaraHierarchySection* Section) const
{
	return Sections.Find(Section);
}

bool UNiagaraHierarchyRoot::Modify(bool bAlwaysMarkDirty)
{
	bool bSavedToTransactionBuffer = true;
	
	for(UNiagaraHierarchySection* Section : Sections)
	{
		bSavedToTransactionBuffer &= Section->Modify();
	}
	
	bSavedToTransactionBuffer &= Super::Modify(bAlwaysMarkDirty);	

	return bSavedToTransactionBuffer;
}

bool FNiagaraHierarchyCategoryViewModel::IsTopCategoryActive() const
{
	if(UNiagaraHierarchyCategory* Category = Cast<UNiagaraHierarchyCategory>(GetDataMutable()))
	{
		const UNiagaraHierarchyCategory* Result = Category;
		const UNiagaraHierarchyCategory* TopLevelCategory = Result;
		
		for (; TopLevelCategory != nullptr; TopLevelCategory = TopLevelCategory->GetTypedOuter<UNiagaraHierarchyCategory>() )
		{
			if(TopLevelCategory != nullptr)
			{
				Result = TopLevelCategory;
			}
		}
		
		return HierarchyViewModel->IsSectionActive(Result->GetSection());
	}

	return false;	
}

void UNiagaraHierarchySection::SetSectionNameAsText(const FText& Text)
{
	Section = FName(Text.ToString());
}

UNiagaraHierarchyViewModelBase::UNiagaraHierarchyViewModelBase()
{
	SourceRoot = NewObject<UNiagaraHierarchyRoot>(this, FName("HierarchySourceRoot"));
	SourceViewModelRoot = MakeShared<FNiagaraHierarchyRootViewModel>(SourceRoot.Get(), this);
	Commands = MakeShared<FUICommandList>();
}

UNiagaraHierarchyViewModelBase::~UNiagaraHierarchyViewModelBase()
{
	RefreshSourceViewDelegate.Unbind();
	RefreshHierarchyWidgetDelegate.Unbind();
	RefreshSectionsWidgetDelegate.Unbind();
	IsItemSelectedDelegate.Unbind();
	SelectObjectInDetailsPanelDelegate.Unbind();
}

void UNiagaraHierarchyViewModelBase::ForceFullRefresh()
{
	PrepareSourceItems();
	HierarchyViewModelRoot->SyncToData();
	RefreshSourceView(true);
	RefreshHierarchyView(true);
	RefreshSectionsWidget();
}

TSharedRef<SWidget> UNiagaraHierarchyViewModelBase::GenerateRowContentWidget(TSharedRef<FNiagaraHierarchyItemViewModelBase> ItemBaseViewModel) const
{
	if(UNiagaraHierarchyCategory* Category = Cast<UNiagaraHierarchyCategory>(ItemBaseViewModel->GetDataMutable()))
	{
		TSharedRef<FNiagaraHierarchyCategoryViewModel> TreeViewCategory = StaticCastSharedRef<FNiagaraHierarchyCategoryViewModel>(ItemBaseViewModel);
		return SNew(SNiagaraHierarchyCategory, TreeViewCategory)
		.IsSelected(FIsSelected::CreateLambda([this, ItemBaseViewModel]
		{
			return this->IsItemSelected(ItemBaseViewModel);
		}));
	}

	return SNullWidget::NullWidget;
}

void UNiagaraHierarchyViewModelBase::RefreshSourceView(bool bFullRefresh) const
{
	if(RefreshSourceViewDelegate.IsBound())
	{
		RefreshSourceViewDelegate.Execute(bFullRefresh);
	}
}

void UNiagaraHierarchyViewModelBase::RefreshHierarchyView(bool bFullRefresh) const
{
	if(RefreshHierarchyWidgetDelegate.IsBound())
	{
		RefreshHierarchyWidgetDelegate.Execute(bFullRefresh);
	}
}

void UNiagaraHierarchyViewModelBase::RefreshSectionsWidget() const
{
	if(RefreshSectionsWidgetDelegate.IsBound())
	{
		RefreshSectionsWidgetDelegate.Execute();
	}
}

void UNiagaraHierarchyViewModelBase::SelectItemInDetailsPanel(UObject* Object) const
{
	if(SelectObjectInDetailsPanelDelegate.IsBound())
	{
		SelectObjectInDetailsPanelDelegate.Execute(Object);
	}
}

bool UNiagaraHierarchyViewModelBase::IsItemSelected(TSharedPtr<FNiagaraHierarchyItemViewModelBase> Item) const
{
	if(IsItemSelectedDelegate.IsBound())
	{
		return IsItemSelectedDelegate.Execute(Item);
	}

	return false;
}

void UNiagaraHierarchyViewModelBase::PostUndo(bool bSuccess)
{
	ForceFullRefresh();
}

void UNiagaraHierarchyViewModelBase::PostRedo(bool bSuccess)
{
	PostUndo(bSuccess);
}

void UNiagaraHierarchyViewModelBase::Initialize()
{
	if(GEditor)
	{
		GEditor->RegisterForUndo(this);
	}
	
	HierarchyRoot = GetHierarchyDataRoot();
	HierarchyRoot->SetFlags(RF_Transactional);

	TArray<UNiagaraHierarchyItemBase*> AllItems;
	HierarchyRoot->GetChildrenOfType<UNiagaraHierarchyItemBase>(AllItems, true);
	for(UNiagaraHierarchyItemBase* Item : AllItems)
	{
		Item->SetFlags(RF_Transactional);
	}

	for(UNiagaraHierarchySection* Section : HierarchyRoot->GetSectionDataMutable())
	{
		Section->SetFlags(RF_Transactional);
	}

	SetupCommands();

	HierarchyViewModelRoot = MakeShared<FNiagaraHierarchyRootViewModel>(HierarchyRoot.Get(), this);
	HierarchyViewModelRoot->SyncToData();

	SetActiveSection(nullptr);

	PrepareSourceItems();

	InitializeInternal();
}

void UNiagaraHierarchyViewModelBase::Finalize()
{
	if(GEditor)
	{
		GEditor->UnregisterForUndo(this);
	}
	
	SourceViewModelRoot->Finalize();
	SourceViewModelRoot.Reset();
	HierarchyViewModelRoot.Reset();
	SourceRoot = nullptr;
	HierarchyRoot = nullptr;
	
	FinalizeInternal();
}

const TArray<TSharedPtr<FNiagaraHierarchyItemViewModelBase>>& UNiagaraHierarchyViewModelBase::GetSourceItems() const
{
	return SourceViewModelRoot->GetChildren();
}

const TArray<TSharedPtr<FNiagaraHierarchyItemViewModelBase>>& UNiagaraHierarchyViewModelBase::GetHierarchyItems() const
{
	return HierarchyViewModelRoot->GetFilteredChildren();
}

void UNiagaraHierarchyViewModelBase::OnGetChildren(TSharedPtr<FNiagaraHierarchyItemViewModelBase> Item, TArray<TSharedPtr<FNiagaraHierarchyItemViewModelBase>>& OutChildren) const
{
	OutChildren.Append(Item->GetFilteredChildren());
}

void UNiagaraHierarchyViewModelBase::SetActiveSection(TSharedPtr<FNiagaraHierarchySectionViewModel> Section)
{
	ActiveSection = Section;
	if(Section.IsValid())
	{
		SelectItemInDetailsPanel(Section->GetDataMutable());
	}
	else
	{
		SelectItemInDetailsPanel(nullptr);
	}
	
	RefreshHierarchyView(true);
}

TSharedPtr<FNiagaraHierarchySectionViewModel> UNiagaraHierarchyViewModelBase::GetActiveSection() const
{
	return ActiveSection.IsValid() ? ActiveSection.Pin() : nullptr;
}

UNiagaraHierarchySection* UNiagaraHierarchyViewModelBase::GetActiveSectionData() const
{
	return ActiveSection.IsValid() ? Cast<UNiagaraHierarchySection>(ActiveSection.Pin()->GetDataMutable()) : nullptr;
}

bool UNiagaraHierarchyViewModelBase::IsSectionActive(const UNiagaraHierarchySection* Section) const
{
	return ActiveSection == nullptr || ActiveSection.Pin()->GetData() == Section;
}

FString UNiagaraHierarchyViewModelBase::OnItemToStringDebug(TSharedPtr<FNiagaraHierarchyItemViewModelBase> ItemBaseViewModel) const
{
	return ItemBaseViewModel->ToString();	
}

void FNiagaraHierarchyItemViewModelBase::SyncToData()
{
	// this will refresh all underlying data recursively
	ItemBase->Refresh();

	// now that the data is refreshed, we can sync to the data by recycling view models & creating new ones
	TArray<TSharedPtr<FNiagaraHierarchyItemViewModelBase>> NewChildren;
	for(UNiagaraHierarchyItemBase* Child : ItemBase->GetChildren())
	{		
		int32 ViewModelIndex = FindIndexOfChild(Child);
		// if we couldn't find a view model for a data child, we create it here
		if(ViewModelIndex == INDEX_NONE)
		{
			if(UNiagaraHierarchyItem* Item = Cast<UNiagaraHierarchyItem>(Child))
			{
				TSharedPtr<FNiagaraHierarchyItemViewModel> NewItem = MakeShared<FNiagaraHierarchyItemViewModel>(Item, AsShared(), HierarchyViewModel);
				NewItem->SyncToData();
				NewChildren.Add(NewItem);
			}
			else if(UNiagaraHierarchyCategory* Category = Cast<UNiagaraHierarchyCategory>(Child))
			{
				TSharedPtr<FNiagaraHierarchyCategoryViewModel> NewCategory = MakeShared<FNiagaraHierarchyCategoryViewModel>(Category, AsShared(), HierarchyViewModel);
				NewCategory->SyncToData();
				NewChildren.Add(NewCategory);
			}
		}
		// if we could find it, we refresh its contained view models and readd it
		else
		{
			Children[ViewModelIndex]->SyncToData();
			NewChildren.Add(Children[ViewModelIndex]);
		}
	}

	/** Give the view models a chance to further customize the children sync process. */
	SyncChildrenViewModelsInternal();
	
	Children.Empty();
	Children.Append(NewChildren);

	// first we sort the data. Categories before items.
	GetDataMutable()->GetChildrenMutable().StableSort([](const UNiagaraHierarchyItemBase& ItemA, const UNiagaraHierarchyItemBase& ItemB)
	{
		return ItemA.IsA<UNiagaraHierarchyCategory>() && ItemB.IsA<UNiagaraHierarchyItem>();
	});

	// then we sort the view models according to the data order as this is what will determine widget order created from the view models
	Children.Sort([this](const TSharedPtr<FNiagaraHierarchyItemViewModelBase>& ItemA, const TSharedPtr<FNiagaraHierarchyItemViewModelBase>& ItemB)
		{
			return FindIndexOfDataChild(ItemA) < FindIndexOfDataChild(ItemB);
		});
	
	// we refresh the filtered children here as well
	GetFilteredChildren();

	OnSyncedDelegate.Broadcast();
}

const TArray<TSharedPtr<FNiagaraHierarchyItemViewModelBase>>& FNiagaraHierarchyItemViewModelBase::GetFilteredChildren() const
{
	FilteredChildren.Empty();

	for(TSharedPtr<FNiagaraHierarchyItemViewModelBase> Child : Children)
	{
		if(const UNiagaraHierarchyCategory* Category = Cast<UNiagaraHierarchyCategory>(Child->GetData()))
		{
			if(StaticCastSharedPtr<FNiagaraHierarchyCategoryViewModel>(Child)->IsTopCategoryActive()) //HierarchyViewModel->IsSectionActive(Category->GetSection()))
			{
				FilteredChildren.Add(Child);
			}
		}
		else
		{
			FilteredChildren.Add(Child);
		}
	}

	return FilteredChildren;
}

bool FNiagaraHierarchyItemViewModelBase::HasParent(TSharedPtr<FNiagaraHierarchyItemViewModelBase> ParentCandidate, bool bRecursive)
{
	if(Parent.IsValid())
	{
		if(Parent == ParentCandidate)
		{
			return true;
		}
		else if(bRecursive)
		{
			Parent.Pin()->HasParent(ParentCandidate, bRecursive);
		}
	}

	return false;
}

void FNiagaraHierarchyItemViewModelBase::AddChild(TSharedPtr<FNiagaraHierarchyItemViewModelBase> Item)
{
	if(Item->Parent.IsValid())
	{
		ensure(Item->Parent.Pin() == AsShared());
	}
	
	Children.Add(Item);
}

UNiagaraHierarchyItemBase* FNiagaraHierarchyItemViewModelBase::AddNewItem(TSubclassOf<UNiagaraHierarchyItemBase> NewItemClass)
{
	FText TransactionText = FText::FormatOrdered(LOCTEXT("Transaction_AddedItem", "Added new {0} to hierarchy"), FText::FromString(NewItemClass->GetName()));
	FScopedTransaction Transaction(TransactionText);
	HierarchyViewModel->GetHierarchyDataRoot()->Modify();
	
	UNiagaraHierarchyItemBase* NewItem = NewObject<UNiagaraHierarchyItemBase>(GetDataMutable(), NewItemClass, NAME_None, RF_Transactional);
	NewItem->Modify();
	GetDataMutable()->GetChildrenMutable().Add(NewItem);
	SyncToData();

	return NewItem;
}

void FNiagaraHierarchyItemViewModelBase::DuplicateToThis(TSharedPtr<FNiagaraHierarchyItemViewModelBase> ItemToDuplicate, int32 InsertIndex)
{
	if(InsertIndex == INDEX_NONE)
	{
		GetDataMutable()->GetChildrenMutable().Add(Cast<UNiagaraHierarchyItemBase>(StaticDuplicateObject(ItemToDuplicate->GetData(), GetDataMutable())));
	}
	else
	{
		GetDataMutable()->GetChildrenMutable().Insert(Cast<UNiagaraHierarchyItemBase>(StaticDuplicateObject(ItemToDuplicate->GetData(), GetDataMutable())), InsertIndex);
	}
	
	SyncToData();
	HierarchyViewModel->OnHierarchyChanged().Broadcast();
}

void FNiagaraHierarchyItemViewModelBase::ReparentToThis(TSharedPtr<FNiagaraHierarchyItemViewModelBase> ItemToMove, int32 InsertIndex)
{
	if(InsertIndex == INDEX_NONE)
	{
		GetDataMutable()->GetChildrenMutable().Add(Cast<UNiagaraHierarchyItemBase>(StaticDuplicateObject(ItemToMove->GetDataMutable(), GetDataMutable())));
	}
	else
	{
		GetDataMutable()->GetChildrenMutable().Insert(Cast<UNiagaraHierarchyItemBase>(StaticDuplicateObject(ItemToMove->GetData(), GetDataMutable())), InsertIndex);
	}
	
	ItemToMove->Finalize();
	ItemToMove->Parent.Pin()->SyncToData();
	SyncToData();
	HierarchyViewModel->OnHierarchyChanged().Broadcast();
}

int32 FNiagaraHierarchyItemViewModelBase::FindIndexOfChild(TSharedPtr<FNiagaraHierarchyItemViewModelBase> Child) const
{
	return Children.Find(Child);
}

int32 FNiagaraHierarchyItemViewModelBase::FindIndexOfChild(UNiagaraHierarchyItemBase* Child) const
{
	return Children.FindLastByPredicate([Child](TSharedPtr<FNiagaraHierarchyItemViewModelBase> Item)
	{
		return Item->GetData() == Child;
	});
}

int32 FNiagaraHierarchyItemViewModelBase::FindIndexOfDataChild(TSharedPtr<FNiagaraHierarchyItemViewModelBase> Child) const
{
	return GetData()->GetChildren().Find(Child->GetDataMutable());
}

int32 FNiagaraHierarchyItemViewModelBase::FindIndexOfDataChild(UNiagaraHierarchyItemBase* Child) const
{
	return GetData()->GetChildren().Find(Child);
}

void FNiagaraHierarchyItemViewModelBase::Finalize()
{	
	for(TSharedPtr<FNiagaraHierarchyItemViewModelBase> ChildViewModel : Children)
	{
		ChildViewModel->Finalize();
	}	

	GetDataMutable()->Finalize();
	FinalizeInternal();
	SyncToData();
}

TOptional<EItemDropZone> FNiagaraHierarchyItemViewModelBase::OnCanAcceptDrop(const FDragDropEvent& DragDropEvent, EItemDropZone ItemDropZone, TSharedPtr<FNiagaraHierarchyItemViewModelBase> Item)
{
	if(TSharedPtr<FNiagaraHierarchyDragDropOp> HierarchyDragDropOp = DragDropEvent.GetOperationAs<FNiagaraHierarchyDragDropOp>())
	{
		return HierarchyViewModel->CanDropOn(HierarchyDragDropOp->GetDraggedItem().Pin().ToSharedRef(), AsShared(), ItemDropZone);
	}

	return TOptional<EItemDropZone>();	
}

TSharedRef<FNiagaraHierarchyDragDropOp> FNiagaraHierarchyItemViewModelBase::CreateDragDropOp()
{
	check(CanDrag() == true);
		
	return HierarchyViewModel->CreateDragDropOp(AsShared());
}

FReply FNiagaraHierarchyItemViewModelBase::OnDragDetected(const FGeometry& Geometry, const FPointerEvent& PointerEvent,	bool bIsSource)
{
	if(CanDrag())
	{
		// if the drag is coming from source, we check if any of the hierarchy data already contains that item and we don't start a drag drop in that case
		if(bIsSource)
		{
			TArray<UNiagaraHierarchyItemBase*> AllItems;
			GetHierarchyViewModel()->GetHierarchyDataRoot()->GetChildrenOfType<UNiagaraHierarchyItemBase>(AllItems, true);

			for(UNiagaraHierarchyItemBase* Item : AllItems)
			{
				if(GetData()->GetPersistentIdentity() == Item->GetPersistentIdentity())
				{
					return FReply::Unhandled();
				}
			}
		}
		
		TSharedRef<FNiagaraHierarchyDragDropOp> HierarchyDragDropOp = CreateDragDropOp();
		HierarchyDragDropOp->SetFromSourceList(bIsSource);

		return FReply::Handled().BeginDragDrop(HierarchyDragDropOp);			
	}
		
	return FReply::Unhandled();
}

TSharedPtr<FNiagaraHierarchySectionViewModel> FNiagaraHierarchyRootViewModel::AddNewSection()
{
	FScopedTransaction ScopedTransaction(LOCTEXT("NewSectionAdded","Added Section"));
	HierarchyViewModel->GetHierarchyDataRoot()->Modify();
	
	UNiagaraHierarchySection* SectionData = Cast<UNiagaraHierarchyRoot>(ItemBase)->AddSection(LOCTEXT("NiagaraHierarchyEditorDefaultNewSectionName", "New Section"));
	SectionData->Modify();
	TSharedPtr<FNiagaraHierarchySectionViewModel> NewSectionViewModel = MakeShared<FNiagaraHierarchySectionViewModel>(SectionData, StaticCastSharedRef<FNiagaraHierarchyRootViewModel>(AsShared()), HierarchyViewModel);
	SectionViewModels.Add(NewSectionViewModel);
	SyncToData();
	HierarchyViewModel->SetActiveSection(NewSectionViewModel);
	NewSectionViewModel->RequestRename();
	

	return NewSectionViewModel;
}

void FNiagaraHierarchyRootViewModel::SyncChildrenViewModelsInternal()
{
	const UNiagaraHierarchyRoot* RootData = Cast<UNiagaraHierarchyRoot>(GetData());

	TArray<TSharedPtr<FNiagaraHierarchySectionViewModel>> SectionsToRemove;
	for(TSharedPtr<FNiagaraHierarchySectionViewModel> SectionViewModel : SectionViewModels)
	{
		if(SectionViewModel->GetDataMutable()->IsFinalized() || !RootData->GetSectionData().Contains(SectionViewModel->GetData()))
		{
			SectionsToRemove.Add(SectionViewModel);
		}
	}

	for (TSharedPtr<FNiagaraHierarchySectionViewModel> SectionViewModel : SectionsToRemove)
	{
		SectionViewModel->Finalize();
		SectionViewModels.Remove(SectionViewModel);
	}
	
	for(UNiagaraHierarchySection* Section : RootData->GetSectionData())
	{
		if(Section->IsFinalized())
		{
			continue;
		}
		
		if(!SectionViewModels.ContainsByPredicate([Section](TSharedPtr<FNiagaraHierarchySectionViewModel> SectionViewModel)
		{
			return SectionViewModel->GetData() == Section;
		}))
		{
			TSharedPtr<FNiagaraHierarchySectionViewModel> NewSectionViewModel = MakeShared<FNiagaraHierarchySectionViewModel>(Section, StaticCastSharedRef<FNiagaraHierarchyRootViewModel>(AsShared()), HierarchyViewModel); 
			NewSectionViewModel->SyncToData();
			SectionViewModels.Add(NewSectionViewModel);
		}
	}

	SectionViewModels.Sort([this](const TSharedPtr<FNiagaraHierarchySectionViewModel>& ItemA, const TSharedPtr<FNiagaraHierarchySectionViewModel>& ItemB)
		{
			return Cast<UNiagaraHierarchyRoot>(GetDataMutable())->GetSectionData().Find(Cast<UNiagaraHierarchySection>(ItemA->GetDataMutable()))
						<
					Cast<UNiagaraHierarchyRoot>(GetDataMutable())->GetSectionData().Find(Cast<UNiagaraHierarchySection>(ItemB->GetDataMutable())); 
		});
}

FReply FNiagaraHierarchySectionViewModel::OnDroppedOn(const FDragDropEvent& DragDropEvent, EItemDropZone ItemDropZone, TSharedPtr<FNiagaraHierarchyItemViewModelBase> Item)
{
	if(DragDropEvent.GetOperation()->IsOfType<FNiagaraSectionDragDropOp>())
	{
		TSharedPtr<FNiagaraSectionDragDropOp> SectionDragDropOp = DragDropEvent.GetOperationAs<FNiagaraSectionDragDropOp>();
		TWeakPtr<FNiagaraHierarchySectionViewModel> DraggedItem = SectionDragDropOp->GetDraggedSection();
		UNiagaraHierarchySection* DraggedSectionData = Cast<UNiagaraHierarchySection>(DraggedItem.Pin()->GetDataMutable());

		int32 IndexOfThis = HierarchyViewModel->GetHierarchyDataRoot()->GetSectionData().Find(Cast<UNiagaraHierarchySection>(GetDataMutable()));
		int32 DraggedSectionIndex = HierarchyViewModel->GetHierarchyDataRoot()->GetSectionData().Find(Cast<UNiagaraHierarchySection>(DraggedItem.Pin()->GetDataMutable()));

		TArray<UNiagaraHierarchySection*>& SectionData = HierarchyViewModel->GetHierarchyDataRoot()->GetSectionDataMutable();
		int32 Count = SectionData.Num();

		bool bDropSucceeded = false;
		// above constitutes to the left here
		if(ItemDropZone == EItemDropZone::AboveItem)
		{
			SectionData.RemoveAt(DraggedSectionIndex);
			SectionData.Insert(DraggedSectionData, FMath::Max(IndexOfThis, 0));

			bDropSucceeded = true;
		}
		else if(ItemDropZone == EItemDropZone::BelowItem)
		{
			SectionData.RemoveAt(DraggedSectionIndex);

			if(IndexOfThis + 1 > SectionData.Num())
			{
				SectionData.Add(DraggedSectionData);
			}
			else
			{
				SectionData.Insert(DraggedSectionData, FMath::Min(IndexOfThis+1, Count));
			}

			bDropSucceeded = true;

		}

		if(bDropSucceeded)
		{
			HierarchyViewModel->ForceFullRefresh();
			HierarchyViewModel->OnHierarchyChanged().Broadcast();
			return FReply::Handled();
		}

		return FReply::Unhandled();
	}
	else if(DragDropEvent.GetOperation()->IsOfType<FNiagaraHierarchyDragDropOp>())
	{		
		TSharedPtr<FNiagaraHierarchyDragDropOp> HierarchyDragDropOp = DragDropEvent.GetOperationAs<FNiagaraHierarchyDragDropOp>();
		TWeakPtr<FNiagaraHierarchyItemViewModelBase> DraggedItem = HierarchyDragDropOp->GetDraggedItem();

		if(UNiagaraHierarchyCategory* HierarchyCategory = Cast<UNiagaraHierarchyCategory>(DraggedItem.Pin()->GetDataMutable()))
		{
			FScopedTransaction Transaction(LOCTEXT("Transaction_OnSectionDrop", "Moved category to section"));
			HierarchyViewModel->GetHierarchyDataRoot()->Modify();
			
			HierarchyCategory->SetSection(Cast<UNiagaraHierarchySection>(GetDataMutable()));

			// we null out any sections for all contained categories
			TArray<UNiagaraHierarchyCategory*> AllChildCategories;
			HierarchyCategory->GetChildrenOfType<UNiagaraHierarchyCategory>(AllChildCategories, true);
			for(UNiagaraHierarchyCategory* ChildCategory : AllChildCategories)
			{
				ChildCategory->SetSection(nullptr);
			}

			// we only need to reparent if the parent isn't already the root. This stops unnecessary reordering
			if(DraggedItem.Pin()->GetParent() != HierarchyViewModel->GetHierarchyViewModelRoot())
			{
				HierarchyViewModel->GetHierarchyViewModelRoot()->ReparentToThis(DraggedItem.Pin());
			}
			
			HierarchyViewModel->RefreshHierarchyView();
			HierarchyViewModel->OnHierarchyChanged().Broadcast();

			return FReply::Handled();
		}
	}

	return FReply::Unhandled();
}

void FNiagaraHierarchySectionViewModel::FinalizeInternal()
{
	if(HierarchyViewModel->GetActiveSection() == AsShared())
	{
		HierarchyViewModel->SetActiveSection(nullptr);
	}

	// we make sure to reset all categories' section entry that were referencing this section
	TArray<UNiagaraHierarchyCategory*> AllCategories;
	HierarchyViewModel->GetHierarchyDataRoot()->GetChildrenOfType<UNiagaraHierarchyCategory>(AllCategories, true);

	for(UNiagaraHierarchyCategory* Category : AllCategories)
	{
		if(Category->GetSection() == GetData())
		{
			Category->SetSection(nullptr);
		}
	}
}

FReply FNiagaraHierarchyItemViewModel::OnDroppedOn(const FDragDropEvent& DragDropEvent, EItemDropZone ItemDropZone, TSharedPtr<FNiagaraHierarchyItemViewModelBase> Item)
{
	FScopedTransaction Transaction(LOCTEXT("Transaction_MovedItem", "Moved an item in the hierarchy"));
	HierarchyViewModel->GetHierarchyDataRoot()->Modify();
	
	if(DragDropEvent.GetOperation()->IsOfType<FNiagaraHierarchyDragDropOp>())
	{
		TSharedPtr<FNiagaraHierarchyDragDropOp> HierarchyDragDropOp = DragDropEvent.GetOperationAs<FNiagaraHierarchyDragDropOp>();
		bool bDropSucceeded = false;
		if(ItemDropZone == EItemDropZone::AboveItem)
		{
			int32 IndexOfThis = Parent.Pin()->FindIndexOfDataChild(AsShared());

			if(HierarchyDragDropOp->GetIsFromSourceList())
			{
				Parent.Pin()->DuplicateToThis(HierarchyDragDropOp->GetDraggedItem().Pin(), FMath::Max(IndexOfThis, 0));
			}
			else
			{
				Parent.Pin()->ReparentToThis(HierarchyDragDropOp->GetDraggedItem().Pin(), FMath::Max(IndexOfThis, 0));
			}

			bDropSucceeded = true;
		}
		else if(ItemDropZone == EItemDropZone::BelowItem)
		{
			int32 IndexOfThis = Parent.Pin()->FindIndexOfDataChild(AsShared());
			Parent.Pin()->ReparentToThis(HierarchyDragDropOp->GetDraggedItem().Pin(), FMath::Min(IndexOfThis+1, Parent.Pin()->GetChildren().Num()));

			bDropSucceeded = true;
		}

		if(bDropSucceeded)
		{
			HierarchyViewModel->RefreshHierarchyView();
			HierarchyViewModel->RefreshSourceView();
			return FReply::Handled();
		}
	}

	return FReply::Unhandled();
}

FReply FNiagaraHierarchyCategoryViewModel::OnDroppedOn(const FDragDropEvent& DragDropEvent, EItemDropZone ItemDropZone, TSharedPtr<FNiagaraHierarchyItemViewModelBase> Item)
{
	if(DragDropEvent.GetOperation()->IsOfType<FNiagaraHierarchyDragDropOp>())
	{
		FScopedTransaction Transaction(LOCTEXT("Transaction_OnCategoryDrop", "Dropped item on/above/below category"));
		HierarchyViewModel->GetHierarchyDataRoot()->Modify();
		
		TSharedPtr<FNiagaraHierarchyDragDropOp> HierarchyDragDropOp = DragDropEvent.GetOperationAs<FNiagaraHierarchyDragDropOp>();
		TSharedPtr<FNiagaraHierarchyItemViewModelBase> DraggedItem = HierarchyDragDropOp->GetDraggedItem().Pin();
		
		if(ItemDropZone == EItemDropZone::OntoItem)
		{
			// if we are dragging a category onto another category, we null out its section
			if(UNiagaraHierarchyCategory* Category = Cast<UNiagaraHierarchyCategory>(DraggedItem->GetDataMutable()))
			{
				Category->SetSection(nullptr);

				// we null out any sections for all contained categories
				TArray<UNiagaraHierarchyCategory*> AllChildCategories;
				Category->GetChildrenOfType<UNiagaraHierarchyCategory>(AllChildCategories, true);
				for(UNiagaraHierarchyCategory* ChildCategory : AllChildCategories)
				{
					ChildCategory->SetSection(nullptr);
				}
			}

			if(HierarchyDragDropOp->GetIsFromSourceList())
			{
				DuplicateToThis(HierarchyDragDropOp->GetDraggedItem().Pin());
			}
			else
			{
				ReparentToThis(HierarchyDragDropOp->GetDraggedItem().Pin());
			}
		}
		else if(ItemDropZone == EItemDropZone::AboveItem)
		{
			int32 IndexOfThis = Parent.Pin()->FindIndexOfDataChild(AsShared());
			if(HierarchyDragDropOp->GetIsFromSourceList())
			{
				Parent.Pin()->DuplicateToThis(HierarchyDragDropOp->GetDraggedItem().Pin(), FMath::Max(IndexOfThis, 0));
			}
			else
			{
				Parent.Pin()->ReparentToThis(HierarchyDragDropOp->GetDraggedItem().Pin(), FMath::Max(IndexOfThis, 0));
			}
		}
		else if(ItemDropZone == EItemDropZone::BelowItem)
		{
			int32 IndexOfThis = Parent.Pin()->FindIndexOfDataChild(AsShared());
			if(HierarchyDragDropOp->GetIsFromSourceList())
			{
				Parent.Pin()->DuplicateToThis(HierarchyDragDropOp->GetDraggedItem().Pin(), FMath::Min(IndexOfThis+1, Parent.Pin()->GetChildren().Num()));
			}
			else
			{
				Parent.Pin()->ReparentToThis(HierarchyDragDropOp->GetDraggedItem().Pin(), FMath::Min(IndexOfThis+1, Parent.Pin()->GetChildren().Num()));
			}
		}
		
		// todo (me)
		// refreshing both views whenever we drag something seems wasteful
		HierarchyViewModel->RefreshHierarchyView();
		HierarchyViewModel->RefreshSourceView();
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

void UNiagaraHierarchyViewModelBase::AddCategory() const
{
	UNiagaraHierarchyCategory* Category = Cast<UNiagaraHierarchyCategory>(HierarchyViewModelRoot->AddNewItem(UNiagaraHierarchyCategory::StaticClass()));
	TArray<UNiagaraHierarchyCategory*> SiblingCategories;
	Category->GetTypedOuter<UNiagaraHierarchyItemBase>()->GetChildrenOfType<UNiagaraHierarchyCategory>(SiblingCategories);
	
	TSet<FName> CategoryNames;
	for(const auto& SiblingCategory : SiblingCategories)
	{
		CategoryNames.Add(SiblingCategory->GetCategoryName());
	}

	Category->SetCategoryName(FNiagaraUtilities::GetUniqueName(FName("New Category"), CategoryNames));
	
	// we only set the section property if the current section isn't set to "All"
	Category->SetSection(GetActiveSectionData());
	
	RefreshHierarchyView();

	OnHierarchyChangedDelegate.Broadcast();
}

void UNiagaraHierarchyViewModelBase::AddSection() const
{
	HierarchyViewModelRoot->AddNewSection();
	OnHierarchyChangedDelegate.Broadcast();
}

TSharedPtr<SWidget> FNiagaraHierarchyDragDropOp::GetDefaultDecorator() const
{
	TSharedRef<SWidget> CustomDecorator = CreateCustomDecorator();

	SVerticalBox::FSlot* CustomSlot;
	TSharedPtr<SWidget> Decorator = SNew(SToolTip)
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.Expose(CustomSlot).
		AutoHeight()
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(2.f)
		[
			SNew(STextBlock)
			.Text(this, &FNiagaraHierarchyDragDropOp::GetAdditionalLabel)
			.TextStyle(&FAppStyle::GetWidgetStyle<FTextBlockStyle>("NormalText.Important"))
			.Visibility_Lambda([this]()
			{
				return GetAdditionalLabel().IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible;
			})
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(2.f)
		[
			SNew(STextBlock)
			.Text(this, &FNiagaraHierarchyDragDropOp::GetDescription)
			.TextStyle(&FAppStyle::GetWidgetStyle<FTextBlockStyle>("NormalText"))
			.Visibility_Lambda([this]()
			{
				return GetDescription().IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible;
			})
		]
	];

	if(CustomDecorator != SNullWidget::NullWidget)
	{
		CustomSlot->AttachWidget(CustomDecorator);
	}

	return Decorator;
}

TSharedRef<SWidget> FNiagaraSectionDragDropOp::CreateCustomDecorator() const
{
	return SNew(SCheckBox)
		.Visibility(EVisibility::HitTestInvisible)
		.Style(FAppStyle::Get(), "DetailsView.SectionButton")
		.IsChecked(ECheckBoxState::Unchecked)
		[
			SNew(SInlineEditableTextBlock)
			.Text(GetDraggedSection().Pin()->GetSectionNameAsText())
		];
}

#undef LOCTEXT_NAMESPACE
