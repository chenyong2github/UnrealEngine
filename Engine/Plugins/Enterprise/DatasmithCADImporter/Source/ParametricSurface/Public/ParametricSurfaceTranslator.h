// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "DatasmithImportOptions.h"
#include "DatasmithTranslator.h"
#include "UObject/ObjectMacros.h"

class UDatasmithCommonTessellationOptions;

namespace CADLibrary
{
	class FImportParameters;
	struct FMeshParameters;
}

class PARAMETRICSURFACE_API FParametricSurfaceTranslator : public IDatasmithTranslator
{
public:

	// Begin IDatasmithTranslator overrides
	virtual void GetSceneImportOptions(TArray<TStrongObjectPtr<UDatasmithOptionsBase>>& Options) override;
	virtual void SetSceneImportOptions(TArray<TStrongObjectPtr<UDatasmithOptionsBase>>& Options) override;
	// End IDatasmithTranslator overrides

	bool AddSurfaceData(const TCHAR* MeshFilePath, const CADLibrary::FImportParameters& InSceneParameters, const CADLibrary::FMeshParameters& MeshParameters, FDatasmithMeshElementPayload& OutMeshPayload);

protected:
	const FDatasmithTessellationOptions& GetCommonTessellationOptions()
	{
		return CommonTessellationOptions;
	}

	/** 
	 * Call when the UDatasmithCommonTessellationOptions object is created. This is the unique opportunity for
	 * child class to overwrite some values
	 * @param TessellationOptions Reference on member of UDatasmithCommonTessellationOptions
	 */
	virtual void InitCommonTessellationOptions(FDatasmithTessellationOptions& TessellationOptions) {}

private:
	FDatasmithTessellationOptions CommonTessellationOptions;
};

