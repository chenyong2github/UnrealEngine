// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Common/TargetPlatformBase.h"
#include "Interfaces/IPluginManager.h"
#include "CookedEditorPackageManager.h"



/**
 * Templated class for a target platform used to cook a cooked editor. It needs to inherit from a 
 * desktop platform's target platform such as TGenericWindowsTargetPlatform. See
 * CookedEditorPackageManager.h for some typedef'd standard base classes that can be used.
 * 
 * The majority of the functionality is performed in an instance of a ICookedEditorPackageManager subclass.
 * See that class for more information.
 */
template<typename Base>
class TCookedEditorTargetPlatform : public Base
{
public:

	TCookedEditorTargetPlatform()
	{
		PackageManager = ICookedEditorPackageManager::FactoryForTargetPlatform(this);
	}

	/** Allows for a custom target platform module to initialize this TargetPlatform with an existing
	 * PackageManager instead of going through the standard factory function */
	explicit TCookedEditorTargetPlatform(TUniquePtr<ICookedEditorPackageManager>&& ExistingManager)
	{
		PackageManager = MoveTemp(ExistingManager);
	}

	~TCookedEditorTargetPlatform()
	{
	}

	virtual FString PlatformName() const override
	{
		static FString CachedPlatformName = this->IniPlatformName() + TEXT("CookedEditor");
		return CachedPlatformName;
	}

	virtual FString CookingDeviceProfileName() const override
	{
		return Base::PlatformName();
	}

	/**
	 * If you override this to return false, you will have to stage uncooked assets to allow the editor to run properly
	 */
	virtual bool AllowsEditorObjects() const override
	{
		return true;
	}

	virtual bool AllowObject(const class UObject* Obj) const override
	{
		// probably don't need this check, but it can't hurt
		if (!AllowsEditorObjects())
		{
			return true;
		}

		return PackageManager->AllowObjectToBeCooked(Obj);
	}

	virtual void GetExtraPackagesToCook(TArray<FName>& PackageNames) const override
	{
		if (AllowsEditorObjects())
		{
			PackageManager->GatherAllPackages(PackageNames, this);
		}
	}

	virtual bool IsRunningPlatform() const override
	{
		return false;
	}

	virtual float GetVariantPriority() const override
	{
		// by returning -1, we will never use this variant when targeting host platform this class implements
		// (without this, cooking for Windows in the editor may choose this variant to cook for, which we never want)
		return -1.0f;
	}

	virtual void GetAllDevices(TArray<ITargetDevicePtr>& OutDevices) const override
	{
	}

	virtual ITargetDevicePtr GetDefaultDevice() const override
	{
		return nullptr;
	}


	virtual void GetReflectionCaptureFormats(TArray<FName>& OutFormats) const override
	{
		Base::GetReflectionCaptureFormats(OutFormats);

		// UMapBuildDataRegistry::PostLoad() assumes that editor always needs encoded data, so when cooking for the editor,
		// make sure that the EncodedHDR format is included
		OutFormats.AddUnique(TEXT("EncodedHDR"));
	}



protected:

	TUniquePtr<ICookedEditorPackageManager> PackageManager;

};
