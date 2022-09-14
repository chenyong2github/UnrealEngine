// Copyright Epic Games, Inc. All Rights Reserved.

#include "Action/RCActionContainer.h"

#include "Behaviour/RCBehaviour.h"
#include "Controller/RCController.h"
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

TRCActionUniquenessTest URCActionContainer::GetDefaultActionUniquenessTest(const TSharedRef<const FRemoteControlField> InRemoteControlField)
{
	const FGuid FieldId = InRemoteControlField->GetId();

	return [FieldId](const TSet<TObjectPtr<URCAction>>& InActions)
	{
		for (const URCAction* Action : InActions)
		{
			if (Action->ExposedFieldId == FieldId)
			{
				return false;
			}
		}

		return true;
	};
}

URCAction* URCActionContainer::AddAction(const TSharedRef<const FRemoteControlField> InRemoteControlField)
{
	return AddAction(GetDefaultActionUniquenessTest(InRemoteControlField), InRemoteControlField);
}


URCAction* URCActionContainer::AddAction(TRCActionUniquenessTest InUniquenessTest, const TSharedRef<const FRemoteControlField> InRemoteControlField)
{
	if (!InUniquenessTest(Actions))
	{
		return nullptr;
	}

	URCAction* NewAction = nullptr;

	if (InRemoteControlField->FieldType == EExposedFieldType::Property)
	{
		NewAction = AddPropertyAction(StaticCastSharedRef<const FRemoteControlProperty>(InRemoteControlField));
	}
	else if (InRemoteControlField->FieldType == EExposedFieldType::Function)
	{
		NewAction = AddFunctionAction(StaticCastSharedRef<const FRemoteControlFunction>(InRemoteControlField));
	}

	return NewAction;
}

URCBehaviour* URCActionContainer::GetParentBehaviour()
{
	return Cast<URCBehaviour>(GetOuter());
}

URCPropertyAction* URCActionContainer::AddPropertyAction(const TSharedRef<const FRemoteControlProperty> InRemoteControlProperty)
{
	// Create new Property
	URCPropertyAction* NewPropertyAction = NewObject<URCPropertyAction>(this);
	NewPropertyAction->PresetWeakPtr = PresetWeakPtr;
	NewPropertyAction->ExposedFieldId = InRemoteControlProperty->GetId();
	NewPropertyAction->Id = FGuid::NewGuid();

	if (!InRemoteControlProperty->GetBoundObjects().Num())
	{
		// This is possible if an exposed property was either deleted directly by the user, or if a project exits without saving the linked actor, etc

		return nullptr;
	}
	
	if (FRCObjectReference ObjectRef; IRemoteControlModule::Get().ResolveObjectProperty(ERCAccess::READ_ACCESS, InRemoteControlProperty->GetBoundObjects()[0], InRemoteControlProperty->FieldPathInfo.ToString(), ObjectRef))
	{
		const FName& PropertyName = InRemoteControlProperty->GetProperty()->GetFName();
		NewPropertyAction->PropertySelfContainer->DuplicatePropertyWithCopy(PropertyName, InRemoteControlProperty->GetProperty(), (uint8*)ObjectRef.ContainerAdress);
	}

	AddAction(NewPropertyAction);
	
	return NewPropertyAction;
}

URCFunctionAction* URCActionContainer::AddFunctionAction(const TSharedRef<const FRemoteControlFunction> InRemoteControlFunction)
{
	// Create new Function Action
	URCFunctionAction* NewFunctionAction = NewObject<URCFunctionAction>(this);
	NewFunctionAction->PresetWeakPtr = PresetWeakPtr;
	NewFunctionAction->ExposedFieldId = InRemoteControlFunction->GetId();
	NewFunctionAction->Id = FGuid::NewGuid();

	AddAction(NewFunctionAction);
	
	return NewFunctionAction;
}

void URCActionContainer::AddAction(URCAction* NewAction)
{
	Actions.Add(NewAction);
}

#if WITH_EDITOR
void URCActionContainer::PostEditUndo()
{
	Super::PostEditUndo();

	OnActionsListModified.Broadcast();
}
#endif

URCAction* URCActionContainer::FindActionByFieldId(const FGuid InId) const
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

URCAction* URCActionContainer::FindActionByField(const TSharedRef<const FRemoteControlField> InRemoteControlField) const
{
	return FindActionByFieldId(InRemoteControlField->GetId());
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