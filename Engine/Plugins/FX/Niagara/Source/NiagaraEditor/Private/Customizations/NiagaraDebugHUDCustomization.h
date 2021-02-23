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
class FNiagaraDebugger;

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

//////////////////////////////////////////////////////////////////////////

class FNiagaraDebugHUDSettingsDetailsCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance()
	{
		return MakeShared<FNiagaraDebugHUDSettingsDetailsCustomization>();
	}

	/** IPropertyTypeCustomization interface begin */
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, class IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
	/** IPropertyTypeCustomization interface end */
private:

	/** State */
	TSharedPtr<IPropertyHandle> PropertyHandle;
};

//////////////////////////////////////////////////////////////////////////
