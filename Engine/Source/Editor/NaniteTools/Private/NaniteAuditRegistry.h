// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UMaterial;
class UMaterialInterface;
class UStaticMesh;
class UInstancedStaticMesh;

struct FNaniteMaterialError
{
	UMaterial* ErrorMaterial;
	FString ErrorMessage;
};

struct FNaniteAuditRecord : TSharedFromThis<FNaniteAuditRecord>
{
	TWeakObjectPtr<UStaticMesh> StaticMesh = nullptr;
	TArray<TWeakObjectPtr<UStaticMeshComponent>> StaticMeshComponents;
	TArray<FNaniteMaterialError> MaterialErrors;
	uint32 InstanceCount = 0;
	uint32 TriangleCount = 0;
	uint32 LODCount = 0;
};

class FNaniteAuditRegistry
{
public:
	FNaniteAuditRegistry();

	void PerformAudit(uint32 TriangleThreshold);

	inline TArray<TSharedPtr<FNaniteAuditRecord>>& GetErrorRecords()
	{
		return ErrorRecords;
	}

	inline TArray<TSharedPtr<FNaniteAuditRecord>>& GetOptimizeRecords()
	{
		return OptimizeRecords;
	}

private:
	TArray<TSharedPtr<FNaniteAuditRecord>> ErrorRecords;
	TArray<TSharedPtr<FNaniteAuditRecord>> OptimizeRecords;
};