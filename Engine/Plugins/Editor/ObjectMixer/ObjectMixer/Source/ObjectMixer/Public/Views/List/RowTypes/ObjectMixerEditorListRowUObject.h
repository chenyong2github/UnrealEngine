// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Views/List/ObjectMixerEditorListRowData.h"

#include "Containers/Array.h"
#include "Containers/UnrealString.h"
#include "Folder.h"
#include "GameFramework/Actor.h"
#include "ISceneOutlinerTreeItem.h"
#include "Layout/Visibility.h"
#include "PropertyHandle.h"
#include "SSceneOutliner.h"
#include "Templates/SharedPointer.h"
#include "UObject/Object.h"

struct OBJECTMIXEREDITOR_API FObjectMixerEditorListRowUObject : ISceneOutlinerTreeItem
{	
	explicit FObjectMixerEditorListRowUObject(
		UObject* InObject, 
		SSceneOutliner* InSceneOutliner, const FText& InDisplayNameOverride = FText::GetEmpty())
	: ISceneOutlinerTreeItem(Type)
	, ObjectPtr(InObject)
	, ID(InObject)
	{
		RowData = FObjectMixerEditorListRowData(InSceneOutliner, InDisplayNameOverride);
	}
	
	FObjectMixerEditorListRowData RowData;

	TObjectPtr<UObject> ObjectPtr;
	
	/** Constant identifier for this tree item */
	const FObjectKey ID;

	/* Begin ISceneOutlinerTreeItem Implementation */
	static const FSceneOutlinerTreeItemType Type;
	virtual FString GetDisplayString() const override;
	virtual bool CanInteract() const override { return true; }
	virtual bool IsValid() const override { return ObjectPtr != nullptr; }
	virtual FSceneOutlinerTreeItemID GetID() const override { return ID; }
	/* End ISceneOutlinerTreeItem Implementation */
};
