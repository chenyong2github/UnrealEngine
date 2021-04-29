// Copyright Epic Games, Inc. All Rights Reserved.

#include "PostProcess/DisplayClusterPostprocessOutputRemap.h"

#include "Misc/DisplayClusterHelpers.h"
#include "DisplayClusterPostprocessLog.h"
#include "DisplayClusterPostprocessStrings.h"
#include "DisplayClusterRootActor.h"

#include "Render/Containers/DisplayClusterRender_MeshComponentProxy.h"
#include "Render/Containers/DisplayClusterRender_MeshGeometry.h"

#include "Render/Viewport/IDisplayClusterViewportManager.h"

#include "IDisplayClusterShaders.h"

#include "DisplayClusterRootActor.h"

#include "Engine/RendererSettings.h"
#include "Misc/Paths.h"

FDisplayClusterPostprocessOutputRemap::FDisplayClusterPostprocessOutputRemap()
	: MeshComponentProxy(*(new FDisplayClusterRender_MeshComponentProxy()))
	, ShadersAPI(IDisplayClusterShaders::Get())
{
}

FDisplayClusterPostprocessOutputRemap::~FDisplayClusterPostprocessOutputRemap()
{
	delete (&MeshComponentProxy);
}

//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterPostProcess
//////////////////////////////////////////////////////////////////////////////////////////////
bool FDisplayClusterPostprocessOutputRemap::IsPostProcessFrameAfterWarpBlendRequired() const
{
	return bIsInitialized;
}

void FDisplayClusterPostprocessOutputRemap::InitializePostProcess(class IDisplayClusterViewportManager& InViewportManager, const TMap<FString, FString>& Parameters)
{
	// OBJ file (optional)
	FString ExtMeshFile;
	if (DisplayClusterHelpers::map::template ExtractValue(Parameters, DisplayClusterPostprocessStrings::output_remap::File, ExtMeshFile))
	{
		UE_LOG(LogDisplayClusterPostprocessOutputRemap, Log, TEXT("Found Argument '%s'='%s'"), DisplayClusterPostprocessStrings::output_remap::File, *ExtMeshFile);

		FDisplayClusterRender_MeshGeometry MeshGeometry;
		if (!MeshGeometry.LoadFromFile(ExtMeshFile))
		{
			UE_LOG(LogDisplayClusterPostprocessOutputRemap, Error, TEXT("Failed to load ext mesh from file '%s'"), *ExtMeshFile);
			return;
		}

		MeshComponentProxy.UpdateDeffered(MeshGeometry);
		bIsInitialized = true;
		return;
	}

	// Mesh from RootActor
	FString RootActorMeshComponentName;
	if (DisplayClusterHelpers::map::template ExtractValue(Parameters, DisplayClusterPostprocessStrings::output_remap::Mesh, RootActorMeshComponentName))
	{
		UE_LOG(LogDisplayClusterPostprocessOutputRemap, Log, TEXT("Found Argument '%s'='%s'"), DisplayClusterPostprocessStrings::output_remap::Mesh, *RootActorMeshComponentName);

		ADisplayClusterRootActor* RootActor = InViewportManager.GetRootActor();
		if (RootActor == nullptr)
		{
			UE_LOG(LogDisplayClusterPostprocessOutputRemap, Error, TEXT("RootActor not found: Can't load static mesh component '%s'"), *RootActorMeshComponentName);
			return;
		}

		// Get mesh component
		UStaticMeshComponent* MeshComponent = RootActor->GetMeshById(RootActorMeshComponentName);
		if (MeshComponent == nullptr)
		{
			UE_LOG(LogDisplayClusterPostprocessOutputRemap, Error, TEXT("Static mesh component '%s' not found in RootActor"), *RootActorMeshComponentName);
			return;
		}

		MeshComponentProxy.AssignMeshRefs(MeshComponent);
		MeshComponentProxy.UpdateDefferedRef();
		bIsInitialized = true;
		return;
	}

	UE_LOG(LogDisplayClusterPostprocessOutputRemap, Error, TEXT("OutputRemap parameters is undefined"));
}

void FDisplayClusterPostprocessOutputRemap::PerformPostProcessFrameAfterWarpBlend_RenderThread(FRHICommandListImmediate& RHICmdList, const TArray<FRHITexture2D*>* InFrameTargets, const TArray<FRHITexture2D*>* InAdditionalFrameTargets) const
{
	if (InFrameTargets && InAdditionalFrameTargets)
	{
		// Support mesh refresh:
		if(MeshComponentProxy.MeshComponentRef.IsMeshComponentChanged())
		{ 
			MeshComponentProxy.UpdateDefferedRef();
		}

		for (int Index = 0; Index < InFrameTargets->Num(); Index++)
		{
			FRHITexture2D* InOutTexture = (*InFrameTargets)[Index];
			FRHITexture2D* TempTargetableTexture = (*InAdditionalFrameTargets)[Index];

			if (ShadersAPI.RenderPostprocess_OutputRemap(RHICmdList, InOutTexture, TempTargetableTexture, MeshComponentProxy))
			{
				//Copy remapped result back
				RHICmdList.CopyToResolveTarget(TempTargetableTexture, InOutTexture, FResolveParams());
			}
		}
	}
}
