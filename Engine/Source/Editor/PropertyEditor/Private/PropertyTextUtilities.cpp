// Copyright Epic Games, Inc. All Rights Reserved.

#include "PropertyTextUtilities.h"
#include "PropertyNode.h"
#include "PropertyHandleImpl.h"

void FPropertyTextUtilities::PropertyToTextHelper(FString& OutString, const FPropertyNode* InPropertyNode, const FProperty* Property, uint8* ValueAddress, EPropertyPortFlags PortFlags)
{
	if (InPropertyNode->GetArrayIndex() != INDEX_NONE || Property->ArrayDim == 1)
	{
		Property->ExportText_Direct(OutString, ValueAddress, ValueAddress, nullptr, PortFlags);
	}
	else
	{
		FArrayProperty::ExportTextInnerItem(OutString, Property, ValueAddress, Property->ArrayDim, ValueAddress, Property->ArrayDim, nullptr, PortFlags);
	}
}

void FPropertyTextUtilities::PropertyToTextHelper(FString& OutString, const FPropertyNode* InPropertyNode, const FProperty* Property, const FObjectBaseAddress& ObjectAddress, EPropertyPortFlags PortFlags)
{
	bool bIsSparseProperty = !!InPropertyNode->HasNodeFlags(EPropertyNodeFlags::IsSparseClassData);
	bool bIsInContainer = false;
	const FProperty* Outer = Property->GetOwner<FProperty>();
	if (bIsSparseProperty)
	{
		while (Outer)
		{
			const FArrayProperty* ArrayOuter = Property->GetOwner<FArrayProperty>();
			const FSetProperty* SetOuter = Property->GetOwner<FSetProperty>();
			const FMapProperty* MapOuter = Property->GetOwner<FMapProperty>();
			if (ArrayOuter || SetOuter || MapOuter)
			{
				bIsInContainer = true;
				break;
			}

			Outer = Outer->GetOwner<FProperty>();
		}
	}

	if (!bIsSparseProperty || bIsInContainer)
	{
		PropertyToTextHelper(OutString, InPropertyNode, Property, ObjectAddress.BaseAddress, PortFlags);
	}
	else
	{
		// TODO: once we're sure that these don't differ we should always use the call to PropertyToTextHelper
		UObject* Object = (UObject*) ObjectAddress.GetUObject();
		void* BaseAddress = Object->GetClass()->GetOrCreateSparseClassData();
		void* ValueAddress = Property->ContainerPtrToValuePtr<void>(BaseAddress);
		Property->ExportText_Direct(OutString, ValueAddress, ValueAddress, nullptr, PortFlags);

		FString Test;
		PropertyToTextHelper(Test, InPropertyNode, Property, (uint8*)ValueAddress, PortFlags);
		check(Test.Compare(OutString) == 0);
	}
}

void FPropertyTextUtilities::TextToPropertyHelper(const TCHAR* Buffer, const FPropertyNode* InPropertyNode, const FProperty* Property, uint8* ValueAddress, UObject* Object, EPropertyPortFlags PortFlags)
{
	if (InPropertyNode->GetArrayIndex() != INDEX_NONE || Property->ArrayDim == 1)
	{
		Property->ImportText(Buffer, ValueAddress, PortFlags, Object);
	}
	else
	{
		FArrayProperty::ImportTextInnerItem(Buffer, Property, ValueAddress, PortFlags, Object);
	}
}

void FPropertyTextUtilities::TextToPropertyHelper(const TCHAR* Buffer, const FPropertyNode* InPropertyNode, const FProperty* Property, const FObjectBaseAddress& ObjectAddress, EPropertyPortFlags PortFlags)
{
	uint8* BaseAddress = InPropertyNode ? InPropertyNode->GetValueBaseAddressFromObject(ObjectAddress.GetUObject()) : ObjectAddress.BaseAddress;
	TextToPropertyHelper(Buffer, InPropertyNode, Property, ObjectAddress.BaseAddress, ObjectAddress.GetUObject(), PortFlags);
}