// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GeometryBase.h"
#include "GeometryScriptTypes.generated.h"

PREDECLARE_GEOMETRY(class FDynamicMesh3);
PREDECLARE_GEOMETRY(template<typename MeshType> class TMeshAABBTree3);
PREDECLARE_GEOMETRY(template<typename MeshType> class TFastWindingTree);
PREDECLARE_GEOMETRY(typedef TMeshAABBTree3<FDynamicMesh3> FDynamicMeshAABBTree3);


UENUM(BlueprintType)
enum EGeometryScriptOutcomePins
{
	Failure,
	Success
};

UENUM(BlueprintType)
enum EGeometryScriptSearchOutcomePins
{
	Found,
	NotFound
};

UENUM(BlueprintType)
enum EGeometryScriptContainmentOutcomePins
{
	Inside, 
	Outside
};


UENUM(BlueprintType)
enum class EGeometryScriptLODType : uint8
{
	MaxAvailable,
	HiResSourceModel,
	SourceModel,
	RenderData
};


USTRUCT(BlueprintType)
struct GEOMETRYSCRIPTINGCORE_API FGeometryScriptMeshReadLOD
{
	GENERATED_BODY()
public:
	UPROPERTY(BlueprintReadWrite, Category = LOD)
	EGeometryScriptLODType LODType = EGeometryScriptLODType::MaxAvailable;

	UPROPERTY(BlueprintReadWrite, Category = LOD)
	int32 LODIndex = 0;
};


USTRUCT(BlueprintType)
struct GEOMETRYSCRIPTINGCORE_API FGeometryScriptMeshWriteLOD
{
	GENERATED_BODY()
public:
	UPROPERTY(BlueprintReadWrite, Category = LOD)
	bool bWriteHiResSource = false;

	UPROPERTY(BlueprintReadWrite, Category = LOD)
	int32 LODIndex = 0;
};





//
// Triangles
//

USTRUCT(BlueprintType)
struct GEOMETRYSCRIPTINGCORE_API FGeometryScriptTriangle
{
	GENERATED_BODY()
public:
	UPROPERTY(BlueprintReadWrite, Category = Options)
	FVector Vector0;

	UPROPERTY(BlueprintReadWrite, Category = Options)
	FVector Vector1;

	UPROPERTY(BlueprintReadWrite, Category = Options)
	FVector Vector2;
};


USTRUCT(BlueprintType)
struct GEOMETRYSCRIPTINGCORE_API FGeometryScriptTrianglePoint
{
	GENERATED_BODY()
public:
	UPROPERTY(BlueprintReadWrite, Category = Options)
	bool bValid = false;

	UPROPERTY(BlueprintReadWrite, Category = Options)
	int TriangleID = -1;

	UPROPERTY(BlueprintReadWrite, Category = Options)
	FVector Position;

	UPROPERTY(BlueprintReadWrite, Category = Options)
	FVector BaryCoords;
};


USTRUCT(BlueprintType)
struct GEOMETRYSCRIPTINGCORE_API FGeometryScriptUVTriangle
{
	GENERATED_BODY()
public:
	UPROPERTY(BlueprintReadWrite, Category = Options)
	FVector2D UV0;

	UPROPERTY(BlueprintReadWrite, Category = Options)
	FVector2D UV1;

	UPROPERTY(BlueprintReadWrite, Category = Options)
	FVector2D UV2;
};



//
// Spatial data structures
//


USTRUCT(BlueprintType, meta = (DisplayName = "DynamicMesh BVH Cache"))
struct GEOMETRYSCRIPTINGCORE_API FGeometryScriptDynamicMeshBVH
{
	GENERATED_BODY()
public:

	TSharedPtr<UE::Geometry::FDynamicMeshAABBTree3> Spatial;
	TSharedPtr<UE::Geometry::TFastWindingTree<UE::Geometry::FDynamicMesh3>> FWNTree;
};



//
// Errors/Debugging
//



UENUM(BlueprintType)
enum class EGeometryScriptDebugMessageType : uint8
{
	ErrorMessage,
	WarningMessage
};


UENUM(BlueprintType)
enum class EGeometryScriptErrorType : uint8
{
	// warning: must only append members!
	NoError,
	UnknownError,
	InvalidInputs,
	OperationFailed
};


USTRUCT(BlueprintType)
struct GEOMETRYSCRIPTINGCORE_API FGeometryScriptDebugMessage
{
	GENERATED_BODY()
public:
	UPROPERTY(BlueprintReadWrite, Category = Options)
	EGeometryScriptDebugMessageType MessageType = EGeometryScriptDebugMessageType::ErrorMessage;

	UPROPERTY(BlueprintReadWrite, Category = Options)
	EGeometryScriptErrorType ErrorType = EGeometryScriptErrorType::UnknownError;

	UPROPERTY(BlueprintReadWrite, Category = Options)
	FText Message = FText();
};


UCLASS(BlueprintType, meta = (TestMetadata))
class GEOMETRYSCRIPTINGCORE_API UGeometryScriptDebug : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY(BlueprintReadWrite, Category = Messages)
	TArray<FGeometryScriptDebugMessage> Messages;


	void Append(const FGeometryScriptDebugMessage& MessageIn)
	{
		Messages.Add(MessageIn);
	}
};



namespace UE
{
namespace Geometry
{
	GEOMETRYSCRIPTINGCORE_API FGeometryScriptDebugMessage MakeScriptError(EGeometryScriptErrorType ErrorTypeIn, const FText& MessageIn);

	GEOMETRYSCRIPTINGCORE_API void AppendError(UGeometryScriptDebug* Debug, EGeometryScriptErrorType ErrorTypeIn, const FText& MessageIn);
}
}