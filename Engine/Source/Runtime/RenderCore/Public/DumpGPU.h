// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/WildcardString.h"
#include "UObject/NameTypes.h"


class RENDERCORE_API IDumpGPUUploadServiceProvider
{
public:

	struct RENDERCORE_API FDumpParameters
	{
		static constexpr const TCHAR* kServiceFileName = TEXT("Base/DumpService.json");

		FString Type;
		FString LocalPath;
		FString Time;

		FName CompressionName;
		FWildcardString CompressionFiles;

		FString DumpServiceParametersFileContent() const;
		bool DumpServiceParametersFile() const;
	};

	virtual void UploadDump(const FDumpParameters& Parameters) = 0;
	virtual ~IDumpGPUUploadServiceProvider() = default;

	static IDumpGPUUploadServiceProvider* GProvider;
};
