// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "WorldPartition/WorldPartitionBuilder.h"
#include "WorldPartitionHLODsBuilder.generated.h"

UCLASS()
class UWorldPartitionHLODsBuilder : public UWorldPartitionBuilder
{
	GENERATED_UCLASS_BODY()

public:
	// UWorldPartitionBuilder interface begin
	virtual bool RequiresCommandletRendering() const override;
	virtual bool RequiresEntireWorldLoading() const override { return false; }
	virtual bool PreWorldInitialization(FPackageSourceControlHelper& PackageHelper) override;
	virtual bool Run(UWorld* World, FPackageSourceControlHelper& PackageHelper) override;
	// UWorldPartitionBuilder interface end

protected:
	bool IsDistributedBuild() const { return bDistributedBuild; }
	bool IsUsingBuildManifest() const { return !BuildManifest.IsEmpty(); }
	bool ValidateParams() const;

	bool SetupHLODActors(bool bCreateOnly);
	bool BuildHLODActors();
	bool DeleteHLODActors();
	bool SubmitHLODActors();

	bool GenerateBuildManifest(TMap<FString, int32>& FilesToBuilderMap) const;
	bool GetHLODActorsToBuild(TArray<FGuid>& HLODActorsToBuild) const;

	TArray<TArray<FGuid>> GetHLODWorldloads(int32 NumWorkloads) const;
	bool ValidateWorkload(const TArray<FGuid>& Workload) const;

	bool CopyFilesToWorkingDir(const FString& TargetDir, const TArray<FString>& FilesToCopy, TArray<FString>& BuildProducts);
	bool CopyFilesFromWorkingDir(const FString& SourceDir);

private:
	class UWorldPartition* WorldPartition;
	class FSourceControlHelper* SourceControlHelper;

	// Options --
	bool bSetupHLODs;
	bool bBuildHLODs;
	bool bDeleteHLODs;
	bool bSubmitHLODs;
	bool bDistributedBuild;
	FString BuildManifest;
	int32 BuilderIdx;
	int32 BuilderCount;

	TSet<FString> ModifiedFiles;

	const FString DistributedBuildWorkingDir;
	const FString DistributedBuildManifest;
};