// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "MSPythonBridge.generated.h"

UCLASS(Blueprintable)
class UMSPythonBridge : public UObject
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category = Python)
		static UMSPythonBridge * Get();

	UFUNCTION(BlueprintImplementableEvent, Category = Python)
		void InitializePythonWindow() const;

	UFUNCTION(BlueprintImplementableEvent, Category = Python)
	void GetUeData(const FString& CommandName) const;

	UFUNCTION(BlueprintImplementableEvent, Category = Python)
		void TestFbxImport() const;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Python)
		FString JsonData;

	//Testing bridge from Python to C++
	UFUNCTION(BlueprintCallable, Category = Python)
		static void CalledFromPython(FString InputString);

	//UPROPERTY(EditAnywhere, BlueprintReadWrite)
	//FString jsondata;
	//TArray<FString> filenames;

	//UFUNCTION(BlueprintImplementableEvent, Category = Python)
	//void DownloadByTestFilenames() const;

};

