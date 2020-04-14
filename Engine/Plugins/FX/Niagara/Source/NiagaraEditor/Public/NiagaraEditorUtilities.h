// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NiagaraCommon.h"
#include "UObject/StructOnScope.h"
#include "Misc/Attribute.h"
#include "AssetData.h"
#include "NiagaraGraph.h"
#include "NiagaraEditorSettings.h"

class UNiagaraNodeInput;
class UNiagaraNodeOutput;
class UNiagaraNodeFunctionCall;
struct FNiagaraVariable;
struct FNiagaraTypeDefinition;
class UNiagaraGraph;
class UNiagaraSystem;
class FNiagaraSystemViewModel;
struct FNiagaraEmitterHandle;
class UNiagaraEmitter;
class UNiagaraScript;
class FStructOnScope;
class UEdGraph;
class UEdGraphNode;
class SWidget;
class UNiagaraNode;
class UEdGraphSchema_Niagara;
class UEdGraphPin;
class FCompileConstantResolver;
class UNiagaraStackEditorData;
class FMenuBuilder;
class FNiagaraEmitterViewModel;
class FNiagaraEmitterHandleViewModel;
enum class ECheckBoxState : uint8;
struct FNiagaraNamespaceMetadata;
class FNiagaraParameterHandle;

namespace FNiagaraEditorUtilities
{
	/** Determines if the contents of two sets matches */
	// TODO: Move this to TSet.
	template<typename ElementType>
	bool SetsMatch(const TSet<ElementType>& SetA, const TSet<ElementType>& SetB)
	{
		if (SetA.Num() != SetB.Num())
		{
			return false;
		}

		for (ElementType SetItemA : SetA)
		{
			if (SetB.Contains(SetItemA) == false)
			{
				return false;
			}
		}

		return true;
	}

	/** Determines if the contents of an array matches a set */
	// TODO: Move this to TSet.
	template<typename ElementType>
	bool ArrayMatchesSet(const TArray<ElementType>& Array, const TSet<ElementType>& Set)
	{
		if (Array.Num() != Set.Num())
		{
			return false;
		}

		for (ElementType ArrayItem : Array)
		{
			if (Set.Contains(ArrayItem) == false)
			{
				return false;
			}
		}

		return true;
	}

	/** Gets a set of the system constant names. */
	TSet<FName> GetSystemConstantNames();

	/** Resets the variables value to default, either based on the struct, or if available through registered type utilities. */
	void ResetVariableToDefaultValue(FNiagaraVariable& Variable);

	/** Fills DefaultData with the types default, either based on the struct, or if available through registered type utilities. */
	void NIAGARAEDITOR_API GetTypeDefaultValue(const FNiagaraTypeDefinition& Type, TArray<uint8>& DefaultData);

	/** Sets up a niagara input node for parameter usage. */
	void InitializeParameterInputNode(UNiagaraNodeInput& InputNode, const FNiagaraTypeDefinition& Type, const UNiagaraGraph* Graph, FName InputName = FName(TEXT("NewInput")));

	/** Writes text to a specified location on disk.*/
	void NIAGARAEDITOR_API WriteTextFileToDisk(FString SaveDirectory, FString FileName, FString TextToSave, bool bAllowOverwriting = false);

	/** Gathers up the change Id's and optionally writes them to disk.*/
	void GatherChangeIds(UNiagaraEmitter& Emitter, TMap<FGuid, FGuid>& ChangeIds, const FString& InDebugName, bool bWriteToLogDir = false);
	void GatherChangeIds(UNiagaraGraph& Graph, TMap<FGuid, FGuid>& ChangeIds, const FString& InDebugName, bool bWriteToLogDir = false);

	/** Options for the GetParameterVariablesFromSystem function. */
	struct FGetParameterVariablesFromSystemOptions
	{
		FGetParameterVariablesFromSystemOptions()
			: bIncludeStructParameters(true)
			, bIncludeDataInterfaceParameters(true)
		{
		}

		bool bIncludeStructParameters;
		bool bIncludeDataInterfaceParameters;
	};

	/** Gets the niagara variables for the input parameters on a niagara System. */
	void GetParameterVariablesFromSystem(UNiagaraSystem& System, TArray<FNiagaraVariable>& ParameterVariables, FGetParameterVariablesFromSystemOptions Options = FGetParameterVariablesFromSystemOptions());

	/** Helper to clean up copy & pasted graphs.*/
	void FixUpPastedNodes(UEdGraph* Graph, TSet<UEdGraphNode*> PastedNodes);

	/** Helper to convert compile status to text.*/
	FText StatusToText(ENiagaraScriptCompileStatus Status);

	/** Helper method to union two distinct compiler statuses.*/
	ENiagaraScriptCompileStatus UnionCompileStatus(const ENiagaraScriptCompileStatus& StatusA, const ENiagaraScriptCompileStatus& StatusB);

