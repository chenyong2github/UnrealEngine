// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraMessageManager.h"
#include "NiagaraScriptSource.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraNodeEmitter.h"
#include "Modules/ModuleManager.h"
#include "AssetRegistryModule.h"
#include "IAssetRegistry.h"

#include "NiagaraScriptToolkit.h"
#include "../Public/NiagaraEditorModule.h"
#include "Subsystems/AssetEditorSubsystem.h"

#define LOCTEXT_NAMESPACE "NiagaraMessageManager"

FNiagaraMessageManager* FNiagaraMessageManager::Singleton = nullptr;
const double FNiagaraMessageManager::MaxJobWorkTime = .02f; // do message jobs at 50 fps

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

void FNiagaraMessageManager::QueueMessageJob(TSharedPtr<const INiagaraMessageJob> InMessageJob, const FGuid& InMessageJobAssetKey)
{
	TArray<TSharedPtr<const INiagaraMessageJob>> JobBatch{ InMessageJob };
	MessageJobBatchArr.Add(FNiagaraMessageJobBatch(JobBatch, InMessageJobAssetKey));
}

void FNiagaraMessageManager::QueueMessageJobBatch(TArray<TSharedPtr<const INiagaraMessageJob>> InMessageJobBatch, const FGuid& InMessageJobBatchAssetKey)
{
	if (ensureMsgf(InMessageJobBatch.Num() > 0, TEXT("Queued a message job batch with no messages!")))
	{
		MessageJobBatchArr.Add(FNiagaraMessageJobBatch(InMessageJobBatch, InMessageJobBatchAssetKey));
	}
}

void FNiagaraMessageManager::RefreshMessagesForAssetKey(const FGuid& InAssetKey)
{
	ObjectToTypeMappedMessagesMap.Remove(InAssetKey);
	MessageJobBatchArr.RemoveAll([InAssetKey](const FNiagaraMessageJobBatch& Batch) {return Batch.MessageJobsAssetKey == InAssetKey; });
}

void FNiagaraMessageManager::RefreshMessagesForAssetKeyAndMessageJobType(const FGuid& InAssetKey, const ENiagaraMessageJobType& InMessageJobType)
{
	TMap<const ENiagaraMessageJobType, TArray<TSharedRef<const INiagaraMessage>>>* MessageJobTypeToMessageMap = ObjectToTypeMappedMessagesMap.Find(InAssetKey);
	if (MessageJobTypeToMessageMap != nullptr)
	{
		TArray<TSharedRef<const INiagaraMessage>>* MessageArr = MessageJobTypeToMessageMap->Find(InMessageJobType);
		if (MessageArr != nullptr)
		{
			MessageArr->Reset();
		}
	} 

	for (FNiagaraMessageJobBatch& Batch : MessageJobBatchArr)
	{
		Batch.MessageJobs.RemoveAll([InMessageJobType](const TSharedPtr<const INiagaraMessageJob>& Job) {return Job->GetMessageJobType() == InMessageJobType; });
	}
}

const TOptional<const FString> FNiagaraMessageManager::GetStringForScriptUsageInStack(const ENiagaraScriptUsage InScriptUsage)
{
	switch (InScriptUsage) {
	case ENiagaraScriptUsage::ParticleSpawnScript:
		return TOptional<const FString>(FString("Particle Spawn Script"));

	case ENiagaraScriptUsage::ParticleSpawnScriptInterpolated:
		return TOptional<const FString>(TEXT("Particle Spawn Script Interpolated"));

	case ENiagaraScriptUsage::ParticleGPUComputeScript:
		return TOptional<const FString>(TEXT("Particle GPU Compute Script"));

	case ENiagaraScriptUsage::ParticleUpdateScript:
		return TOptional<const FString>(TEXT("Particle Update Script"));
		
	case ENiagaraScriptUsage::ParticleEventScript:
		return TOptional<const FString>(TEXT("Particle Event Script"));

	case ENiagaraScriptUsage::EmitterSpawnScript:
		return TOptional<const FString>(TEXT("Emitter Spawn Script"));

	case ENiagaraScriptUsage::EmitterUpdateScript:
		return TOptional<const FString>(TEXT("Emitter Update Script"));

	case ENiagaraScriptUsage::SystemSpawnScript:
		return TOptional<const FString>(TEXT("System Spawn Script"));

	case ENiagaraScriptUsage::SystemUpdateScript:
		return TOptional<const FString>(TEXT("System Update Script"));

	//We don't expect to see these usages in the stack so do not set the toptional
	case ENiagaraScriptUsage::DynamicInput:
	case ENiagaraScriptUsage::Function:
	case ENiagaraScriptUsage::Module:
		return TOptional<const FString>();

	//unhandled cases
	default:
		ensureMsgf(false, TEXT("Tried to get script usage text for usage that is not handled!"));
		return TOptional<const FString>();
	}
	return TOptional<const FString>();
}

