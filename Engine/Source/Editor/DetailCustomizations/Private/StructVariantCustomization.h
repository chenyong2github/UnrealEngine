// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "IPropertyTypeCustomization.h"

class FStructOnScope;
struct FStructVariant;

/**
 * Implements a details view customization for the FStructVariant structure.
 */
class FStructVariantCustomization : public IPropertyTypeCustomization
{
public:
	/**
	 * Creates an instance of this class.
	 *
	 * @return The new instance.
	 */
	static TSharedRef<IPropertyTypeCustomization> MakeInstance()
	{
		return MakeShared<FStructVariantCustomization>();
	}

	~FStructVariantCustomization();

public:
	//~ IPropertyTypeCustomization
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> InStructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> InStructPropertyHandle, IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;

private:
	/** Get/Set the struct type for the FStructVariant */
	const UScriptStruct* GetSelectedStructType() const;
	void SetSelectedStructType(const UScriptStruct* InStructType);

	/** Sync the current state of the editable struct instance to/from the source instance(s) */
	void SyncEditableInstanceFromVariants(bool* OutStructMismatch = nullptr);
	void SyncEditableInstanceToVariants(bool* OutStructMismatch = nullptr);

	/** Pre/Post change notifications for struct value changes */
	void OnStructValuePreChange();
	void OnStructValuePostChange();

	/** Enumerate the array of FStructVariant instances this customization is currently editing */
	void ForEachStructVariant(TFunctionRef<bool(FStructVariant* /*Variant*/, const int32 /*VariantIndex*/, const int32 /*NumVariants*/)> Callback);
	void ForEachConstStructVariant(TFunctionRef<bool(const FStructVariant* /*Variant*/, const int32 /*VariantIndex*/, const int32 /*NumVariants*/)> Callback) const;

	/** Utils for the property editor being used */
	TSharedPtr<IPropertyUtilities> PropertyUtilities;

	/** Handle to the struct property being edited */
	TSharedPtr<IPropertyHandle> StructPropertyHandle;

	/** Struct instance that is being edited; this is a copy of the variant struct data to avoid lifetime issues when the underlying variant is updated/deleted */
	TSharedPtr<FStructOnScope> StructInstanceData;

	/** Handle for the periodic call to SyncEditableInstanceFromVariants */
	FTSTicker::FDelegateHandle SyncEditableInstanceFromVariantsTickHandle;
};