	/** Returns whether the data in a niagara variable and a struct on scope match */
	bool DataMatches(const FNiagaraVariable& Variable, const FStructOnScope& StructOnScope);

	/** Returns whether the data in two niagara variables match. */
	bool DataMatches(const FNiagaraVariable& VariableA, const FNiagaraVariable& VariableB);

	/** Returns whether the data in two structs on scope matches. */
	bool DataMatches(const FStructOnScope& StructOnScopeA, const FStructOnScope& StructOnScopeB);

	void NIAGARAEDITOR_API CopyDataTo(FStructOnScope& DestinationStructOnScope, const FStructOnScope& SourceStructOnScope, bool bCheckTypes = true);

	TSharedPtr<SWidget> CreateInlineErrorText(TAttribute<FText> ErrorMessage, TAttribute<FText> ErrorTooltip);

	void CompileExistingEmitters(const TArray<UNiagaraEmitter*>& AffectedEmitters);

	bool TryGetEventDisplayName(UNiagaraEmitter* Emitter, FGuid EventUsageId, FText& OutEventDisplayName);

	bool IsCompilableAssetClass(UClass* AssetClass);

	void MarkDependentCompilableAssetsDirty(TArray<UObject*> InObjects);

	void ResolveNumerics(UNiagaraGraph* SourceGraph, bool bForceParametersToResolveNumerics, TArray<FNiagaraVariable>& ChangedNumericParams);

	void FixUpNumericPins(const UEdGraphSchema_Niagara* Schema, UNiagaraNode* Node);

	void SetStaticSwitchConstants(UNiagaraGraph* Graph, const TArray<UEdGraphPin*>& CallInputs, const FCompileConstantResolver& ConstantResolver);

	bool ResolveConstantValue(UEdGraphPin* Pin, int32& Value);

	TSharedPtr<FStructOnScope> StaticSwitchDefaultIntToStructOnScope(int32 InStaticSwitchDefaultValue, FNiagaraTypeDefinition InSwitchType);

	void PreprocessFunctionGraph(const UEdGraphSchema_Niagara* Schema, UNiagaraGraph* Graph, const TArray<UEdGraphPin*>& CallInputs, const TArray<UEdGraphPin*>& CallOutputs, ENiagaraScriptUsage ScriptUsage, const FCompileConstantResolver& ConstantResolver);

	bool PODPropertyAppendCompileHash(const void* Container, FProperty* Property, const FString& PropertyName, struct FNiagaraCompileHashVisitor* InVisitor);
	bool NestedPropertiesAppendCompileHash(const void* Container, const UStruct* Struct, EFieldIteratorFlags::SuperClassFlags IteratorFlags, const FString& BaseName, struct FNiagaraCompileHashVisitor* InVisitor);

	/** Options for the GetScriptsByFilter function. 
	** @Param ScriptUsageToInclude Only return Scripts that have this usage
	** @Param (Optional) TargetUsageToMatch Only return Scripts that have this target usage (output node) 
	** @Param bIncludeDeprecatedScripts Whether or not to return Scripts that are deprecated (defaults to false) 
	** @Param bIncludeNonLibraryScripts Whether or not to return non-library scripts (defaults to false)
	*/
	struct FGetFilteredScriptAssetsOptions
	{
		FGetFilteredScriptAssetsOptions()
			: ScriptUsageToInclude(ENiagaraScriptUsage::Module)
			, TargetUsageToMatch()
			, bIncludeDeprecatedScripts(false)
			, bIncludeNonLibraryScripts(false)
		{
		}

		ENiagaraScriptUsage ScriptUsageToInclude;
		TOptional<ENiagaraScriptUsage> TargetUsageToMatch;
		bool bIncludeDeprecatedScripts;
		bool bIncludeNonLibraryScripts;
	};

	NIAGARAEDITOR_API void GetFilteredScriptAssets(FGetFilteredScriptAssetsOptions InFilter, TArray<FAssetData>& OutFilteredScriptAssets); 

	NIAGARAEDITOR_API UNiagaraNodeOutput* GetScriptOutputNode(UNiagaraScript& Script);

	UNiagaraScript* GetScriptFromSystem(UNiagaraSystem& System, FGuid EmitterHandleId, ENiagaraScriptUsage Usage, FGuid UsageId);

	/**
	 * Gets an emitter handle from a system and an owned emitter.  This handle will become invalid if emitters are added or
	 * removed from the system, so in general this value should not be cached across frames.
	 * @param System The source system which owns the emitter handles.
	 * @param The emitter to search for in the system.
	 * @returns The emitter handle for the supplied emitter, or nullptr if the emitter isn't owned by this system.
	 */
	const FNiagaraEmitterHandle* GetEmitterHandleForEmitter(UNiagaraSystem& System, UNiagaraEmitter& Emitter);

