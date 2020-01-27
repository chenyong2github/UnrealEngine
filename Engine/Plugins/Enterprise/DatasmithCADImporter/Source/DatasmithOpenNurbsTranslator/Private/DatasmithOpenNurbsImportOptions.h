// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DatasmithImportOptions.h"
#include "DatasmithAssetImportData.h"

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"

#include "DatasmithOpenNurbsImportOptions.generated.h"


UENUM()
enum class EDatasmithOpenNurbsBrepTesselatedSource : uint8
{
	/** Tessellate all Breps on import */
	UseUnrealNurbsTessellation,

	/** Use meshes stored in the scene file */
	UseSceneRenderMeshes,
};


USTRUCT(BlueprintType)
struct FDatasmithOpenNurbsOptions : public FDatasmithTessellationOptions
{
	GENERATED_BODY()

	FDatasmithOpenNurbsOptions()
    {
#ifdef CAD_LIBRARY
		BrepTesselation = EDatasmithOpenNurbsBrepTesselatedSource::UseUnrealNurbsTessellation;
#else
		BrepTesselation = EDatasmithOpenNurbsBrepTesselatedSource::UseSceneRenderMeshes;
#endif
    }

	UPROPERTY(config, EditAnywhere, SimpleDisplay, Category = "Geometry & Tessellation Options")
	EDatasmithOpenNurbsBrepTesselatedSource BrepTesselation;

	uint32 GetHash() const
	{
		return HashCombine(FDatasmithTessellationOptions::GetHash(), GetTypeHash(BrepTesselation));;
	}
};


UCLASS(BlueprintType, config = EditorPerProjectUserSettings)
class UDatasmithOpenNurbsImportOptions: public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Geometry & Tessellation Options", meta = (ShowOnlyInnerProperties))
	FDatasmithOpenNurbsOptions Options;

#if WITH_EDITOR
	bool CanEditChange(const FProperty* InProperty) const;
#endif //WITH_EDITOR

};

