// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Elements/Framework/TypedElementListProxy.h"
#include "Elements/Framework/TypedElementSelectionSet.h"
#include "TypedElementSelectionSetLibrary.generated.h"

UCLASS()
class UTypedElementSelectionSetLibrary : public UObject
{
	GENERATED_BODY()
	
public:
	/**
	 * Attempt to select the given elements.
	 * @return True if the selection was changed, false otherwise.
	 */
	UFUNCTION(BlueprintCallable, Category="TypedElementFramework|Selection", meta=(ScriptMethod))
	static bool SelectElementsFromList(UTypedElementSelectionSet* SelectionSet, const FTypedElementListProxy ElementList, const FTypedElementSelectionOptions SelectionOptions)
	{
		FTypedElementListConstPtr ElementListPtr = ElementList.GetElementList();
		return ElementListPtr && SelectionSet->SelectElements(ElementListPtr.ToSharedRef(), SelectionOptions);
	}

	/**
	 * Attempt to deselect the given elements.
	 * @return True if the selection was changed, false otherwise.
	 */
	UFUNCTION(BlueprintCallable, Category="TypedElementFramework|Selection", meta=(ScriptMethod))
	static bool DeselectElementsFromList(UTypedElementSelectionSet* SelectionSet, const FTypedElementListProxy ElementList, const FTypedElementSelectionOptions SelectionOptions)
	{
		FTypedElementListConstPtr ElementListPtr = ElementList.GetElementList();
		return ElementListPtr && SelectionSet->DeselectElements(ElementListPtr.ToSharedRef(), SelectionOptions);
	}

	/**
	 * Attempt to make the selection the given elements.
	 * @note Equivalent to calling ClearSelection then SelectElements, but happens in a single batch.
	 * @return True if the selection was changed, false otherwise.
	 */
	UFUNCTION(BlueprintCallable, Category="TypedElementFramework|Selection", meta=(ScriptMethod))
	static bool SetSelectionFromList(UTypedElementSelectionSet* SelectionSet, const FTypedElementListProxy ElementList, const FTypedElementSelectionOptions SelectionOptions)
	{
		FTypedElementListConstPtr ElementListPtr = ElementList.GetElementList();
		return ElementListPtr
			? SelectionSet->SetSelection(ElementListPtr.ToSharedRef(), SelectionOptions)
			: SelectionSet->ClearSelection(SelectionOptions);
	}

	/**
	 * Get a normalized version of this selection set that can be used to perform operations like gizmo manipulation, deletion, copying, etc.
	 * This will do things like expand out groups, and resolve any parent<->child elements so that duplication operations aren't performed on both the parent and the child.
	 */
	UFUNCTION(BlueprintPure, Category="TypedElementFramework|Selection", meta=(ScriptMethod))
	static FTypedElementListProxy GetNormalizedSelection(UTypedElementSelectionSet* SelectionSet, const FTypedElementSelectionNormalizationOptions NormalizationOptions)
	{
		return SelectionSet->GetNormalizedSelection(NormalizationOptions);
	}

	/**
	 * Get a normalized version of the given element list that can be used to perform operations like gizmo manipulation, deletion, copying, etc.
	 * This will do things like expand out groups, and resolve any parent<->child elements so that duplication operations aren't performed on both the parent and the child.
	 */
	UFUNCTION(BlueprintPure, Category="TypedElementFramework|Selection", meta=(ScriptMethod))
	static FTypedElementListProxy GetNormalizedElementList(UTypedElementSelectionSet* SelectionSet, const FTypedElementListProxy ElementList, const FTypedElementSelectionNormalizationOptions NormalizationOptions)
	{
		FTypedElementListConstPtr ElementListPtr = ElementList.GetElementList();
		return ElementListPtr ? SelectionSet->GetNormalizedElementList(ElementListPtr.ToSharedRef(), NormalizationOptions) : FTypedElementListProxy();
	}
};
