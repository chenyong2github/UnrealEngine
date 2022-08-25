// Copyright Epic Games, Inc. All Rights Reserved.

#include "UObject/LinkerLoadImportBehavior.h"
#include "UObject/LinkerLoad.h"
#include "UObject/ObjectResource.h"
#include "UObject/Package.h"
#include "Misc/CommandLine.h"

#if UE_WITH_OBJECT_HANDLE_LATE_RESOLVE

namespace UE::LinkerLoad
{

/// @brief Finds LoadBehavior meta data recursively
/// @return Eager by default in not found
static EImportBehavior FindLoadBehavior(const UClass& Class)
{
	static const FName Name_LoadBehavior("LoadBehavior");
	if (const FString* LoadBehaviorMeta = Class.FindMetaData(Name_LoadBehavior))
	{
		if (*LoadBehaviorMeta == "LazyOnDemand")
		{
			return EImportBehavior::LazyOnDemand;
		}
		return EImportBehavior::Eager;
	}
	else
	{
		const UClass* Super = Class.GetSuperClass();
		if (Super != nullptr)
		{
			return FindLoadBehavior(*Super);
		}
		return EImportBehavior::Eager;
	}
}

EImportBehavior GetPropertyImportLoadBehavior(const FObjectImport& Import, const FLinkerLoad& LinkerLoad)
{
	if (Import.bImportSearchedFor)
	{
		// If it was something that's been searched for, we've already attempted a resolve, might as well use it
		return EImportBehavior::Eager;
	}

	if (!LinkerLoad.IsImportLazyLoadEnabled() || !LinkerLoad.IsAllowingLazyLoading() || (LinkerLoad.LinkerRoot && LinkerLoad.LinkerRoot->HasAnyPackageFlags(PKG_PlayInEditor)))
	{
		return EImportBehavior::Eager;
	}

	// Attempt to get the meta from the referenced class.  This only looks in already loaded classes.  May need to resolve the class in the future.
	static const bool bDefaultLoadBehaviorTest = FParse::Param(FCommandLine::Get(), TEXT("DefaultLoadBehaviorTest"));
	if (bDefaultLoadBehaviorTest)
	{
		return EImportBehavior::LazyOnDemand;
	}

	//Packages can't have meta data because they 
	if (Import.ClassName == TEXT("Package"))
	{
		return EImportBehavior::LazyOnDemand;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(LinkerLoader::GetPropertyImportLoadBehavior);

	UObject* ClassPackage = FindObjectFast<UPackage>(nullptr, Import.ClassPackage);
	const UClass* FindClass = ClassPackage ? FindObjectFast<const UClass>(ClassPackage, Import.ClassName) : nullptr;
	if (FindClass != nullptr)
	{
		return FindLoadBehavior(*FindClass);
	}
	return EImportBehavior::Eager;
}
}

#endif // UE_WITH_OBJECT_HANDLE_LATE_RESOLVE

