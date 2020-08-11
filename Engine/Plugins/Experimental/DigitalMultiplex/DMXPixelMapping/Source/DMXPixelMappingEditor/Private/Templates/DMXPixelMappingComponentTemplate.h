// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DMXPixelMappingRuntimeCommon.h"
#include "Templates/SubclassOf.h"
#include "UObject/WeakObjectPtr.h"

class UDMXPixelMappingBaseComponent;
class UClass;

/**
 * The Component template represents a Component or a set of Components to create and spawn into the Component tree.
 */
class FDMXPixelMappingComponentTemplate
	: public TSharedFromThis<FDMXPixelMappingComponentTemplate>
{
public:
	/** Constructor */
	explicit FDMXPixelMappingComponentTemplate(TSubclassOf<UDMXPixelMappingBaseComponent> InComponentClass);

	/** Virtual Destructor */
	virtual ~FDMXPixelMappingComponentTemplate() {}

	/** Gets the category for the Component */
	FText GetCategory() const;

	/** Creates an instance of the Component for the tree. */
	UDMXPixelMappingBaseComponent* Create(UDMXPixelMappingBaseComponent* InParentComponent);

public:
	/** The name of the Component template. */
	FText Name;

private:
	/** The Component class that will be created by this template */
	TWeakObjectPtr<UClass> ComponentClass;
};