	NIAGARAEDITOR_API bool IsScriptAssetInLibrary(const FAssetData& ScriptAssetData);

	NIAGARAEDITOR_API FText FormatScriptName(FName Name, bool bIsInLibrary);

	NIAGARAEDITOR_API FText FormatScriptDescription(FText Description, FName Path, bool bIsInLibrary);

	NIAGARAEDITOR_API FText FormatVariableDescription(FText Description, FText Name, FText Type);

	void ResetSystemsThatReferenceSystemViewModel(const FNiagaraSystemViewModel& ReferencedSystemViewModel);

	TArray<UNiagaraComponent*> GetComponentsThatReferenceSystem(const UNiagaraSystem& ReferencedSystem);

	TArray<UNiagaraComponent*> GetComponentsThatReferenceSystemViewModel(const FNiagaraSystemViewModel& ReferencedSystemViewModel);

	const FGuid AddEmitterToSystem(UNiagaraSystem& InSystem, UNiagaraEmitter& InEmitterToAdd);

	void RemoveEmittersFromSystemByEmitterHandleId(UNiagaraSystem& InSystem, TSet<FGuid> EmitterHandleIdsToDelete);

	/** Kills all system instances using the referenced system. */
	void KillSystemInstances(const UNiagaraSystem& System);


	bool VerifyNameChangeForInputOrOutputNode(const UNiagaraNode& NodeBeingChanged, FName OldName, FName NewName, FText& OutErrorMessage);

	/**
	 * Adds a new Parameter to a target ParameterStore with an undo/redo transaction and name collision handling.
	 * @param NewParameterVariable The FNiagaraVariable to be added to TargetParameterStore. MUST be a unique object, do not pass an existing reference.
	 * @param TargetParameterStore The ParameterStore to receive NewVariable.
	 * @param ParameterStoreOwner The UObject to call Modify() on for the undo/redo transaction of adding NewVariable.
	 * @param StackEditorData The editor data used to mark the newly added FNiagaraVariable in the Stack for renaming.
	 * @returns Bool for whether adding the parameter succeeded.
	 */
	bool AddParameter(FNiagaraVariable& NewParameterVariable, FNiagaraParameterStore& TargetParameterStore, UObject& ParameterStoreOwner, UNiagaraStackEditorData* StackEditorData);

	NIAGARAEDITOR_API bool AddEmitterContextMenuActions(FMenuBuilder& MenuBuilder, const TSharedPtr<FNiagaraEmitterHandleViewModel>& EmitterHandleViewModel);

	void ShowParentEmitterInContentBrowser(TSharedRef<FNiagaraEmitterViewModel> EmitterViewModel);

	void OpenParentEmitterForEdit(TSharedRef<FNiagaraEmitterViewModel> Emitter);
	ECheckBoxState GetSelectedEmittersEnabledCheckState(TSharedRef<FNiagaraSystemViewModel> SystemViewModel);
	void ToggleSelectedEmittersEnabled(TSharedRef<FNiagaraSystemViewModel> SystemViewModel);

	ECheckBoxState GetSelectedEmittersIsolatedCheckState(TSharedRef<FNiagaraSystemViewModel> SystemViewModel);
	void ToggleSelectedEmittersIsolated(TSharedRef<FNiagaraSystemViewModel> SystemViewModel);

	void CreateAssetFromEmitter(TSharedRef<FNiagaraEmitterHandleViewModel> EmitterHandleViewModel);

	NIAGARAEDITOR_API void WarnWithToastAndLog(FText WarningMessage);
	NIAGARAEDITOR_API void InfoWithToastAndLog(FText WarningMessage, float ToastDuration = 5.0f);

	void GetScriptRunAndExecutionIndexFromUsage(const ENiagaraScriptUsage& InUsage, int32& OutRunIndex, int32&OutExecutionIndex);

	FName GetUniqueObjectName(UObject* Outer, UClass* ObjectClass, const FString& CandidateName);

	template<typename T>
	FName GetUniqueObjectName(UObject* Outer, const FString& CandidateName)
	{
		return GetUniqueObjectName(Outer, T::StaticClass(), CandidateName);
	}

	/** Gets the Scope and notifies if it does not apply due to an override being set.
	 * @params MetaData				The MetaData to get the namespace string for.
	 * @params OutScope		The Scope to return.
	 * @return bool			Whether the returned scope is not overridden. Is false if bUseLegacyNameString is set.
	 */
	bool GetVariableMetaDataScope(const FNiagaraVariableMetaData& MetaData, ENiagaraParameterScope& OutScope);

