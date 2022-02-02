// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EDITOR

#include "CoreMinimal.h"

#include "Containers/List.h"
#include "Misc/Optional.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

/**
 * This class is used for packages that need to be split into multiple runtime packages.
 * It provides the instructions to the cooker for how to split the package.
 */
class ICookPackageSplitter
{
public:
	virtual ~ICookPackageSplitter() {}

	/** Data sent to the cooker to describe each desired generated package */
	struct FGeneratedPackage
	{
		FString RelativePath;		// Generated package relative to the Parent/_Generated_ root
		TArray<FName> Dependencies; // LongPackageNames that the generated package references
		/* GetGenerateList must specify true if the package will be a map (.umap, contains a UWorld or ULevel), else false */
		void SetCreateAsMap(bool bInCreateAsMap) { bCreateAsMap = bInCreateAsMap; }
		const TOptional<bool>& GetCreateAsMap() const { return bCreateAsMap; }
	private:
		TOptional<bool> bCreateAsMap;
	};
	
	/** 
	 * Return whether the CookPackageSplitter subclass should handle the given SplitDataClass instance. 
	 * Note that this is a static function referenced by macros, not part of the virtual api.
	 */
	static bool ShouldSplit(UObject* SplitData) { return false; }
	
	/**
	 * If true, this splitter forces the Generator package objects it needs to remain referenced, and the cooker
	 * should expect them to still be in memory after a garbage collect so long as the splitter is alive.
	 */
	virtual bool UseInternalReferenceToAvoidGarbageCollect() { return false; }

	/** Return the list of packages to generate. */
	virtual TArray<FGeneratedPackage> GetGenerateList(const UPackage* OwnerPackage, const UObject* OwnerObject) = 0;

	struct FGeneratedPackageForPopulate
	{
		/** RelativePath returned from GetGenerateList */
		FString RelativePath;
		/**
		 * Non-null, constructed package. May have been previously passed into PreSaveGeneratedPackage,
		 * or may be completely empty if that previous package was garbage collected before this call.
		 */
		UPackage* Package = nullptr;
		/** *GetCreateAsMap returned from GetGenerateList. The package filename extension has already been set based on this. */
		bool bCreatedAsMap = false;
		/** Whether the Package was populated by PreSave or constructed empty */
		bool bPopulatedByPreSave = false;
	};
	/**
	 * Try to populate a generated package.
	 *
	 * Receive an empty UPackage generated from an element in GetGenerateList and populate it
	 * After returning, the given package will be queued for saving into the TargetDomain
	 * Note that TryPopulatePackage will not be called for every package on
	 * an iterative cook; it will only be called for the packages with changed dependencies. 
	 *
	 * @param OwnerPackage				The parent package being split
	 * @param OwnerObject				The SplitDataClass instance that this CookPackageSplitter instance was created for
	 * @param GeneratedPackage			Pointer and information about the package to populate
	 * @param bWasOwnerReloaded			True iff the OwnerPackage was garbage collected and reloaded since the previous call
	 *                                  to GetGenerateList or TryPopulatePackage
	 * @return							True if successfully populates,  false on error (this will cause a cook error).
	 */
	virtual bool TryPopulatePackage(const UPackage* OwnerPackage, const UObject* OwnerObject,
		const FGeneratedPackageForPopulate& GeneratedPackage, bool bWasOwnerReloaded) = 0;

	struct FGeneratedPackageForPreSave
	{
		/** RelativePath returned from GetGenerateList */
		FString RelativePath;
		/**
		 * Non-null, constructed package. May have been previously passed into PreSaveGeneratedPackage,
		 * or may be completely empty if that previous package was garbage collected before this call.
		 */
		UPackage* Package = nullptr;
		/** *GetCreateAsMap returned from GetGenerateList. The package filename extension has already been set based on this. */
		bool bCreatedAsMap = false;
	};
	/**
	 * Called before saving the parent generator package, which itself occurs before TryPopulatePackage is called on the generated packages.
	 * Make any required adjustments to the parent package before it is saved into the target domain.
	 */
	virtual void PreSaveGeneratorPackage(UPackage* OwnerPackage, UObject* OwnerObject, const TArray<FGeneratedPackageForPreSave>& PlaceholderPackages) {}

	/**
	 * Called after saving the parent generator package. Undo any required adjustments to the parent package that
	 * were made in PreSaveGeneratorPackage, so that the package is once again ready for use in the editor or in
	 * future GetGenerateList or TryPopulatePackage calls
	 */
	virtual void PostSaveGeneratorPackage(UPackage* OwnerPackage, UObject* OwnerObject) {}
};

namespace UE
{
namespace Cook
{
namespace Private
{

/** Interface for internal use only (used by REGISTER_COOKPACKAGE_SPLITTER to register an ICookPackageSplitter for a class) */
class UNREALED_API FRegisteredCookPackageSplitter
{
public:
	FRegisteredCookPackageSplitter();
	virtual ~FRegisteredCookPackageSplitter();

	virtual UClass* GetSplitDataClass() const = 0;
	virtual bool ShouldSplitPackage(UObject* Object) const = 0;
	virtual ICookPackageSplitter* CreateInstance(UObject* Object) const = 0;

	static void ForEach(TFunctionRef<void(FRegisteredCookPackageSplitter*)> Func);

private:
	
	static TLinkedList<FRegisteredCookPackageSplitter*>*& GetRegisteredList();
	TLinkedList<FRegisteredCookPackageSplitter*> GlobalListLink;
};

}
}
}

/**
 * Used to Register an ICookPackageSplitter for a class
 *
 * Example usage:
 *
 * class FMyCookPackageSplitter : public ICookPackageSplitter { ... }
 * REGISTER_COOKPACKAGE_SPLITTER(FMyCookPackageSplitter, UMySplitDataClass);
 */
#define REGISTER_COOKPACKAGE_SPLITTER(SplitterClass, SplitDataClass) \
class PREPROCESSOR_JOIN(PREPROCESSOR_JOIN(SplitterClass, SplitDataClass), _Register) : public UE::Cook::Private::FRegisteredCookPackageSplitter \
{ \
	virtual UClass* GetSplitDataClass() const override { return SplitDataClass::StaticClass(); } \
	virtual bool ShouldSplitPackage(UObject* Object) const override { return SplitterClass::ShouldSplit(Object); } \
	virtual ICookPackageSplitter* CreateInstance(UObject* SplitData) const override { return new SplitterClass(); } \
}; \
namespace PREPROCESSOR_JOIN(SplitterClass, SplitDataClass) \
{ \
	static PREPROCESSOR_JOIN(PREPROCESSOR_JOIN(SplitterClass, SplitDataClass), _Register) DefaultObject; \
}

#endif