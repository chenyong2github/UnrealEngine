// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "NiagaraMessageManager.h"
#include "NiagaraScriptSource.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraNodeEmitter.h"
#include "Modules/ModuleManager.h"
#include "AssetRegistryModule.h"
#include "IAssetRegistry.h"
#include "Toolkits/AssetEditorManager.h"
#include "NiagaraScriptToolkit.h"
#include "../Public/NiagaraEditorModule.h"

#define LOCTEXT_NAMESPACE "NiagaraMessageManager"

FNiagaraMessageManager* FNiagaraMessageManager::Singleton = nullptr;
TQueue<TSharedPtr<INiagaraMessageJob>> FNiagaraMessageManager::JobQueue;

FNiagaraMessageManager::FNiagaraMessageManager()
{
}

FNiagaraMessageManager* FNiagaraMessageManager::Get()
{
	if (Singleton == nullptr)
	{
		Singleton = new FNiagaraMessageManager();
	}
	return Singleton;
}

TSharedPtr<INiagaraMessage> FNiagaraMessageManager::QueueMessageJob(TSharedRef<INiagaraMessageJob> InMessageJob)
{
	//JobQueue.Enqueue(InMessageJob); //@todo(message manager) stubbing the asynchronous implementation
	return InMessageJob->GenerateNiagaraMessage();
}

FNiagaraMessageCompileEvent::FNiagaraMessageCompileEvent(
	  const FNiagaraCompileEvent& InCompileEvent
	, TArray<FNiagaraScriptNameAndAssetPath>& InContextScriptNamesAndAssetPaths
	, TOptional<const FString>& InEmitterScriptName
	, TOptional<const FNiagaraScriptNameAndAssetPath>& InCompiledScriptNameAndAssetPath
	)
	: INiagaraMessage(ENiagaraMessageType::CompileEventMessage)
	, CompileEvent(InCompileEvent)
	, ContextScriptNamesAndAssetPaths(InContextScriptNamesAndAssetPaths)
	, EmitterScriptName(InEmitterScriptName)
	, CompiledScriptNameAndAssetPath(InCompiledScriptNameAndAssetPath)
{
}

