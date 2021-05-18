// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "WorldPartition/WorldPartitionBuilder.h"
#include "WorldPartitionHLODsBuilder.generated.h"

struct FHLODModifiedFiles
{
	enum EFileOperation
	{
		FileAdded,
		FileEdited,
		FileDeleted,
		NumFileOperations
	};

	void Add(EFileOperation FileOp, const FString& File)
	{
		Files[FileOp].Add(File);
	}

	const TSet<FString>& Get(EFileOperation FileOp) const
	{
		return Files[FileOp];
	}

	void Append(EFileOperation FileOp, const TArray<FString>& InFiles)
	{
		Files[FileOp].Append(InFiles);
	}

	void Append(const FHLODModifiedFiles& Other)
	{
		Files[EFileOperation::FileAdded].Append(Other.Files[EFileOperation::FileAdded]);
		Files[EFileOperation::FileEdited].Append(Other.Files[EFileOperation::FileEdited]);
		Files[EFileOperation::FileDeleted].Append(Other.Files[EFileOperation::FileDeleted]);
	}

	void Empty()
	{
		Files[EFileOperation::FileAdded].Empty();
		Files[EFileOperation::FileEdited].Empty();
		Files[EFileOperation::FileDeleted].Empty();
	}

	TArray<FString> GetAllFiles() const
	{
		TArray<FString> AllFiles;
		AllFiles.Append(Files[EFileOperation::FileAdded].Array());
		AllFiles.Append(Files[EFileOperation::FileEdited].Array());
		AllFiles.Append(Files[EFileOperation::FileDeleted].Array());
		return AllFiles;
	}

private:
	TSet<FString> Files[NumFileOperations];
};

UCLASS()
class UWorldPartitionHLODsBuilder : public UWorldPartitionBuilder
{
	GENERATED_UCLASS_BODY()
public:
	// UWorldPartitionBuilder interface begin
	virtual bool RequiresCommandletRendering() const override;
	virtual ELoadingMode GetLoadingMode() const override { return ELoadingMode::Custom; }
	virtual bool PreWorldInitialization(FPackageSourceControlHelper& PackageHelper) override;
protected:
	virtual bool RunInternal(UWorld* World, const FBox& Bounds, FPackageSourceControlHelper& PackageHelper) override;
	// UWorldPartitionBuilder interface end

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

	bool CopyFilesToWorkingDir(const FString& TargetDir, const FHLODModifiedFiles& ModifiedFiles, TArray<FString>& BuildProducts);
	bool CopyFilesFromWorkingDir(const FString& SourceDir);

private:
	class UWorldPartition* WorldPartition;
	class FSourceControlHelper* SourceControlHelper;

	// Options --
	bool bSetupHLODs;
	bool bBuildHLODs;
	bool bDeleteHLODs;
	bool bSubmitHLODs;
	bool bSingleBuildStep;
	bool bAutoSubmit;
	bool bDistributedBuild;
	FString BuildManifest;
	int32 BuilderIdx;
	int32 BuilderCount;

	const FString DistributedBuildWorkingDir;
	const FString DistributedBuildManifest;
	
	FHLODModifiedFiles ModifiedFiles;
};