// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "DatasmithImportOptions.h"
#include "DatasmithTranslator.h"
#include "UObject/ObjectMacros.h"

class UDatasmithCommonTessellationOptions;


class DATASMITHCORETECHPARAMETRICSURFACEDATA_API FDatasmithCoreTechTranslator : public IDatasmithTranslator
{
public:
	virtual ~FDatasmithCoreTechTranslator() {}

	// Begin IDatasmithTranslator overrides
	virtual void GetSceneImportOptions(TArray<TStrongObjectPtr<UObject>>& Options) override;
	virtual void SetSceneImportOptions(TArray<TStrongObjectPtr<UObject>>& Options) override;
	// End IDatasmithTranslator overrides

protected:
	const FDatasmithTessellationOptions& GetCommonTessellationOptions()
	{
		return GetCommonTessellationOptionsPtr()->Options;
	}

	/** 
	 * Call when the UDatasmithCommonTessellationOptions object is created. This is the unique opportunity for
	 * child class to overwrite some values
	 * @param TessellationOptions Reference on member of UDatasmithCommonTessellationOptions
	 */
	virtual void InitCommonTessellationOptions(FDatasmithTessellationOptions& TessellationOptions) {}

private:
	TStrongObjectPtr<UDatasmithCommonTessellationOptions>& GetCommonTessellationOptionsPtr();
	TStrongObjectPtr<UDatasmithCommonTessellationOptions> CommonTessellationOptionsPtr;
};