TSharedRef<FTokenizedMessage> FNiagaraMessageCompileEvent::GenerateTokenizedMessage() const
{
	EMessageSeverity::Type MessageSeverity = EMessageSeverity::Info;
	switch (CompileEvent.Severity) {
	case FNiagaraCompileEventSeverity::Error:
		MessageSeverity = EMessageSeverity::Error;
		break;
	case FNiagaraCompileEventSeverity::Warning:
		MessageSeverity = EMessageSeverity::Warning;
		break;
	case FNiagaraCompileEventSeverity::Log:
		MessageSeverity = EMessageSeverity::Info;
		break;
	default:
		ensureMsgf(false, TEXT("Compile event severity type not handled!"));
		MessageSeverity = EMessageSeverity::Info;
		break;
	}
	TSharedRef<FTokenizedMessage> Message = FTokenizedMessage::Create(MessageSeverity);

	//Add message from compile event at start of message
	if (CompiledScriptNameAndAssetPath.IsSet())
	{
		//Compile event is from a script asset
		if (ContextScriptNamesAndAssetPaths.Num() == 0)
		{

			if (CompileEvent.PinGuid != FGuid())
			{
				Message->AddToken(FNiagaraCompileEventToken::Create(CompiledScriptNameAndAssetPath.GetValue().ScriptAssetPathString, FText::FromString(CompileEvent.Message), CompileEvent.NodeGuid, CompileEvent.PinGuid));
			}
			else if (CompileEvent.NodeGuid != FGuid())
			{
				Message->AddToken(FNiagaraCompileEventToken::Create(CompiledScriptNameAndAssetPath.GetValue().ScriptAssetPathString, FText::FromString(CompileEvent.Message), CompileEvent.NodeGuid));
			}
			else
			{
				Message->AddToken(FTextToken::Create(FText::FromString(CompileEvent.Message)));
			}		
		}
		else
		{
			if (CompileEvent.PinGuid != FGuid())
			{
				Message->AddToken(FNiagaraCompileEventToken::Create(ContextScriptNamesAndAssetPaths.Last().ScriptAssetPathString, FText::FromString(CompileEvent.Message), CompileEvent.NodeGuid, CompileEvent.PinGuid));
			}
			else if (CompileEvent.NodeGuid != FGuid())
			{
				Message->AddToken(FNiagaraCompileEventToken::Create(ContextScriptNamesAndAssetPaths.Last().ScriptAssetPathString, FText::FromString(CompileEvent.Message), CompileEvent.NodeGuid));
			}
			else
			{
				Message->AddToken(FNiagaraCompileEventToken::Create(ContextScriptNamesAndAssetPaths.Last().ScriptAssetPathString, FText::FromString(CompileEvent.Message)));
			}
		}
	}
	else
	{
		//Compile event is from an emitter or system asset
		if (ContextScriptNamesAndAssetPaths.Num() == 0)
		{
			Message->AddToken(FTextToken::Create(FText::FromString(CompileEvent.Message)));
		}
		else if (CompileEvent.PinGuid != FGuid())
		{
			Message->AddToken(FNiagaraCompileEventToken::Create(ContextScriptNamesAndAssetPaths.Last().ScriptAssetPathString, FText::FromString(CompileEvent.Message), CompileEvent.NodeGuid, CompileEvent.PinGuid));
		}
		else if (CompileEvent.NodeGuid != FGuid())
		{
			Message->AddToken(FNiagaraCompileEventToken::Create(ContextScriptNamesAndAssetPaths.Last().ScriptAssetPathString, FText::FromString(CompileEvent.Message), CompileEvent.NodeGuid));
		}
		else
		{
			Message->AddToken(FNiagaraCompileEventToken::Create(ContextScriptNamesAndAssetPaths.Last().ScriptAssetPathString, FText::FromString(CompileEvent.Message)));
		}
	}
	
	//Now add the emitter name if we passed through an Emitter Node
	if (EmitterScriptName.IsSet())
	{
		Message->AddToken(FTextToken::Create(FText::FromString(*EmitterScriptName.GetValue())));
	}

	//Finally add the context stack of the scripts that were passed through to get to the originating graph
	for (const FNiagaraScriptNameAndAssetPath& ScriptNameAndPath : ContextScriptNamesAndAssetPaths)
	{
		Message->AddToken(FNiagaraCompileEventToken::Create(ScriptNameAndPath.ScriptAssetPathString, FText::FromString(*ScriptNameAndPath.ScriptNameString)));
	}
	return Message;
}

FNiagaraMessageJobCompileEvent::FNiagaraMessageJobCompileEvent(const FNiagaraCompileEvent& InCompileEvent, const TWeakObjectPtr<UNiagaraScript>& InOriginatingScriptWeakObjPtr, const bool bInFromScriptToolkit)
	: CompileEvent(InCompileEvent)
	, OriginatingScriptWeakObjPtr(InOriginatingScriptWeakObjPtr)
	, bFromScriptToolkit(bInFromScriptToolkit)
{
}

