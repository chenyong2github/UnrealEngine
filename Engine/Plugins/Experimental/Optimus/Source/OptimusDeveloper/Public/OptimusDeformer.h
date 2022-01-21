// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IOptimusNodeFunctionLibraryOwner.h"
#include "IOptimusNodeGraphCollectionOwner.h"
#include "IOptimusPathResolver.h"
#include "OptimusCoreNotify.h"
#include "OptimusDataType.h"
#include "Animation/MeshDeformer.h"
#include "Animation/MeshDeformerInstance.h"

#include "ComputeFramework/ComputeGraph.h"
#include "ComputeFramework/ComputeGraphInstance.h"
#include "Interfaces/Interface_PreviewMeshProvider.h"
#include "Logging/TokenizedMessage.h"
#include "Types/OptimusType_ShaderText.h"
#include "Misc/TVariant.h"
#include "OptimusDeformer.generated.h"

class UComputeDataProvider;
class USkeletalMesh;
class UOptimusActionStack;
class UOptimusDeformer;
class UOptimusResourceDescription;
class UOptimusVariableDescription;
class UOptimusNode_CustomComputeKernel;
enum class EOptimusDiagnosticLevel : uint8;


DECLARE_MULTICAST_DELEGATE_OneParam(FOptimusCompileBegin, UOptimusDeformer *);
DECLARE_MULTICAST_DELEGATE_OneParam(FOptimusCompileEnd, UOptimusDeformer *);
DECLARE_MULTICAST_DELEGATE_OneParam(FOptimusGraphCompileMessageDelegate, const TSharedRef<FTokenizedMessage>&);


USTRUCT()
struct FOptimusComputeGraphInfo
{
	GENERATED_BODY()

	UPROPERTY()
	EOptimusNodeGraphType GraphType;

	UPROPERTY()
	FName GraphName;

	UPROPERTY(transient)
	bool bExecuteTrigger = false;

	UPROPERTY()
	TObjectPtr<UComputeGraph> ComputeGraph = nullptr;
};


/**
  * A Deformer Graph is an asset that is used to create and control custom deformations on 
  * skeletal meshes.
  */
