// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterProjectionModule.h"

#include "DisplayClusterProjectionLog.h"
#include "DisplayClusterProjectionStrings.h"

#include "Policy/Camera/DisplayClusterProjectionCameraPolicyFactory.h"
#include "Policy/EasyBlend/DisplayClusterProjectionEasyBlendPolicyFactory.h"
#include "Policy/Simple/DisplayClusterProjectionSimplePolicyFactory.h"
#include "Policy/MPCDI/DisplayClusterProjectionMPCDIPolicyFactory.h"
#include "Policy/Manual/DisplayClusterProjectionManualPolicyFactory.h"

#include "IDisplayCluster.h"
#include "Render/IDisplayClusterRenderManager.h"


FDisplayClusterProjectionModule::FDisplayClusterProjectionModule()
{
	TSharedPtr<IDisplayClusterProjectionPolicyFactory> Factory;

	// Camera projection
	Factory = MakeShareable(new FDisplayClusterProjectionCameraPolicyFactory);
	ProjectionPolicyFactories.Emplace(DisplayClusterStrings::projection::Camera, Factory);

	// Simple projection
	Factory = MakeShareable(new FDisplayClusterProjectionSimplePolicyFactory);
	ProjectionPolicyFactories.Emplace(DisplayClusterStrings::projection::Simple, Factory);

	// MPCDI projection
	Factory = MakeShareable(new FDisplayClusterProjectionMPCDIPolicyFactory);
	ProjectionPolicyFactories.Emplace(DisplayClusterStrings::projection::MPCDI, Factory);

	// EasyBlend projection
	Factory = MakeShareable(new FDisplayClusterProjectionEasyBlendPolicyFactory);
	ProjectionPolicyFactories.Emplace(DisplayClusterStrings::projection::EasyBlend, Factory);

	// Manual projection
	Factory = MakeShareable(new FDisplayClusterProjectionManualPolicyFactory);
	ProjectionPolicyFactories.Emplace(DisplayClusterStrings::projection::Manual, Factory);

	UE_LOG(LogDisplayClusterProjection, Log, TEXT("Projection module has been instantiated"));
}

FDisplayClusterProjectionModule::~FDisplayClusterProjectionModule()
{
	UE_LOG(LogDisplayClusterProjection, Log, TEXT("Projection module has been destroyed"));
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IModuleInterface
//////////////////////////////////////////////////////////////////////////////////////////////
void FDisplayClusterProjectionModule::StartupModule()
{
	UE_LOG(LogDisplayClusterProjection, Log, TEXT("Projection module startup"));

	IDisplayClusterRenderManager* RenderMgr = IDisplayCluster::Get().GetRenderMgr();
	if (RenderMgr)
	{
		for (auto it = ProjectionPolicyFactories.CreateIterator(); it; ++it)
		{
			UE_LOG(LogDisplayClusterProjection, Log, TEXT("Registering <%s> projection policy factory..."), *it->Key);

			if (!RenderMgr->RegisterProjectionPolicyFactory(it->Key, it->Value))
			{
				UE_LOG(LogDisplayClusterProjection, Warning, TEXT("Couldn't register <%s> projection policy factory"), *it->Key);
			}
		}
	}

	UE_LOG(LogDisplayClusterProjection, Log, TEXT("Projection module has started"));
}

void FDisplayClusterProjectionModule::ShutdownModule()
{
	UE_LOG(LogDisplayClusterProjection, Log, TEXT("Projection module shutdown"));

	IDisplayClusterRenderManager* RenderMgr = IDisplayCluster::Get().GetRenderMgr();
	if (RenderMgr)
	{
		for (auto it = ProjectionPolicyFactories.CreateConstIterator(); it; ++it)
		{
			UE_LOG(LogDisplayClusterProjection, Log, TEXT("Un-registering <%s> projection factory..."), *it->Key);

			if (!RenderMgr->UnregisterProjectionPolicyFactory(it->Key))
			{
				UE_LOG(LogDisplayClusterProjection, Warning, TEXT("An error occurred during un-registering the <%s> projection factory"), *it->Key);
			}
		}
	}
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterProjection
//////////////////////////////////////////////////////////////////////////////////////////////
void FDisplayClusterProjectionModule::GetSupportedProjectionTypes(TArray<FString>& OutProjectionTypes)
{
	ProjectionPolicyFactories.GenerateKeyArray(OutProjectionTypes);
}

TSharedPtr<IDisplayClusterProjectionPolicyFactory> FDisplayClusterProjectionModule::GetProjectionFactory(const FString& ProjectionType)
{
	if (ProjectionPolicyFactories.Contains(ProjectionType))
	{
		return ProjectionPolicyFactories[ProjectionType];
	}

	UE_LOG(LogDisplayClusterProjection, Warning, TEXT("No <%s> projection factory available"), *ProjectionType);

	return nullptr;
}


IMPLEMENT_MODULE(FDisplayClusterProjectionModule, DisplayClusterProjection)