const TArray<TSharedRef<const INiagaraMessage>> FNiagaraMessageManager::GetMessagesForAssetKey(const FGuid& InAssetKey) const
{
 	TArray<TSharedRef<const INiagaraMessage>> FoundMessages;
	const TMap <const ENiagaraMessageJobType, TArray<TSharedRef<const INiagaraMessage>>>* MessageJobTypeToMessagesMapPtr = ObjectToTypeMappedMessagesMap.Find(InAssetKey);
	if (MessageJobTypeToMessagesMapPtr != nullptr)
	{
		for (const TTuple<const ENiagaraMessageJobType, TArray<TSharedRef<const INiagaraMessage>>>& MessageJobTypeToMessagesPair : *MessageJobTypeToMessagesMapPtr)
		{
			FoundMessages.Append(MessageJobTypeToMessagesPair.Value);
		}
	}
	return FoundMessages;
}

void FNiagaraMessageManager::Tick(float DeltaSeconds)
{
	DoMessageJobsInQueueTick();
}

void FNiagaraMessageManager::DoMessageJobsInQueueTick()
{
	if (MessageJobBatchArr.Num() > 0)
	{
		double WorkStartTime = FPlatformTime::Seconds();
		double CurrentWorkLoopTime = WorkStartTime;

		while(MessageJobBatchArr.Num() > 0)
		{ 
			FNiagaraMessageJobBatch& CurrentMessageJobBatch = MessageJobBatchArr[0];
			
			while(CurrentMessageJobBatch.MessageJobs.Num() > 0)
			{
				TSharedPtr<const INiagaraMessageJob> CurrentMessageJob = CurrentMessageJobBatch.MessageJobs.Pop(false);
				TSharedRef<const INiagaraMessage> GeneratedMessage = CurrentMessageJob->GenerateNiagaraMessage();
				GeneratedMessagesForCurrentMessageJobBatch.Add(GeneratedMessage);
				ObjectToTypeMappedMessagesMap.FindOrAdd(CurrentMessageJobBatch.MessageJobsAssetKey).FindOrAdd(CurrentMessageJob->GetMessageJobType()).Add(GeneratedMessage);
				CurrentWorkLoopTime = FPlatformTime::Seconds();

				if (CurrentWorkLoopTime - WorkStartTime > MaxJobWorkTime)
				{
					return;
				}
			}
			OnRequestRefresh.Broadcast(CurrentMessageJobBatch.MessageJobsAssetKey, GeneratedMessagesForCurrentMessageJobBatch);
			GeneratedMessagesForCurrentMessageJobBatch.Reset();
			MessageJobBatchArr.Pop();
		} 
	}
}

FNiagaraMessageCompileEvent::FNiagaraMessageCompileEvent(
	  const FNiagaraCompileEvent& InCompileEvent
	, TArray<FNiagaraScriptNameAndAssetPath>& InContextScriptNamesAndAssetPaths
	, TOptional<const FText>& InOwningScriptNameAndUsageText
	, TOptional<const FNiagaraScriptNameAndAssetPath>& InCompiledScriptNameAndAssetPath
	)
	: CompileEvent(InCompileEvent)
	, ContextScriptNamesAndAssetPaths(InContextScriptNamesAndAssetPaths)
	, OwningScriptNameAndUsageText(InOwningScriptNameAndUsageText)
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
	
	//Now add the owning script name and usage if it is set
	if (OwningScriptNameAndUsageText.IsSet())
	{
		Message->AddToken(FTextToken::Create(OwningScriptNameAndUsageText.GetValue()));
	}

	//Finally add the context stack of the scripts that were passed through to get to the originating graph
	for (const FNiagaraScriptNameAndAssetPath& ScriptNameAndPath : ContextScriptNamesAndAssetPaths)
	{
		Message->AddToken(FNiagaraCompileEventToken::Create(ScriptNameAndPath.ScriptAssetPathString, FText::FromString(*ScriptNameAndPath.ScriptNameString)));
	}
	return Message;
}

