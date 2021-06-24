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
