// Copyright Epic Games, Inc. All Rights Reserved.

#include "UObject/LinkerLoadImportBehavior.h"
#include "UObject/ObjectResource.h"
#include "UObject/Package.h"

#if UE_WITH_OBJECT_HANDLE_LATE_RESOLVE

namespace UE::LinkerLoad
{
PropertyImportBehaviorFunction* PropertyImportBehaviorCallback = nullptr;

void SetPropertyImportBehaviorCallback(PropertyImportBehaviorFunction* Function)
{
	void* PreviousValue = (void*)FPlatformAtomics::InterlockedExchangePtr((void**)&PropertyImportBehaviorCallback, (void*)Function);
	checkf((Function == nullptr) || (PreviousValue == nullptr), TEXT("LinkerLoad property import behavior callback set more than once.  This is not supported."))
}

void GetPropertyImportLoadBehavior(const FObjectImport& Import, const FLinkerLoad& LinkerLoad, EImportBehavior& OutBehavior)
{
	OutBehavior = EImportBehavior::Eager;

	if (Import.bImportSearchedFor)
	{
		// If it was something that's been searched for, we've already attempted a resolve, might as well use it
		return;
	}

	if (!LinkerLoad.IsImportLazyLoadEnabled() || !LinkerLoad.IsAllowingLazyLoading() || (LinkerLoad.LinkerRoot && LinkerLoad.LinkerRoot->HasAnyPackageFlags(PKG_PlayInEditor)))
	{
		return;
	}

	if (PropertyImportBehaviorCallback)
	{
		PropertyImportBehaviorCallback(Import, LinkerLoad, OutBehavior);
	}
}
}

#endif // UE_WITH_OBJECT_HANDLE_LATE_RESOLVE

