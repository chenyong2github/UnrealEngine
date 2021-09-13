// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GeometryScript/GeometryScriptTypes.h"
#include "SceneUtilityFunctions.generated.h"

class UStaticMesh;
class UDynamicMesh;


USTRUCT(BlueprintType)
struct GEOMETRYSCRIPTINGCORE_API FGeometryScriptCopyMeshFromComponentOptions
{
	GENERATED_BODY()
public:
	UPROPERTY(BlueprintReadWrite, Category = Options)
	bool bWantNormals = true;

	UPROPERTY(BlueprintReadWrite, Category = Options)
	bool bWantTangents = true;

	UPROPERTY(BlueprintReadWrite, Category = Options)
	FGeometryScriptMeshReadLOD RequestedLOD = FGeometryScriptMeshReadLOD();
};


UCLASS()
class GEOMETRYSCRIPTINGCORE_API UGeometryScriptLibrary_SceneUtilityFunctions : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:

	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Scene", meta = (ExpandEnumAsExecs = "Outcome"))
	static UPARAM(DisplayName = "Dynamic Mesh") UDynamicMesh*
	CopyMeshFromComponent(
		USceneComponent* Component,
		UDynamicMesh* ToDynamicMesh,
		FGeometryScriptCopyMeshFromComponentOptions Options,
		bool bTransformToWorld,
		FTransform& LocalToWorld,
		TEnumAsByte<EGeometryScriptOutcomePins>& Outcome,
		UGeometryScriptDebug* Debug = nullptr);

};