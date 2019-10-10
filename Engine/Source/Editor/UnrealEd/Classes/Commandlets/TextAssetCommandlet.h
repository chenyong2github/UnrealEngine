// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	TextAssetCommandlet.cpp: Commandlet for saving assets in text asset format
=============================================================================*/

#pragma once
#include "Commandlets/Commandlet.h"
#include "TextAssetCommandlet.generated.h"

UNREALED_API DECLARE_LOG_CATEGORY_EXTERN(LogTextAsset, Log, All);

UCLASS(config=Editor)
class UTextAssetCommandlet
	: public UCommandlet
{
	GENERATED_UCLASS_BODY()

public:

	enum class EProcessingMode
	{
		ResaveText,
		ResaveBinary,
		RoundTrip,
		LoadBinary,
		LoadText,
		FindMismatchedSerializers,
	};

	struct FProcessingArgs
	{
		EProcessingMode ProcessingMode = EProcessingMode::ResaveText;
		int32 NumSaveIterations = 1;
		bool bIncludeEngineContent = false;
		bool bFilenameIsFilter = true;
		FString Filename;
		FString CSVFilename;
		FString OutputPath;
		bool bVerifyJson = true;
	};

	//~ Begin UCommandlet Interface

	virtual int32 Main(const FString& CmdLineParams) override;
	
	//~ End UCommandlet Interface
	static UNREALED_API bool DoTextAssetProcessing(const FString& InCommandLine);
	static UNREALED_API bool DoTextAssetProcessing(const FProcessingArgs& InArgs);
};