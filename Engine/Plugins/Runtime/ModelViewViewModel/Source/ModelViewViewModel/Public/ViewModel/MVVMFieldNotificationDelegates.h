// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FieldNotification/FieldId.h"
#include "FieldNotification/FieldMulticastDelegate.h"
#include "FieldNotification/IFieldValueChanged.h"
#include "Net/Core/PushModel/PushModel.h"

namespace UE::MVVM
{

/** Basic implementation of the INotifyFieldValueChanged implementation. */
class MODELVIEWVIEWMODEL_API FMVVMFieldNotificationDelegates
{
public:
	FDelegateHandle AddFieldValueChangedDelegate(UObject* Owner, UE::FieldNotification::FFieldId InFieldId, INotifyFieldValueChanged::FFieldValueChangedDelegate InNewDelegate);
	void AddFieldValueChangedDelegate(UObject* Owner, UE::FieldNotification::FFieldId FieldId, const FFieldValueChangedDynamicDelegate& Delegate);
	bool RemoveFieldValueChangedDelegate(UObject* Owner, UE::FieldNotification::FFieldId InFieldId, FDelegateHandle InHandle);
	bool RemoveFieldValueChangedDelegate(UObject* Owner, UE::FieldNotification::FFieldId FieldId, const FFieldValueChangedDynamicDelegate& Delegate);
	int32 RemoveAllFieldValueChangedDelegates(UObject* Owner, const void* InUserObject);
	int32 RemoveAllFieldValueChangedDelegates(UObject* Owner, UE::FieldNotification::FFieldId InFieldId, const void* InUserObject);
	void BroadcastFieldValueChanged(UObject* Owner, UE::FieldNotification::FFieldId InFieldId);

private:
	UE::FieldNotification::FFieldMulticastDelegate Delegates;
	TBitArray<> EnabledFieldNotifications;
};

} //namespace