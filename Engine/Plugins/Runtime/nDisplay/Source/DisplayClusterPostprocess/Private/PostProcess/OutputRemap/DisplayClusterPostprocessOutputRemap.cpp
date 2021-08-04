// Copyright Epic Games, Inc. All Rights Reserved.

#include "PostProcess/OutputRemap/DisplayClusterPostprocessOutputRemap.h"

#include "Misc/DisplayClusterHelpers.h"
#include "DisplayClusterPostprocessLog.h"
#include "DisplayClusterPostprocessStrings.h"
#include "DisplayClusterRootActor.h"

#include "Render/Containers/DisplayClusterRender_MeshComponent.h"
#include "Render/Containers/DisplayClusterRender_MeshComponentProxy.h"
#include "Render/Containers/DisplayClusterRender_MeshComponentProxyData.h"
#include "Render/Containers/DisplayClusterRender_MeshGeometry.h"

#include "Render/Viewport/IDisplayClusterViewportManager.h"

#include "IDisplayClusterShaders.h"

#include "DisplayClusterRootActor.h"

#include "Engine/RendererSettings.h"
#include "Misc/Paths.h"

//////////////////////////////////////////////////////////////////////////////////////////////
// FDisplayClusterPostprocessOutputRemap
//////////////////////////////////////////////////////////////////////////////////////////////
FDisplayClusterPostprocessOutputRemap::FDisplayClusterPostprocessOutputRemap(const FString& PostprocessId, const FDisplayClusterConfigurationPostprocess* InConfigurationPostprocess)
	: FDisplayClusterPostprocessBase(PostprocessId, InConfigurationPostprocess)
	, OutputRemapMesh(*(new FDisplayClusterRender_MeshComponent()))
	, ShadersAPI(IDisplayClusterShaders::Get())
{
}

FDisplayClusterPostprocessOutputRemap::~FDisplayClusterPostprocessOutputRemap()
{
	check(IsInRenderingThread());

	ImplRelease();
	delete (&OutputRemapMesh);
}

const FString FDisplayClusterPostprocessOutputRemap::GetTypeId() const
{
	return DisplayClusterPostprocessStrings::postprocess::OutputRemap;
}

bool FDisplayClusterPostprocessOutputRemap::HandleStartScene(IDisplayClusterViewportManager* InViewportManager)
{
	check(IsInGameThread());
	check(InViewportManager);

	if (!bIsInitialized)
	{
		if (!ImplInitialize(InViewportManager))
		{
			UE_LOG(LogDisplayClusterPostprocessOutputRemap, Warning, TEXT("Couldn't initialize pp_OutputRemap '%s'"), *GetId());

			return false;
		}
	}

	return true;
}

void FDisplayClusterPostprocessOutputRemap::HandleEndScene(IDisplayClusterViewportManager* InViewportManager)
{
	check(IsInGameThread());

	ImplRelease();
}

