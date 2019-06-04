// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NiagaraShared.h"
#include "Logging/TokenizedMessage.h"
#include "NiagaraGraph.h"
#include "TickableEditorObject.h"

class FNiagaraScriptToolkit;

//enum to specify the class of an FNiagaraMessage. 
enum class ENiagaraMessageType : uint8
{
	None = 0,
	CompileEventMessage,
	NeedRecompileMessage
};

enum class ENiagaraMessageJobType : uint8 {
	None = 0,
	CompileEventMessageJob
};

//Struct for passing around script asset info from compile event message job to message types
struct FNiagaraScriptNameAndAssetPath
{
public:
	FNiagaraScriptNameAndAssetPath(const FString& InScriptNameString, const FString& InScriptAssetPathString)
		: ScriptNameString(InScriptNameString)
		, ScriptAssetPathString(InScriptAssetPathString)
	{};

	const FString ScriptNameString;
	const FString ScriptAssetPathString;
};

/** Interface for view-agnostic message that holds limited lifetime information on a message (e.g. a weak pointer to an asset.)
*	Implements GenerateTokenizedMessage() to provide FTokenizedMessage for the message log, and should separately define a GenerateMessage() implementation for any specific views it needs to support (e.g. errors in SNiagaraStack).
*/
class INiagaraMessage 
{
public:
	virtual TSharedRef<FTokenizedMessage> GenerateTokenizedMessage() const = 0;

	virtual const ENiagaraMessageType GetMessageType() const = 0;

	virtual ~INiagaraMessage() {};
};

/** Interface for job supplied to FNiagaraMessageManager to generated FNiagaraMessages.
*	Implements GenerateNiagaraMessage() to return a derived type of abstract INiagaraMessage.
*/
class INiagaraMessageJob
{
public:
	virtual TSharedRef<const INiagaraMessage> GenerateNiagaraMessage() const = 0;

	virtual const ENiagaraMessageJobType GetMessageJobType() const = 0;

	virtual ~INiagaraMessageJob() {};
};

class FNiagaraMessageJobCompileEvent : public INiagaraMessageJob
{
public:
	FNiagaraMessageJobCompileEvent(
		  const FNiagaraCompileEvent& InCompileEvent
		, const TWeakObjectPtr<const UNiagaraScript>& InOriginatingScriptWeakObjPtr
		, const TOptional<const FString>& InOwningScriptNameString = TOptional<const FString>()
		, const TOptional<const FString>& InSourceScriptAssetPath = TOptional<const FString>()
	);

	virtual TSharedRef<const INiagaraMessage> GenerateNiagaraMessage() const override;

	virtual const ENiagaraMessageJobType GetMessageJobType() const override { return ENiagaraMessageJobType::CompileEventMessageJob; };

private:

	bool RecursiveGetScriptNamesAndAssetPathsFromContextStack(
		  TArray<FGuid>& InContextStackNodeGuids
		, const UNiagaraGraph* InGraphToSearch
		, TArray<FNiagaraScriptNameAndAssetPath>& OutContextScriptNamesAndAssetPaths
		, TOptional<const FString>& OutEmitterName
		, TOptional<const FText>& OutFailureReason
	) const;

	const FNiagaraCompileEvent CompileEvent;
	const TWeakObjectPtr<const UNiagaraScript> OriginatingScriptWeakObjPtr;
	TOptional<const FString> OwningScriptNameString;
	TOptional<const FString> SourceScriptAssetPath;
};

class FNiagaraMessageJobPostCompileSummary : public INiagaraMessageJob
{
public:
	FNiagaraMessageJobPostCompileSummary(const int32& InNumCompileErrors, const int32& InNumCompileWarnings, const ENiagaraScriptCompileStatus& InScriptCompileStatus, const FText& InCompiledObjectNameText)
		: NumCompileErrors(InNumCompileErrors)
		, NumCompileWarnings(InNumCompileWarnings)
		, ScriptCompileStatus(InScriptCompileStatus)
		, CompiledObjectNameText(InCompiledObjectNameText)
	{
	};

	virtual TSharedRef<const INiagaraMessage> GenerateNiagaraMessage() const override;

	// we alias the messagejob type as post compile summary is always generated with compile events
	virtual const ENiagaraMessageJobType GetMessageJobType() const override { return ENiagaraMessageJobType::CompileEventMessageJob; };

private:
	const int32 NumCompileErrors;
	const int32 NumCompileWarnings;
	const ENiagaraScriptCompileStatus ScriptCompileStatus;
	const FText CompiledObjectNameText;
};

class FNiagaraMessageCompileEvent : public INiagaraMessage
{
public:
	FNiagaraMessageCompileEvent(
		  const FNiagaraCompileEvent& InCompileEvent
		, TArray<FNiagaraScriptNameAndAssetPath>& InContextScriptNamesAndAssetPaths
		, TOptional<const FText>& InOwningScriptNameAndUsageText
		, TOptional<const FNiagaraScriptNameAndAssetPath>& InCompiledScriptNameAndAssetPath
	);

	virtual TSharedRef<FTokenizedMessage> GenerateTokenizedMessage() const override;
	//@todo(message manager) make stack specific message type generator here

	virtual const ENiagaraMessageType GetMessageType() const override { return ENiagaraMessageType::CompileEventMessage; };

private:
	const FNiagaraCompileEvent CompileEvent;
	const TArray<FNiagaraScriptNameAndAssetPath> ContextScriptNamesAndAssetPaths;
	const TOptional<const FText> OwningScriptNameAndUsageText;
	const TOptional<const FNiagaraScriptNameAndAssetPath> CompiledScriptNameAndAssetPath;
};

