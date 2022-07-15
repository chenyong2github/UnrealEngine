// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ObjectFilter/ObjectMixerEditorObjectFilter.h"
#include "ObjectMixerEditorProjectSettings.h"
#include "Widgets/SWidget.h"

struct FObjectMixerEditorListRow;
typedef TSharedPtr<FObjectMixerEditorListRow> FObjectMixerEditorListRowPtr;

class SObjectMixerEditorList;

class OBJECTMIXEREDITOR_API FObjectMixerEditorList : public TSharedFromThis<FObjectMixerEditorList>
{
public:

	FObjectMixerEditorList();

	virtual ~FObjectMixerEditorList();

	TSharedRef<SWidget> GetOrCreateWidget();

	UObjectMixerObjectFilter* GetObjectFilter();

	void CacheObjectFilterObject();

	/*
	 * Returns either ObjectClassFromWhichToGeneratePropertyColumns or ObjectClassOverride, if set.
	 * @param bForceNoOverride If true, always return ObjectClassFromWhichToGeneratePropertyColumns and not ObjectClassOverride.
	 */
	TArray<UClass*> GetObjectClasses(bool bForceNoOverride = false)
	{
		TArray<UClass*> ReturnValue;

		if (const UObjectMixerObjectFilter* Filter = GetObjectFilter())
		{
			ReturnValue = Filter->GetObjectClassesToFilter();
		}
		
		return ReturnValue;
	}

	TWeakPtr<FObjectMixerEditorListRow> GetSoloRow()
	{
		return SoloRow;
	}

	void SetSoloRow(TSharedRef<FObjectMixerEditorListRow> InRow)
	{
		SoloRow = InRow;
	}

	void ClearSoloRow()
	{
		SoloRow = nullptr;
	}

	void SetSearchString(const FString& SearchString);

	void ClearList() const;

	/*
	 * Regenerate the list items and refresh the list. Call when adding or removing variables.
	 */
	void RebuildList(const FString& InItemToScrollTo = "") const;

	/**
	 * Refresh filters and sorting.
	 * Useful for when the list state has gone stale but the variable count has not changed.
	 */
	void RefreshList() const;

	TSubclassOf<UObjectMixerObjectFilter> GetObjectFilterClass() const
	{
		return ObjectFilterClass;
	}

	void SetObjectFilterClass(TSubclassOf<UObjectMixerObjectFilter> InObjectFilterClass)
	{
		ObjectFilterClass = InObjectFilterClass;
		CacheObjectFilterObject();
		RebuildList();
	}

private:

	/**
	 * The class used to generate property edit columns
	 */
	TSubclassOf<UObjectMixerObjectFilter> ObjectFilterClass;

	TSharedPtr<SObjectMixerEditorList> ListWidget;

	TWeakObjectPtr<UObjectMixerObjectFilter> ObjectFilterPtr;

	TWeakPtr<FObjectMixerEditorListRow> SoloRow = nullptr;

	// Delegates
	TFunction<void()> RebuildDelegate = [this]()
	{
		RebuildList();
	};
	
	TSet<FDelegateHandle> EditorDelegateHandles;
};