bool FDisplayClusterPostprocessOutputRemap::ImplInitialize(IDisplayClusterViewportManager* InViewportManager)
{
	// Mesh from RootActor
	FString RootActorMeshComponentName;
	if (DisplayClusterHelpers::map::template ExtractValue(GetParameters(), DisplayClusterPostprocessStrings::output_remap::Mesh, RootActorMeshComponentName))
	{
		UE_LOG(LogDisplayClusterPostprocessOutputRemap, Log, TEXT("OutputRemap '%s' - Found Argument '%s'='%s'"), *GetId(), DisplayClusterPostprocessStrings::output_remap::Mesh, *RootActorMeshComponentName);

		ADisplayClusterRootActor* RootActor = InViewportManager->GetRootActor();
		if (RootActor == nullptr)
		{
			UE_LOG(LogDisplayClusterPostprocessOutputRemap, Error, TEXT("OutputRemap '%s' - RootActor not found: Can't load static mesh component '%s'"), *GetId(), *RootActorMeshComponentName);
			return false;
		}

		// Get mesh component
		UStaticMeshComponent* MeshComponent = RootActor->GetComponentByName<UStaticMeshComponent>(RootActorMeshComponentName);
		if (MeshComponent == nullptr)
		{
			UE_LOG(LogDisplayClusterPostprocessOutputRemap, Error, TEXT("OutputRemap '%s' - Static mesh component '%s' not found in RootActor"), *GetId(), *RootActorMeshComponentName);
			return false;
		}

		OutputRemapMesh.DataFunc = FDisplayClusterRender_MeshComponentProxyDataFunc::OutputRemapScreenSpace;
		OutputRemapMesh.AssignMeshRefs(MeshComponent);

		bIsInitialized = true;

		return true;
	}

	// OBJ file (optional)
	FString ExtMeshFile;
	if (DisplayClusterHelpers::map::template ExtractValue(GetParameters(), DisplayClusterPostprocessStrings::output_remap::File, ExtMeshFile))
	{
		UE_LOG(LogDisplayClusterPostprocessOutputRemap, Log, TEXT("OutputRemap '%s' - Found Argument '%s'='%s'"), *GetId(), DisplayClusterPostprocessStrings::output_remap::File, *ExtMeshFile);

		// Support related paths
		FString FullPathFileName = DisplayClusterHelpers::filesystem::GetFullPathForConfigResource(ExtMeshFile);
		if (FullPathFileName.IsEmpty() || !FPaths::FileExists(FullPathFileName))
		{
			UE_LOG(LogDisplayClusterPostprocessOutputRemap, Error, TEXT("OutputRemap '%s' - Failed to find file '%s'"), *GetId(), *ExtMeshFile);
			return false;
		}

		FDisplayClusterRender_MeshGeometry MeshGeometry;
		if (!MeshGeometry.LoadFromFile(FullPathFileName))
		{
			UE_LOG(LogDisplayClusterPostprocessOutputRemap, Error, TEXT("OutputRemap '%s' - Failed to load ext mesh from file '%s'"), *GetId() , *FullPathFileName);
			return false;
		}

		OutputRemapMesh.DataFunc = FDisplayClusterRender_MeshComponentProxyDataFunc::OutputRemapScreenSpace;
		OutputRemapMesh.UpdateDeffered(MeshGeometry);

		bIsInitialized = true;

		return true;
	}

	UE_LOG(LogDisplayClusterPostprocessOutputRemap, Error, TEXT("OutputRemap '%s' parameters is undefined"), *GetId());

	return false;
}

void FDisplayClusterPostprocessOutputRemap::ImplRelease()
{
	bIsInitialized = false;
}

void FDisplayClusterPostprocessOutputRemap::Tick()
{
	check(IsInGameThread());

	if (bIsInitialized && OutputRemapMesh.MeshComponentRef.IsDefinedSceneComponent())
	{
		// Support mesh refresh:
		if (OutputRemapMesh.MeshComponentRef.IsMeshComponentChanged())
		{
			OutputRemapMesh.UpdateDefferedRef();
		}
	}
}

void FDisplayClusterPostprocessOutputRemap::PerformPostProcessFrameAfterWarpBlend_RenderThread(FRHICommandListImmediate& RHICmdList, const TArray<FRHITexture2D*>* InFrameTargets, const TArray<FRHITexture2D*>* InAdditionalFrameTargets) const
{
	check(IsInRenderingThread());

	if (InFrameTargets && InAdditionalFrameTargets)
	{
		if (bIsInitialized)
		{
			for (int Index = 0; Index < InFrameTargets->Num(); Index++)
			{
				FRHITexture2D* InOutTexture = (*InFrameTargets)[Index];
				FRHITexture2D* TempTargetableTexture = (*InAdditionalFrameTargets)[Index];

				const FDisplayClusterRender_MeshComponentProxy* MeshProxy = OutputRemapMesh.GetProxy();
				if (MeshProxy && MeshProxy->IsValid_RenderThread())
				{
					if (ShadersAPI.RenderPostprocess_OutputRemap(RHICmdList, InOutTexture, TempTargetableTexture, *MeshProxy))
					{
						//Copy remapped result back
						RHICmdList.CopyToResolveTarget(TempTargetableTexture, InOutTexture, FResolveParams());
					}
				}
			}
		}
	}
}