FNiagaraMessageJobCompileEvent::FNiagaraMessageJobCompileEvent(
	  const FNiagaraCompileEvent& InCompileEvent
	, const TWeakObjectPtr<const UNiagaraScript>& InOriginatingScriptWeakObjPtr
	, const TOptional<const FString>& InOwningScriptNameString
	, const TOptional<const FString>& InSourceScriptAssetPath
	)
	: CompileEvent(InCompileEvent)
	, OriginatingScriptWeakObjPtr(InOriginatingScriptWeakObjPtr)
	, OwningScriptNameString(InOwningScriptNameString)
	, SourceScriptAssetPath(InSourceScriptAssetPath)
{
}

TSharedRef<const INiagaraMessage> FNiagaraMessageJobCompileEvent::GenerateNiagaraMessage() const
{
	TArray<FGuid> ContextStackGuids = CompileEvent.StackGuids;

	if (OriginatingScriptWeakObjPtr.IsValid())
	{
		const UNiagaraScriptSourceBase* FunctionScriptSourceBase = OriginatingScriptWeakObjPtr->GetSource();
		checkf(FunctionScriptSourceBase->IsA<UNiagaraScriptSource>(), TEXT("Script source for function call node is not assigned or is not of type UNiagaraScriptSource!"))
		const UNiagaraScriptSource* FunctionScriptSource = Cast<UNiagaraScriptSource>(FunctionScriptSourceBase);
		checkf(FunctionScriptSource, TEXT("Script source base was somehow not a derived type!"));
		const UNiagaraGraph* ScriptGraph = FunctionScriptSource->NodeGraph;
		checkf(ScriptGraph, TEXT("Function Script does not have a UNiagaraGraph!"));

		TArray<FNiagaraScriptNameAndAssetPath> ContextScriptNamesAndPaths;
		TOptional<const FNiagaraScriptNameAndAssetPath> CompiledScriptNameAndPath;
		TOptional<const FText> OwningScriptNameAndUsageText;
		TOptional<const FText> ScriptNameAndPathsGetterFailureReason;
		TOptional<const FString> OwningScriptNameStringCopy = OwningScriptNameString;
		bool bSuccessfullyFoundScripts = RecursiveGetScriptNamesAndAssetPathsFromContextStack(ContextStackGuids, ScriptGraph, ContextScriptNamesAndPaths, OwningScriptNameStringCopy, ScriptNameAndPathsGetterFailureReason);

		if (bSuccessfullyFoundScripts == false)
		{
			//If we can't find the scripts in the context stack of the compile event, return a message with the error and ask for a recompile.
			return MakeShared<FNiagaraMessageNeedRecompile>(ScriptNameAndPathsGetterFailureReason.GetValue());
		}

		const ENiagaraScriptUsage ScriptUsage = OriginatingScriptWeakObjPtr->GetUsage();

		if (OwningScriptNameStringCopy.IsSet())
		{
			FString ScriptAndUsage = OwningScriptNameStringCopy.GetValue();
			const TOptional<const FString> ScriptUsageInStackString = FNiagaraMessageManager::GetStringForScriptUsageInStack(ScriptUsage);
			if (ScriptUsageInStackString.IsSet())
			{
				ScriptAndUsage = ScriptAndUsage + FString(", ") + ScriptUsageInStackString.GetValue() + FString(", ");
			}
			OwningScriptNameAndUsageText = TOptional<const FText>(FText::FromString(ScriptAndUsage));
		}

		if (SourceScriptAssetPath.IsSet())
		{
			//If this compile event is from a script, set the compiled script name and asset path so the user can navigate to errors locally.
			CompiledScriptNameAndPath = TOptional<const FNiagaraScriptNameAndAssetPath>(FNiagaraScriptNameAndAssetPath(OriginatingScriptWeakObjPtr->GetName(), SourceScriptAssetPath.GetValue()));
		}
		return MakeShared<FNiagaraMessageCompileEvent>(CompileEvent, ContextScriptNamesAndPaths, OwningScriptNameAndUsageText, CompiledScriptNameAndPath);
	}
	//The originating script weak ptr is no longer valid, send an error message asking for recompile.
	FText MessageText = LOCTEXT("CompileEventMessageJobFail", "Cached info for compile event is out of date, recompile to get full info. Event: {0}");
	//Add in the message of the compile event for visibility on which compile events do not have info.
	FText::Format(MessageText, FText::FromString(CompileEvent.Message));
	return MakeShared<FNiagaraMessageNeedRecompile>(MessageText);
}