class FNiagaraMessageNeedRecompile : public INiagaraMessage
{
public:
	FNiagaraMessageNeedRecompile(const FText& InNeedRecompileMessage)
		: NeedRecompileMessage(InNeedRecompileMessage)
	{
	};

	virtual TSharedRef<FTokenizedMessage> GenerateTokenizedMessage() const override;

	virtual const ENiagaraMessageType GetMessageType() const override { return ENiagaraMessageType::NeedRecompileMessage; };

private:
	const FText NeedRecompileMessage;
};

class FNiagaraMessagePostCompileSummary : public INiagaraMessage
{
public: 
	FNiagaraMessagePostCompileSummary(const FText& InPostCompileSummaryText, const EMessageSeverity::Type& InMessageSeverity)
		: PostCompileSummaryText(InPostCompileSummaryText)
		, MessageSeverity(InMessageSeverity)
	{
	};

	virtual TSharedRef<FTokenizedMessage> GenerateTokenizedMessage() const override;
	
	// we alias the message type as post compile summary is always generated with compile events
	virtual const ENiagaraMessageType GetMessageType() const override { return  ENiagaraMessageType::CompileEventMessage; };

private:
	const FText PostCompileSummaryText;
	const EMessageSeverity::Type MessageSeverity;
};

//Extension of message token to allow opening the asset editor when clicking on the linked asset name.
class FNiagaraCompileEventToken : public IMessageToken
{
public:
	/** Factory method, tokens can only be constructed as shared refs */
	static TSharedRef<FNiagaraCompileEventToken> Create(
		  const FString& InScriptAssetPath
		, const FText& InMessage
		, const TOptional<const FGuid>& InNodeGUID = TOptional<const FGuid>()
		, const TOptional<const FGuid>& InPinGUID = TOptional<const FGuid>()
	);

	/** Begin IMessageToken interface */
	virtual EMessageToken::Type GetType() const override
	{
		return EMessageToken::AssetName;
	}
	/** End IMessageToken interface */

private:
	/** Private constructor */
	FNiagaraCompileEventToken(
		  const FString& InScriptAssetPath
		, const FText& InMessage
		, const TOptional<const FGuid>& InNodeGUID
		, const TOptional<const FGuid>& InPinGUID
	);

	/**
	 * Find and open an asset in editor
	 * @param	Token		The token that was clicked
	 * @param	InAssetPath		The asset to find
	 */
	static void OpenScriptAssetByPathAndFocusNodeOrPinIfSet(
		  const TSharedRef<IMessageToken>& Token
		, FString InScriptAssetPath
		, const TOptional<const FGuid> InNodeGUID
		, const TOptional<const FGuid> InPinGUID
	);

	/** The script asset path to open the editor for. */
	const FString ScriptAssetPath;

	/** The optional Node or Pin GUID to find and focus after opening the script asset */
	const TOptional<const FGuid> NodeGUID;
	const TOptional<const FGuid> PinGUID;
};

class FNiagaraMessageManager : FTickableEditorObject
{
	struct FNiagaraMessageJobBatch
	{
	public:
		FNiagaraMessageJobBatch(TArray<TSharedPtr<const INiagaraMessageJob>> InMessageJobs, const FGuid& InMessageJobsAssetKey)
			: MessageJobs(InMessageJobs)
			, MessageJobsAssetKey(InMessageJobsAssetKey)
		{
		};

		TArray<TSharedPtr<const INiagaraMessageJob>> MessageJobs;
		FGuid MessageJobsAssetKey;

	};

public:
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnRequestRefresh, const FGuid& /*MessageJobBatchAssetKey*/, const TArray<TSharedRef<const INiagaraMessage>> /*NewMessages*/)

	static FNiagaraMessageManager* Get();

	void QueueMessageJob(TSharedPtr<const INiagaraMessageJob> InMessageJob, const FGuid& InMessageJobAssetKey);

	void QueueMessageJobBatch(TArray<TSharedPtr<const INiagaraMessageJob>> InMessageJobBatch, const FGuid& InMessageJobBatchAssetKey);

	void RefreshMessagesForAssetKey(const FGuid& InAssetKey);

	void RefreshMessagesForAssetKeyAndMessageJobType(const FGuid& InAssetKey, const ENiagaraMessageJobType& InMessageJobType);

	static const TOptional<const FString> GetStringForScriptUsageInStack(const ENiagaraScriptUsage InScriptUsage);

	const TArray<TSharedRef<const INiagaraMessage>> GetMessagesForAssetKey(const FGuid& InAssetKey) const;

	FOnRequestRefresh& GetOnRequestRefresh() { return OnRequestRefresh; };

	//~ Begin FTickableEditorObject Interface.
	virtual void Tick(float DeltaSeconds) override;

	virtual ETickableTickType GetTickableTickType() const override { return ETickableTickType::Always; };

	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(FNiagaraMessageManager, STATGROUP_Tickables); };
	//~ End FTickableEditorObject Interface

private:
	FNiagaraMessageManager();

	FOnRequestRefresh OnRequestRefresh;

	void DoMessageJobsInQueueTick();
	static const double MaxJobWorkTime;
	TArray<FNiagaraMessageJobBatch> MessageJobBatchArr;
	TArray<TSharedRef<const INiagaraMessage>> GeneratedMessagesForCurrentMessageJobBatch;
	TMap<FGuid, TMap<const ENiagaraMessageJobType, TArray<TSharedRef<const INiagaraMessage>>>> ObjectToTypeMappedMessagesMap;
	static FNiagaraMessageManager* Singleton;
};
