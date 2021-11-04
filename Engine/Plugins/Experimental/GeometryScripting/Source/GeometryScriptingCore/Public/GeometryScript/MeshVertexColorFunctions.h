// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GeometryScript/GeometryScriptTypes.h"
#include "MeshVertexColorFunctions.generated.h"

class UDynamicMesh;



UCLASS(meta = (ScriptName = "GeometryScript_VertexColors"))
class GEOMETRYSCRIPTINGCORE_API UGeometryScriptLibrary_MeshVertexColorFunctions : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:

	UFUNCTION(BlueprintCallable, Category = "GeometryScript|VertexColor", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh*
	SetMeshConstantVertexColor(
		UDynamicMesh* TargetMesh,
		FLinearColor Color,
		FGeometryScriptColorFlags Flags,
		bool bClearExisting = false,
		UGeometryScriptDebug* Debug = nullptr);

	UFUNCTION(BlueprintCallable, Category = "GeometryScript|VertexColor", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh*
	SetMeshPerVertexColors(
		UDynamicMesh* TargetMesh,
		FGeometryScriptColorList VertexColorList,
		UGeometryScriptDebug* Debug = nullptr);


};

