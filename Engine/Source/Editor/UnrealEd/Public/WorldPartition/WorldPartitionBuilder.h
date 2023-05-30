// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PackageSourceControlHelper.h"
#include "Engine/World.h"
#include "WorldPartitionBuilder.generated.h"

typedef UE::Math::TIntVector3<int64> FWorldBuilderCellCoord;

/**
 * Structure containing information about a World Partition Builder cell
 */
struct FCellInfo
{
	FCellInfo();

	/**
	 * Location of the cell, expressed inside World Partition Builder space
	 * (floor(Coordinate) / IterativeCellSize)
	 */
	FWorldBuilderCellCoord Location;

	/** Bounds of the cell */
	FBox Bounds;

	/** Whole space */
	FBox EditorBounds;

	/** The size of a cell used by the World Partition Builder */
	int32 IterativeCellSize;

	static UNREALED_API FWorldBuilderCellCoord GetCellCoord(const FVector& InPos, const int32 InCellSize);
	static UNREALED_API FWorldBuilderCellCoord GetCellCount(const FBox& InBounds, const int32 InCellSize);
};

UCLASS(Abstract, Config=Engine)
class UNREALED_API UWorldPartitionBuilder : public UObject
{
	GENERATED_UCLASS_BODY()

public:
	enum ELoadingMode
	{
		Custom,
		EntireWorld,
		IterativeCells,
		IterativeCells2D,
	};

	bool RunBuilder(UWorld* World);

	virtual bool RequiresCommandletRendering() const PURE_VIRTUAL(UWorldPartitionBuilder::RequiresCommandletRendering, return false;);
	virtual ELoadingMode GetLoadingMode() const PURE_VIRTUAL(UWorldPartitionBuilder::GetLoadingMode, return ELoadingMode::Custom;);
	
	bool Run(UWorld* World, FPackageSourceControlHelper& PackageHelper);

	virtual bool PreWorldInitialization(UWorld* World, FPackageSourceControlHelper& PackageHelper) { return true; }

	static bool SavePackages(const TArray<UPackage*>& Packages, FPackageSourceControlHelper& PackageHelper, bool bErrorsAsWarnings = false);
	static bool DeletePackages(const TArray<UPackage*>& Packages, FPackageSourceControlHelper& PackageHelper, bool bErrorsAsWarnings = false);
	static bool DeletePackages(const TArray<FString>& PackageNames, FPackageSourceControlHelper& PackageHelper, bool bErrorsAsWarnings = false);

	DECLARE_DELEGATE_RetVal_TwoParams(bool, FModifiedFilesHandler, const TArray<FString>&, const FString&); /*bool ModifiedFileHander(ModifiedFiles, ChangeDescription) */
	void SetModifiedFilesHandler(const FModifiedFilesHandler& ModifiedFilesHandler);

protected:
	/**
	 * Overridable method for derived classes to perform operations when world builder process starts.
	 * This is called before loading data (e.g. data layers, editor cells) and before calling `RunInternal`.
	 */
	virtual bool PreRun(UWorld* World, FPackageSourceControlHelper& PackageHelper) { return true; }

	virtual bool RunInternal(UWorld* World, const FCellInfo& InCellInfo, FPackageSourceControlHelper& PackageHelper) PURE_VIRTUAL(UWorldPartition::RunInternal, return false;);

	/**
	 * Overridable method for derived classes to perform operations when world builder process completes.
	 * This is called after loading all data (e.g. data layers, editor cells) and after calling `RunInternal` for all editor cells.
	 */
	virtual bool PostRun(UWorld* World, FPackageSourceControlHelper& PackageHelper, const bool bInRunSuccess) { return bInRunSuccess; }

	/**
	 * Overridable method for derived classes to perform operations when world builder has unloaded the world.
	 */
	virtual bool PostWorldTeardown(FPackageSourceControlHelper& PackageHelper) { return true; }

	/**
	 * When using EloadingMode::IterativeCells/IterativeCells2D return true to skip processing of cell.
	 */
	virtual bool ShouldSkipCell(const FWorldBuilderCellCoord& CellCoord) const { return false; }

	/** 
	 * Some builders may have the ability to process non partitioned worlds.
	 */
	virtual bool CanProcessNonPartitionedWorlds() const { return false; }

	bool OnFilesModified(const TArray<FString>& InModifiedFiles, const FString& InChangelistDescription) const;
	bool OnPackagesModified(const TArray<UPackage*>& InModifiedPackages, const FString& InChangelistDescription) const;

	/**
	 * Test if the builder was provided the given parameter.
	 * @param	Param	Parameter to look for.
	 * @return true if Parameter was provided, false otherwise.
	 */
	bool HasParam(const FString& Param) const
	{
		return FParse::Param(*Args, *Param);
	}

	/**
	 * Retrieve the given parameter's value.
	 * @param	Param	Parameter to look for.
	 * @param	Value	[out] Will contain the value if parameter is found, otherwise will be left unchanged.
	 * @return true if Parameter was provided, false otherwise.
	 */
	template <typename T>
	bool GetParamValue(const FString& Param, T& Value) const
	{
		return FParse::Value(*Args, *Param, Value);
	}

	/**
	 * Retrieve the arguments provided to the builder.
	 * @return the arguments provided to the builder.
	 */
	const FString& GetBuilderArgs() const
	{
		return Args;
	}

	UE_DEPRECATED(5.3, "Please use OnFilesModified")
	bool AutoSubmitFiles(const TArray<FString>& InModifiedFiles, const FString& InChangelistDescription) const;
	UE_DEPRECATED(5.3, "Please use OnPackagesModified")
	bool AutoSubmitPackages(const TArray<UPackage*>& InModifiedPackages, const FString& InChangelistDescription) const;

	virtual UWorld::InitializationValues GetWorldInitializationValues() const;

	UE_DEPRECATED(5.3, "You must override the version that takes a World parameter")
	virtual bool PreWorldInitialization(FPackageSourceControlHelper& PackageHelper) final { return true; }

	int32 IterativeCellSize = 102400;
	int32 IterativeCellOverlapSize = 0;
	FBox  IterativeWorldBounds;

	TSet<FName> DataLayerShortNames;
	TSet<FName> ExcludedDataLayerShortNames;
	bool bLoadNonDynamicDataLayers = true;
	bool bLoadInitiallyActiveDataLayers = true;

	FModifiedFilesHandler ModifiedFilesHandler;

private:
	void LoadDataLayers(UWorld* InWorld);

	friend struct FWorldPartitionBuilderArgsScope;
	static FString Args;
};

/**
 * Assign parameters to the World Partition builders for the lifetime of this scope.
 */
struct FWorldPartitionBuilderArgsScope
{
	FWorldPartitionBuilderArgsScope(const FString& InArgs)
	{
		check(UWorldPartitionBuilder::Args.IsEmpty());
		UWorldPartitionBuilder::Args = InArgs;
	}

	~FWorldPartitionBuilderArgsScope()
	{
		UWorldPartitionBuilder::Args.Empty();
	}
};
