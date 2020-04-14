// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "NiagaraCommon.h"
#include "EdGraph/EdGraph.h"
#include "NiagaraScript.h"
#include "Misc/SecureHash.h"
#include "NiagaraParameterStore.h"
#include "NiagaraGraph.generated.h"

class UNiagaraScriptVariable;

/** This is the type of action that occurred on a given Niagara graph. Note that this should follow from EEdGraphActionType, leaving some slop for growth. */
enum ENiagaraGraphActionType
{
	GRAPHACTION_GenericNeedsRecompile = 0x1 << 16,
};

struct FInputPinsAndOutputPins
{
	TArray<UEdGraphPin*> InputPins;
	TArray<UEdGraphPin*> OutputPins;
};

class UNiagaraNode;
class UNiagaraGraph;

USTRUCT()
struct FNiagaraGraphParameterReference
{
	GENERATED_USTRUCT_BODY()
public:
	FNiagaraGraphParameterReference() {}
	FNiagaraGraphParameterReference(const FGuid& InKey, UObject* InValue): Key(InKey), Value(InValue){}


	UPROPERTY()
	FGuid Key;

	UPROPERTY()
	TWeakObjectPtr<UObject> Value;

	FORCEINLINE bool operator==(const FNiagaraGraphParameterReference& Other)const
	{
		return Other.Key == Key && Other.Value == Value;
	}
};

USTRUCT()
struct FNiagaraGraphParameterReferenceCollection
{

	GENERATED_USTRUCT_BODY()
public:
	FNiagaraGraphParameterReferenceCollection(const bool bInCreated = false);

	/** All the references in the graph. */
	UPROPERTY()
	TArray<FNiagaraGraphParameterReference> ParameterReferences;

	UPROPERTY()
	const UNiagaraGraph* Graph;

	/** Returns true if this parameter was initially created by the user. */
	bool WasCreated() const;

private:
	/** Whether this parameter was initially created by the user. */
	UPROPERTY()
	bool bCreated;
};


/** Container for UNiagaraGraph cached data for managing CompileIds and Traversals.*/
USTRUCT()
struct FNiagaraGraphScriptUsageInfo
{
	GENERATED_USTRUCT_BODY()

public:
	FNiagaraGraphScriptUsageInfo();

	/** A guid which is generated when this usage info is created.  Allows for forced recompiling when the cached ids are invalidated. */
	UPROPERTY()
	FGuid BaseId;

	/** The context in which this sub-graph traversal will be used.*/
	UPROPERTY()
	ENiagaraScriptUsage UsageType;
	
	/** The particular instance of the usage type. Event scripts, for example, have potentially multiple graphs.*/
	UPROPERTY()
	FGuid UsageId;

	/** The hash that we calculated last traversal. */
	UPROPERTY()
	FNiagaraCompileHash CompileHash;

	/** The hash that we calculated last traversal. */
	UPROPERTY()
	FNiagaraCompileHash CompileHashFromGraph;

	UPROPERTY(Transient)
	TArray<FNiagaraCompileHashVisitorDebugInfo> CompileLastObjects;


	/** The traversal of output to input nodes for this graph. This is not a recursive traversal, it just includes nodes from this graph.*/
	UPROPERTY()
	TArray<UNiagaraNode*> Traversal;

	void PostLoad(UObject* Owner);

private:
	UPROPERTY()
	TArray<uint8> DataHash_DEPRECATED;

	UPROPERTY()
	FGuid GeneratedCompileId_DEPRECATED;
};

struct FNiagaraGraphFunctionAliasContext
{
	ENiagaraScriptUsage CompileUsage;
	TArray<UEdGraphPin*> StaticSwitchValues;
};

UCLASS(MinimalAPI)
class UNiagaraGraph : public UEdGraph
{
	GENERATED_UCLASS_BODY()

