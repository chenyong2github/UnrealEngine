// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

class SWidget;
class ITableRow;

/** 
 * Interface for any class that lays out details for a specific class
 */
class IDetailRootObjectCustomization : public TSharedFromThis<IDetailRootObjectCustomization>
{
public:
	enum EExpansionArrowUsage
	{
		Custom,
		Default,
		None,
	};

	virtual ~IDetailRootObjectCustomization() {}

	/** 
	 * Called when the details panel wants to display an object header widget for a given object
	 *
	 * @param	InRootObject	The object whose header is being customized
 	 */
	virtual TSharedPtr<SWidget> CustomizeObjectHeader(const UObject* InRootObject) { return nullptr; }

	/** 
	 * Called when the details panel wants to display an object header widget for a given object
	 *
	 * @param	InRootObject	The object whose header is being customized
	 * @param	InTableRow		The ITableRow object (table views to talk to their rows) that will use the current IDetailRootObjectCustomization element.  This may be null if the customization is not being shown in a table view
 	 */
	virtual TSharedPtr<SWidget> CustomizeObjectHeader(const UObject* InRootObject, const TSharedPtr<ITableRow>& InTableRow)
	{
		return CustomizeObjectHeader(InRootObject);
	}

	/**
	 * Whether or not the object and all of its children should be visible in the details panel
	 */
	virtual bool IsObjectVisible(const UObject* InRootObject) const = 0;

	/**
	 * Whether or not the object should have a header displayed or just show the children directly
	 *
	 * @return true if the this customization should be displayed or false to just show the children directly
	 */
	virtual bool ShouldDisplayHeader(const UObject* InRootObject) const = 0;

	/**
	 * Gets the setup for expansion arrows in this customization
	 */
	virtual EExpansionArrowUsage GetExpansionArrowUsage() const
	{ 
		// Note: Returns none for backwards compatibility
		return EExpansionArrowUsage::None; 
	}
};
