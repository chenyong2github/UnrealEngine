// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "PropertyEditorDelegates.h"
#include "PropertyHandle.h"

FPropertyAndParent::FPropertyAndParent(const TSharedRef<IPropertyHandle>& InPropertyHandle, const TArray<TWeakObjectPtr<UObject>>& InObjects) :
	Property(*InPropertyHandle->GetProperty()),
	Objects(InObjects)
{
	TSharedPtr<IPropertyHandle> ParentHandle = InPropertyHandle->GetParentHandle();
	while (ParentHandle.IsValid())
	{
		const FProperty* ParentProperty = ParentHandle->GetProperty();
		if (ParentProperty != nullptr)
		{
			ParentProperties.Add(ParentProperty);
		}

		ParentHandle = ParentHandle->GetParentHandle();
	}
}