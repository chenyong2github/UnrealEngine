// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IAnimBlueprintCompilerHandler.h"
#include "IPropertyAccessCompiler.h"
#include "Animation/AnimNodeBase.h"

class UAnimGraphNode_Base;
struct FAnimGraphNodePropertyBinding;
class UK2Node_CustomEvent;
class IAnimBlueprintCompilationContext;
class IAnimBlueprintGeneratedClassCompiledData;
class IAnimBlueprintCompilerCreationContext;
class IAnimBlueprintCompilationBracketContext;
class IAnimBlueprintPostExpansionStepContext;
class IAnimBlueprintCopyTermDefaultsContext;

class FAnimBlueprintCompilerHandler_Base : public IAnimBlueprintCompilerHandler
{
public:
	FAnimBlueprintCompilerHandler_Base(IAnimBlueprintCompilerCreationContext& InCreationContext);

	// Adds a map of struct eval handlers for the specified node
	void AddStructEvalHandlers(UAnimGraphNode_Base* InNode, IAnimBlueprintCompilationContext& InCompilationContext, IAnimBlueprintGeneratedClassCompiledData& OutCompiledData);

	// Create an 'expanded' evaluation handler for the specified node, called in the compiler's node expansion step
	void CreateEvaluationHandlerForNode(IAnimBlueprintCompilationContext& InCompilationContext, UAnimGraphNode_Base* InNode);

private:
	void StartCompilingClass(const UClass* InClass, IAnimBlueprintCompilationBracketContext& InCompilationContext, IAnimBlueprintGeneratedClassCompiledData& OutCompiledData);
	void FinishCompilingClass(const UClass* InClass, IAnimBlueprintCompilationBracketContext& InCompilationContext, IAnimBlueprintGeneratedClassCompiledData& OutCompiledData);
	void PostExpansionStep(const UEdGraph* InGraph, IAnimBlueprintPostExpansionStepContext& InCompilationContext, IAnimBlueprintGeneratedClassCompiledData& OutCompiledData);
	void CopyTermDefaultsToDefaultObject(UObject* InDefaultObject, IAnimBlueprintCopyTermDefaultsContext& InCompilationContext, IAnimBlueprintGeneratedClassCompiledData& OutCompiledData);

private:
	/** Record of a single copy operation */
	struct FPropertyCopyRecord
	{
		FPropertyCopyRecord(UEdGraphPin* InDestPin, FProperty* InDestProperty, int32 InDestArrayIndex, TArray<FString>&& InDestPropertyPath)
			: DestPin(InDestPin)
			, DestProperty(InDestProperty)
			, DestArrayIndex(InDestArrayIndex)
			, DestPropertyPath(MoveTemp(InDestPropertyPath))
			, LibraryCopyIndex(INDEX_NONE)
			, LibraryBatchType(EPropertyAccessBatchType::Unbatched)
			, Operation(EPostCopyOperation::None)
			, bIsFastPath(true)
		{}

		FPropertyCopyRecord(const TArray<FString>& InSourcePropertyPath, const TArray<FString>& InDestPropertyPath)
			: DestPin(nullptr)
			, DestProperty(nullptr)
			, DestArrayIndex(INDEX_NONE)
			, SourcePropertyPath(InSourcePropertyPath)
			, DestPropertyPath(InDestPropertyPath)
			, LibraryCopyIndex(INDEX_NONE)
			, LibraryBatchType(EPropertyAccessBatchType::Unbatched)
			, Operation(EPostCopyOperation::None)
			, bIsFastPath(true)
		{}

		bool IsFastPath() const
		{
			return SourcePropertyPath.Num() > 0 && bIsFastPath;
		}

		void InvalidateFastPath()
		{
			bIsFastPath = false;
		}

		/** The destination pin we are copying to */
		UEdGraphPin* DestPin;

		/** The destination property we are copying to (on an animation node) */
		FProperty* DestProperty;

		/** The array index we use if the destination property is an array */
		int32 DestArrayIndex;

		/** The property path relative to the class */
		TArray<FString> SourcePropertyPath;

		/** The property path relative to the class */
		TArray<FString> DestPropertyPath;

		/** The index of the copy in the property access library */
		int32 LibraryCopyIndex;

		/** the batch type within the property access library */
		EPropertyAccessBatchType LibraryBatchType;

		/** Any operation we want to perform post-copy on the destination data */
		EPostCopyOperation Operation;

		/** Fast-path flag */
		bool bIsFastPath;
	};

	// Context used to build fast-path copy records
	struct FCopyRecordGraphCheckContext
	{
		FCopyRecordGraphCheckContext(FPropertyCopyRecord& InCopyRecord, TArray<FPropertyCopyRecord>& InAdditionalCopyRecords, FCompilerResultsLog& InMessageLog)
			: CopyRecord(&InCopyRecord)
			, AdditionalCopyRecords(InAdditionalCopyRecords)
			, MessageLog(InMessageLog)
		{}

		// Copy record we are operating on
		FPropertyCopyRecord* CopyRecord;

		// Things like split input pins can add additional copy records
		TArray<FPropertyCopyRecord>& AdditionalCopyRecords;

		// Message log used to recover original nodes
		FCompilerResultsLog& MessageLog;
	};

	// Wireup record for a single anim node property (which might be an array)
	struct FAnimNodeSinglePropertyHandler
	{
		/** Copy records */
		TArray<FPropertyCopyRecord> CopyRecords;

		// If the anim instance is the container target instead of the node.
		bool bInstanceIsTarget;