const TSharedPtr<INiagaraMessage> FNiagaraMessageJobCompileEvent::GenerateNiagaraMessage() const
{
	TArray<FGuid> ContextStackGuids = CompileEvent.StackGuids;

	if (OriginatingScriptWeakObjPtr.IsValid())
	{
		UNiagaraScriptSourceBase* FunctionScriptSourceBase = OriginatingScriptWeakObjPtr->GetSource();
		checkf(FunctionScriptSourceBase->IsA<UNiagaraScriptSource>(), TEXT("Script source for function call node is not assigned or is not of type UNiagaraScriptSource!"))
		UNiagaraScriptSource* FunctionScriptSource = Cast<UNiagaraScriptSource>(FunctionScriptSourceBase);
		checkf(FunctionScriptSource, TEXT("Script source base was somehow not a derived type!"));
		UNiagaraGraph* ScriptGraph = FunctionScriptSource->NodeGraph;
		checkf(ScriptGraph, TEXT("Function Script does not have a UNiagaraGraph!"));

		TArray<FNiagaraScriptNameAndAssetPath> ContextScriptNamesAndPaths;
		TOptional<const FString> EmitterScriptName;
		TOptional<const FNiagaraScriptNameAndAssetPath> CompiledScriptNameAndPath;
		TOptional<const FText> ScriptNameAndPathsGetterFailureReason;
		const bool bSuccessfullyFoundScripts = RecursiveGetScriptNamesAndAssetPathsFromContextStack(ContextStackGuids, ScriptGraph, ContextScriptNamesAndPaths, EmitterScriptName, ScriptNameAndPathsGetterFailureReason);

		if (bSuccessfullyFoundScripts == false)
		{
			//If we can't find the scripts in the context stack of the compile event, return a message with the error and ask for a recompile.
			return MakeShared<FNiagaraMessageNeedRecompile>(ScriptNameAndPathsGetterFailureReason.GetValue());
		}

		if (bFromScriptToolkit)
		{
			//If this compile event is from a script, set the compiled script name and asset path so the user can navigate to errors locally.
			CompiledScriptNameAndPath = TOptional<const FNiagaraScriptNameAndAssetPath>(FNiagaraScriptNameAndAssetPath(OriginatingScriptWeakObjPtr->GetName(), OriginatingScriptWeakObjPtr->GetPathName()));
		}
		return MakeShared<FNiagaraMessageCompileEvent>(CompileEvent, ContextScriptNamesAndPaths, EmitterScriptName, CompiledScriptNameAndPath);
	}
	//The originating script weak ptr is no longer valid, send an error message asking for recompile.
	FText MessageText = LOCTEXT("CompileEventMessageJobFail", "Cached info for compile event is out of date, recompile to get full info. Event: {0}");
	//Add in the message of the compile event for visibility on which compile events do not have info.
	FText::Format(MessageText, FText::FromString(CompileEvent.Message));
	return MakeShared<FNiagaraMessageNeedRecompile>(MessageText);
}

