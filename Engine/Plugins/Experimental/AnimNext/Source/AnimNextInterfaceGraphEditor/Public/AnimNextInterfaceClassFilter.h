// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Param/ParamTypeHandle.h"
#include "ClassViewerFilter.h"
#include "IAnimNextInterface.h"

namespace UE::AnimNext::InterfaceGraphEditor
{
	// Filter class for things which implement IAnimNextInterface
	class FAnimNextInterfaceClassFilter : public IClassViewerFilter
	{
	public:
		FAnimNextInterfaceClassFilter(FParamTypeHandle InTypeHandle) : TypeHandle(InTypeHandle) {};
		virtual ~FAnimNextInterfaceClassFilter() override {};
	
		virtual bool IsClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const UClass* InClass, TSharedRef<FClassViewerFilterFuncs> InFilterFuncs) override
		{
			if (InClass->ImplementsInterface(UAnimNextInterface::StaticClass()))
			{
				// pass filter if the class has "" return type (for wrappers)
				// pass all classes if the requested typename is ""
				// otherwise fail if the TypeNames don't match
				// for Object type, check type hierarchy. (todo)
			
				IAnimNextInterface* Object = static_cast<IAnimNextInterface*>(InClass->GetDefaultObject()->GetInterfaceAddress(UAnimNextInterface::StaticClass()));
				if (Object->GetReturnTypeHandle() == TypeHandle || !Object->GetReturnTypeHandle().IsValid() || !TypeHandle.IsValid())
				{
					// @TODO: deal with inheritance directionality here
					return true;
				}
			}

			return false;
		}

		virtual bool IsUnloadedClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const TSharedRef<const IUnloadedBlueprintData> InUnloadedClassData, TSharedRef<FClassViewerFilterFuncs> InFilterFuncs) override
		{
			return false;
		}
	private:
		FParamTypeHandle TypeHandle;
	};
}