bool FNiagaraMessageJobCompileEvent::RecursiveGetScriptNamesAndAssetPathsFromContextStack(
	  TArray<FGuid>& InContextStackNodeGuids
	, const UNiagaraGraph* InGraphToSearch
	, TArray<FNiagaraScriptNameAndAssetPath>& OutContextScriptNamesAndAssetPaths
	, TOptional<const FString>& OutEmitterName
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
				FText FailureReason = LOCTEXT("GenerateCompileEventMessage_FunctionCallNodeScriptNotFound", "Script for Function Call Node \"{0}\" not found!");
				OutFailureReason = FText::Format(FailureReason, FText::FromString(FunctionCallNode->GetFunctionName()));
				return false;
			}
			UNiagaraScriptSourceBase* FunctionScriptSourceBase = FunctionCallNodeAssignedScript->GetSource();
			if (FunctionScriptSourceBase == nullptr)
			{
				FText FailureReason = LOCTEXT("GenerateCompileEventMessage_FunctionCallNodeScriptSourceBaseNotFound", "Source Script for Function Call Node \"{0}\" not found!");
				OutFailureReason = FText::Format(FailureReason, FText::FromString(FunctionCallNode->GetFunctionName()));
				return false;
			}
			UNiagaraScriptSource* FunctionScriptSource = Cast<UNiagaraScriptSource>(FunctionScriptSourceBase);
			checkf(FunctionScriptSource, TEXT("Script source base was somehow not a derived type!"));

			UNiagaraGraph* FunctionScriptGraph = FunctionScriptSource->NodeGraph;
			if (FunctionScriptGraph == nullptr)
			{
				FText FailureReason = LOCTEXT("GenerateCompileEventMessage_FunctionCallNodeGraphNotFound", "Graph for Function Call Node \"{0}\" not found!");
				OutFailureReason = FText::Format(FailureReason, FText::FromString(FunctionCallNode->GetFunctionName()));
				return false;
			}
			OutContextScriptNamesAndAssetPaths.Add(FNiagaraScriptNameAndAssetPath(FunctionCallNodeAssignedScript->GetName(), FunctionCallNodeAssignedScript->GetPathName()));
			return RecursiveGetScriptNamesAndAssetPathsFromContextStack(InContextStackNodeGuids, FunctionScriptGraph, OutContextScriptNamesAndAssetPaths, OutEmitterName, OutFailureReason);
		}

		UNiagaraNodeEmitter* EmitterNode = Cast<UNiagaraNodeEmitter>(ContextNode.GetValue());
		if (EmitterNode)
		{
			// node is an emitter node, now get the Emitter name, add a message token and recursively call into the graph of that emitter.
			UNiagaraScriptSource* EmitterScriptSource = EmitterNode->GetScriptSource();
			checkf(EmitterScriptSource, TEXT("Emitter Node does not have a Script Source!"));
			UNiagaraGraph* EmitterScriptGraph = EmitterScriptSource->NodeGraph;
			checkf(EmitterScriptGraph, TEXT("Emitter Script Source does not have a UNiagaraGraph!"));

			OutEmitterName = EmitterNode->GetEmitterUniqueName();
			return RecursiveGetScriptNamesAndAssetPathsFromContextStack(InContextStackNodeGuids, EmitterScriptGraph, OutContextScriptNamesAndAssetPaths, OutEmitterName, OutFailureReason);
		}
		checkf(false, TEXT("Matching node is not a function call or emitter node!"));
	}
	FText FailureReason = LOCTEXT("CompileEventMessageGenerator_CouldNotFindMatchingNodeGUID", "Failed to walk the entire context stack, is this compile event out of date ? Event: '{0}'");
	FText::Format(FailureReason, FText::FromString(CompileEvent.Message));
	OutFailureReason = FailureReason;
	return false;
}