const bool FNiagaraMessageJobCompileEvent::RecursiveGetScriptNamesAndAssetPathsFromContextStack(
	  TArray<FGuid>& InContextStackNodeGuids
	, const UNiagaraGraph* InGraphToSearch
	, TArray<FNiagaraScriptNameAndAssetPath>& OutContextScriptNamesAndAssetPaths
	, TOptional<const FString>& OutEmitterScriptName
	, TOptional<const FText>& OutFailureReason
) const
{
	checkf(InGraphToSearch, TEXT("Failed to get a node graph to search!"));

	if (InGraphToSearch && InContextStackNodeGuids.Num() == 0)
	{
		//StackGuids arr has been cleared out which means we have walked the entire context stack.
		return true;
	}

	// Search in the current graph for a node with a GUID that matches a GUID in the list of Function Call and Emitter node GUIDs that define the context stack for a compile event
	auto FindNodeInGraphWithContextStackGuid = [&InGraphToSearch, &InContextStackNodeGuids]()->TOptional<UEdGraphNode*> {
		for (UEdGraphNode* GraphNode : InGraphToSearch->Nodes)
		{
			for (int i = 0; i < InContextStackNodeGuids.Num(); i++)
			{
				if (GraphNode->NodeGuid == InContextStackNodeGuids[i])
				{
					InContextStackNodeGuids.RemoveAt(i);
					return GraphNode;
				}
			}
		}
		return TOptional<UEdGraphNode*>();
	};

	TOptional<UEdGraphNode*> ContextNode = FindNodeInGraphWithContextStackGuid();
	if (ContextNode.IsSet())
	{
		// found a node in the current graph that has a GUID in the context list

		UNiagaraNodeFunctionCall* FunctionCallNode = Cast<UNiagaraNodeFunctionCall>(ContextNode.GetValue());
		if (FunctionCallNode)
		{
			// node is a function call node, now get the Niagara Script assigned to this node, add a message token and recursively call into the graph of that script.
			UNiagaraScript* FunctionCallNodeAssignedScript = FunctionCallNode->FunctionScript;
			if (FunctionCallNodeAssignedScript == nullptr)
			{
				FText FailureReason = LOCTEXT("CompileEventMessageGenerator_FunctionCallNodeScriptNotFound", "Script for Function Call Node \"{0}\" not found!");
				OutFailureReason = FText::Format(FailureReason, FText::FromString(FunctionCallNode->GetFunctionName()));
				return false;
			}
			UNiagaraScriptSourceBase* FunctionScriptSourceBase = FunctionCallNodeAssignedScript->GetSource();
			if (FunctionScriptSourceBase == nullptr)
			{
				FText FailureReason = LOCTEXT("CompileEventMessageGenerator_FunctionCallNodeScriptSourceBaseNotFound", "Source Script for Function Call Node \"{0}\" not found!");
				OutFailureReason = FText::Format(FailureReason, FText::FromString(FunctionCallNode->GetFunctionName()));
				return false;
			}
			UNiagaraScriptSource* FunctionScriptSource = Cast<UNiagaraScriptSource>(FunctionScriptSourceBase);
			checkf(FunctionScriptSource, TEXT("Script source base was somehow not a derived type!"));

			UNiagaraGraph* FunctionScriptGraph = FunctionScriptSource->NodeGraph;
			if (FunctionScriptGraph == nullptr)
			{
				FText FailureReason = LOCTEXT("CompileEventMessageGenerator_FunctionCallNodeGraphNotFound", "Graph for Function Call Node \"{0}\" not found!");
				OutFailureReason = FText::Format(FailureReason, FText::FromString(FunctionCallNode->GetFunctionName()));
				return false;
			}
			OutContextScriptNamesAndAssetPaths.Add(FNiagaraScriptNameAndAssetPath(FunctionCallNodeAssignedScript->GetName(), FunctionCallNodeAssignedScript->GetPathName()));
			return RecursiveGetScriptNamesAndAssetPathsFromContextStack(InContextStackNodeGuids, FunctionScriptGraph, OutContextScriptNamesAndAssetPaths, OutEmitterScriptName, OutFailureReason);
		}

		UNiagaraNodeEmitter* EmitterNode = Cast<UNiagaraNodeEmitter>(ContextNode.GetValue());
		if (EmitterNode)
		{
			// node is an emitter node, now get the Emitter name, add a message token and recursively call into the graph of that emitter.
			UNiagaraScriptSource* EmitterScriptSource = EmitterNode->GetScriptSource();
			checkf(EmitterScriptSource, TEXT("Emitter Node does not have a Script Source!"));
			UNiagaraGraph* EmitterScriptGraph = EmitterScriptSource->NodeGraph;
			checkf(EmitterScriptGraph, TEXT("Emitter Script Source does not have a UNiagaraGraph!"));

			OutEmitterScriptName = EmitterNode->GetEmitterUniqueName();
			return RecursiveGetScriptNamesAndAssetPathsFromContextStack(InContextStackNodeGuids, EmitterScriptGraph, OutContextScriptNamesAndAssetPaths, OutEmitterScriptName, OutFailureReason);
		}
		checkf(false, TEXT("Matching node is not a function call or emitter node!"));
	}
	FText FailureReason = LOCTEXT("CompileEventMessageGenerator_CouldNotFindMatchingNodeGUID", "Failed to walk the entire context stack, is this compile event out of date ? Event: '{0}'");
	FText::Format(FailureReason, FText::FromString(CompileEvent.Message));
	OutFailureReason = FailureReason;
	return false;
}

