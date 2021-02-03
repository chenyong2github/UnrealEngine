// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

class FPropertyWidgetGenerator
{
public:

	// Returns true if the property is a struct, map, array, set, etc
	static bool IsPropertyContainer(const FProperty* Property);

	/**
	 * Creates a widget displaying a property value similar to the details panel view.
	 * @param OwningObject The top-level UObject which owns the Property
	 * @return A SWidget representing the value of the property (with no name information) 
	 */
	static TSharedPtr<SWidget> GenerateWidgetForUClassProperty(FProperty* Property, UObject* OwningObject);

	/**
	 * Creates a widget displaying a property value similar to the details panel view.
	 * @param OwningObject The top-level UObject which owns the Property
	 * @param ArrayIndex The count into an FProperty's ArrayDim. Useful for getting a single array member.
	 * @param OuterPtr Pass in 'Property->ContainerPtrToValuePtr<void>(Outer)' where Property is the immediate containing parent property and Outer is the pointer to its value.
	 * @return A SWidget representing the value of the property (with no name information)
	 */
	static TSharedPtr<SWidget> GenerateWidgetForPropertyInContainer(
		FProperty* Property, UObject* OwningObject, void* OuterPtr, int32 ArrayIndex = 0);

	/**
	 * Get a UClass Property value as string.
	 * @param OwningObjectA The top-level UObject which owns the Property
	 */
	static bool AreUClassPropertyValuesEqual(FProperty* Property, UObject* OwningObjectA, UObject* OwningObjectB);

	/**
	 * Get the Property value as string if the property is within another container property (struct, array, set, map, etc.)
	 * @param ArrayIndex The count into an FProperty's ArrayDim. Useful for getting a single array member.
	 * @param OuterPtrA Pass in 'Property->ContainerPtrToValuePtr<void>(Outer)' where Property is the immediate containing parent property and Outer is the pointer to its value.
	 */
	static bool ArePropertyValuesInContainerEqual(FProperty* Property, void* OuterPtrA, void* OuterPtrB, int32 ArrayIndex = 0);

private:
	static TSharedRef<SWidget> MakeComboBoxWithSelection(const FString& InString, const FText& ToolTipText);

	static TSharedPtr<SWidget> GenerateWidgetForProperty_Internal(
		FProperty* Property, UObject* OwningObject, void* ValuePtr);

	static bool ArePropertyValuesEqual_Internal(FProperty* Property, void* ValuePtrA, void* ValuePtrB);
};
