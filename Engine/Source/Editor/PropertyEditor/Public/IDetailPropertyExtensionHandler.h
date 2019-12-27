// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SWidget.h"

class IPropertyHandle;
class IDetailLayoutBuilder;

class IDetailPropertyExtensionHandler
{
public:
	virtual ~IDetailPropertyExtensionHandler(){ }

	virtual bool IsPropertyExtendable(const UClass* InObjectClass, const class IPropertyHandle& PropertyHandle) const = 0;

	UE_DEPRECATED(4.24, "Please use the overload that takes a IDetailLayoutBuilder")
	virtual TSharedRef<SWidget> GenerateExtensionWidget(const UClass* InObjectClass, TSharedPtr<IPropertyHandle> PropertyHandle) 
	{ 
		return SNullWidget::NullWidget;
	}

	virtual TSharedRef<SWidget> GenerateExtensionWidget(const IDetailLayoutBuilder& InDetailBuilder, const UClass* InObjectClass, TSharedPtr<IPropertyHandle> PropertyHandle)
	{
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
		// Call old deprecated path for back-compat
		return GenerateExtensionWidget(InObjectClass, PropertyHandle);
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}
};
