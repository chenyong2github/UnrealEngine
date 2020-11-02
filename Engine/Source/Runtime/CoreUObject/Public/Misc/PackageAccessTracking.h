// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Misc/Optional.h"
#include "Misc/PackageName.h"
#include "UObject/NameTypes.h"
#include "UObject/ObjectHandle.h"
#include "UObject/Package.h"

#define UE_WITH_PACKAGE_ACCESS_TRACKING UE_WITH_OBJECT_HANDLE_TRACKING

#if UE_WITH_PACKAGE_ACCESS_TRACKING
#define UE_TRACK_REFERENCING_PACKAGE_SCOPED(Package, OpName) PackageAccessTracking_Private::FPackageAccessRefScope ANONYMOUS_VARIABLE(PackageAccessTracker_)(Package, OpName);
#define UE_TRACK_REFERENCING_PACKAGE_DELAYED_SCOPED(TrackerName, OpName) TOptional<PackageAccessTracking_Private::FPackageAccessRefScope> TrackerName; FName TrackerName##_OpName(OpName);
#define UE_TRACK_REFERENCING_PACKAGE_DELAYED(TrackerName, Package) if (TrackerName) TrackerName->SetPackageName(Package->GetFName()); else TrackerName.Emplace(Package->GetFName(), TrackerName##_OpName);
#else
#define UE_TRACK_REFERENCING_PACKAGE_SCOPED(Package, OpName)
#define UE_TRACK_REFERENCING_PACKAGE_DELAYED_SCOPED(TrackerName, OpName)
#define UE_TRACK_REFERENCING_PACKAGE_DELAYED(TrackerName, Package)
#endif //UE_WITH_PACKAGE_ACCESS_TRACKING

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#if UE_WITH_PACKAGE_ACCESS_TRACKING
namespace PackageAccessTracking_Private
{
	class FPackageAccessRefScope
	{
	public:
		COREUOBJECT_API FPackageAccessRefScope(FName InPackageName, FName InOpName);
		COREUOBJECT_API FPackageAccessRefScope(const UPackage* InPackage, FName InOpName);

		COREUOBJECT_API ~FPackageAccessRefScope();

		FORCEINLINE FName GetPackageName() const { return PackageName; }
		FORCEINLINE void SetPackageName(FName InPackageName)
		{
			checkf(FPackageName::IsValidLongPackageName(InPackageName.ToString(), true), TEXT("Invalid package name: %s"), *InPackageName.ToString());
			PackageName = InPackageName;
		}
		FORCEINLINE FName GetOpName() const { return OpName; }
		FORCEINLINE FPackageAccessRefScope* GetOuter() const { return Outer; }

		static COREUOBJECT_API FPackageAccessRefScope* GetCurrentThreadScope();
	private:
		FName PackageName;
		FName OpName;
		FPackageAccessRefScope* Outer = nullptr;
		static thread_local FPackageAccessRefScope* CurrentThreadScope;
	};
};
#endif // UE_WITH_PACKAGE_ACCESS_TRACKING
