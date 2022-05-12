// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "FieldNotification/FieldId.h"
#include "FieldNotification/FieldNotificationDeclaration.h"
#include "FieldNotification/FieldMulticastDelegate.h"
#include "FieldNotification/IFieldValueChanged.h"
#include "Types/MVVMAvailableBinding.h"
#include "Types/MVVMBindingName.h"

#include "MVVMViewModelBase.generated.h"

/** After a field value changed. Execute all the notification (replication and broadcast). */
#define UE_MVVM_NOTIFY_FIELD_VALUE_CHANGED(MemberName) \
	NotifyFieldValudChanged(ThisClass::FFieldNotificationClassDescriptor::MemberName)

/** After a field value changed. Broadcast the event (doesn't execute the replication code). */
#define UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(MemberName) \
	BroadcastFieldValueChanged(ThisClass::FFieldNotificationClassDescriptor::MemberName)

/** Set the new value and notify if the property value changed. */
#define UE_MVVM_SET_PROPERTY_VALUE(MemberName, NewValue) \
	SetPropertyValue(MemberName, NewValue, ThisClass::FFieldNotificationClassDescriptor::MemberName)


/** Base class for MVVM viewmodel. */
UCLASS(Blueprintable, Abstract, DisplayName="MVVM ViewModel")
class MODELVIEWVIEWMODEL_API UMVVMViewModelBase : public UObject, public INotifyFieldValueChanged
{
	GENERATED_BODY()

public:
	struct MODELVIEWVIEWMODEL_API FFieldNotificationClassDescriptor : public ::UE::FieldNotification::IClassDescriptor
	{
		virtual void ForEachField(const UClass* Class, TFunctionRef<bool(UE::FieldNotification::FFieldId FielId)> Callback) const override;
	};

public:
	//~ Begin INotifyFieldValueChanged Interface
	virtual FDelegateHandle AddFieldValueChangedDelegate(UE::FieldNotification::FFieldId InFieldId, FFieldValueChangedDelegate InNewDelegate) override final;
	virtual bool RemoveFieldValueChangedDelegate(UE::FieldNotification::FFieldId InFieldId, FDelegateHandle InHandle) override final;
	virtual int32 RemoveAllFieldValueChangedDelegates(const void* InUserObject) override final;
	virtual int32 RemoveAllFieldValueChangedDelegates(UE::FieldNotification::FFieldId InFieldId, const void* InUserObject) override final;
	virtual const UE::FieldNotification::IClassDescriptor& GetFieldNotificationDescriptor() const override;
	//~ End INotifyFieldValueChanged Interface


protected:
	/** Execute all the notification (replication and broadcast). */
	void NotifyFieldValudChanged(UE::FieldNotification::FFieldId InFieldId);

protected:
	UFUNCTION(BlueprintCallable, Category="FieldNotify", meta=(DisplayName="Broadcast Field Value Changed", ScriptName="BroadcastFieldValueChanged", BlueprintInternalUseOnly="true"))
	void K2_BroadcastFieldValueChanged(FFieldNotificationId FieldId);

	/** Broadcast the event (doesn't execute the replication code). */
	void BroadcastFieldValueChanged(UE::FieldNotification::FFieldId InFieldId);

protected:
	UFUNCTION(BlueprintCallable, CustomThunk, Category="Viewmodel", meta=(CustomStructureParam="OldValue,NewValue", ScriptName="SetPropertyValue", BlueprintInternalUseOnly="true"))
	bool K2_SetPropertyValue(UPARAM(ref) const int32& OldValue, UPARAM(ref) const int32& NewValue);

	/** Set the new value and notify if the property value changed. */
	template<typename T>
	bool SetPropertyValue(T& Value, const T& NewValue, UE::FieldNotification::FFieldId FieldId)
	{
		if (Value == NewValue)
		{
			return false;
		}

		Value = NewValue;
		NotifyFieldValudChanged(FieldId);
		return true;
	}

private:
	DECLARE_FUNCTION(execK2_SetPropertyValue);

private:
	UE::FieldNotification::FFieldMulticastDelegate Delegates;
	TBitArray<> EnabledFieldNotifications;
};
