// Copyright Epic Games, Inc. All Rights Reserved.

#include "Engine/DataTable.h"
#include "GLTFExporterTestTargetTableRow.generated.h"

class UStaticMesh;

USTRUCT(BlueprintType)
struct FGLTFExporterTestTargetTableRow : public FTableRowBase
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	UStaticMesh* TargetStaticMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString ExpectedOutput;
};
