// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NiagaraShared.h"
#include "Logging/TokenizedMessage.h"
#include "NiagaraGraph.h"

class FNiagaraScriptToolkit;

//enum to specify the class of an FNiagaraMessage. 
enum class ENiagaraMessageType : uint8
{
	None = 0,
	CompileEventMessage,
	NeedRecompileMessage
};

//Struct for passing around script asset info from compile event message job to message types
struct FNiagaraScriptNameAndAssetPath
{
public:
	FNiagaraScriptNameAndAssetPath(const FString InScriptNameString, const FString InScriptAssetPathString)
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
	INiagaraMessage(const ENiagaraMessageType InMessageType)
		: MessageType(InMessageType)
	{
	};

	virtual TSharedRef<FTokenizedMessage> GenerateTokenizedMessage() const = 0;

	const ENiagaraMessageType GetMessageType() {return MessageType;}

	virtual ~INiagaraMessage() {};

private:
	const ENiagaraMessageType MessageType;
};

/** Interface for job supplied to FNiagaraMessageManager to generated FNiagaraMessages.
*	Implements GenerateNiagaraMessage() to return a derived type of abstract INiagaraMessage.
*/
class INiagaraMessageJob
{
public:
	

	virtual const TSharedPtr<INiagaraMessage> GenerateNiagaraMessage() const = 0;

	virtual ~INiagaraMessageJob() {};
};

class FNiagaraMessageJobCompileEvent : public INiagaraMessageJob
{
public:
	FNiagaraMessageJobCompileEvent( const FNiagaraCompileEvent& InCompileEvent, const TWeakObjectPtr<UNiagaraScript>& InOriginatingScriptWeakObjPtr, const bool bInFromScriptToolkit);

	virtual const TSharedPtr<INiagaraMessage> GenerateNiagaraMessage() const override;

private:
	//hidden private ctor
	FNiagaraMessageJobCompileEvent();

	const bool RecursiveGetScriptNamesAndAssetPathsFromContextStack(
		  TArray<FGuid>& InContextStackNodeGuids
		, const UNiagaraGraph* InGraphToSearch
		, TArray<FNiagaraScriptNameAndAssetPath>& OutContextScriptNamesAndAssetPaths
		, TOptional<const FString>& OutEmitterScriptName
		, TOptional<const FText>& OutFailureReason
	) const;

	const FNiagaraCompileEvent CompileEvent;
	const TWeakObjectPtr<UNiagaraScript> OriginatingScriptWeakObjPtr;
	const bool bFromScriptToolkit;
};

class FNiagaraMessageCompileEvent : public INiagaraMessage
{
public:
	FNiagaraMessageCompileEvent(
		  const FNiagaraCompileEvent& InCompileEvent
		, TArray<FNiagaraScriptNameAndAssetPath>& InContextScriptNamesAndAssetPaths
		, TOptional<const FString>& InEmitterScriptName
		, TOptional<const FNiagaraScriptNameAndAssetPath>& InCompiledScriptNameAndAssetPath
	);

	virtual TSharedRef<FTokenizedMessage> GenerateTokenizedMessage() const override;
	//@todo(message manager) make stack specific message type generator here

private:
	const FNiagaraCompileEvent CompileEvent;
	const TArray<FNiagaraScriptNameAndAssetPath> ContextScriptNamesAndAssetPaths;
	const TOptional<const FString> EmitterScriptName;
	const TOptional<const FNiagaraScriptNameAndAssetPath> CompiledScriptNameAndAssetPath;
};

class FNiagaraMessageNeedRecompile : public INiagaraMessage
{
public:
	FNiagaraMessageNeedRecompile(const FText& InNeedRecompileMessage);

	virtual TSharedRef<FTokenizedMessage> GenerateTokenizedMessage() const override;

private:
	const FText NeedRecompileMessage;
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

class FNiagaraMessageManager
{
public:
	static FNiagaraMessageManager* Get();

	static TSharedPtr<INiagaraMessage> QueueMessageJob(TSharedRef<INiagaraMessageJob> InMessageJob);

private:
	FNiagaraMessageManager();

	static TQueue<TSharedPtr<INiagaraMessageJob>> JobQueue;
	static FNiagaraMessageManager* Singleton;
};
