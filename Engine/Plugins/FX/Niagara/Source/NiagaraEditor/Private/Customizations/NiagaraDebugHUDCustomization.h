// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "IPropertyTypeCustomization.h"
#include "NiagaraCommon.h"
#include "EdGraph/EdGraphSchema.h"
#include "Layout/Visibility.h"

class FDetailWidgetRow;
class IPropertyHandle;
class IPropertyHandleArray;
enum class ECheckBoxState : uint8;

class FNiagaraDebugHUDVariableCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance()
	{
		return MakeShared<FNiagaraDebugHUDVariableCustomization>();
	}

	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils);

	virtual void CustomizeChildren( TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils )
	{
	}

	ECheckBoxState IsEnabled() const;
	void SetEnabled(ECheckBoxState NewState);

	FText GetText() const;
	void SetText(const FText& NewText, ETextCommit::Type CommitInfo);
	bool IsTextEditable() const;

	TSharedPtr<IPropertyHandle> EnabledPropertyHandle;
	TSharedPtr<IPropertyHandle> NamePropertyHandle;
};
