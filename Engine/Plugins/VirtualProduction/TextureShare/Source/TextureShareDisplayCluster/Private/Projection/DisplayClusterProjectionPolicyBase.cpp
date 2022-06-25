// Copyright Epic Games, Inc. All Rights Reserved.

#include "Projection/DisplayClusterProjectionPolicyBase.h"
#include "Projection/DisplayClusterProjectionStrings.h"

#include "IDisplayCluster.h"
#include "Game/IDisplayClusterGameManager.h"

#include "DisplayClusterConfigurationTypes_Base.h"

#include "Render/Viewport/IDisplayClusterViewportManager.h"
#include "Render/Viewport/IDisplayClusterViewport.h"

//////////////////////////////////////////////////////////////////////////////////////////////
// FDisplayClusterProjectionPolicyBase
//////////////////////////////////////////////////////////////////////////////////////////////
FDisplayClusterProjectionPolicyBase::FDisplayClusterProjectionPolicyBase(const FString& InProjectionPolicyId, const FDisplayClusterConfigurationProjection* InConfigurationProjectionPolicy)
	: ProjectionPolicyId(InProjectionPolicyId)
{
	Parameters.Append(InConfigurationProjectionPolicy->Parameters);
}

FDisplayClusterProjectionPolicyBase::~FDisplayClusterProjectionPolicyBase()
{
}

bool FDisplayClusterProjectionPolicyBase::IsEditorOperationMode_RenderThread(const IDisplayClusterViewportProxy* InViewportProxy)
{
	check(IsInRenderingThread());

	if (const bool bIsClusterOperationMode = IDisplayCluster::Get().GetOperationMode() == EDisplayClusterOperationMode::Cluster)
	{
		return false;
	}

	return true;
}

bool FDisplayClusterProjectionPolicyBase::IsEditorOperationMode(class IDisplayClusterViewport* InViewport)
{
	check(IsInGameThread());

	// Hide spam in logs when configuring VP in editor
	if (const bool bIsClusterOperationMode = IDisplayCluster::Get().GetOperationMode() == EDisplayClusterOperationMode::Cluster)
	{
		return false;
	}

	// Get state from viewport world (UE-114493)
	if (InViewport)
	{
		if (UWorld* CurrentWorld = InViewport->GetOwner().GetCurrentWorld())
		{
			if (CurrentWorld)
			{
				switch (CurrentWorld->WorldType)
				{
				case EWorldType::Editor:
				case EWorldType::EditorPreview:
					return true;

				default:
					break;
				}
			}
		}
	}

	return false;
}

bool FDisplayClusterProjectionPolicyBase::IsConfigurationChanged(const struct FDisplayClusterConfigurationProjection* InConfigurationProjectionPolicy) const
{
	if (InConfigurationProjectionPolicy->Parameters.Num() != Parameters.Num())
	{
		return true;
	}

	for (const TPair<FString, FString>& NewParamIt : InConfigurationProjectionPolicy->Parameters)
	{
		const FString* CurrentValue = Parameters.Find(NewParamIt.Key);

		if (CurrentValue==nullptr || CurrentValue->Compare(NewParamIt.Value, ESearchCase::IgnoreCase) != 0)
		{
			return true;
		}
	}

	// Parameters not changed
	return false;
}