		FAnimNodeSinglePropertyHandler()
			: bInstanceIsTarget(false)
		{
		}
	};

	// Record for a property that was exposed as a pin, but wasn't wired up (just a literal)
	struct FEffectiveConstantRecord
	{
	public:
		// The node variable that the handler is in
		class FStructProperty* NodeVariableProperty;

		// The property within the struct to set
		class FProperty* ConstantProperty;

		// The array index if ConstantProperty is an array property, or INDEX_NONE otherwise
		int32 ArrayIndex;

		// The pin to pull the DefaultValue/DefaultObject from
		UEdGraphPin* LiteralSourcePin;

		FEffectiveConstantRecord()
			: NodeVariableProperty(NULL)
			, ConstantProperty(NULL)
			, ArrayIndex(INDEX_NONE)
			, LiteralSourcePin(NULL)
		{
		}

		FEffectiveConstantRecord(FStructProperty* ContainingNodeProperty, UEdGraphPin* SourcePin, FProperty* SourcePinProperty, int32 SourceArrayIndex)
			: NodeVariableProperty(ContainingNodeProperty)
			, ConstantProperty(SourcePinProperty)
			, ArrayIndex(SourceArrayIndex)
			, LiteralSourcePin(SourcePin)
		{
		}

		bool Apply(UObject* Object);
	};

	/** BP execution handler for Anim node */
	struct FEvaluationHandlerRecord
	{
	public:
		// The Node this record came from
		UAnimGraphNode_Base* AnimGraphNode;

		// The node variable that the handler is in
		FStructProperty* NodeVariableProperty;

		// The specific evaluation handler inside the specified node
		int32 EvaluationHandlerIdx;

		// Whether or not our serviced properties are actually on the anim node 
		bool bServicesNodeProperties;

		// Whether or not our serviced properties are actually on the instance instead of the node
		bool bServicesInstanceProperties;

		// Set of properties serviced by this handler (Map from property name to the record for that property)
		TMap<FName, FAnimNodeSinglePropertyHandler> ServicedProperties;

		// The generated custom event node
		TArray<UEdGraphNode*> CustomEventNodes;

		// The resulting function name
		FName HandlerFunctionName;

	public:

		FEvaluationHandlerRecord()
			: AnimGraphNode(nullptr)
			, NodeVariableProperty(nullptr)
			, EvaluationHandlerIdx(INDEX_NONE)
			, bServicesNodeProperties(false)
			, bServicesInstanceProperties(false)
			, HandlerFunctionName(NAME_None)
		{}

		bool IsFastPath() const
		{
			for(TMap<FName, FAnimNodeSinglePropertyHandler>::TConstIterator It(ServicedProperties); It; ++It)
			{
				const FAnimNodeSinglePropertyHandler& AnimNodeSinglePropertyHandler = It.Value();
				for (const FPropertyCopyRecord& CopyRecord : AnimNodeSinglePropertyHandler.CopyRecords)
				{
					if (!CopyRecord.IsFastPath())
					{
						return false;
					}
				}
			}

			return true;
		}

		bool IsValid() const
		{
			return NodeVariableProperty != nullptr;
		}

		void PatchFunctionNameAndCopyRecordsInto(FExposedValueHandler& Handler) const;

		void RegisterPin(UEdGraphPin* DestPin, FProperty* AssociatedProperty, int32 AssociatedPropertyArrayIndex);

		void RegisterPropertyBinding(FProperty* InProperty, const FAnimGraphNodePropertyBinding& InBinding);

		FStructProperty* GetHandlerNodeProperty() const { return NodeVariableProperty; }

		void BuildFastPathCopyRecords(FAnimBlueprintCompilerHandler_Base& InHandler, IAnimBlueprintPostExpansionStepContext& InCompilationContext);

	private:

		bool CheckForVariableGet(FCopyRecordGraphCheckContext& Context, UEdGraphPin* DestPin);

		bool CheckForLogicalNot(FCopyRecordGraphCheckContext& Context, UEdGraphPin* DestPin);

		bool CheckForStructMemberAccess(FCopyRecordGraphCheckContext& Context, UEdGraphPin* DestPin);

		bool CheckForMemberOnlyAccess(FPropertyCopyRecord& Context, UEdGraphPin* DestPin);

		bool CheckForSplitPinAccess(FCopyRecordGraphCheckContext& Context, UEdGraphPin* DestPin);

		bool CheckForArrayAccess(FCopyRecordGraphCheckContext& Context, UEdGraphPin* DestPin);
	};

	// Create an evaluation handler for the specified node/record
	void CreateEvaluationHandler(IAnimBlueprintCompilationContext& InCompilationContext, UAnimGraphNode_Base* InNode, FEvaluationHandlerRecord& Record);

private:
	// Records of pose pins for later patchup with an associated evaluation handler
	TMap<UAnimGraphNode_Base*, FEvaluationHandlerRecord> PerNodeStructEvalHandlers;

	// List of successfully created evaluation handlers
	TArray<FEvaluationHandlerRecord> ValidEvaluationHandlerList;
	TMap<UAnimGraphNode_Base*, int32> ValidEvaluationHandlerMap;

	// List of animation node literals (values exposed as pins but never wired up) that need to be pushed into the CDO
	TArray<FEffectiveConstantRecord> ValidAnimNodePinConstants;

	// Set of used handler function names
	TSet<FName> HandlerFunctionNames;

	// Delegate handle for registering against library pre/post-compilation
	FDelegateHandle PreLibraryCompiledDelegateHandle;
	FDelegateHandle PostLibraryCompiledDelegateHandle;
};