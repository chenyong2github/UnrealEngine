// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GeometryScript/GeometryScriptTypes.h"
#include "MeshSubdivideFunctions.generated.h"

class UDynamicMesh;



USTRUCT(BlueprintType, meta = (DisplayName = "PN Tessellate Options"))
struct GEOMETRYSCRIPTINGCORE_API FGeometryScriptPNTessellateOptions
{
	GENERATED_BODY()
public:
	UPROPERTY(BlueprintReadWrite, Category = Options)
	bool bRecomputeNormals = true;
};



UCLASS(meta = (ScriptName = "GeometryScript_MeshSubdivide"))
class GEOMETRYSCRIPTINGCORE_API UGeometryScriptLibrary_MeshSubdivideFunctions : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:

	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Subdivide", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	ApplyRecursivePNTessellation(
		UDynamicMesh* TargetMesh,
		FGeometryScriptPNTessellateOptions Options,
		int NumIterations = 3,
		UGeometryScriptDebug* Debug = nullptr );

};