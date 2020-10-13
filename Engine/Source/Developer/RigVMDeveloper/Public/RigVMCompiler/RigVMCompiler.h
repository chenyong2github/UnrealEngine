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

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = FRigVMCompileSettings)
	bool ConsolidateWorkRegisters;

	UPROPERTY(EditAnywhere, Transient, BlueprintReadWrite, Category = FRigVMCompileSettings)
	FRigVMParserASTSettings ASTSettings;

	UPROPERTY()
	bool SetupNodeInstructionIndex;

	static FRigVMCompileSettings Fast()
	{
		FRigVMCompileSettings Settings;
		Settings.ConsolidateWorkRegisters = false;
		Settings.EnablePinWatches = true;
		Settings.ASTSettings = FRigVMParserASTSettings::Fast();
		return Settings;
	}

	static FRigVMCompileSettings Optimized()
	{
		FRigVMCompileSettings Settings;
		Settings.ConsolidateWorkRegisters = false;
		Settings.EnablePinWatches = false;
		Settings.ASTSettings = FRigVMParserASTSettings::Optimized();
		return Settings;
	}
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
	TArray<const FRigVMExprAST*> ExprToSkip;
	TMap<FString, int32> ProcessedLinks;
	TArray<TSharedPtr<FStructOnScope>> DefaultStructs;
	TMap<int32, int32> RegisterRefCount;
	TArray<TPair<FName, int32>> RefCountSteps;
	TMap<int32, FRigVMOperand> IntegerLiterals;
	FRigVMOperand ComparisonLiteral;

	int32 IncRefRegister(int32 InRegister, int32 InIncrement = 1);
	int32 DecRefRegister(int32 InRegister, int32 InDecrement = 1);
	void RecordRefCountStep(int32  InRegister, int32 InRefCount);
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
		bool Compile(URigVMGraph* InGraph, URigVMController* InController, URigVM* OutVM)
	{
		return Compile(InGraph, InController, OutVM, TArray<FRigVMExternalVariable>(), TArray<FRigVMUserDataArray>(), nullptr);
	}

	bool Compile(URigVMGraph* InGraph, URigVMController* InController, URigVM* OutVM, const TArray<FRigVMExternalVariable>& InExternalVariables, const TArray<FRigVMUserDataArray>& InRigVMUserData, TMap<FString, FRigVMOperand>* OutOperands, TSharedPtr<FRigVMParserAST> InAST = TSharedPtr<FRigVMParserAST>());

	static UScriptStruct* GetScriptStructForCPPType(const FString& InCPPType);
	static FString GetPinHash(URigVMPin* InPin, const FRigVMVarExprAST* InVarExpr, bool bIsDebugValue = false);

private:

	TArray<URigVMPin*> GetLinkedPins(URigVMPin* InPin, bool bInputs = true, bool bOutputs = true, bool bRecursive = true);
	uint16 GetElementSizeFromCPPType(const FString& InCPPType, UScriptStruct* InScriptStruct);

	void TraverseExpression(const FRigVMExprAST* InExpr, FRigVMCompilerWorkData& WorkData);
	void TraverseChildren(const FRigVMExprAST* InExpr, FRigVMCompilerWorkData& WorkData);
	void TraverseBlock(const FRigVMBlockExprAST* InExpr, FRigVMCompilerWorkData& WorkData);
	void TraverseEntry(const FRigVMEntryExprAST* InExpr, FRigVMCompilerWorkData& WorkData);
	int32 TraverseCallExtern(const FRigVMCallExternExprAST* InExpr, FRigVMCompilerWorkData& WorkData);
	void TraverseForLoop(const FRigVMCallExternExprAST* InExpr, FRigVMCompilerWorkData& WorkData);
	void TraverseNoOp(const FRigVMNoOpExprAST* InExpr, FRigVMCompilerWorkData& WorkData);
	void TraverseVar(const FRigVMVarExprAST* InExpr, FRigVMCompilerWorkData& WorkData);
	void TraverseLiteral(const FRigVMVarExprAST* InExpr, FRigVMCompilerWorkData& WorkData);
	void TraverseAssign(const FRigVMAssignExprAST* InExpr, FRigVMCompilerWorkData& WorkData);
	void TraverseCopy(const FRigVMCopyExprAST* InExpr, FRigVMCompilerWorkData& WorkData);
	void TraverseCachedValue(const FRigVMCachedValueExprAST* InExpr, FRigVMCompilerWorkData& WorkData);
	void TraverseExit(const FRigVMExitExprAST* InExpr, FRigVMCompilerWorkData& WorkData);
	void TraverseBranch(const FRigVMBranchExprAST* InExpr, FRigVMCompilerWorkData& WorkData);
	void TraverseIf(const FRigVMIfExprAST* InExpr, FRigVMCompilerWorkData& WorkData);
	void TraverseSelect(const FRigVMSelectExprAST* InExpr, FRigVMCompilerWorkData& WorkData);

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
