// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RigVMModel/RigVMGraph.h"
#include "DataInterfaceGraph_EdGraph.h"
#include "DataInterfaceGraph_EditorData.generated.h"

class UDataInterfaceGraph;
enum class ERigVMGraphNotifType : uint8;
namespace UE::DataInterfaceGraphUncookedOnly
{
	struct FUtils;
}

namespace UE::DataInterfaceGraphEditor
{
	class FGraphEditor;
}

UCLASS(Optional, MinimalAPI)
class UDataInterfaceGraph_EditorData : public UObject, public IRigVMGraphHost, public IRigVMControllerHost
{
	GENERATED_BODY()

	UDataInterfaceGraph_EditorData(const FObjectInitializer& ObjectInitializer);
	
	friend class UDataInterfaceGraphFactory;
	friend class UDataInterfaceGraph_EdGraph;
	friend struct UE::DataInterfaceGraphUncookedOnly::FUtils;
	friend class UE::DataInterfaceGraphEditor::FGraphEditor;
	friend struct FDataInterfaceGraphSchemaAction_RigUnit;

	// UObject interface
	virtual void PostLoad() override;
	virtual bool IsEditorOnly() const override { return true; }
	
	// IRigVMGraphHost interface
	virtual URigVMGraph* GetRigVMGraph(const UObject* InEditorObject) const override;

	// IRigVMControllerHost interface
	virtual URigVMController* GetRigVMController(const URigVMGraph* InRigVMGraph) const override;
	virtual URigVMController* GetRigVMController(const UObject* InEditorObject) const override;
	virtual URigVMController* GetOrCreateRigVMController(URigVMGraph* InRigVMGraph) override;
	virtual URigVMController* GetOrCreateRigVMController(const UObject* InEditorObject) override;

	DATAINTERFACEGRAPHUNCOOKEDONLY_API void Initialize(bool bRecompileVM);

	void RecompileVM();
	
	void RecompileVMIfRequired();

	void RequestAutoVMRecompilation();
	
	void HandleModifiedEvent(ERigVMGraphNotifType InNotifType, URigVMGraph* InGraph, UObject* InSubject);

	URigVMGraph* GetVMGraphForEdGraph(const UEdGraph* InGraph) const;

	void CreateEdGraphForCollapseNode(URigVMCollapseNode* InNode);
	
	UPROPERTY()
	TObjectPtr<UDataInterfaceGraph_EdGraph> RootGraph;

	UPROPERTY()
	TObjectPtr<UDataInterfaceGraph_EdGraph> EntryPointGraph;
	
	UPROPERTY()
	TObjectPtr<UDataInterfaceGraph_EdGraph> FunctionLibraryEdGraph;
	
	UPROPERTY()
	TObjectPtr<URigVMGraph> RigVMGraph;
	
	UPROPERTY()
	TObjectPtr<URigVMFunctionLibrary> RigVMFunctionLibrary;

	UPROPERTY()
	TObjectPtr<URigVMLibraryNode> EntryPoint;
	
	UPROPERTY(transient)
	TMap<TObjectPtr<URigVMGraph>, TObjectPtr<URigVMController>> Controllers;

	UPROPERTY(EditAnywhere, Category = "User Interface")
	FRigGraphDisplaySettings RigGraphDisplaySettings;

	UPROPERTY(EditAnywhere, Category = "VM")
	FRigVMRuntimeSettings VMRuntimeSettings;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VM", meta = (AllowPrivateAccess = "true"))
	FRigVMCompileSettings VMCompileSettings;

	UPROPERTY(EditAnywhere, Category = "Python Log Settings")
	FControlRigPythonSettings PythonLogSettings;

	UPROPERTY(transient, DuplicateTransient)
	TMap<FString, FRigVMOperand> PinToOperandMap;

	UPROPERTY(transient, DuplicateTransient)
	bool bVMRecompilationRequired = false;

	UPROPERTY(transient, DuplicateTransient)
	bool bIsCompiling = false;
	
	FCompilerResultsLog CompileLog;

	FOnVMCompiledEvent VMCompiledEvent;
	FRigVMGraphModifiedEvent ModifiedEvent;

	bool bAutoRecompileVM = true;
	bool bErrorsDuringCompilation = false;
	bool bSuspendModelNotificationsForSelf = false;
	bool bSuspendModelNotificationsForOthers = false;
	bool bSuspendAllNotifications = false;
	bool bCompileInDebugMode = false;
};