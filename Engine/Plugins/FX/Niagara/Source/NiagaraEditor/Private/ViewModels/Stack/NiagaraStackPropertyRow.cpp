// Copyright Epic Games, Inc. All Rights Reserved.

#include "ViewModels/Stack/NiagaraStackPropertyRow.h"
#include "NiagaraNode.h"

#include "IDetailTreeNode.h"
#include "PropertyHandle.h"
#include "IDetailPropertyRow.h"

void UNiagaraStackPropertyRow::Initialize(FRequiredEntryData InRequiredEntryData, TSharedRef<IDetailTreeNode> InDetailTreeNode, FString InOwnerStackItemEditorDataKey, FString InOwnerStackEditorDataKey, UNiagaraNode* InOwningNiagaraNode)
{
	TSharedPtr<IPropertyHandle> PropertyHandle = InDetailTreeNode->CreatePropertyHandle();
	FString RowStackEditorDataKey = FString::Printf(TEXT("%s-%s"), *InOwnerStackEditorDataKey, *InDetailTreeNode->GetNodeName().ToString());
	Super::Initialize(InRequiredEntryData, InOwnerStackItemEditorDataKey, RowStackEditorDataKey);
	bool bRowIsAdvanced = PropertyHandle.IsValid() && PropertyHandle->GetProperty() && PropertyHandle->GetProperty()->HasAnyPropertyFlags(CPF_AdvancedDisplay);
	SetIsAdvanced(bRowIsAdvanced);
	DetailTreeNode = InDetailTreeNode;
	OwningNiagaraNode = InOwningNiagaraNode;
	RowStyle = DetailTreeNode->GetNodeType() == EDetailNodeType::Category
		? EStackRowStyle::ItemCategory
		: EStackRowStyle::ItemContent;
	bCannotEditInThisContext = false;
	if (PropertyHandle.IsValid() && PropertyHandle.Get() && PropertyHandle->GetProperty())
	{
		FProperty* Prop = PropertyHandle->GetProperty();
		FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(Prop);
		if (ObjProp && ObjProp->PropertyClass && (ObjProp->PropertyClass->IsChildOf(AActor::StaticClass()) || ObjProp->PropertyClass->IsChildOf(UActorComponent::StaticClass())))
		{
			bCannotEditInThisContext = true;
		}
	}
}

TSharedRef<IDetailTreeNode> UNiagaraStackPropertyRow::GetDetailTreeNode() const
{
	return DetailTreeNode.ToSharedRef();
}

bool UNiagaraStackPropertyRow::GetIsEnabled() const
{
	if (bCannotEditInThisContext) 
		return false;
	return OwningNiagaraNode == nullptr || OwningNiagaraNode->GetDesiredEnabledState() == ENodeEnabledState::Enabled;
}

bool UNiagaraStackPropertyRow::HasOverridenContent() const
{
	TSharedPtr<IPropertyHandle> PropertyHandle = DetailTreeNode->CreatePropertyHandle();
	if (PropertyHandle.IsValid() && PropertyHandle.Get())
	{
		return PropertyHandle->DiffersFromDefault();
	}
	return false;
}

void UNiagaraStackPropertyRow::FinalizeInternal()
{
	Super::FinalizeInternal();
	DetailTreeNode.Reset();
}

void UNiagaraStackPropertyRow::RefreshChildrenInternal(const TArray<UNiagaraStackEntry*>& CurrentChildren, TArray<UNiagaraStackEntry*>& NewChildren, TArray<FStackIssue>& NewIssues)
{
	TArray<TSharedRef<IDetailTreeNode>> NodeChildren;
	DetailTreeNode->GetChildren(NodeChildren);
	for (TSharedRef<IDetailTreeNode> NodeChild : NodeChildren)
	{
		if (NodeChild->GetNodeType() == EDetailNodeType::Advanced)
		{
			continue;
		}

		UNiagaraStackPropertyRow* ChildRow = FindCurrentChildOfTypeByPredicate<UNiagaraStackPropertyRow>(CurrentChildren,
			[=](UNiagaraStackPropertyRow* CurrentChild) { return CurrentChild->GetDetailTreeNode() == NodeChild; });

		if (ChildRow == nullptr)
		{
			ChildRow = NewObject<UNiagaraStackPropertyRow>(this);
			ChildRow->Initialize(CreateDefaultChildRequiredData(), NodeChild, GetOwnerStackItemEditorDataKey(), GetStackEditorDataKey(), OwningNiagaraNode);
		}

		NewChildren.Add(ChildRow);
	}
}
void UNiagaraStackPropertyRow::GetSearchItems(TArray<FStackSearchItem>& SearchItems) const
{
	SearchItems.Add({ FName("DisplayName"), GetDisplayName() });

	TArray<FString> NodeFilterStrings;
	DetailTreeNode->GetFilterStrings(NodeFilterStrings);
	for (FString& FilterString : NodeFilterStrings)
	{
		SearchItems.Add({ "PropertyRowFilterString", FText::FromString(FilterString) });
	}

	TSharedPtr<IDetailPropertyRow> DetailPropertyRow = DetailTreeNode->GetRow();
	if (DetailPropertyRow.IsValid())
	{
		TSharedPtr<IPropertyHandle> PropertyHandle = DetailPropertyRow->GetPropertyHandle();
		if (PropertyHandle)
		{
			FText PropertyRowHandleText;
			PropertyHandle->GetValueAsDisplayText(PropertyRowHandleText);
			SearchItems.Add({ "PropertyRowHandleText", PropertyRowHandleText });
		}
	}	
}
