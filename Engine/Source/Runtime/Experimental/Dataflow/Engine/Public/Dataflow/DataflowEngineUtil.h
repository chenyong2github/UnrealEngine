// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/UnrealTypePrivate.h"
#include "ReferenceSkeleton.h"
namespace Dataflow
{
	namespace Reflection
	{
		template<class T>
		const T* FindObjectPtrProperty(const UObject* Owner, FName Name)
		{
			if (Owner && Owner->GetClass())
			{
				if (const ::FProperty* UEProperty = Owner->GetClass()->FindPropertyByName(Name))
				{
					if (const FObjectProperty* ObjectProperty = CastField<FObjectProperty>(UEProperty))
					{
						if (const void* ObjectContainer = ObjectProperty->ContainerPtrToValuePtr<void>(Owner))
						{
							if (const UObject* Value = ObjectProperty->GetObjectPropertyValue(ObjectContainer))
							{
								return Cast<T>(Value);
							}
						}
					}
				}
			}
			return nullptr;
		}
	}

	namespace Animation
	{
		void DATAFLOWENGINE_API GlobalTransforms(const FReferenceSkeleton&, TArray<FTransform>& GlobalTransforms);
	}
}