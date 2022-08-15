// Copyright Epic Games, Inc. All Rights Reserved.

#include "Behaviour/Builtin/Bind/RCBehaviourBind.h"

#include "Action/RCAction.h"
#include "Action/RCFunctionAction.h"
#include "Action/Bind/RCPropertyBindAction.h"
#include "Behaviour/Builtin/Bind/RCBehaviourBindNode.h"
#include "Controller/RCController.h"
#include "RemoteControlField.h"
#include "RCVirtualProperty.h"

URCBehaviourBind::URCBehaviourBind()
{
}

void URCBehaviourBind::Initialize()
{
	Super::Initialize();
}

URCPropertyBindAction* URCBehaviourBind::AddPropertyBindAction(const TSharedRef<const FRemoteControlProperty> InRemoteControlProperty)
{
	if (!ensure(ControllerWeakPtr.IsValid() && ActionContainer))
	{
		return nullptr;
	}

	URCPropertyBindAction* BindAction = NewObject<URCPropertyBindAction>(ActionContainer);
	BindAction->PresetWeakPtr = ActionContainer->PresetWeakPtr;
	BindAction->ExposedFieldId = InRemoteControlProperty->GetId();
	BindAction->Controller = ControllerWeakPtr.Get();
	BindAction->Id = FGuid::NewGuid();

	// Add action to array
	ActionContainer->Actions.Add(BindAction);

	return BindAction;
}

URCAction* URCBehaviourBind::AddAction(const TSharedRef<const FRemoteControlField> InRemoteControlField)
{
	const TSharedRef<const FRemoteControlProperty> RemoteControlProperty = StaticCastSharedRef<const FRemoteControlProperty>(InRemoteControlField);

	URCAction* BindAction = AddPropertyBindAction(RemoteControlProperty);

	if (ensure(BindAction))
	{
		// For Bind Behaviour we want to pick up the Controller's current value immediately.
		// Subsequent value updates from Controller to Action will be propagated via the usual OnModifyPropertyValue code path

		BindAction->Execute();
	}

	return BindAction;
}

bool URCBehaviourBind::CanHaveActionForField(const TSharedPtr<FRemoteControlField> InRemoteControlField) const
{
	// Basic check (uniqueness)
	if (ActionContainer->FindActionByFieldId(InRemoteControlField->GetId()))
	{
		return false; // already exists!
	}

	// Advanced checks (by Controller type and Target type)
	if(TSharedPtr<FRemoteControlProperty> RemoteControlEntityAsProperty = StaticCastSharedPtr<FRemoteControlProperty>(InRemoteControlField))
	{
		if (URCController* Controller = ControllerWeakPtr.Get())
		{
			const FProperty* ControllerAsProperty = Controller->GetProperty();
			const FProperty* RemoteControlProperty = RemoteControlEntityAsProperty->GetProperty();
			const FStructProperty* RemoteControlEntityAsStructProperty = CastField<FStructProperty>(RemoteControlProperty);

			bool IsNumericStruct = RemoteControlEntityAsStructProperty
				&& (RemoteControlEntityAsStructProperty->Struct == TBaseStructure<FVector>::Get() ||
					RemoteControlEntityAsStructProperty->Struct == TBaseStructure<FRotator>::Get() ||
					RemoteControlEntityAsStructProperty->Struct == TBaseStructure<FColor>::Get() ||
					RemoteControlEntityAsStructProperty->Struct == TBaseStructure<FLinearColor>::Get());

			// String Controller
			if (ControllerAsProperty->IsA(FStrProperty::StaticClass()))
			{
				if(RemoteControlProperty->IsA(FStrProperty::StaticClass()) ||
					RemoteControlProperty->IsA(FTextProperty::StaticClass()) ||
					RemoteControlProperty->IsA(FNameProperty::StaticClass()))
				{
					return true; // Implicit / direct conversions are possible between these types
				}
				else if (RemoteControlProperty->IsA(FNumericProperty::StaticClass()) ||
					RemoteControlProperty->IsA(FBoolProperty::StaticClass()) ||
					RemoteControlProperty->IsA(FByteProperty::StaticClass()))
				{
					// Explicit conversion to numbers, governed by user set flag
					return bAllowNumericInputAsStrings;
				}
			}
			// Numeric Controller (Float/Int)
			else if (ControllerAsProperty->IsA(FNumericProperty::StaticClass()))
			{
				if (RemoteControlProperty->IsA(FNumericProperty::StaticClass()) ||
					RemoteControlProperty->IsA(FBoolProperty::StaticClass()) || 
					RemoteControlProperty->IsA(FByteProperty::StaticClass()))
				{
					return true; // Numeric to Numeric - Implicit / direct conversion

					// Note: Numeric to Numeric struct conversion is not currently supported as it would ideally require Masking support (on the struct) for meaningful usage
				}
				else if (RemoteControlProperty->IsA(FStrProperty::StaticClass()) ||
					RemoteControlProperty->IsA(FTextProperty::StaticClass()) ||
					RemoteControlProperty->IsA(FNameProperty::StaticClass()))
				{
					// Explicit conversion to String/Name/Text, governed by user set flag
					return bAllowNumericInputAsStrings;
				}
			}
			// Boolean Controller
			else if (ControllerAsProperty->IsA(FBoolProperty::StaticClass()))
			{
				if (RemoteControlProperty->IsA(FFloatProperty::StaticClass()) || // Note: FNumericPropery includes Enum, so we check explicitly for Float and Int here
					RemoteControlProperty->IsA(FIntProperty::StaticClass()) ||
					RemoteControlProperty->IsA(FBoolProperty::StaticClass()))
				{
					return true;
				}
				// Notes for explicit Bool conversions: 
				// 1. Boolean to String/Text/Name is currently not deemed as a requirement, so hasn't been implemented here
				// 2. Boolean to Struct is also not implemented. 
			}
			// Struct
			else if (ControllerAsProperty->IsA(FStructProperty::StaticClass()))
			{
				if (const FStructProperty* StructProperty = CastField<FStructProperty>(ControllerAsProperty))
				{
					// FVector / FColor / FRotator
					if (const FStructProperty* RCFieldStructProperty = CastField<FStructProperty>(RemoteControlProperty))
					{
						// Generally - A Struct Controller can only drive properties of the _same_ Struct type...
						if (RCFieldStructProperty->Struct == StructProperty->Struct)
						{
							return true;
						}
						// ... with an exception for the following Struct conversions:
						else
						{
							// FColor conversion:
							if (StructProperty->Struct == TBaseStructure<FColor>::Get())
							{
								// To FLinearColor, is supported.
								return RCFieldStructProperty->Struct == TBaseStructure<FLinearColor>::Get();
							}
						}

					}
				}
			}
		}
	}

	return false;
}
