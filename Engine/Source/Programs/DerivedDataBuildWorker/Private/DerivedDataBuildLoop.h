// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "DerivedDataBuildFunction.h"
#include "DerivedDataBuildWorkerInterface.h"
#include "Serialization/CompactBinary.h"
#include "Templates/Function.h"
#include "UObject/NameTypes.h"

namespace UE::DerivedData
{

class FBuildLoop
{
public:
	bool Init();

	using FBuildFunctionCallback = TFunctionRef<bool(FName, FBuildContext&)>;
	void PerformBuilds(const FBuildFunctionCallback& BuildFunctionCallback);

	void Teardown();
private:
	FString CommonInputPath;
	FString CommonOutputPath;

	friend class FWorkerBuildContext;

	struct FBuildActionRecord
	{
		const FString SourceFilePath;
		const FString OutputFilePath;
		const FString InputPath;
		const FString OutputPath;
		const FCbObject BuildAction;

		FBuildActionRecord(const FString& InSourceFilePath, const FString& InCommonInputPath, const FString& InCommonOutputPath, FSharedBuffer&& InSharedBuffer);
	};
	TArray<FBuildActionRecord> BuildActionRecords;
};

}