	DECLARE_MULTICAST_DELEGATE(FOnDataInterfaceChanged);
	DECLARE_DELEGATE_RetVal_OneParam(TSharedRef<SWidget>, FOnGetPinVisualWidget, const UEdGraphPin*);
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnSubObjectSelectionChanged, const UObject*);

	//~ Begin UObject Interface
	virtual void PostLoad() override;
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
	//~ End UObjet Interface
	
	/** Get the source that owns this graph */
	class UNiagaraScriptSource* GetSource() const;

	/** Determine if there are any nodes in this graph.*/
	bool IsEmpty() const { return Nodes.Num() == 0; }
			
	/** Find the first output node bound to the target usage type.*/
	class UNiagaraNodeOutput* FindOutputNode(ENiagaraScriptUsage TargetUsageType, FGuid TargetUsageId = FGuid()) const;
	class UNiagaraNodeOutput* FindEquivalentOutputNode(ENiagaraScriptUsage TargetUsageType, FGuid TargetUsageId = FGuid()) const;

	/** Find all output nodes.*/
	void FindOutputNodes(TArray<UNiagaraNodeOutput*>& OutputNodes) const;
	void FindOutputNodes(ENiagaraScriptUsage TargetUsageType, TArray<UNiagaraNodeOutput*>& OutputNodes) const;
	void FindEquivalentOutputNodes(ENiagaraScriptUsage TargetUsageType, TArray<UNiagaraNodeOutput*>& OutputNodes) const;

	/** Options for the FindInputNodes function */
	struct FFindInputNodeOptions
	{
		FFindInputNodeOptions()
			: bSort(false)
			, bIncludeParameters(true)
			, bIncludeAttributes(true)
			, bIncludeSystemConstants(true)
			, bIncludeTranslatorConstants(false)
			, bFilterDuplicates(false)
			, bFilterByScriptUsage(false)
			, TargetScriptUsage(ENiagaraScriptUsage::Function)
		{
		}

		/** Whether or not to sort the nodes, defaults to false. */
		bool bSort;
		/** Whether or not to include parameters, defaults to true. */
		bool bIncludeParameters;
		/** Whether or not to include attributes, defaults to true. */
		bool bIncludeAttributes;
		/** Whether or not to include system parameters, defaults to true. */
		bool bIncludeSystemConstants;
		/** Whether or not to include translator parameters, defaults to false. */
		bool bIncludeTranslatorConstants;
		/** Whether of not to filter out duplicate nodes, defaults to false. */
		bool bFilterDuplicates;
		/** Whether or not to limit to nodes connected to an output node of the specified script type.*/
		bool bFilterByScriptUsage;
		/** The specified script usage required for an input.*/
		ENiagaraScriptUsage TargetScriptUsage;
		/** The specified id within the graph of the script usage*/
		FGuid TargetScriptUsageId;
	};

	/** Options for the AddParameter function. */
	struct FAddParameterOptions
	{
		FAddParameterOptions()
			: NewParameterScopeName(TOptional<FName>())
			, NewParameterUsage(TOptional<ENiagaraScriptParameterUsage>())
			, bRefreshMetaDataScopeAndUsage(false)
			, bIsStaticSwitch(false)
			, bMakeParameterNameUnique(false)
			, bAddedFromSystemEditor(false)
		{};

		/** Optional scope to assign to the new parameter. New scope is force assigned and will ignore any scope that would be assigned if bRefreshMetaDataScopeAndUsage is true. */
		TOptional<FName> NewParameterScopeName;
		/** Optional usage to assign to the new parameter. New usage is force assigned and will ignore any usage that would be assigned if bRefreshMetaDataScopeAndUsage is true.*/
		TOptional<ENiagaraScriptParameterUsage> NewParameterUsage;
		/** Whether the new or already existing parameter should refresh its scope and usage. If no scope is assigned, assign a scope from the variable name. Set a new usage depending on associated in/out pins in the graph. */
		bool bRefreshMetaDataScopeAndUsage;
		/** Whether the new parameter should be a static switch parameter. */
		bool bIsStaticSwitch;
		/** Whether the new parameter should have a unique name (append 001 onwards if new name is an alias.) */
		bool bMakeParameterNameUnique;
		/** Whether the new parameter was added from the System Toolkit. Will set bCreatedInSystemEditor on the new parameter's metadata. */
		bool bAddedFromSystemEditor;
	};

	/** Finds input nodes in the graph with. */
	void FindInputNodes(TArray<class UNiagaraNodeInput*>& OutInputNodes, FFindInputNodeOptions Options = FFindInputNodeOptions()) const;

	/** Returns a list of variable inputs for all static switch nodes in the graph. */
	TArray<FNiagaraVariable> FindStaticSwitchInputs(bool bReachableOnly = false) const;

	/** Get an in-order traversal of a graph by the specified target output script usage.*/
	void BuildTraversal(TArray<class UNiagaraNode*>& OutNodesTraversed, ENiagaraScriptUsage TargetUsage, FGuid TargetUsageId, bool bEvaluateStaticSwitches = false) const;
	static void BuildTraversal(TArray<class UNiagaraNode*>& OutNodesTraversed, UNiagaraNode* FinalNode, bool bEvaluateStaticSwitches = false);

	/** Generates a list of unique input and output parameters for when this script is used as a function. */
	void GetParameters(TArray<FNiagaraVariable>& Inputs, TArray<FNiagaraVariable>& Outputs) const;

	/** Returns the index of this variable in the output node of the graph. INDEX_NONE if this is not a valid attribute. */
	int32 GetOutputNodeVariableIndex(const FNiagaraVariable& Attr)const;
	void GetOutputNodeVariables(TArray< FNiagaraVariable >& OutAttributes)const;
	void GetOutputNodeVariables(ENiagaraScriptUsage InTargetScriptUsage, TArray< FNiagaraVariable >& OutAttributes)const;

	bool HasNumericParameters()const;

	bool HasParameterMapParameters()const;

	/** Signal to listeners that the graph has changed */
	void NotifyGraphNeedsRecompile();
		
	/** Notifies the graph that a contained data interface has changed. */
	void NotifyGraphDataInterfaceChanged();

	/** Get all referenced graphs in this specified graph, including this graph. */
	void GetAllReferencedGraphs(TArray<const UNiagaraGraph*>& Graphs) const;

	/** Gather all the change ids of external references for this specific graph traversal.*/
	void GatherExternalDependencyData(ENiagaraScriptUsage InUsage, const FGuid& InUsageId, TArray<FNiagaraCompileHash>& InReferencedCompileHashes, TArray<FString>& InReferencedObjs);

	/** Determine if there are any external dependencies wrt to scripts and ensure that those dependencies are sucked into the existing package.*/
	void SubsumeExternalDependencies(TMap<const UObject*, UObject*>& ExistingConversions);

	/** Determine if another item has been synchronized with this graph.*/
	bool IsOtherSynchronized(const FGuid& InChangeId) const;

	/** Identify that this graph has undergone changes that will require synchronization with a compiled script.*/
	void MarkGraphRequiresSynchronization(FString Reason);

	/** A change was made to the graph that external parties should take note of. The ChangeID will be updated.*/
	virtual void NotifyGraphChanged() override;

	/** Each graph is given a Change Id that occurs anytime the graph's content is manipulated. This key changing induces several important activities, including being a 
	value that third parties can poll to see if their cached handling of the graph needs to potentially adjust to changes. Furthermore, for script compilation we cache 
	the changes that were produced during the traversal of each output node, which are referred to as the CompileID.*/
	FGuid GetChangeID() { return ChangeId; }

	/** Gets the current compile data hash associated with the output node traversal specified by InUsage and InUsageId. If the usage is not found, an invalid hash is returned.*/
	FNiagaraCompileHash GetCompileDataHash(ENiagaraScriptUsage InUsage, const FGuid& InUsageId) const;

	/** Gets the current base id associated with the output node traversal specified by InUsage and InUsageId. If the usage is not found, an invalid guid is returned. */
	FGuid GetBaseId(ENiagaraScriptUsage InUsage, const FGuid& InUsageId) const;

	/** Forces the base compile id for the supplied script.  This should only be used to keep things consistent after an emitter merge. */
	void ForceBaseId(ENiagaraScriptUsage InUsage, const FGuid& InUsageId, const FGuid InForcedBaseId);

	/** Walk through the graph for an ParameterMapGet nodes and see if any of them specify a default for VariableName.*/
	UEdGraphPin* FindParameterMapDefaultValuePin(const FName VariableName, ENiagaraScriptUsage InUsage, ENiagaraScriptUsage InParentUsage) const;

	/** Walk through the graph for an ParameterMapGet nodes and find all matching default pins for VariableName, irrespective of usage. */
	TArray<UEdGraphPin*> FindParameterMapDefaultValuePins(const FName VariableName) const;

	/** Gets the meta-data associated with this variable, if it exists.*/
	TOptional<FNiagaraVariableMetaData> GetMetaData(const FNiagaraVariable& InVar) const;

	/** Sets the meta-data associated with this variable. Creates a new UNiagaraScriptVariable if the target variable cannot be found. Illegal to call on FNiagaraVariables that are Niagara Constants. */
	void SetMetaData(const FNiagaraVariable& InVar, const FNiagaraVariableMetaData& MetaData);

	/** Sets the usage metadata associated with this variable for a given script. Creates a new UNiagaraScriptVariable if the target variable cannot be found. */
	void SetPerScriptMetaData(const FNiagaraVariable& InVar, const FNiagaraVariableMetaData& InMetaData);


	const TMap<FNiagaraVariable, UNiagaraScriptVariable*>& GetAllMetaData() const;
	TMap<FNiagaraVariable, UNiagaraScriptVariable*>& GetAllMetaData();

	const TMap<FNiagaraVariable, FNiagaraGraphParameterReferenceCollection>& GetParameterReferenceMap() const; // NOTE: The const is a lie! (This indirectly calls RefreshParameterReferences, which can recreate the entire map)

	UNiagaraScriptVariable* GetScriptVariable(FNiagaraVariable Parameter) const;
	UNiagaraScriptVariable* GetScriptVariable(FName ParameterName) const;

	/** Adds parameter to parameters map setting it as created by the user.*/
	UNiagaraScriptVariable* AddParameter(const FNiagaraVariable& Parameter, bool bIsStaticSwitch = false);

	UNiagaraScriptVariable* AddParameter(FNiagaraVariable& Parameter, const FAddParameterOptions Options);

	/** Adds an FNiagaraGraphParameterReference to the ParameterToReferenceMap. */
	void AddParameterReference(const FNiagaraVariable& Parameter, FNiagaraGraphParameterReference& NewParameterReference);

	/** Remove parameter from map and all the pins associated. */
	void RemoveParameter(const FNiagaraVariable& Parameter, bool bAllowDeleteStaticSwitch = false);

	/**
	* Rename parameter from map and all the pins associated.
	*
	* @param Parameter				The target FNiagaraVariable key to lookup the canonical UNiagaraScriptVariable to rename.
	* @param NewName				The new name to apply.
	* @param bFromStaticSwitch		Whether the target parameter is from a static switch. Used to determine whether to fixup binding strings.
	* @param NewScopeName			The new scope name from the FNiagaraVariable's associated FNiagaraVariableMetaData. Used if changing the scope triggered a rename. Applied to the canonical UNiagaraScriptVariable's Metadata.
	* @param bMerged				Whether or not the rename ended up merging with a different parameter because the names are the same.
	* @return						true if the new name was applied. 
	*/
	bool RenameParameter(const FNiagaraVariable& Parameter, FName NewName, bool bRenameRequestedFromStaticSwitch = false, FName NewScopeName = FName(), bool* bMerged = nullptr);

	/** Rename a pin inline in a graph. If this is the only instance used in the graph, then rename them all, otherwise make a duplicate. */
	bool RenameParameterFromPin(const FNiagaraVariable& Parameter, FName NewName, UEdGraphPin* InPin);


	/** 
	 * Sets the ENiagaraScriptParameterUsage on the Metadata of a ScriptVariable depending on its pin usage in the node graph (used on input/output pins). 
	 * 
	 * @param ScriptVariable		The ScriptVariable to update the Metadata of.
	 * @return						false if the ScriptVariable no longer has any referencing Map Get or Set pins in its owning Graph.
	*/
	bool UpdateUsageForScriptVariable(UNiagaraScriptVariable* ScriptVariable) const;

	/** Gets a delegate which is called whenever a contained data interfaces changes. */
	FOnDataInterfaceChanged& OnDataInterfaceChanged();

	/** Gets a delegate which is called whenever a custom subobject in the graph is selected*/
	FOnSubObjectSelectionChanged& OnSubObjectSelectionChanged();

	void ForceGraphToRecompileOnNextCheck();

	/** Add a listener for OnGraphNeedsRecompile events */
	FDelegateHandle AddOnGraphNeedsRecompileHandler(const FOnGraphChanged::FDelegate& InHandler);

	/** Remove a listener for OnGraphNeedsRecompile events */
	void RemoveOnGraphNeedsRecompileHandler(FDelegateHandle Handle);

	FNiagaraTypeDefinition GetCachedNumericConversion(class UEdGraphPin* InPin);

	const class UEdGraphSchema_Niagara* GetNiagaraSchema() const;

	void InvalidateNumericCache();

	/** If this graph is the source of a function call, it can add a string to the function name to discern it from different
	  * function calls to the same graph. For example, if the graph contains static switches and two functions call it with
	  * different switch parameters, the final function names in the hlsl must be different.
	  */
	FString GetFunctionAliasByContext(const FNiagaraGraphFunctionAliasContext& FunctionAliasContext);

	void RebuildCachedCompileIds(bool bForce = false);

	void CopyCachedReferencesMap(UNiagaraGraph* TargetGraph);

	bool IsPinVisualWidgetProviderRegistered() const;

	/** Wrapper to provide visual widgets for pins that are managed by external viewmodels.
	 *  Used with FNiagaraScriptToolkitParameterPanelViewModel to provide SNiagaraParameterNameView widget with validation delegates.
	 */
	TSharedRef<SWidget> GetPinVisualWidget(const UEdGraphPin* Pin) const;

	FDelegateHandle RegisterPinVisualWidgetProvider(FOnGetPinVisualWidget OnGetPinVisualWidget);
	void UnregisterPinVisualWidgetProvider(const FDelegateHandle& InHandle);

	void ScriptVariableChanged(FNiagaraVariable Variable);

	/** Go through all known parameter names in this graph and generate a new unique one.*/
	FName MakeUniqueParameterName(const FName& InName);


	static FName MakeUniqueParameterNameAcrossGraphs(const FName& InName, TArray<TWeakObjectPtr<UNiagaraGraph>>& InGraphs);

	static FName StandardizeName(FName Name, ENiagaraScriptUsage Usage, bool bIsGet, bool bIsSet);

