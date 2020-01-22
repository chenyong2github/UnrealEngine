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
#include "RigVMCore/RigVMStruct.h"
#include "RigVMCompiler/RigVMAST.h"

#include "RigVMCompiler.generated.h"

USTRUCT(BlueprintType)
struct RIGVMDEVELOPER_API FRigVMCompileSettings
{
	GENERATED_BODY()

public:

	FRigVMCompileSettings();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = FRigVMCompileSettings)
	bool SurpressInfoMessages;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = FRigVMCompileSettings)
	bool SurpressWarnings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = FRigVMCompileSettings)
	bool SurpressErrors;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = FRigVMCompileSettings)
	bool EnablePinWatches;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = FRigVMCompileSettings)
	bool SplitLiteralsFromWorkMemory;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = FRigVMCompileSettings)
	bool ConsolidateWorkRegisters;

	UPROPERTY(EditAnywhere, Transient, BlueprintReadWrite, Category = FRigVMCompileSettings)
	FRigVMParserASTSettings ASTSettings;
};

struct RIGVMDEVELOPER_API FRigVMCompilerWorkData
{
public:
	bool bSetupMemory;
	URigVM* VM;
	FRigVMUserDataArray RigVMUserData;
	TMap<FString, FRigVMOperand>* PinPathToOperand;
	TMap<const FRigVMVarExprAST*, FRigVMOperand> ExprToOperand;
	TMap<const FRigVMExprAST*, bool> ExprComplete;
	TMap<FString, int32> ProcessedLinks;
	TArray<FRigVMStruct*> DefaultStructs;
	TMap<int32, int32> RegisterRefCount;
	int32 NumInstructions;

	int32 IncRefRegister(int32 InRegister, int32 InIncrement = 1);
	int32 DecRefRegister(int32 InRegister, int32 InDecrement = 1);
};

UCLASS(BlueprintType)
class RIGVMDEVELOPER_API URigVMCompiler : public UObject
{
	GENERATED_BODY()

public:

	URigVMCompiler();

	UPROPERTY(BlueprintReadWrite, Category = FRigVMCompiler)
	FRigVMCompileSettings Settings;

	UFUNCTION(BlueprintCallable, Category = FRigVMCompiler)
	bool Compile(URigVMGraph* InGraph, URigVM* OutVM)
	{
		return Compile(InGraph, OutVM, FRigVMUserDataArray(), nullptr);
	}

	bool Compile(URigVMGraph* InGraph, URigVM* OutVM, const FRigVMUserDataArray& InRigVMUserData, TMap<FString, FRigVMOperand>* OutOperands);
	static UScriptStruct* GetScriptStructForCPPType(const FString& InCPPType);
	static FString GetPinHash(URigVMPin* InPin, const FRigVMVarExprAST* InVarExpr, bool bIsDebugValue = false);

private:

	TArray<URigVMPin*> GetLinkedPins(URigVMPin* InPin, bool bInputs = true, bool bOutputs = true, bool bRecursive = true);
	uint16 GetElementSizeFromCPPType(const FString& InCPPType, UScriptStruct* InScriptStruct);

	void TraverseExpression(const FRigVMExprAST* InExpr, FRigVMCompilerWorkData& WorkData);
	void TraverseChildren(const FRigVMExprAST* InExpr, FRigVMCompilerWorkData& WorkData);
	void TraverseBlock(const FRigVMBlockExprAST* InExpr, FRigVMCompilerWorkData& WorkData);
	void TraverseEntry(const FRigVMEntryExprAST* InExpr, FRigVMCompilerWorkData& WorkData);
	void TraverseCallExtern(const FRigVMCallExternExprAST* InExpr, FRigVMCompilerWorkData& WorkData);
	void TraverseNoOp(const FRigVMNoOpExprAST* InExpr, FRigVMCompilerWorkData& WorkData);
	void TraverseVar(const FRigVMVarExprAST* InExpr, FRigVMCompilerWorkData& WorkData);
	void TraverseLiteral(const FRigVMVarExprAST* InExpr, FRigVMCompilerWorkData& WorkData);
	void TraverseAssign(const FRigVMAssignExprAST* InExpr, FRigVMCompilerWorkData& WorkData);
	void TraverseCopy(const FRigVMCopyExprAST* InExpr, FRigVMCompilerWorkData& WorkData);
	void TraverseCachedValue(const FRigVMCachedValueExprAST* InExpr, FRigVMCompilerWorkData& WorkData);
	void TraverseExit(const FRigVMExitExprAST* InExpr, FRigVMCompilerWorkData& WorkData);

	FRigVMOperand FindOrAddRegister(const FRigVMVarExprAST* InVarExpr, FRigVMCompilerWorkData& WorkData, bool bIsDebugValue = false);

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

	friend class FRigVMCompilerImportErrorContext;
};
