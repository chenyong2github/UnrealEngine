// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/PackageAccessTracking.h"

#if UE_WITH_PACKAGE_ACCESS_TRACKING
thread_local PackageAccessTracking_Private::FPackageAccessRefScope* PackageAccessTracking_Private::FPackageAccessRefScope::CurrentThreadScope = nullptr;

namespace PackageAccessTracking_Private
{

FPackageAccessRefScope::FPackageAccessRefScope(FName InPackageName, FName InOpName)
	: PackageName(InPackageName)
	, OpName(InOpName)
{
	checkf(FPackageName::IsValidLongPackageName(InPackageName.ToString(), true), TEXT("Invalid package name: %s"), *InPackageName.ToString());
	Outer = CurrentThreadScope;
	CurrentThreadScope = this;
}

FPackageAccessRefScope::FPackageAccessRefScope(const UPackage* InPackage, FName InOpName)
	: FPackageAccessRefScope(InPackage->GetFName(), InOpName)
{
}

FPackageAccessRefScope::~FPackageAccessRefScope()
{
	check(CurrentThreadScope == this);
	CurrentThreadScope = Outer;
}

FPackageAccessRefScope* FPackageAccessRefScope::GetCurrentThreadScope()
{
	return CurrentThreadScope;
}

} // PackageAccessTracking_Private
#endif // UE_WITH_OBJECT_HANDLE_TRACKING
