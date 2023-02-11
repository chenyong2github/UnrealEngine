// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Factory which resets the FRemoteControlProperty to its default values.
 */
class IRCDefaultValueFactory : public TSharedFromThis<IRCDefaultValueFactory>
{
public:

	/** Virtual destructor */
	virtual ~IRCDefaultValueFactory() {}

	/**
	 * Returns true when the given object can be reset to its default value, false otherwise.
	 * @param InObject Reference to the exposed object.
	 */
	virtual bool CanResetToDefaultValue(UObject* InObject) const = 0;

	/**
	 * Performs actual data reset on the given remote object.
	 * @param InObject Reference to the exposed object.
	 * @param InProperty Reference to the exposed property.
	 */
	virtual void ResetToDefaultValue(UObject* InObject, FProperty* InProperty) = 0;
	
	/**
	 * Whether the factory support exposed entity.
	 * @param InObjectClass Reference to the exposed object class.
	 * @return true if the exposed object is supported by given factory
	 */
	virtual bool SupportsClass(const UClass* InObjectClass) const = 0;

	/**
	 * Whether the factory support property.
	 * @param InProperty Reference to the exposed property.
	 * @return true if the exposed property is supported by given factory
	 */
	virtual bool SupportsProperty(const FProperty* InProperty) const = 0;
};
