// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "PropertyBag.h"

#include "RCVirtualPropertyContainer.generated.h"

class URCVirtualPropertyInContainer;
class URCVirtualPropertyBase;
class URemoteControlPreset;
class FStructOnScope;

/**
 * Container for more then one Virtual Property
 */
UCLASS(BlueprintType)
class REMOTECONTROL_API URCVirtualPropertyContainerBase : public UObject
{
	GENERATED_BODY()

	friend class URCVirtualPropertyInContainer;
	
public:
	/**
	 * Add property to this container.
	 * That will add property to Property Bag and Create Remote Control Virtual Property
	 *
	 * @param InPropertyName				Property Name to add
	 * @param InPropertyClass				Class of the Virtual Property 
	 * @param InValueType					Property Type
	 * @param InValueTypeObject				Property Type object if exists
	 *
	 * @return Virtual Property Object
	 */
	virtual URCVirtualPropertyInContainer* AddProperty(const FName& InPropertyName, TSubclassOf<URCVirtualPropertyInContainer> InPropertyClass, const EPropertyBagPropertyType InValueType, UObject* InValueTypeObject = nullptr);

	/**
	 * Duplicates property from give Property.
	 * That will add property to Property Bag and Create Remote Control Virtual Property
	 *
	 * @param InPropertyName				Name of the property
	 * @param InSourceProperty				Source FProperty for duplication
	 * @param InPropertyClass				Class of the Virtual Property 
	 *
	 * @return Virtual Property Object
	 */
	virtual URCVirtualPropertyInContainer* DuplicateProperty(const FName& InPropertyName, const FProperty* InSourceProperty, TSubclassOf<URCVirtualPropertyInContainer> InPropertyClass);

	/**
	 * Duplicates property from give Property and copy the property value
	 * That will add property to Property Bag and Create Remote Control Virtual Property
	 *
	 * @param InPropertyName				Name of the property
	 * @param InSourceProperty				Source FProperty for duplication
	 * @param InPropertyClass				Class of the Virtual Property
	 * @param InSourceContainerPtr			Pointer to source container
	 *
	 * @return Virtual Property Object
	 */
	virtual URCVirtualPropertyInContainer* DuplicatePropertyWithCopy(const FName& InPropertyName, const FProperty* InSourceProperty, const uint8* InSourceContainerPtr, TSubclassOf<URCVirtualPropertyInContainer> InPropertyClass);

	/**
	 * Removes a property from the container by name if it exists.
	 */
	virtual bool RemoveProperty(const FName& InPropertyName);

	/** Resets the property bag instance to empty and remove Virtual Properties */
	virtual void Reset();

	/**
	 * Returns virtual property by specified name.
	 */ 
	virtual URCVirtualPropertyBase* GetVirtualProperty(const FName InPropertyName) const;

	/**
	 * Returns virtual property by unique Id.
	 */
	URCVirtualPropertyBase* GetVirtualProperty(const FGuid& InId) const;

	/**
	 * Returns virtual property by user-friendly display name (Controller Name)
	 */
	virtual URCVirtualPropertyBase* GetVirtualPropertyByDisplayName(const FName InDisplayName) const;

	/**
	 * Returns number of virtual properties.
	 */ 
	int32 GetNumVirtualProperties() const;

	/**
	 * Creates new Struct on Scope for this Property Bag UStruct and Memory
	 */
	TSharedPtr<FStructOnScope> CreateStructOnScope() const;

	/**
	 * Generates unique name for the property for specified property container
	 */
	static FName GenerateUniquePropertyName(const FName& InPropertyName, const EPropertyBagPropertyType InValueType, UObject* InValueTypeObject, const URCVirtualPropertyContainerBase* InContainer);

#if WITH_EDITOR
	/** Delegate when object changed */
	virtual void OnModifyPropertyValue(const FPropertyChangedEvent& PropertyChangedEvent);
#endif

protected:
	/** Holds bag of properties. */
	UPROPERTY()
	FInstancedPropertyBag Bag;

public:
	/** Set of the virtual properties */
	UPROPERTY()
	TSet<TObjectPtr<URCVirtualPropertyBase>> VirtualProperties;
	
	/** Pointer to Remote Control Preset */
	UPROPERTY()
	TWeakObjectPtr<URemoteControlPreset> PresetWeakPtr;
};


