// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NiagaraCommon.h"

NIAGARAEDITOR_API DECLARE_LOG_CATEGORY_EXTERN(LogNiagaraEditor, All, All);

/** Information about a Niagara operation. */
class FNiagaraOpInfo
{
public:
	FNiagaraOpInfo()
		: Keywords(FText()), NumericOuputTypeSelectionMode(ENiagaraNumericOutputTypeSelectionMode::Largest), bSupportsAddedInputs(false)
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
