// Copyright Epic Games, Inc. All Rights Reserved.

#include "PackageTracker.h"

#include "CookPackageData.h"
#include "UObject/Package.h"
#include "UObject/UObjectIterator.h"

namespace UE
{
namespace Cook
{

	void FThreadSafeUnsolicitedPackagesList::AddCookedPackage(const FFilePlatformRequest& PlatformRequest)
	{
		FScopeLock S(&SyncObject);
		CookedPackages.Add(PlatformRequest);
	}

	void FThreadSafeUnsolicitedPackagesList::GetPackagesForPlatformAndRemove(const ITargetPlatform* Platform, TArray<FName>& PackageNames)
	{
		FScopeLock _(&SyncObject);

		for (int I = CookedPackages.Num() - 1; I >= 0; --I)
		{
			FFilePlatformRequest& Request = CookedPackages[I];

			if (Request.GetPlatforms().Contains(Platform))
			{
				// remove the platform
				Request.RemovePlatform(Platform);
				PackageNames.Emplace(Request.GetFilename());

				if (Request.GetPlatforms().Num() == 0)
				{
					CookedPackages.RemoveAt(I);
				}
			}
		}
	}

	void FThreadSafeUnsolicitedPackagesList::Empty()
	{
		FScopeLock _(&SyncObject);
		CookedPackages.Empty();
	}


	FPackageTracker::FPackageTracker(FPackageDatas& InPackageDatas)
		:PackageDatas(InPackageDatas)
	{
		for (TObjectIterator<UPackage> It; It; ++It)
		{
			UPackage* Package = *It;

			if (Package->GetOuter() == nullptr)
			{
				LoadedPackages.Add(Package);
			}
		}

		NewPackages = LoadedPackages;

		GUObjectArray.AddUObjectDeleteListener(this);
		GUObjectArray.AddUObjectCreateListener(this);
	}

	FPackageTracker::~FPackageTracker()
	{
		GUObjectArray.RemoveUObjectDeleteListener(this);
		GUObjectArray.RemoveUObjectCreateListener(this);
	}

	TArray<UPackage*> FPackageTracker::GetNewPackages()
	{
		return MoveTemp(NewPackages);
	}

	void FPackageTracker::NotifyUObjectCreated(const class UObjectBase* Object, int32 Index)
	{
		if (Object->GetClass() == UPackage::StaticClass())
		{
			auto Package = const_cast<UPackage*>(static_cast<const UPackage*>(Object));

			if (Package->GetOuter() == nullptr)
			{
				LoadedPackages.Add(Package);
				NewPackages.Add(Package);
			}
		}
	}

	void FPackageTracker::NotifyUObjectDeleted(const class UObjectBase* Object, int32 Index)
	{
		if (Object->GetClass() == UPackage::StaticClass())
		{
			auto Package = const_cast<UPackage*>(static_cast<const UPackage*>(Object));

			LoadedPackages.Remove(Package);
			NewPackages.Remove(Package);
			PostLoadFixupPackages.Remove(Package);
		}
	}

	void FPackageTracker::OnUObjectArrayShutdown()
	{
		GUObjectArray.RemoveUObjectDeleteListener(this);
		GUObjectArray.RemoveUObjectCreateListener(this);
	}

}
}