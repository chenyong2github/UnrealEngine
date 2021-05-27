// Copyright Epic Games, Inc. All Rights Reserved.

#include "RCPanelWidgetRegistry.h"

#include "IDetailTreeNode.h"
#include "IPropertyRowGenerator.h"
#include "Modules/ModuleManager.h"
#include "PropertyHandle.h"
#include "UObject/StructOnScope.h"

namespace WidgetRegistryUtils
{
	bool FindPropertyHandleRecursive(const TSharedPtr<IPropertyHandle>& PropertyHandle, const FString& PropertyNameOrPath, ERCFindNodeMethod FindMethod)
	{
		if (PropertyHandle && PropertyHandle->IsValidHandle())
		{
			uint32 ChildrenCount = 0;
			PropertyHandle->GetNumChildren(ChildrenCount);
			for (uint32 Index = 0; Index < ChildrenCount; ++Index)
			{
				TSharedPtr<IPropertyHandle> ChildHandle = PropertyHandle->GetChildHandle(Index);
				if (FindPropertyHandleRecursive(ChildHandle, PropertyNameOrPath, FindMethod))
				{
					return true;
				}
			}

			if (PropertyHandle->GetProperty())
			{
				if (FindMethod == ERCFindNodeMethod::Path)
				{
					if (PropertyHandle->GeneratePathToProperty() == PropertyNameOrPath)
					{
						return true;
					}
				}
				else if (PropertyHandle->GetProperty()->GetName() == PropertyNameOrPath)
				{
					return true;
				}
			}
		}

		return false;
	}

	TSharedPtr<IDetailTreeNode> FindTreeNodeRecursive(const TSharedRef<IDetailTreeNode>& RootNode, const FString& PropertyNameOrPath, ERCFindNodeMethod FindMethod)
	{
		TArray<TSharedRef<IDetailTreeNode>> Children;
		RootNode->GetChildren(Children);
		for (TSharedRef<IDetailTreeNode>& Child : Children)
		{
			TSharedPtr<IDetailTreeNode> FoundNode = FindTreeNodeRecursive(Child, PropertyNameOrPath, FindMethod);
			if (FoundNode.IsValid())
			{
				return FoundNode;
			}
		}

		TSharedPtr<IPropertyHandle> Handle = RootNode->CreatePropertyHandle();
		if (FindPropertyHandleRecursive(Handle, PropertyNameOrPath, FindMethod))
		{
			return RootNode;
		}

		return nullptr;
	}

	/** Find a node by its name in a detail tree node hierarchy. */
	TSharedPtr<IDetailTreeNode> FindNode(const TArray<TSharedRef<IDetailTreeNode>>& RootNodes, const FString& QualifiedPropertyName, ERCFindNodeMethod FindMethod)
	{
		for (const TSharedRef<IDetailTreeNode>& CategoryNode : RootNodes)
		{
			TSharedPtr<IDetailTreeNode> FoundNode = FindTreeNodeRecursive(CategoryNode, QualifiedPropertyName, FindMethod);
			if (FoundNode.IsValid())
			{
				return FoundNode;
			}
		}

		return nullptr;
	}
}

TSharedPtr<IDetailTreeNode> FRCPanelWidgetRegistry::GetObjectTreeNode(UObject* InObject, const FString& InField, ERCFindNodeMethod InFindMethod)
{
	const TPair<TWeakObjectPtr<UObject>, FString> CacheKey{InObject, InField};
	if (TWeakPtr<IDetailTreeNode>* Node = TreeNodeCache.Find(CacheKey))
	{
		if (Node->IsValid())
		{
			return Node->Pin();
		}
		else
		{
			TreeNodeCache.Remove(CacheKey);
		}
	}
	
	TSharedPtr<IPropertyRowGenerator> Generator;
	TWeakObjectPtr<UObject> WeakObject = InObject;
	
	if (TSharedPtr<IPropertyRowGenerator>* FoundGenerator = ObjectToRowGenerator.Find(WeakObject))
	{
		Generator = *FoundGenerator;
	}
	else
	{
		// Since we must keep many PRG objects alive in order to access the handle data, validating the nodes each tick is very taxing.
		// We can override the validation with a lambda since the validation function in PRG is not necessary for our implementation
		auto ValidationLambda = ([](const FRootPropertyNodeList& PropertyNodeList) { return true; });
		Generator = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor").CreatePropertyRowGenerator(FPropertyRowGeneratorArgs());
		Generator->SetCustomValidatePropertyNodesFunction(FOnValidatePropertyRowGeneratorNodes::CreateLambda(MoveTemp(ValidationLambda)));
		Generator->SetObjects({InObject});
		ObjectToRowGenerator.Add(WeakObject, Generator);
	}
	
	TSharedPtr<IDetailTreeNode> Node = WidgetRegistryUtils::FindNode(Generator->GetRootTreeNodes(), InField, ERCFindNodeMethod::Path);
	// Cache the node to avoid having to do the recursive find again.
	TreeNodeCache.Add(CacheKey, Node);
	
	return Node;
}

TSharedPtr<IDetailTreeNode> FRCPanelWidgetRegistry::GetStructTreeNode(const TSharedPtr<FStructOnScope>& InStruct, const FString& InField, ERCFindNodeMethod InFindMethod)
{
	TSharedPtr<IPropertyRowGenerator> Generator;
	
	if (TSharedPtr<IPropertyRowGenerator>* FoundGenerator = StructToRowGenerator.Find(InStruct))
	{
		Generator = *FoundGenerator;
	}
	else
	{
		// In RC, struct details node are only used for function arguments, so default to showing hidden properties. 
		FPropertyRowGeneratorArgs Args;
		Args.bShouldShowHiddenProperties = true;
		
		Generator = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor").CreatePropertyRowGenerator(Args);
		Generator->SetStructure(InStruct);
		StructToRowGenerator.Add(InStruct, Generator);
	}

	return WidgetRegistryUtils::FindNode(Generator->GetRootTreeNodes(), InField, InFindMethod);
}

void FRCPanelWidgetRegistry::Refresh(UObject* InObject)
{
	if (TSharedPtr<IPropertyRowGenerator>* Generator = ObjectToRowGenerator.Find({InObject}))
	{
		(*Generator)->SetObjects({InObject});
	}
}

void FRCPanelWidgetRegistry::Refresh(const TSharedPtr<FStructOnScope>& InStruct)
{
	if (TSharedPtr<IPropertyRowGenerator>* Generator = StructToRowGenerator.Find(InStruct))
	{
		(*Generator)->SetStructure(InStruct);
	}
}

void FRCPanelWidgetRegistry::Clear()
{
	ObjectToRowGenerator.Empty();
	StructToRowGenerator.Empty();
	TreeNodeCache.Empty();
}