protected:
	void RebuildNumericCache();
	bool bNeedNumericCacheRebuilt;
	TMap<TPair<FGuid, UEdGraphNode*>, FNiagaraTypeDefinition> CachedNumericConversions;
	void ResolveNumerics(TMap<UNiagaraNode*, bool>& VisitedNodes, UEdGraphNode* Node);
	bool AppendCompileHash(FNiagaraCompileHashVisitor* InVisitor, const TArray<UNiagaraNode*>& InTraversal) const;

private:
	virtual void NotifyGraphChanged(const FEdGraphEditAction& InAction) override;

	/** Find parameters in the graph. */
	void RefreshParameterReferences() const;

	/** Marks the found parameter collections as invalid so they're rebuilt the next time they're requested. */
	void InvalidateCachedParameterData();

	/** When a new variable is added to the VariableToScriptVariableMap, generate appropriate scope and usage. */
	void GenerateMetaDataForScriptVariable(UNiagaraScriptVariable* InScriptVariable) const;

	/** Helper to get a map of variables to all input/output pins with the same name. */
	const TMap<FNiagaraVariable, FInputPinsAndOutputPins> CollectVarsToInOutPinsMap() const;

	/**
	 * Set the usage of a script variable depending on input/output pins with same name.
	 * @param VarToPinsMap			Mapping of Pins to the associated UNiagaraScriptVariable.
	 * @param ScriptVariable		The UNiagaraScriptVariable to modify its Metadata Usage.
	 * @return ShouldDelete			True if the ScriptVariable was previously of a non-local type and no longer has any associated Input or Output pins.
	 */
	bool SetScriptVariableUsageForPins(const TMap<FNiagaraVariable, FInputPinsAndOutputPins>& VarToPinsMap, UNiagaraScriptVariable* ScriptVariable) const;

	/** A delegate that broadcasts a notification whenever the graph needs recompile due to structural change. */
	FOnGraphChanged OnGraphNeedsRecompile;

	/** Find all nodes in the graph that can be reached during compilation. */
	TArray<UEdGraphNode*> FindReachbleNodes() const;

	/** Compares the values on the default pins with the metadata and syncs the two if necessary */
	void ValidateDefaultPins();

	void StandardizeParameterNames();