FNiagaraMessageNeedRecompile::FNiagaraMessageNeedRecompile(const FText& InNeedRecompileMessage)
	: INiagaraMessage(ENiagaraMessageType::NeedRecompileMessage)
	, NeedRecompileMessage(InNeedRecompileMessage)
{
}

TSharedRef<FTokenizedMessage> FNiagaraMessageNeedRecompile::GenerateTokenizedMessage() const
{
	return FTokenizedMessage::Create(EMessageSeverity::Error, NeedRecompileMessage);
}

FNiagaraCompileEventToken::FNiagaraCompileEventToken(
	  const FString& InScriptAssetPath
	, const FText& InMessage
	, const TOptional<const FGuid>& InNodeGUID
	, const TOptional<const FGuid>& InPinGUID)
	: ScriptAssetPath(InScriptAssetPath)
	, NodeGUID(InNodeGUID)
	, PinGUID(InPinGUID)
{
	if (!InMessage.IsEmpty())
	{
		CachedText = InMessage;
	}
	else
	{
		CachedText = FText::FromString(InScriptAssetPath);
	}

	MessageTokenActivated = FOnMessageTokenActivated::CreateStatic(&FNiagaraCompileEventToken::OpenScriptAssetByPathAndFocusNodeOrPinIfSet, ScriptAssetPath, NodeGUID, PinGUID);
}

void FNiagaraCompileEventToken::OpenScriptAssetByPathAndFocusNodeOrPinIfSet(
	  const TSharedRef<IMessageToken>& Token
	, FString InScriptAssetPath
	, const TOptional<const FGuid> InNodeGUID
	, const TOptional<const FGuid> InPinGUID)
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
	FAssetData AssetData = AssetRegistry.GetAssetByObjectPath(*InScriptAssetPath);
	if (AssetData.IsValid())
	{
		if (UNiagaraScript* ScriptAsset = Cast<UNiagaraScript>(AssetData.GetAsset()))
		{
			FAssetEditorManager::Get().OpenEditorForAsset(ScriptAsset, EToolkitMode::Standalone);
			FNiagaraEditorModule& NiagaraEditorModule = FModuleManager::LoadModuleChecked<FNiagaraEditorModule>("NiagaraEditor");
			if (InPinGUID.IsSet())
			{
				FNiagaraScriptGraphPinToFocusInfo PinToFocusInfo = FNiagaraScriptGraphPinToFocusInfo(ScriptAsset->GetUniqueID(), InPinGUID.GetValue());
				NiagaraEditorModule.GetOnScriptToolkitsShouldFocusGraphElement().Broadcast(&PinToFocusInfo);
			}
			else if (InNodeGUID.IsSet())
			{
				FNiagaraScriptGraphNodeToFocusInfo NodeToFocusInfo = FNiagaraScriptGraphNodeToFocusInfo(ScriptAsset->GetUniqueID(), InNodeGUID.GetValue());
				NiagaraEditorModule.GetOnScriptToolkitsShouldFocusGraphElement().Broadcast(&NodeToFocusInfo);
			}
		}
		else
		{
			checkf(false, TEXT("CompileEventJob referenced asset was not a UNiagaraScript!"));
		}
	}
}

TSharedRef<FNiagaraCompileEventToken> FNiagaraCompileEventToken::Create(
	  const FString& InScriptAssetPath
	, const FText& InMessage
	, const TOptional<const FGuid>& InNodeGUID /*= TOptional<const FGuid>() */
	, const TOptional<const FGuid>& InPinGUID /*= TOptional<const FGuid>()*/ )
{
	return MakeShareable(new FNiagaraCompileEventToken(InScriptAssetPath, InMessage, InNodeGUID, InPinGUID));
}

#undef LOCTEXT_NAMESPACE //NiagaraMessageManager