	/** Gets the Namespace string and notifies if it does not apply due to an override being set.
	 * @params MetaData				The MetaData to get the namespace string for.
	 * @params OutNamespaceString	The Namespace string to return.
	 * @return bool					Whether the returned Namespace string is valid. Is false if bUseLegacyNameString is set.
	 */
	bool GetVariableMetaDataNamespaceString(const FNiagaraVariableMetaData& MetaData, FString& OutNamespaceString);

	/** Gets the Namespace string and notifies if it does not apply due to an override being set.
	 * @params MetaData				The MetaData to get the namespace string for.
	 * @params NewScopeName			The NewScopeName to consider when getting the namespace string.
	 * @params OutNamespaceString	The Namespace string to return.
	 * @return bool					Whether the returned Namespace string is valid. Is false if bUseLegacyNameString is set.
	 */
	bool GetVariableMetaDataNamespaceStringForNewScope(const FNiagaraVariableMetaData& MetaData, const FName& NewScopeName, FString& OutNamespaceString);

	FName GetScopeNameForParameterScope(ENiagaraParameterScope InScope);

	bool IsScopeEditable(const FName& InScopeName);
	bool IsScopeUserAssignable(const FName& InScopeName);

	TArray<FName> DecomposeVariableNamespace(const FName& InVarNameToken, FName& OutName);

	void  RecomposeVariableNamespace(const FName& InVarNameToken, const TArray<FName>& InParentNamespaces, FName& OutName);

	void GetParameterMetaDataFromName(const FName& InVarNameToken, FNiagaraVariableMetaData& OutMetaData);

	FString GetNamespacelessVariableNameString(const FName& InVarName);

	void GetReferencingFunctionCallNodes(UNiagaraScript* Script, TArray<UNiagaraNodeFunctionCall*>& OutReferencingFunctionCallNodes);

	// Compare two FNiagaraVariable names for the sort priority relative to the first argument VarNameA. Sorting is ordered by namespace and then alphabetized. 
	bool GetVariableSortPriority(const FName& VarNameA, const FName& VarNameB);

	// Compare two FNiagaraNamespaceMetadata for the sort priority relative to the first argument A, where a lower number represents a higher priority.
	int32 GetNamespaceMetaDataSortPriority(const FNiagaraNamespaceMetadata& A, const FNiagaraNamespaceMetadata& B);

	// Get the sort priority of a registered namespace FName, where a lower number represents a higher priority.
	int32 GetNamespaceSortPriority(const FName& Namespace);

	const FNiagaraNamespaceMetadata GetNamespaceMetaDataForVariableName(const FName& VarName);
};

namespace FNiagaraParameterUtilities
{
	bool DoesParameterNameMatchSearchText(FName ParameterName, const FString& SearchTextString);

	FText FormatParameterNameForTextDisplay(FName ParameterName);

	bool GetNamespaceEditData(
		FName InParameterName,
		FNiagaraParameterHandle& OutParameterHandle,
		FNiagaraNamespaceMetadata& OutNamespaceMetadata,
		FText& OutErrorMessage);

	bool GetNamespaceModifierEditData(
		FName InParameterName,
		FNiagaraParameterHandle& OutParameterHandle,
		FNiagaraNamespaceMetadata& OutNamespaceMetadata,
		FText& OutErrorMessage);

	enum class EParameterContext
	{
		Script,
		System
	};

	struct FChangeNamespaceMenuData
	{
		bool bCanChange;
		FText CanChangeToolTip;
		FName NamespaceParameterName;
		FNiagaraNamespaceMetadata Metadata;
	};

	NIAGARAEDITOR_API void GetChangeNamespaceMenuData(FName InParameterName, EParameterContext InParameterContext, TArray<FChangeNamespaceMenuData>& OutChangeNamespaceMenuData);

	NIAGARAEDITOR_API TSharedRef<SWidget> CreateNamespaceMenuItemWidget(FName Namespace, FText ToolTip);

	NIAGARAEDITOR_API bool TestCanChangeNamespaceWithMessage(FName ParameterName, const FNiagaraNamespaceMetadata& NewNamespaceMetadata, FText& OutMessage);

	NIAGARAEDITOR_API FName ChangeNamespace(FName ParameterName, const FNiagaraNamespaceMetadata& NewNamespaceMetadata);

	NIAGARAEDITOR_API bool TestCanAddNamespaceModifierWithMessage(FName ParameterName, FText& OutMessage);

	NIAGARAEDITOR_API FName AddNamespaceModifier(FName InParameterName);

	NIAGARAEDITOR_API bool TestCanRemoveNamespaceModifierWithMessage(FName ParameterName, FText& OutMessage);

	NIAGARAEDITOR_API FName RemoveNamespaceModifier(FName InParameterName);

	NIAGARAEDITOR_API bool TestCanEditNamespaceModifierWithMessage(FName ParameterName, FText& OutMessage);

	NIAGARAEDITOR_API bool TestCanRenameWithMessage(FName ParameterName, FText& OutMessage);
};
