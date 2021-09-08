// Copyright Epic Games, Inc. All Rights Reserved.

#include "Data/RestorableObjectSelection.h"

#include "AddedAndRemovedComponentInfo.h"
#include "PropertySelectionMap.h"
#include "PropertySelection.h"

const FPropertySelection* FRestorableObjectSelection::GetPropertySelection() const
{
	return Owner.EditorWorldObjectToSelectedProperties.Find(ObjectPath);
}

const FAddedAndRemovedComponentInfo* FRestorableObjectSelection::GetComponentSelection() const
{
	return Owner.EditorActorToComponentSelection.Find(ObjectPath);
}

const FCustomSubobjectRestorationInfo* FRestorableObjectSelection::GetCustomSubobjectSelection() const
{
	return Owner.EditorWorldObjectToCustomSubobjectSelection.Find(ObjectPath);
}
