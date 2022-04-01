// Copyright Epic Games, Inc. All Rights Reserved.

#include "MVVMViewModelBase.h"

#include "Bindings/MVVMBindingHelper.h"


FDelegateHandle UMVVMViewModelBase::AddFieldValueChangedDelegate(UE::FieldNotification::FFieldId InFieldId, FFieldValueChangedDelegate InNewDelegate)
{
	FDelegateHandle Result;
	if (InFieldId.IsValid())
	{
		Result = Delegates.Add(this, InFieldId, MoveTemp(InNewDelegate));
		if (Result.IsValid())
		{
			EnabledFieldNotifications.PadToNum(InFieldId.GetIndex() + 1, false);
			EnabledFieldNotifications[InFieldId.GetIndex()] = true;
		}
	}
	return Result;
}


bool UMVVMViewModelBase::RemoveFieldValueChangedDelegate(UE::FieldNotification::FFieldId InFieldId, FDelegateHandle InHandle)
{
	bool bResult = false;
	if (InFieldId.IsValid() && InHandle.IsValid()  && EnabledFieldNotifications.IsValidIndex(InFieldId.GetIndex()) && EnabledFieldNotifications[InFieldId.GetIndex()])
	{
		UE::FieldNotification::FFieldMulticastDelegate::FRemoveFromResult RemoveResult = Delegates.RemoveFrom(this, InFieldId, InHandle);
		bResult = RemoveResult.bRemoved;
		EnabledFieldNotifications[InFieldId.GetIndex()] = RemoveResult.bHasOtherBoundDelegates;
	}
	return bResult;
}


int32 UMVVMViewModelBase::RemoveAllFieldValueChangedDelegates(const void* InUserObject)
{
	int32 bResult = 0;
	if (InUserObject)
	{
		UE::FieldNotification::FFieldMulticastDelegate::FRemoveAllResult RemoveResult = Delegates.RemoveAll(this, InUserObject);
		bResult = RemoveResult.RemoveCount;
		EnabledFieldNotifications = RemoveResult.HasFields;
	}
	return bResult;
}


int32 UMVVMViewModelBase::RemoveAllFieldValueChangedDelegates(UE::FieldNotification::FFieldId InFieldId, const void* InUserObject)
{
	int32 bResult = 0;
	if (InUserObject)
	{
		UE::FieldNotification::FFieldMulticastDelegate::FRemoveAllResult RemoveResult = Delegates.RemoveAll(this, InFieldId, InUserObject);
		bResult = RemoveResult.RemoveCount;
		EnabledFieldNotifications = RemoveResult.HasFields;
	}
	return bResult;
}


const UE::FieldNotification::IClassDescriptor& UMVVMViewModelBase::GetFieldNotificationDescriptor() const
{
	static FFieldNotificationClassDescriptor Local;
	return Local;
}


void UMVVMViewModelBase::BindingFieldValueChanged(UE::FieldNotification::FFieldId InFieldId)
{
	if (InFieldId.IsValid() && EnabledFieldNotifications.IsValidIndex(InFieldId.GetIndex()) && EnabledFieldNotifications[InFieldId.GetIndex()])
	{
		Delegates.Broadcast(this, InFieldId);
	}
}


void UMVVMViewModelBase::BindingValueChanged(FMVVMBindingName ViewModelPropertyName)
{
	UE::FieldNotification::FFieldId FieldId = GetFieldNotificationDescriptor().GetField(GetClass(), ViewModelPropertyName.ToName());
	BindingFieldValueChanged(FieldId);
}

