// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "RigVMModel/RigVMGraph.h"
#include "RigVMModel/Nodes/RigVMStructNode.h"
#include "RigVMModel/Nodes/RigVMVariableNode.h"
#include "RigVMModel/Nodes/RigVMParameterNode.h"
#include "RigVMModel/Nodes/RigVMCommentNode.h"
#include "RigVMModel/Nodes/RigVMRerouteNode.h"
#include "RigVMCore/RigVM.h"

#include "RigVMCompiler.generated.h"

USTRUCT(BlueprintType)
struct RIGVMEDITOR_API FRigVMCompileSettings
{
	GENERATED_BODY()

public:

	FRigVMCompileSettings();

	UPROPERTY(BlueprintReadWrite, Category = FRigVMCompileSettings)
	bool SurpressInfoMessage;

	UPROPERTY(BlueprintReadWrite, Category = FRigVMCompileSettings)
	bool SurpressWarnings;

	UPROPERTY(BlueprintReadWrite, Category = FRigVMCompileSettings)
	bool SurpressErrors;
};

UCLASS(BlueprintType)
class RIGVMEDITOR_API URigVMCompiler : public UObject
{
	GENERATED_BODY()

public:

	URigVMCompiler();

	UPROPERTY(BlueprintReadWrite, Category = FRigVMCompiler)
	FRigVMCompileSettings Settings;

	UFUNCTION(BlueprintCallable, Category = FRigVMCompiler)
	bool Compile(URigVMGraph* InGraph, URigVM* OutVM);

private:

	FString GetPinHash(URigVMPin* InPin);
	UScriptStruct* GetScriptStructForCPPType(const FString& InCPPType);
	uint16 GetElementSizeFromCPPType(const FString& InCPPType, UScriptStruct* InScriptStruct);
	FRigVMOperand AddRegisterForPin(URigVMPin* InPin, URigVM* OutVM);

	void ReportInfo(const FString& InMessage);
	void ReportWarning(const FString& InMessage);
	void ReportError(const FString& InMessage);

	template <typename FmtType, typename... Types>
	void ReportInfof(const FmtType& Fmt, Types... Args)
	{
		ReportInfo(FString::Printf(Fmt, Args...));
	}

	template <typename FmtType, typename... Types>
	void ReportWarningf(const FmtType& Fmt, Types... Args)
	{
		ReportWarning(FString::Printf(Fmt, Args...));
	}

	template <typename FmtType, typename... Types>
	void ReportErrorf(const FmtType& Fmt, Types... Args)
	{
		ReportError(FString::Printf(Fmt, Args...));
	}
};