UCLASS()
class OPTIMUSDEVELOPER_API UOptimusDeformer :
	public UMeshDeformer,
	public IInterface_PreviewMeshProvider,
	public IOptimusPathResolver,
	public IOptimusNodeGraphCollectionOwner,
	public IOptimusNodeFunctionLibraryOwner 
{
	GENERATED_BODY()

public:
	UOptimusDeformer();

	UOptimusActionStack *GetActionStack() const { return ActionStack; }

	/** Returns the global delegate used to notify on global operations (e.g. graph, variable,
	 *  resource lifecycle events).
	 */
	FOptimusGlobalNotifyDelegate& GetNotifyDelegate() { return GlobalNotifyDelegate; }

	/** Add a setup graph. This graph is executed once when the deformer is first run from a
	  * mesh component. If the graph already exists, this function does nothing and returns 
	  * nullptr.
	  */
	UFUNCTION(BlueprintCallable, Category = OptimusNodeGraph)
	UOptimusNodeGraph* AddSetupGraph();

	/** Add a trigger graph. This graph will be scheduled to execute on next tick, prior to the
	  * update graph being executed, after being triggered from a blueprint.
	  * @param InName The name to give the graph. The name "Setup" cannot be used, since it's a
	  *  reserved name.
	  */
	UFUNCTION(BlueprintCallable, Category = OptimusNodeGraph)
	UOptimusNodeGraph* AddTriggerGraph(const FString &InName);

	/// Returns the update graph. The update graph will always exist, and there is only one.
	UOptimusNodeGraph* GetUpdateGraph() const;

	/** Remove a graph and delete it. */
	UFUNCTION(BlueprintCallable, Category = OptimusNodeGraph)
	bool RemoveGraph(UOptimusNodeGraph* InGraph);

	// Variables
	UFUNCTION(BlueprintCallable, Category = OptimusVariables)
	UOptimusVariableDescription* AddVariable(
		FOptimusDataTypeRef InDataTypeRef,
	    FName InName = NAME_None
		);

	UFUNCTION(BlueprintCallable, Category = OptimusVariables)
	bool RemoveVariable(
	    UOptimusVariableDescription* InVariableDesc
		);

	UFUNCTION(BlueprintCallable, Category = OptimusVariables)
	bool RenameVariable(
	    UOptimusVariableDescription* InVariableDesc,
	    FName InNewName);

	UFUNCTION(BlueprintCallable, Category = OptimusVariables)
	const TArray<UOptimusVariableDescription*>& GetVariables() const { return VariableDescriptions; }

	
	UOptimusVariableDescription* ResolveVariable(
		FName InVariableName
		) const override;

	/** Create a resource owned by this deformer but does not add it to the list of known
	  * resources. Call AddResource for that */
	UOptimusVariableDescription* CreateVariableDirect(
		FName InName
		);

	/** Adds a resource that was created by this deformer and is owned by it. */
	bool AddVariableDirect(
		UOptimusVariableDescription* InVariableDesc
		);

	bool RemoveVariableDirect(
		UOptimusVariableDescription* InVariableDesc
		);

	bool RenameVariableDirect(
	    UOptimusVariableDescription* InVariableDesc,
		FName InNewName
		);

	// Resources
	UFUNCTION(BlueprintCallable, Category = OptimusResources)
	UOptimusResourceDescription* AddResource(
		FOptimusDataTypeRef InDataTypeRef,
	    FName InName = NAME_None
		);

	UFUNCTION(BlueprintCallable, Category = OptimusResources)
	bool RemoveResource(
	    UOptimusResourceDescription* InResourceDesc
		);

	UFUNCTION(BlueprintCallable, Category = OptimusResources)
	bool RenameResource(
	    UOptimusResourceDescription* InResourceDesc,
	    FName InNewName);

	UFUNCTION(BlueprintCallable, Category = OptimusResources)
	const TArray<UOptimusResourceDescription*>& GetResources() const { return ResourceDescriptions; }

	
	UOptimusResourceDescription* ResolveResource(
		FName InResourceName
		) const override;

	/** Create a resource owned by this deformer but does not add it to the list of known
	  * resources. Call AddResource for that */
	UOptimusResourceDescription* CreateResourceDirect(
		FName InName
		);

	/** Adds a resource that was created by this deformer and is owned by it. */
	bool AddResourceDirect(
		UOptimusResourceDescription* InResourceDesc
		);

	bool RemoveResourceDirect(
		UOptimusResourceDescription* InResourceDesc
		);

	bool RenameResourceDirect(
	    UOptimusResourceDescription* InResourceDesc,
		FName InNewName
		);

	/// Graph compilation
	bool Compile();

	/** Returns a multicast delegate that can be subscribed to listen for the start of compilation. */
	FOptimusCompileBegin& GetCompileBeginDelegate()  { return CompileBeginDelegate; }
	/** Returns a multicast delegate that can be subscribed to listen for the end of compilation but before shader compilation is complete. */
	FOptimusCompileEnd& GetCompileEndDelegate() { return CompileEndDelegate; }
	/** Returns a multicast delegate that can be subscribed to listen compilation results. Note that the shader compilation results are async and can be returned after the CompileEnd delegate. */
	FOptimusGraphCompileMessageDelegate& GetCompileMessageDelegate() { return CompileMessageDelegate; }

	/// UObject overrides
	void Serialize(FArchive& Ar) override;

	// UMeshDeformer overrides
	UMeshDeformerInstance* CreateInstance(UMeshComponent* InMeshComponent) override;
	
	// IInterface_PreviewMeshProvider overrides
	void SetPreviewMesh(USkeletalMesh* PreviewMesh, bool bMarkAsDirty = true) override;
	USkeletalMesh* GetPreviewMesh() const override;

	// IOptimusNodeGraphCollectionRoot overrides
	IOptimusNodeGraphCollectionOwner* ResolveCollectionPath(const FString& InPath) override;
	UOptimusNodeGraph* ResolveGraphPath(const FString& InGraphPath) override;
	UOptimusNode* ResolveNodePath(const FString& InNodePath) override;
	UOptimusNodePin* ResolvePinPath(const FString& InPinPath) override;

	// IOptimusNodeGraphCollectionOwner overrides
	IOptimusNodeGraphCollectionOwner* GetCollectionOwner() const override { return nullptr; }
	IOptimusNodeGraphCollectionOwner* GetCollectionRoot() const override { return const_cast<UOptimusDeformer*>(this); }
	FString GetCollectionPath() const override { return FString(); }

	
	UFUNCTION(BlueprintCallable, Category = OptimusNodeGraph)
	const TArray<UOptimusNodeGraph*> &GetGraphs() const override { return Graphs; }

	UOptimusNodeGraph* CreateGraph(
	    EOptimusNodeGraphType InType,
	    FName InName,
	    TOptional<int32> InInsertBefore) override;
	bool AddGraph(
	    UOptimusNodeGraph* InGraph,
		int32 InInsertBefore) override;
	bool RemoveGraph(
	    UOptimusNodeGraph* InGraph,
		bool bDeleteGraph) override;

	UFUNCTION(BlueprintCallable, Category = OptimusNodeGraph)
	bool MoveGraph(
	    UOptimusNodeGraph* InGraph,
	    int32 InInsertBefore) override;

	UFUNCTION(BlueprintCallable, Category = OptimusNodeGraph)
	bool RenameGraph(
	    UOptimusNodeGraph* InGraph,
	    const FString& InNewName) override;

	
	UPROPERTY(EditAnywhere, Category=Preview)
	USkeletalMesh *Mesh = nullptr;

protected:
	friend class UOptimusNodeGraph;
	friend class UOptimusDeformerInstance;
	
	void Notify(EOptimusGlobalNotifyType InNotifyType, UObject *InObject) const;

	// The compute graphs to execute.
	UPROPERTY()
	TArray<FOptimusComputeGraphInfo> ComputeGraphs;
	
private:
	UOptimusNodeGraph* ResolveGraphPath(const FStringView InPath, FStringView& OutRemainingPath) const;
	UOptimusNode* ResolveNodePath(const FStringView InPath, FStringView& OutRemainingPath) const;
	int32 GetUpdateGraphIndex() const;

	// Compile a node graph to a compute graph. Returns either a completed compute graph, or
	// the error message to pass back, if the compilation failed.
	using FOptimusCompileResult = TVariant<FEmptyVariantState, UComputeGraph*, TSharedRef<FTokenizedMessage>>;
	FOptimusCompileResult CompileNodeGraphToComputeGraph(
		const UOptimusNodeGraph *InNodeGraph
		);
	
	FOptimusType_CompilerDiagnostic ProcessCompilationMessage(
		const UOptimusNode* InKernelNode,
		const FString& InMessage
		);

	UPROPERTY()
	TArray<TObjectPtr<UOptimusNodeGraph>> Graphs;

	UPROPERTY()
	TArray<TObjectPtr<UOptimusVariableDescription>> VariableDescriptions;

	UPROPERTY()
	TArray<TObjectPtr<UOptimusResourceDescription>> ResourceDescriptions;

	UPROPERTY(transient)
	TObjectPtr<UOptimusActionStack> ActionStack = nullptr;

	FOptimusGlobalNotifyDelegate GlobalNotifyDelegate;

	FOptimusCompileBegin CompileBeginDelegate;
	
	FOptimusCompileEnd CompileEndDelegate;

	FOptimusGraphCompileMessageDelegate CompileMessageDelegate;
};