private:
	/** The current change identifier for this graph overall. Used to sync status with UNiagaraScripts.*/
	UPROPERTY()
	FGuid ChangeId;

	/** Internal value used to invalidate a DDC key for the script no matter what.*/
	UPROPERTY()
	FGuid ForceRebuildId;

	UPROPERTY()
	FGuid LastBuiltTraversalDataChangeId;

	UPROPERTY()
	TArray<FNiagaraGraphScriptUsageInfo> CachedUsageInfo;

	/** Storage of meta-data for variables defined for use explicitly with this graph.*/
	UPROPERTY()
	mutable TMap<FNiagaraVariable, FNiagaraVariableMetaData> VariableToMetaData_DEPRECATED;

	/** Storage of variables defined for use with this graph.*/
	UPROPERTY()
	mutable TMap<FNiagaraVariable, UNiagaraScriptVariable*> VariableToScriptVariable;
	
	/** A map of parameters in the graph to their referencers. */
	UPROPERTY(Transient)
	mutable TMap<FNiagaraVariable, FNiagaraGraphParameterReferenceCollection> ParameterToReferencesMap;

	FOnDataInterfaceChanged OnDataInterfaceChangedDelegate;
	FOnSubObjectSelectionChanged OnSelectedSubObjectChanged;

	/** Whether currently renaming a parameter to prevent recursion. */
	bool bIsRenamingParameter;

	mutable bool bParameterReferenceRefreshPending;

	FOnGetPinVisualWidget OnGetPinVisualWidgetDelegate;
};
