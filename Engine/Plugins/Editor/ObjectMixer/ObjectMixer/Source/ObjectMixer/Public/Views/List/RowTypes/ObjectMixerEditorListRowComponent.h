// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Views/List/ObjectMixerEditorListRowData.h"

#include "ComponentTreeItem.h"
#include "Containers/Array.h"
#include "Containers/UnrealString.h"
#include "Folder.h"
#include "GameFramework/Actor.h"
#include "Layout/Visibility.h"
#include "PropertyHandle.h"
#include "SSceneOutliner.h"
#include "Templates/SharedPointer.h"
#include "UObject/Object.h"

struct OBJECTMIXEREDITOR_API FObjectMixerEditorListRowComponent : FComponentTreeItem
{
	explicit FObjectMixerEditorListRowComponent(
		UActorComponent* InObject, 
		SSceneOutliner* InSceneOutliner, const FText& InDisplayNameOverride = FText::GetEmpty())
	: FComponentTreeItem(InObject)
	{
		RowData = FObjectMixerEditorListRowData(InSceneOutliner, InDisplayNameOverride);
	}
	
	FObjectMixerEditorListRowData RowData;

	AActor* GetOwningActor() const
	{
		if (const UActorComponent* WrappedComponent = Component.Get())
		{
			return WrappedComponent->GetOwner();
		}

		return nullptr;
	}
	
	/* Begin ISceneOutlinerTreeItem Implementation */
	static const FSceneOutlinerTreeItemType Type;
	/* End ISceneOutlinerTreeItem Implementation */
};
