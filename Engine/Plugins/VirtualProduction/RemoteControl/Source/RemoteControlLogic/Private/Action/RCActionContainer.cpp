// Copyright Epic Games, Inc. All Rights Reserved.

#include "Action/RCActionContainer.h"

#include "IRemoteControlModule.h"
#include "RCVirtualProperty.h"
#include "RemoteControlField.h"
#include "Action/RCAction.h"
#include "Action/RCFunctionAction.h"
#include "Action/RCPropertyAction.h"

void URCActionContainer::ExecuteActions()
{
	for (const URCAction* Action : Actions)
	{
		Action->Execute();
	}
}

URCPropertyAction* URCActionContainer::AddAction(const TSharedPtr<FRemoteControlProperty> InRemoteControlProperty)
{
	if (!InRemoteControlProperty.IsValid())
	{
		return nullptr;
	}
	
	if (FindActionByField(InRemoteControlProperty))
	{
		return nullptr;
	}

	// Create new Property
	URCPropertyAction* NewPropertyAction = NewObject<URCPropertyAction>(this);
	NewPropertyAction->PresetWeakPtr = PresetWeakPtr;
	NewPropertyAction->ExposedFieldId = InRemoteControlProperty->GetId();
	NewPropertyAction->Id = FGuid::NewGuid();
	
	if (FRCObjectReference ObjectRef; IRemoteControlModule::Get().ResolveObjectProperty(ERCAccess::READ_ACCESS, InRemoteControlProperty->GetBoundObjects()[0], InRemoteControlProperty->FieldPathInfo.ToString(), ObjectRef))
	{
		const FName& PropertyName = InRemoteControlProperty->GetProperty()->GetFName();
		NewPropertyAction->PropertySelfContainer->DuplicatePropertyWithCopy(PropertyName, InRemoteControlProperty->GetProperty(), (uint8*)ObjectRef.ContainerAdress);
	}

	// Add action to array
	Actions.Add(NewPropertyAction);
	
	return NewPropertyAction;
}

URCFunctionAction* URCActionContainer::AddAction(const TSharedPtr<FRemoteControlFunction> InRemoteControlFunction)
{
	if (!InRemoteControlFunction.IsValid())
	{
		return nullptr;
	}

	if (FindActionByField(InRemoteControlFunction))
	{
		return nullptr;
	}
	
	// Create new Function Action
	URCFunctionAction* NewFunctionAction = NewObject<URCFunctionAction>(this);
	NewFunctionAction->PresetWeakPtr = PresetWeakPtr;
	NewFunctionAction->ExposedFieldId = InRemoteControlFunction->GetId();
	NewFunctionAction->Id = FGuid::NewGuid();

	Actions.Add(NewFunctionAction);
	
	return NewFunctionAction;
}

URCAction* URCActionContainer::FindActionByFieldId(const FGuid InId)
{
	for (URCAction* Action :  Actions)
	{
		if (Action->ExposedFieldId == InId)
		{
			return Action;
		}
	}

	return nullptr;
}

URCAction* URCActionContainer::FindActionByField(const TSharedPtr<FRemoteControlField> InRemoteControlField)
{
	for (URCAction* Action :  Actions)
	{
		if (Action->ExposedFieldId == InRemoteControlField->GetId())
		{
			return Action;
		}
	}

	return nullptr;
}

int32 URCActionContainer::RemoveAction(const FGuid InExposedFieldId)
{
	int32 RemoveCount = 0;
	
	for (auto ActionsIt = Actions.CreateIterator(); ActionsIt; ++ActionsIt)
	{
		if (const URCAction* Action = *ActionsIt; Action->ExposedFieldId == InExposedFieldId)
		{
			ActionsIt.RemoveCurrent();
			RemoveCount++;
		}
	}

	return RemoveCount;
}

int32 URCActionContainer::RemoveAction(URCAction* InAction)
{
	return Actions.Remove(InAction);
}

void URCActionContainer::EmptyActions()
{
	Actions.Empty();
}
