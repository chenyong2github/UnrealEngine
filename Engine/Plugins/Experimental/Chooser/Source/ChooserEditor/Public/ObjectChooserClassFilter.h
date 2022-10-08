// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "ClassViewerFilter.h"
#include "IObjectChooser.h"

namespace UE::ChooserEditor
{
	// Filter class for things which implement a particular interface
	class FInterfaceClassFilter : public IClassViewerFilter
	{
	public:
		FInterfaceClassFilter(UClass* InInterfaceType) : InterfaceType(InInterfaceType)  { };
		virtual ~FInterfaceClassFilter() override {};
		
		virtual bool IsClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const UClass* InClass, TSharedRef< class FClassViewerFilterFuncs > InFilterFuncs ) override
		{
			return (InClass->ImplementsInterface(InterfaceType) && InClass->HasAnyClassFlags(CLASS_Abstract) == false);
		}

		virtual bool IsUnloadedClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const TSharedRef< const class IUnloadedBlueprintData > InUnloadedClassData, TSharedRef< class FClassViewerFilterFuncs > InFilterFuncs) override
		{
			return false;
		}
	private:
		UClass* InterfaceType;
	};
		
}