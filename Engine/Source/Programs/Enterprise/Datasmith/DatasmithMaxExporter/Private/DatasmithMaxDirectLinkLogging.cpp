// Copyright Epic Games, Inc. All Rights Reserved.

#ifdef NEW_DIRECTLINK_PLUGIN

#include "DatasmithMaxDirectLink.h"

#include "Logging/LogMacros.h"

DEFINE_LOG_CATEGORY(LogDatasmithMaxExporter);

#include "Async/Async.h"

#include "Misc/OutputDeviceRedirector.h" // For GLog

#include "Logging/LogMacros.h"


#include "Windows/AllowWindowsPlatformTypes.h"
MAX_INCLUDES_START
	#include "max.h"

	#include "maxscript/maxscript.h"

	#include "ISceneEventManager.h"
MAX_INCLUDES_END



namespace DatasmithMaxDirectLink
{

void LogDebug(const TCHAR* Msg)
{
#ifdef LOG_DEBUG_ENABLE
	mprintf(L"[%s]%s\n", *FDateTime::UtcNow().ToString(TEXT("%Y.%m.%d-%H.%M.%S:%s")), Msg);
	UE_LOG(LogDatasmithMaxExporter, Error, TEXT("%s"), Msg);
	LogFlush();
#endif
}

void LogInfo(const TCHAR* Msg)
{
	UE_LOG(LogDatasmithMaxExporter, Display, TEXT("%s"), Msg);
	LogInfoDialog(Msg);
}

void LogWarning(const TCHAR* Msg)
{
	UE_LOG(LogDatasmithMaxExporter, Warning, TEXT("%s"), Msg);
	LogWarningDialog(Msg);
}

void LogFlush()
{
	Async(EAsyncExecution::TaskGraphMainThread,
		[]()
		{
			GLog->FlushThreadedLogs();
			GLog->Flush();
		});
}


void LogDebug(const FString& Msg)
{
	LogDebug(*Msg);
}

void LogInfo(const FString& Msg)
{
	LogInfo(*Msg);
}

void LogWarning(const FString& Msg)
{
	LogWarning(*Msg);
}

FString GetNodeDesc(INode* Node)
{
	return Node ? FString::Printf(TEXT("%s(%llu)"), Node->GetName(), NodeEventNamespace::GetKeyByNode(Node)) : TEXT("<null>");
}

void LogDebugNode(const FString& Name, INode* Node)
{
#ifdef LOG_DEBUG_HEAVY_ENABLE

	LogDebug(FString::Printf(TEXT("%s: %s - %s, parent: %s")
		, *Name
		, *GetNodeDesc(Node)
		, (Node && Node->IsNodeHidden(TRUE))? TEXT("HIDDEN") : TEXT("")
		, *GetNodeDesc(Node ? Node->GetParentNode() : nullptr)
		));
	if (Node)
	{
		LogDebug(FString::Printf(TEXT("    NumberOfChildren: %d "), Node->NumberOfChildren()));
		
		if (Object* ObjectRef = Node->GetObjectRef())
		{
			Class_ID ClassId = ObjectRef->ClassID();
			LogDebug(FString::Printf(TEXT("    Class_ID: 0x%lx, 0x%lx "), ClassId.PartA(), ClassId.PartB()));
		}
	}
#endif
}

void LogNodeEvent(const MCHAR* Name, INodeEventCallback::NodeKeyTab& nodes)
{
#ifdef LOG_DEBUG_HEAVY_ENABLE
	LogDebug(FString::Printf(TEXT("NodeEventCallback:%s"), Name));
	for (int NodeIndex = 0; NodeIndex < nodes.Count(); ++NodeIndex)
	{
		FNodeKey NodeKey = nodes[NodeIndex];

		Animatable* anim = Animatable::GetAnimByHandle(NodeKey);
		if (INode* Node = NodeEventNamespace::GetNodeByKey(NodeKey)) // Node sometimes is null. Not sure why
		{
			LogDebug(FString::Printf(TEXT("   %s, parent: %s"), *GetNodeDesc(Node), *GetNodeDesc(Node->GetParentNode())));
		}
		else
		{
			LogDebug(FString::Printf(TEXT("   <null>(%llu)"), NodeKey));
		}
	}
#endif
}

}
#include "Windows/HideWindowsPlatformTypes.h"

#endif // NEW_DIRECTLINK_PLUGIN
