// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterMoviePipelineSettings.h"
#include "DisplayClusterConfigurationTypes.h"
#include "MovieRenderPipelineCoreModule.h"
#include "Modules/ModuleManager.h"

///////////////////////////////////////////////////////////////////////////////////////////
// UDisplayClusterMoviePipelineSettings
///////////////////////////////////////////////////////////////////////////////////////////
ADisplayClusterRootActor* UDisplayClusterMoviePipelineSettings::GetRootActor(const UWorld* InWorld) const
{
	if (InWorld)
	{
		for (const TWeakObjectPtr<ADisplayClusterRootActor> RootActorRef : TActorRange<ADisplayClusterRootActor>(InWorld))
		{
			if (ADisplayClusterRootActor* RootActorPtr = RootActorRef.Get())
			{
				if (!DCRootActor.IsValid() || RootActorPtr->GetFName() == DCRootActor->GetFName())
				{
					return RootActorPtr;
				}
			}
		}
	}

	return nullptr;
}

bool UDisplayClusterMoviePipelineSettings::GetViewports(const UWorld* InWorld, TArray<FString>& OutViewports) const
{
	if (ADisplayClusterRootActor* RootActorPtr = GetRootActor(InWorld))
	{
		if (const UDisplayClusterConfigurationData* InConfigurationData = RootActorPtr->GetConfigData())
		{
			if (const UDisplayClusterConfigurationCluster* InClusterCfg =  InConfigurationData->Cluster)
			{
				for (const UE_TRANSITIONAL_OBJECT_PTR_TEMPLATE2_ARG2(TPair, FString, UDisplayClusterConfigurationClusterNode)& NodeIt : InClusterCfg->Nodes)
				{
					if (const UDisplayClusterConfigurationClusterNode* InConfigurationClusterNode = NodeIt.Value)
					{
						const FString& InClusterNodeId = NodeIt.Key;
						for (const UE_TRANSITIONAL_OBJECT_PTR_TEMPLATE2_ARG2(TPair, FString, UDisplayClusterConfigurationViewport)& InConfigurationViewportIt : InConfigurationClusterNode->Viewports)
						{
							if (const UDisplayClusterConfigurationViewport* InConfigurationViewport = InConfigurationViewportIt.Value)
							{
								if (InConfigurationViewport->bAllowRendering)
								{
									const FString& InViewportId = InConfigurationViewportIt.Key;
									if (bRenderAllViewports || AllowedViewportNamesList.Find(InViewportId) != INDEX_NONE)
									{
										OutViewports.Add(InViewportId);
									}
								}
							}
						}
					}
				}

				return OutViewports.Num() > 0;
			}
		}
	}

	return false;
}

IMPLEMENT_MODULE(FDefaultModuleImpl, DisplayClusterMoviePipeline);
