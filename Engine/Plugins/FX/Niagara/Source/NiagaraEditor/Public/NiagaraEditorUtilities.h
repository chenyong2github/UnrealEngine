// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NiagaraCommon.h"
#include "UObject/StructOnScope.h"
#include "Misc/Attribute.h"
#include "AssetData.h"
#include "NiagaraActions.h"
#include "NiagaraGraph.h"
#include "NiagaraEditorSettings.h"
#include "UpgradeNiagaraScriptResults.h"
#include "EdGraph/EdGraphSchema.h"

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
enum class EScriptSource : uint8;
struct FNiagaraNamespaceMetadata;
class FNiagaraParameterHandle;
class INiagaraParameterDefinitionsSubscriberViewModel;

enum class ENiagaraFunctionDebugState : uint8;

struct FRefreshAllScriptsFromExternalChangesArgs
{
	UNiagaraScript* OriginatingScript = nullptr;
	UNiagaraGraph* OriginatingGraph = nullptr;
	UNiagaraParameterDefinitions* OriginatingParameterDefinitions = nullptr;
};

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

	FText GetVariableTypeCategory(const FNiagaraVariable& Variable);

	FText GetTypeDefinitionCategory(const FNiagaraTypeDefinition& TypeDefinition);

	void MarkDependentCompilableAssetsDirty(TArray<UObject*> InObjects);

	void ResolveNumerics(UNiagaraGraph* SourceGraph, bool bForceParametersToResolveNumerics, TArray<FNiagaraVariable>& ChangedNumericParams);

	void FixUpNumericPins(const UEdGraphSchema_Niagara* Schema, UNiagaraNode* Node);

	void SetStaticSwitchConstants(UNiagaraGraph* Graph, TArrayView<UEdGraphPin* const> CallInputs, const FCompileConstantResolver& ConstantResolver);

	bool ResolveConstantValue(UEdGraphPin* Pin, int32& Value);

	TSharedPtr<FStructOnScope> StaticSwitchDefaultIntToStructOnScope(int32 InStaticSwitchDefaultValue, FNiagaraTypeDefinition InSwitchType);

	void PreprocessFunctionGraph(const UEdGraphSchema_Niagara* Schema, UNiagaraGraph* Graph, TArrayView<UEdGraphPin* const> CallInputs, TArrayView<UEdGraphPin* const> CallOutputs, ENiagaraScriptUsage ScriptUsage, const FCompileConstantResolver& ConstantResolver);

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
		enum ESuggestedFiltering
		{
			NoFiltering,
			OnlySuggested,
			NoSuggested
		};
		FGetFilteredScriptAssetsOptions()
			: ScriptUsageToInclude(ENiagaraScriptUsage::Module)
			, TargetUsageToMatch()
			, bIncludeDeprecatedScripts(false)
			, bIncludeNonLibraryScripts(false)
			, SuggestedFiltering(NoFiltering)
		{
		}

		ENiagaraScriptUsage ScriptUsageToInclude;
		TOptional<ENiagaraScriptUsage> TargetUsageToMatch;
		bool bIncludeDeprecatedScripts;
		bool bIncludeNonLibraryScripts;
		ESuggestedFiltering SuggestedFiltering;
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

	NIAGARAEDITOR_API ENiagaraScriptLibraryVisibility GetScriptAssetVisibility(const FAssetData& ScriptAssetData);

	/** Used instead of reading the template tag directly for backwards compatibility reasons when changing from a bool template specifier to an enum */
	NIAGARAEDITOR_API bool GetTemplateSpecificationFromTag(const FAssetData& Data, ENiagaraScriptTemplateSpecification&
	                                                       OutTemplateSpecification);

	NIAGARAEDITOR_API bool IsScriptAssetInLibrary(const FAssetData& ScriptAssetData);

	NIAGARAEDITOR_API int32 GetWeightForItem(const TSharedPtr<FNiagaraMenuAction_Generic>& Item, const TArray<FString>& FilterTerms);

	NIAGARAEDITOR_API bool DoesItemMatchFilterText(const FText& FilterText, const TSharedPtr<FNiagaraMenuAction_Generic>& Item);
	
	NIAGARAEDITOR_API TTuple<EScriptSource, FText> GetScriptSource(const FAssetData& ScriptAssetData);

	NIAGARAEDITOR_API FLinearColor GetScriptSourceColor(EScriptSource ScriptSourceData);

	NIAGARAEDITOR_API FText FormatScriptName(FName Name, bool bIsInLibrary);

	NIAGARAEDITOR_API FText FormatScriptDescription(FText Description, FName Path, bool bIsInLibrary);

	NIAGARAEDITOR_API FText FormatVariableDescription(FText Description, FText Name, FText Type);

	void ResetSystemsThatReferenceSystemViewModel(const FNiagaraSystemViewModel& ReferencedSystemViewModel);

	TArray<UNiagaraComponent*> GetComponentsThatReferenceSystem(const UNiagaraSystem& ReferencedSystem);

	TArray<UNiagaraComponent*> GetComponentsThatReferenceSystemViewModel(const FNiagaraSystemViewModel& ReferencedSystemViewModel);

	NIAGARAEDITOR_API const FGuid AddEmitterToSystem(UNiagaraSystem& InSystem, UNiagaraEmitter& InEmitterToAdd, bool bCreateCopy = true);

	void RemoveEmittersFromSystemByEmitterHandleId(UNiagaraSystem& InSystem, TSet<FGuid> EmitterHandleIdsToDelete);

	/** Kills all system instances using the referenced system. */
	void KillSystemInstances(const UNiagaraSystem& System);


	bool VerifyNameChangeForInputOrOutputNode(const UNiagaraNode& NodeBeingChanged, FName OldName, FString NewName, FText& OutErrorMessage);

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

	NIAGARAEDITOR_API FName GetUniqueObjectName(UObject* Outer, UClass* ObjectClass, const FString& CandidateName);

	template<typename T>
	FName GetUniqueObjectName(UObject* Outer, const FString& CandidateName)
	{
		return GetUniqueObjectName(Outer, T::StaticClass(), CandidateName);
	}

	TArray<FName> DecomposeVariableNamespace(const FName& InVarNameToken, FName& OutName);

	void  RecomposeVariableNamespace(const FName& InVarNameToken, const TArray<FName>& InParentNamespaces, FName& OutName);

	FString NIAGARAEDITOR_API GetNamespacelessVariableNameString(const FName& InVarName);

	void GetReferencingFunctionCallNodes(UNiagaraScript* Script, TArray<UNiagaraNodeFunctionCall*>& OutReferencingFunctionCallNodes);

	// Compare two FNiagaraVariable names for the sort priority relative to the first argument VarNameA. Sorting is ordered by namespace and then alphabetized. 
	bool GetVariableSortPriority(const FName& VarNameA, const FName& VarNameB);

	// Compare two FNiagaraNamespaceMetadata for the sort priority relative to the first argument A, where a lower number represents a higher priority.
	int32 GetNamespaceMetaDataSortPriority(const FNiagaraNamespaceMetadata& A, const FNiagaraNamespaceMetadata& B);

	// Get the sort priority of a registered namespace FName, where a lower number represents a higher priority.
	int32 GetNamespaceSortPriority(const FName& Namespace);

	const FNiagaraNamespaceMetadata GetNamespaceMetaDataForVariableName(const FName& VarName);

	const FNiagaraNamespaceMetadata GetNamespaceMetaDataForId(const FGuid& NamespaceId);

	const FGuid& GetNamespaceIdForUsage(ENiagaraScriptUsage Usage);

	TArray<UNiagaraParameterDefinitions*> GetAllParameterDefinitions();

	bool GetAvailableParameterDefinitions(const TArray<FString>& ExternalPackagePaths, TArray<FAssetData>& OutParameterDefinitionsAssetData);

	TSharedPtr<INiagaraParameterDefinitionsSubscriberViewModel> GetOwningLibrarySubscriberViewModelForGraph(const UNiagaraGraph* Graph);

	TArray<UNiagaraParameterDefinitions*> DowncastParameterDefinitionsBaseArray(const TArray<UNiagaraParameterDefinitionsBase*> BaseArray);

	// Executes python upgrade scripts on the given source node for all the given in-between versions
	void RunPythonUpgradeScripts(UNiagaraNodeFunctionCall* SourceNode, const TArray<FVersionedNiagaraScriptData*>& UpgradeVersionData, const FNiagaraScriptVersionUpgradeContext& UpgradeContext, FString& OutWarnings);

	void RefreshAllScriptsFromExternalChanges(FRefreshAllScriptsFromExternalChangesArgs Args);
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

	enum class EParameterContext : uint8
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

	NIAGARAEDITOR_API int32 GetNumberOfNamePartsBeforeEditableModifier(const FNiagaraNamespaceMetadata& NamespaceMetadata);

	NIAGARAEDITOR_API void GetOptionalNamespaceModifiers(FName ParameterName, EParameterContext InParameterContext, TArray<FName>& OutOptionalNamespaceModifiers);

	NIAGARAEDITOR_API FName GetEditableNamespaceModifierForParameter(FName ParameterName);

	NIAGARAEDITOR_API bool TestCanSetSpecificNamespaceModifierWithMessage(FName InParameterName, FName InNamespaceModifier, FText& OutMessage);

	NIAGARAEDITOR_API FName SetSpecificNamespaceModifier(FName InParameterName, FName InNamespaceModifier);

	NIAGARAEDITOR_API bool TestCanSetCustomNamespaceModifierWithMessage(FName InParameterName, FText& OutMessage);

	NIAGARAEDITOR_API FName SetCustomNamespaceModifier(FName InParameterName);

	NIAGARAEDITOR_API FName SetCustomNamespaceModifier(FName InParameterName, TSet<FName>& CurrentParameterNames);

	NIAGARAEDITOR_API bool TestCanRenameWithMessage(FName ParameterName, FText& OutMessage);
};
