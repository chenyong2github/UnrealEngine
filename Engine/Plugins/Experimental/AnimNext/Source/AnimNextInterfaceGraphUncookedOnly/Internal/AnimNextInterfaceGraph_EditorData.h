// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RigVMModel/RigVMGraph.h"
#include "AnimNextInterfaceGraph_EdGraph.h"
#include "RigVMCore/RigVMGraphFunctionHost.h"
#include "AnimNextInterfaceGraph_EditorData.generated.h"

class UAnimNextInterfaceGraph;
enum class ERigVMGraphNotifType : uint8;
namespace UE::AnimNext::InterfaceGraphUncookedOnly
{
	struct FUtils;
}

namespace UE::AnimNext::InterfaceGraphEditor
{
	class FGraphEditor;
}

UCLASS(MinimalAPI)
class UAnimNextInterfaceGraph_EditorData : public UObject, public IRigVMClientHost, public IRigVMGraphFunctionHost
{
	GENERATED_BODY()

	UAnimNextInterfaceGraph_EditorData(const FObjectInitializer& ObjectInitializer);
	
	friend class UAnimNextInterfaceGraphFactory;
	friend class UAnimNextInterfaceGraph_EdGraph;
	friend struct UE::AnimNext::InterfaceGraphUncookedOnly::FUtils;
	friend class UE::AnimNext::InterfaceGraphEditor::FGraphEditor;
	friend struct FAnimNextInterfaceGraphSchemaAction_RigUnit;

	// UObject interface
	virtual void Serialize(FArchive& Ar) override;
	virtual void PostLoad() override;
	virtual bool IsEditorOnly() const override { return true; }
	
	// IRigVMClientHost interface
	virtual FRigVMClient* GetRigVMClient() override;
	virtual const FRigVMClient* GetRigVMClient() const override;
	virtual IRigVMGraphFunctionHost* GetRigVMGraphFunctionHost() override;
	virtual const IRigVMGraphFunctionHost* GetRigVMGraphFunctionHost() const override;
	virtual void HandleRigVMGraphAdded(const FRigVMClient* InClient, const FString& InNodePath) override;
	virtual void HandleRigVMGraphRemoved(const FRigVMClient* InClient, const FString& InNodePath) override {}
	virtual void HandleRigVMGraphRenamed(const FRigVMClient* InClient, const FString& InOldNodePath, const FString& InNewNodePath) override {}
	virtual void HandleConfigureRigVMController(const FRigVMClient* InClient, URigVMController* InControllerToConfigure) override;
	virtual UObject* GetEditorObjectForRigVMGraph(URigVMGraph* InVMGraph) const override;

	// IRigVMGraphFunctionHost interface
	virtual FRigVMGraphFunctionStore* GetRigVMGraphFunctionStore() override;
	virtual const FRigVMGraphFunctionStore* GetRigVMGraphFunctionStore() const override;

	ANIMNEXTINTERFACEGRAPHUNCOOKEDONLY_API void Initialize(bool bRecompileVM);

	void RecompileVM();
	
	void RecompileVMIfRequired();

	void RequestAutoVMRecompilation();
	
	void HandleModifiedEvent(ERigVMGraphNotifType InNotifType, URigVMGraph* InGraph, UObject* InSubject);

	ANIMNEXTINTERFACEGRAPHUNCOOKEDONLY_API URigVMGraph* GetVMGraphForEdGraph(const UEdGraph* InGraph) const;

	void CreateEdGraphForCollapseNode(URigVMCollapseNode* InNode);
	
	UPROPERTY()
	TObjectPtr<UAnimNextInterfaceGraph_EdGraph> RootGraph;

	UPROPERTY()
	TObjectPtr<UAnimNextInterfaceGraph_EdGraph> EntryPointGraph;
	
	UPROPERTY()
	TObjectPtr<UAnimNextInterfaceGraph_EdGraph> FunctionLibraryEdGraph;

	UPROPERTY()
	FRigVMClient RigVMClient;

	UPROPERTY()
	FRigVMGraphFunctionStore GraphFunctionStore;

	UPROPERTY()
	TObjectPtr<URigVMGraph> RigVMGraph_DEPRECATED;
	
	UPROPERTY()
	TObjectPtr<URigVMFunctionLibrary> RigVMFunctionLibrary_DEPRECATED;

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