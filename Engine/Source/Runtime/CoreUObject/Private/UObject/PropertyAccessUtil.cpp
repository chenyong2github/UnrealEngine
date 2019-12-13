// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "UObject/PropertyAccessUtil.h"
#include "UObject/Object.h"
#include "UObject/Class.h"

namespace PropertyAccessUtil
{

EPropertyAccessResultFlags GetPropertyValue_Object(const FProperty* InProp, const UObject* InObject, void* InDestValue, const int32 InArrayIndex)
{
	check(InObject->IsA(InProp->GetOwnerClass()));
	return GetPropertyValue_InContainer(InProp, InObject, InDestValue, InArrayIndex);
}

EPropertyAccessResultFlags GetPropertyValue_InContainer(const FProperty* InProp, const void* InContainerData, void* InDestValue, const int32 InArrayIndex)
{
	if (InArrayIndex == INDEX_NONE || InProp->ArrayDim == 1)
	{
		const void* SrcValue = InProp->ContainerPtrToValuePtr<void>(InContainerData);
		return GetPropertyValue_DirectComplete(InProp, SrcValue, InDestValue);
	}
	else
	{
		check(InArrayIndex < InProp->ArrayDim);
		const void* SrcValue = InProp->ContainerPtrToValuePtr<void>(InContainerData, InArrayIndex);
		return GetPropertyValue_DirectSingle(InProp, SrcValue, InDestValue);
	}
}

EPropertyAccessResultFlags GetPropertyValue_DirectSingle(const FProperty* InProp, const void* InSrcValue, void* InDestValue)
{
	EPropertyAccessResultFlags Result = CanGetPropertyValue(InProp);
	if (Result != EPropertyAccessResultFlags::Success)
	{
		return Result;
	}

	return GetPropertyValue([InProp, InSrcValue, InDestValue]()
	{
		InProp->CopySingleValue(InDestValue, InSrcValue);
		return true;
	});
}

EPropertyAccessResultFlags GetPropertyValue_DirectComplete(const FProperty* InProp, const void* InSrcValue, void* InDestValue)
{
	EPropertyAccessResultFlags Result = CanGetPropertyValue(InProp);
	if (Result != EPropertyAccessResultFlags::Success)
	{
		return Result;
	}

	return GetPropertyValue([InProp, InSrcValue, InDestValue]()
	{
		InProp->CopyCompleteValue(InDestValue, InSrcValue);
		return true;
	});
}

EPropertyAccessResultFlags GetPropertyValue(const FPropertyAccessGetFunc& InGetFunc)
{
	const bool bGetResult = InGetFunc();
	return bGetResult
		? EPropertyAccessResultFlags::Success
		: EPropertyAccessResultFlags::ConversionFailed;
}

EPropertyAccessResultFlags CanGetPropertyValue(const FProperty* InProp)
{
	if (!InProp->HasAnyPropertyFlags(CPF_Edit | CPF_BlueprintVisible | CPF_BlueprintAssignable))
	{
		return EPropertyAccessResultFlags::PermissionDenied | EPropertyAccessResultFlags::AccessProtected;
	}

	return EPropertyAccessResultFlags::Success;
}

EPropertyAccessResultFlags SetPropertyValue_Object(const FProperty* InProp, UObject* InObject, const void* InSrcValue, const int32 InArrayIndex, const uint64 InReadOnlyFlags)
{
	check(InObject->IsA(InProp->GetOwnerClass()));
	return SetPropertyValue_InContainer(InProp, InObject, InSrcValue, InArrayIndex, InReadOnlyFlags, IsObjectTemplate(InObject), [InProp, InObject]()
	{
		return BuildBasicChangeNotify(InProp, InObject);
	});
}

EPropertyAccessResultFlags SetPropertyValue_InContainer(const FProperty* InProp, void* InContainerData, const void* InSrcValue, const int32 InArrayIndex, const uint64 InReadOnlyFlags, const bool InOwnerIsTemplate, const FPropertyAccessBuildChangeNotifyFunc& InBuildChangeNotifyFunc)
{
	if (InArrayIndex == INDEX_NONE || InProp->ArrayDim == 1)
	{
		void* DestValue = InProp->ContainerPtrToValuePtr<void>(InContainerData);
		return SetPropertyValue_DirectComplete(InProp, InSrcValue, DestValue, InReadOnlyFlags, InOwnerIsTemplate, InBuildChangeNotifyFunc);
	}
	else
	{
		check(InArrayIndex < InProp->ArrayDim);
		void* DestValue = InProp->ContainerPtrToValuePtr<void>(InContainerData, InArrayIndex);
		return SetPropertyValue_DirectSingle(InProp, InSrcValue, DestValue, InReadOnlyFlags, InOwnerIsTemplate, InBuildChangeNotifyFunc);
	}
}

EPropertyAccessResultFlags SetPropertyValue_DirectSingle(const FProperty* InProp, const void* InSrcValue, void* InDestValue, const uint64 InReadOnlyFlags, const bool InOwnerIsTemplate, const FPropertyAccessBuildChangeNotifyFunc& InBuildChangeNotifyFunc)
{
	EPropertyAccessResultFlags Result = CanSetPropertyValue(InProp, InReadOnlyFlags, InOwnerIsTemplate);
	if (Result != EPropertyAccessResultFlags::Success)
	{
		return Result;
	}

	return SetPropertyValue([InProp, InSrcValue, InDestValue](const FPropertyAccessChangeNotify* InChangeNotify)
	{
		if (!InProp->Identical(InSrcValue, InDestValue))
		{
			EmitPreChangeNotify(InChangeNotify);
			InProp->CopySingleValue(InDestValue, InSrcValue);
			EmitPostChangeNotify(InChangeNotify);
		}
		return true;
	}, InBuildChangeNotifyFunc);
}

EPropertyAccessResultFlags SetPropertyValue_DirectComplete(const FProperty* InProp, const void* InSrcValue, void* InDestValue, const uint64 InReadOnlyFlags, const bool InOwnerIsTemplate, const FPropertyAccessBuildChangeNotifyFunc& InBuildChangeNotifyFunc)
{
	EPropertyAccessResultFlags Result = CanSetPropertyValue(InProp, InReadOnlyFlags, InOwnerIsTemplate);
	if (Result != EPropertyAccessResultFlags::Success)
	{
		return Result;
	}

	return SetPropertyValue([InProp, InSrcValue, InDestValue](const FPropertyAccessChangeNotify* InChangeNotify)
	{
		auto IdenticalComplete = [InProp, InSrcValue, InDestValue]()
		{
			bool bIsIdentical = true;
			for (int32 Idx = 0; Idx < InProp->ArrayDim && bIsIdentical; ++Idx)
			{
				const void* SrcElemValue = static_cast<const uint8*>(InSrcValue) + (InProp->ElementSize * Idx);
				const void* DestElemValue = static_cast<const uint8*>(InDestValue) + (InProp->ElementSize * Idx);
				bIsIdentical &= InProp->Identical(SrcElemValue, DestElemValue);
			}
			return bIsIdentical;
		};

		if (!IdenticalComplete())
		{
			EmitPreChangeNotify(InChangeNotify);
			InProp->CopyCompleteValue(InDestValue, InSrcValue);
			EmitPostChangeNotify(InChangeNotify);
		}
		return true;
	}, InBuildChangeNotifyFunc);
}

EPropertyAccessResultFlags SetPropertyValue(const FPropertyAccessSetFunc& InSetFunc, const FPropertyAccessBuildChangeNotifyFunc& InBuildChangeNotifyFunc)
{
	TUniquePtr<FPropertyAccessChangeNotify> ChangeNotify = InBuildChangeNotifyFunc();
	const bool bSetResult = InSetFunc(ChangeNotify.Get());
	return bSetResult
		? EPropertyAccessResultFlags::Success
		: EPropertyAccessResultFlags::ConversionFailed;
}

EPropertyAccessResultFlags CanSetPropertyValue(const FProperty* InProp, const uint64 InReadOnlyFlags, const bool InOwnerIsTemplate)
{
	if (!InProp->HasAnyPropertyFlags(CPF_Edit | CPF_BlueprintVisible | CPF_BlueprintAssignable))
	{
		return EPropertyAccessResultFlags::PermissionDenied | EPropertyAccessResultFlags::AccessProtected;
	}

	if (InOwnerIsTemplate)
	{
		if (InProp->HasAnyPropertyFlags(CPF_DisableEditOnTemplate))
		{
			return EPropertyAccessResultFlags::PermissionDenied | EPropertyAccessResultFlags::CannotEditTemplate;
		}
	}
	else
	{
		if (InProp->HasAnyPropertyFlags(CPF_DisableEditOnInstance))
		{
			return EPropertyAccessResultFlags::PermissionDenied | EPropertyAccessResultFlags::CannotEditInstance;
		}
	}

	if (InProp->HasAnyPropertyFlags(InReadOnlyFlags))
	{
		return EPropertyAccessResultFlags::PermissionDenied | EPropertyAccessResultFlags::ReadOnly;
	}

	return EPropertyAccessResultFlags::Success;
}

void EmitPreChangeNotify(const FPropertyAccessChangeNotify* InChangeNotify)
{
#if WITH_EDITOR
	if (InChangeNotify)
	{
		check(InChangeNotify->ChangedObject);

		// Notify that a change is about to occur
		InChangeNotify->ChangedObject->PreEditChange(const_cast<FEditPropertyChain&>(InChangeNotify->ChangedPropertyChain));
	}
#endif
}

void EmitPostChangeNotify(const FPropertyAccessChangeNotify* InChangeNotify)
{
#if WITH_EDITOR
	if (InChangeNotify)
	{
		check(InChangeNotify->ChangedObject);

		// Notify that the change has occurred
		FPropertyChangedEvent PropertyEvent(InChangeNotify->ChangedPropertyChain.GetActiveNode()->GetValue(), InChangeNotify->ChangeType, MakeArrayView(&InChangeNotify->ChangedObject, 1));
		PropertyEvent.SetActiveMemberProperty(InChangeNotify->ChangedPropertyChain.GetActiveMemberNode()->GetValue());
		FPropertyChangedChainEvent PropertyChainEvent(const_cast<FEditPropertyChain&>(InChangeNotify->ChangedPropertyChain), PropertyEvent);
		InChangeNotify->ChangedObject->PostEditChangeChainProperty(PropertyChainEvent);
	}
#endif
}

TUniquePtr<FPropertyAccessChangeNotify> BuildBasicChangeNotify(const FProperty* InProp, const UObject* InObject)
{
	check(InObject->IsA(InProp->GetOwnerClass()));
#if WITH_EDITOR
	TUniquePtr<FPropertyAccessChangeNotify> ChangeNotify = MakeUnique<FPropertyAccessChangeNotify>();
	ChangeNotify->ChangedObject = const_cast<UObject*>(InObject);
	ChangeNotify->ChangedPropertyChain.AddHead(const_cast<FProperty*>(InProp));
	ChangeNotify->ChangedPropertyChain.SetActivePropertyNode(const_cast<FProperty*>(InProp));
	ChangeNotify->ChangedPropertyChain.SetActiveMemberPropertyNode(const_cast<FProperty*>(InProp));
	return ChangeNotify;
#else
	return nullptr;
#endif
}

bool IsObjectTemplate(const UObject* InObject)
{
	return InObject->IsTemplate() || InObject->IsAsset();
}

FProperty* FindPropertyByName(const FName InPropName, const UStruct* InStruct)
{
	FProperty* Prop = InStruct->FindPropertyByName(InPropName);

	if (!Prop)
	{
		const FName NewPropName = FProperty::FindRedirectedPropertyName(const_cast<UStruct*>(InStruct), InPropName);
		if (!NewPropName.IsNone())
		{
			Prop = InStruct->FindPropertyByName(NewPropName);
		}
	}

	if (!Prop)
	{
		Prop = InStruct->CustomFindProperty(InPropName);
	}

	return Prop;
}

}
