// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "RawMesh.h"

#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Object.h"

#include "DatasmithMeshUObject.generated.h"

USTRUCT()
struct FDatasmithMeshSourceModel
{
	GENERATED_BODY()

public:
	void SerializeBulkData(FArchive& Ar, UObject* Owner);

	FRawMeshBulkData RawMeshBulkData;
};

UCLASS()
class DATASMITHCORE_API UDatasmithMesh : public UObject
{
	GENERATED_BODY()

public:
	static const TCHAR* GetFileExtension() { return TEXT("udsmesh"); }

	virtual void Serialize(FArchive& Ar) override;

	UPROPERTY()
	FString MeshName;

	UPROPERTY()
	bool bIsCollisionMesh;

	UPROPERTY()
	TArray< FDatasmithMeshSourceModel > SourceModels;
};
