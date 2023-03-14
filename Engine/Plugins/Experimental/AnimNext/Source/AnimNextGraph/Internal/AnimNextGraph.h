// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interface/IAnimNextInterface.h"
#include "RigVMCore/RigVM.h"
#include "Param/ParamType.h"
#include "AnimNextGraph.generated.h"

class UEdGraph;
class UAnimNextGraph;
namespace UE::AnimNext::GraphUncookedOnly
{
	struct FUtils;
}

namespace UE::AnimNext::GraphEditor
{
	class FGraphEditor;
}

namespace UE::AnimNext::Graph
{
	extern ANIMNEXTGRAPH_API const FName EntryPointName;
	extern ANIMNEXTGRAPH_API const FName ResultName;
}

// A user-created graph of logic used to supply data
UCLASS(BlueprintType)
class ANIMNEXTGRAPH_API UAnimNextGraph : public UObject, public IAnimNextInterface
{
	GENERATED_BODY()

	// UObject interface
	virtual void PostRename(UObject* OldOuter, const FName OldName) override;
	virtual void GetPreloadDependencies(TArray<UObject*>& OutDeps) override;

	// IAnimNextInterface interface
	virtual UE::AnimNext::FParamTypeHandle GetReturnTypeHandleImpl() const final override;
	virtual bool GetDataImpl(const UE::AnimNext::FContext& Context) const final override;

	/** Set the return type of this graph */
	void SetReturnTypeHandle(UE::AnimNext::FParamTypeHandle InHandle);

	// Support rig VM execution
	TArray<FRigVMExternalVariable> GetRigVMExternalVariables();
	
	friend class UAnimNextGraphFactory;
	friend class UAnimNextGraph_EditorData;
	friend struct UE::AnimNext::GraphUncookedOnly::FUtils;
	friend class UE::AnimNext::GraphEditor::FGraphEditor;
	friend class UAnimNextGraph;
	friend class UAnimGraphNode_AnimNextGraph;
	
	UPROPERTY()
	TObjectPtr<URigVM> RigVM;

	UPROPERTY()
	FRigVMRuntimeSettings VMRuntimeSettings;

	UPROPERTY()
	FAnimNextParamType ReturnType;

#if WITH_EDITORONLY_DATA
	UPROPERTY(VisibleAnywhere, Instanced, Category = "Graph", meta = (ShowInnerProperties))
	TObjectPtr<UObject> EditorData;
#endif
};
