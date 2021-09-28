// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SNullWidget.h"
#include "IDetailPropertyExtensionHandler.h"

class IPropertyHandle;
class IDetailLayoutBuilder;
struct FStateTreeEditorPropertyPath;
class IPropertyAccessEditor;
struct FStateTreeEditorPropertyPath;

namespace UE { namespace StateTree { namespace PropertyBinding {

	/**
	 * Get nearest Outer that implements IStateTreeEditorPropertyBindingsOwner.
	 * @param InObject Object where to start the search.
	 */
	UObject* FindEditorBindingsOwner(UObject* InObject);

	/**
	 * Returns property path for a specific property. It will look for nearest outer struct that contains property named "ID",
	 * and build property path using that struct as root.
	 * @param InPropertyHandle Handle to the property to find path for.
	 * @param OutPath Resulting property path.
	 */
	void GetOuterStructPropertyPath(TSharedPtr<IPropertyHandle> InPropertyHandle, FStateTreeEditorPropertyPath& OutPath);
			
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnStateTreeBindingChanged, const FStateTreeEditorPropertyPath& /*SourcePath*/, const FStateTreeEditorPropertyPath& /*TargetPath*/);
	extern STATETREEEDITORMODULE_API FOnStateTreeBindingChanged OnStateTreeBindingChanged;

} } } // UE::StateTree::PropertyBinding

class FStateTreeBindingExtension : public IDetailPropertyExtensionHandler
{
public:
	// IDetailPropertyExtensionHandler interface
	virtual bool IsPropertyExtendable(const UClass* InObjectClass, const IPropertyHandle& PropertyHandle) const override;
	virtual void ExtendWidgetRow(FDetailWidgetRow& InWidgetRow, const IDetailLayoutBuilder& InDetailBuilder, const UClass* InObjectClass, TSharedPtr<IPropertyHandle> PropertyHandle) override;
};
