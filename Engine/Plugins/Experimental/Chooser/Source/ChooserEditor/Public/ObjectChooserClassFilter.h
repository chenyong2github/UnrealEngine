// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "ClassViewerFilter.h"
#include "IObjectChooser.h"

namespace UE::ChooserEditor
{
	// Filter class for things which implement IChooserColumn
	class FObjectChooserClassFilter : public IClassViewerFilter
	{
	public:
		FObjectChooserClassFilter(FName InTypeName, const UScriptStruct* InStructType) : TypeName(InTypeName), StructType(InStructType) { };
		virtual ~FObjectChooserClassFilter() override {};
	
		virtual bool IsClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const UClass* InClass, TSharedRef< class FClassViewerFilterFuncs > InFilterFuncs ) override
		{
			if (InClass->ImplementsInterface(UObjectChooser::StaticClass()))
			{
				// for now, just return true if the class implements IObjectChooser
				return true;
			}

			return false;
		}

		virtual bool IsUnloadedClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const TSharedRef< const class IUnloadedBlueprintData > InUnloadedClassData, TSharedRef< class FClassViewerFilterFuncs > InFilterFuncs) override
		{
			return false;
		}
	private:
		FName TypeName;
		const UScriptStruct* StructType;
	};
}