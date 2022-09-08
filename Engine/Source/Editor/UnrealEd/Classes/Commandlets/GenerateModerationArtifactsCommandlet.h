// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "BaseIteratePackagesCommandlet.h"
#include "GenerateModerationArtifactsCommandlet.generated.h"

UNREALED_API DECLARE_LOG_CATEGORY_EXTERN(LogModerationArtifactsCommandlet, Log, All);




USTRUCT()
struct FModerationAsset
{
public:
	GENERATED_BODY();

	UObject* Object;

	UPROPERTY()
	FString FullPath;

	UPROPERTY()
	FString ClassName;

	UPROPERTY()
	TArray<FString> ModerationArtifactFilenames;

};


USTRUCT()
struct FModerationPackage
{
public:
	GENERATED_BODY();


	FModerationAsset* FindOrCreateModerationAsset(const UObject* InObject);

	UPackage* Package;

	UPROPERTY()
	FString PackagePath;

	UPROPERTY()
	FString PackageHash;

	UPROPERTY()
	TArray<struct FModerationAsset> Assets;


};


USTRUCT()
struct FModerationManifest
{
	GENERATED_BODY();


	FModerationPackage* FindOrCreateModerationPackage(UPackage* InPackage);

	FString CreateModerationAssetFileName(const UObject* Object, const FString& Extension);

	UPROPERTY()
		TArray<struct FModerationPackage> Packages;

};

UCLASS()
// Added UNREALED_API to expose this to the save packages test
// Base commandlet used to iterate packages provided on a commandline
// has callbacks for processing each package
class UNREALED_API UGenerateModerationArtifactsCommandlet : public UBaseIteratePackagesCommandlet
{
    GENERATED_UCLASS_BODY()

	virtual int32 InitializeParameters( const TArray<FString>& Tokens, TArray<FString>& MapPathNames ) override;

	// virtual void InitializePackageNames(const TArray<FString>& Tokens, TArray<FString>& MapPathNames, bool& bExplicitPackages) { }

	/** Loads and saves a single package */
	//virtual void LoadAndSaveOnePackage(const FString& Filename);

	/** Checks to see if a package should be skipped */
	// virtual bool ShouldSkipPackage(const FString& Filename);

	/** Deletes a single package */
	// virtual void DeleteOnePackage(const FString& Filename);

	/**
	 * Allow the commandlet to perform any operations on the export/import table of the package before all objects in the package are loaded.
	 *
	 * @param	PackageLinker	the linker for the package about to be loaded
	 * @param	bSavePackage	[in]	indicates whether the package is currently going to be saved
	 *							[out]	set to true to resave the package
	 */
	// virtual void PerformPreloadOperations( FLinkerLoad* PackageLinker, bool& bSavePackage );

	/**
	 * Allows the commandlet to perform any additional operations on the object before it is resaved.
	 *
	 * @param	Object			the object in the current package that is currently being processed
	 * @param	bSavePackage	[in]	indicates whether the package is currently going to be saved
	 *							[out]	set to true to resave the package
	 */
	virtual void PerformAdditionalOperations( class UObject* Object, bool& bSavePackage ) override;

	/**
	 * Allows the commandlet to perform any additional operations on the package before it is resaved.
	 *
	 * @param	Package			the package that is currently being processed
	 * @param	bSavePackage	[in]	indicates whether the package is currently going to be saved
	 *							[out]	set to true to resave the package
	 */
	virtual void PerformAdditionalOperations( class UPackage* Package, bool& bSavePackage ) override;

	/**
	* Allows the commandlet to perform any additional operations on the world before it is resaved.
	*
	* @param	World			the world that is currently being processed
	* @param	bSavePackage	[in]	indicates whether the package is currently going to be saved
	*							[out]	set to true to resave the package
	*/
	// virtual void PerformAdditionalOperations(class UWorld* World, bool& bSavePackage) { }

	virtual void PostProcessPackages() override;

private:
	void GatherLocalizationFromPackage(class UPackage* Package);
	void GatherFStringsFromObject(class UObject* Object);
	
	// void GenerateArtifact(class UDataTable* DataTable)
	void GenerateArtifact(class UTexture* Texture);
	void GenerateArtifact(class UStaticMeshComponent* StaticMesh);

	FString CreateOutputFileName(UObject* Object, const FString& Extension);

	FString OutputPath;

	FModerationManifest Manifest; 
	TMap<const UObject*, TWeakPtr<FModerationAsset>> ModerationAssetMap;
	TMap<const UPackage*, TWeakPtr<FModerationPackage>> ModerationPackageMap;

	TMap<FName, float> TimerStats;
};

