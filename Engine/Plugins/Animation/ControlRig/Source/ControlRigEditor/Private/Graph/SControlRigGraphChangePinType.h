// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SWidget.h"
#include "SGraphPin.h"
#include "RigVMModel/RigVMPin.h"
#include "IPropertyAccessEditor.h"
#include "ControlRigBlueprint.h"

DECLARE_DELEGATE_OneParam(FOnTypeSelected, TRigVMTypeIndex);

class SControlRigChangePinType : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SControlRigChangePinType)
	: _Types()
    , _Blueprint(nullptr)
	, _OnTypeSelected(nullptr)
	{}

		SLATE_ARGUMENT(TArray<TRigVMTypeIndex>, Types)
		SLATE_ARGUMENT(UControlRigBlueprint*, Blueprint)
		SLATE_EVENT(FOnTypeSelected, OnTypeSelected)

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

protected:

	static FText GetBindingText(const FRigVMTemplateArgumentType& InType);
	FText GetBindingText(URigVMPin* ModelPin) const;
	FText GetBindingText() const;
	const FSlateBrush* GetBindingImage() const;
	FLinearColor GetBindingColor() const;
	bool OnCanBindProperty(FProperty* InProperty) const;
	bool OnCanBindToClass(UClass* InClass) const;
	void OnAddBinding(FName InPropertyName, const TArray<FBindingChainElement>& InBindingChain);
	void FillPinTypeMenu( FMenuBuilder& MenuBuilder );
	void HandlePinTypeChanged(FRigVMTemplateArgumentType InType);

	TArray<TRigVMTypeIndex> Types;
	UControlRigBlueprint* Blueprint;
	FOnTypeSelected OnTypeSelected;
	FPropertyBindingWidgetArgs BindingArgs;
};