TSharedRef<const INiagaraMessage> FNiagaraMessageJobPostCompileSummary::GenerateNiagaraMessage() const
{
	FText MessageText = FText();
	EMessageSeverity::Type MessageSeverity = EMessageSeverity::Warning;
	switch (ScriptCompileStatus)
	{
	default:
	case ENiagaraScriptCompileStatus::NCS_Unknown:
	case ENiagaraScriptCompileStatus::NCS_Dirty:
		MessageText = LOCTEXT("NiagaraCompileStatusUnknownInfo", "{0} compile status unknown with {1} warning(s) and {2} error(s).");
		MessageText = FText::Format(MessageText, CompiledObjectNameText, FText::FromString(FString::FromInt(NumCompileWarnings)), FText::FromString(FString::FromInt(NumCompileErrors)));
		//MessageSeverity = EMessageSeverity::Warning;
		break;
	case ENiagaraScriptCompileStatus::NCS_Error:
		MessageText = LOCTEXT("NiagaraCompileStatusErrorInfo", "{0} failed to compile with {1} warning(s) and {2} error(s).");
		MessageText =  FText::Format(MessageText, CompiledObjectNameText, FText::FromString(FString::FromInt(NumCompileWarnings)), FText::FromString(FString::FromInt(NumCompileErrors)));
		//MessageSeverity = EMessageSeverity::Error;
		break;
	case ENiagaraScriptCompileStatus::NCS_UpToDate:
		MessageText = LOCTEXT("NiagaraCompileStatusSuccessInfo", "{0} successfully compiled.");
		MessageText = FText::Format(MessageText, CompiledObjectNameText);
		//MessageSeverity = EMessageSeverity::Info;
		break;
	case ENiagaraScriptCompileStatus::NCS_UpToDateWithWarnings:
		MessageText = LOCTEXT("NiagaraCompileStatusWarningInfo", "{0} successfully compiled with {1} warning(s).");
		MessageText =  FText::Format(MessageText, CompiledObjectNameText, FText::FromString(FString::FromInt(NumCompileWarnings)));
		//MessageSeverity = EMessageSeverity::Warning;
		break;
	}
	MessageSeverity = EMessageSeverity::Info;
	return MakeShared<FNiagaraMessagePostCompileSummary>(MessageText, MessageSeverity);
}

TSharedRef<FTokenizedMessage> FNiagaraMessageNeedRecompile::GenerateTokenizedMessage() const
{
	return FTokenizedMessage::Create(EMessageSeverity::Error, NeedRecompileMessage);
}

TSharedRef<FTokenizedMessage> FNiagaraMessagePostCompileSummary::GenerateTokenizedMessage() const
{
	return FTokenizedMessage::Create(MessageSeverity, PostCompileSummaryText);
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
			GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(ScriptAsset, EToolkitMode::Standalone);
			FNiagaraEditorModule& NiagaraEditorModule = FModuleManager::LoadModuleChecked<FNiagaraEditorModule>("NiagaraEditor");
			if (InPinGUID.IsSet())
			{
				
				TSharedRef<FNiagaraScriptGraphPinToFocusInfo> PinToFocusInfo = MakeShared<FNiagaraScriptGraphPinToFocusInfo>(InPinGUID.GetValue());
				FNiagaraScriptIDAndGraphFocusInfo PinToFocusAndScriptID = FNiagaraScriptIDAndGraphFocusInfo(ScriptAsset->GetUniqueID(), PinToFocusInfo);
				NiagaraEditorModule.GetOnScriptToolkitsShouldFocusGraphElement().Broadcast(&PinToFocusAndScriptID);
			}
			else if (InNodeGUID.IsSet())
			{
				TSharedRef<FNiagaraScriptGraphNodeToFocusInfo> NodeToFocusInfo = MakeShared<FNiagaraScriptGraphNodeToFocusInfo>(InNodeGUID.GetValue());
				FNiagaraScriptIDAndGraphFocusInfo NodeToFocusAndScriptID = FNiagaraScriptIDAndGraphFocusInfo(ScriptAsset->GetUniqueID(), NodeToFocusInfo);
				NiagaraEditorModule.GetOnScriptToolkitsShouldFocusGraphElement().Broadcast(&NodeToFocusAndScriptID);
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
