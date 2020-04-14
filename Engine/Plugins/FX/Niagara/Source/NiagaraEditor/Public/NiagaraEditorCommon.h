// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraCommon.h"

NIAGARAEDITOR_API DECLARE_LOG_CATEGORY_EXTERN(LogNiagaraEditor, All, All);

/** Information about a Niagara operation. */
class FNiagaraOpInfo
{
public:
	FNiagaraOpInfo()
		: Keywords(FText())
		, NumericOuputTypeSelectionMode(ENiagaraNumericOutputTypeSelectionMode::Largest)
		, bSupportsAddedInputs(false)
		, bNumericsCanBeIntegers(true)
		, bNumericsCanBeFloats(true)
	{}

	FName Name;
	FText Category;
	FText FriendlyName;
	FText Description;
	FText Keywords;
	ENiagaraNumericOutputTypeSelectionMode NumericOuputTypeSelectionMode;
	TArray<FNiagaraOpInOutInfo> Inputs;
	TArray<FNiagaraOpInOutInfo> Outputs;

	/** If true then this operation supports a variable number of inputs */
	bool bSupportsAddedInputs;

	/** If integer pins are allowed on this op's numeric pins. */
	bool bNumericsCanBeIntegers;

	/** If float pins are allowed on this op's numeric pins. */
	bool bNumericsCanBeFloats;

	/** 
	* The format that can generate the hlsl for the given number of inputs.
	* Used the placeholder {A} and {B} to chain the inputs together.
	*/
	FString AddedInputFormatting;

	/**
	* If added inputs are enabled then this filters the available pin types shown to the user.
	* If empty then all the default niagara types are shown.
	*/
	TArray<FNiagaraTypeDefinition> AddedInputTypeRestrictions;

	static TMap<FName, int32> OpInfoMap;
	static TArray<FNiagaraOpInfo> OpInfos;

	static void Init();
	static const FNiagaraOpInfo* GetOpInfo(FName OpName);
	static const TArray<FNiagaraOpInfo>& GetOpInfoArray();

	void BuildName(FString InName, FString InCategory);

	bool CreateHlslForAddedInputs(int32 InputCount, FString& HlslResult) const;
};

/** Interface for struct representing information about where to focus in a Niagara Script Graph after opening the editor for it. */
struct INiagaraScriptGraphFocusInfo : public TSharedFromThis<INiagaraScriptGraphFocusInfo>
{
public:
	enum class ENiagaraScriptGraphFocusInfoType : uint8
	{
		None = 0,
		Node,
		Pin
	};

	INiagaraScriptGraphFocusInfo(const ENiagaraScriptGraphFocusInfoType InFocusType)
		: FocusType(InFocusType)
	{
	};

	const ENiagaraScriptGraphFocusInfoType& GetFocusType() const { return FocusType; };

	virtual ~INiagaraScriptGraphFocusInfo() = 0;
	
private:
	const ENiagaraScriptGraphFocusInfoType FocusType;
};

struct FNiagaraScriptGraphNodeToFocusInfo : public INiagaraScriptGraphFocusInfo
{
public:
	FNiagaraScriptGraphNodeToFocusInfo(const FGuid& InNodeGuidToFocus)
		: INiagaraScriptGraphFocusInfo(ENiagaraScriptGraphFocusInfoType::Node)
		, NodeGuidToFocus(InNodeGuidToFocus)
	{
	};

	const FGuid& GetNodeGuidToFocus() const { return NodeGuidToFocus; };

private:
	const FGuid NodeGuidToFocus;
};

struct FNiagaraScriptGraphPinToFocusInfo : public INiagaraScriptGraphFocusInfo
{
public:
	FNiagaraScriptGraphPinToFocusInfo(const FGuid& InPinGuidToFocus)
		: INiagaraScriptGraphFocusInfo(ENiagaraScriptGraphFocusInfoType::Pin)
		, PinGuidToFocus(InPinGuidToFocus)
	{
	};

	const FGuid& GetPinGuidToFocus() const { return PinGuidToFocus; };

private:
	const FGuid PinGuidToFocus;
};

struct FNiagaraScriptIDAndGraphFocusInfo
{
public:
	FNiagaraScriptIDAndGraphFocusInfo(const uint32& InScriptUniqueAssetID, const TSharedPtr<INiagaraScriptGraphFocusInfo>& InScriptGraphFocusInfo)
		: ScriptUniqueAssetID(InScriptUniqueAssetID)
		, ScriptGraphFocusInfo(InScriptGraphFocusInfo)
	{
	};

	const uint32& GetScriptUniqueAssetID() const { return ScriptUniqueAssetID; };

	const TSharedPtr<INiagaraScriptGraphFocusInfo>& GetScriptGraphFocusInfo() const { return ScriptGraphFocusInfo; };

private:
	const uint32 ScriptUniqueAssetID;
	const TSharedPtr<INiagaraScriptGraphFocusInfo> ScriptGraphFocusInfo;
};

/** Convenience wrapper for generating entries for scope enum combo boxes in SNiagaraParameterNameView. */
struct FScopeIsEnabledAndTooltip
{
	FScopeIsEnabledAndTooltip()
		: bEnabled(true)
		, Tooltip()
	{};

	FScopeIsEnabledAndTooltip(bool bInEnabled, FText InTooltip)
		: bEnabled(bInEnabled)
		, Tooltip(InTooltip)
	{};

	bool bEnabled;
	FText Tooltip;
};

/** Helper struct for passing along info on parameters and how to display them in SNiagaraParameterNameView */
struct FNiagaraScriptVariableAndViewInfo
{
	FNiagaraScriptVariableAndViewInfo() {}
	FNiagaraScriptVariableAndViewInfo(const FNiagaraVariable& InScriptVariable, const FNiagaraVariableMetaData& InScriptVariableMetaData)
		: bIsSelectionRelevant(false)
	{
		ScriptVariable = InScriptVariable;
		MetaData = InScriptVariableMetaData;
	};

	FNiagaraScriptVariableAndViewInfo(const FNiagaraVariable& InScriptVariable, const FNiagaraVariableMetaData& InMetaData, const TStaticArray<FScopeIsEnabledAndTooltip, (int32)ENiagaraParameterScope::Num>& InParameterScopeToDisplayInfo)
		: ParameterScopeToDisplayInfo(InParameterScopeToDisplayInfo)
		, bIsSelectionRelevant(false)
	{
		ScriptVariable = InScriptVariable;
		MetaData = InMetaData;
	};

	FNiagaraScriptVariableAndViewInfo(const FNiagaraScriptVariableAndViewInfo& Other)
		: ScriptVariable(Other.ScriptVariable)
		, MetaData(Other.MetaData)
		, ParameterScopeToDisplayInfo(Other.ParameterScopeToDisplayInfo)
		, bIsSelectionRelevant(Other.bIsSelectionRelevant)
	{};

	bool operator== (const FNiagaraScriptVariableAndViewInfo& Other) const;

	FNiagaraVariable ScriptVariable;
	FNiagaraVariableMetaData MetaData;

	// Array indexed by ENiagaraParameterScope value containing info on how to present each scope in a ComboBox.
	TStaticArray<FScopeIsEnabledAndTooltip, (int32)ENiagaraParameterScope::Num> ParameterScopeToDisplayInfo;

	// Whether this entry is related to the current selection state, e.g. if a module is selected in the Stack, mark this entry if ScriptVariable is a member of that module.
	bool bIsSelectionRelevant;
};
