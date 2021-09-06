// Copyright Epic Games, Inc. All Rights Reserved.

#include "Render/Viewport/Postprocess/DisplayClusterViewportPostProcessOutputRemap.h"

#include "DisplayClusterConfigurationTypes_OutputRemap.h"

#include "Misc/DisplayClusterHelpers.h"
#include "Misc/DisplayClusterLog.h"

#include "DisplayClusterRootActor.h"

#include "Render/Containers/DisplayClusterRender_MeshComponent.h"
#include "Render/Containers/DisplayClusterRender_MeshComponentProxy.h"
#include "Render/Containers/DisplayClusterRender_MeshComponentProxyData.h"
#include "Render/Containers/DisplayClusterRender_MeshGeometry.h"

#include "Render/Viewport/IDisplayClusterViewportManager.h"

#include "IDisplayClusterShaders.h"

#include "Engine/RendererSettings.h"
#include "Engine/StaticMesh.h"
#include "Misc/Paths.h"


//////////////////////////////////////////////////////////////////////////////////////////////
// FDisplayClusterViewportPostProcessOutputRemap
//////////////////////////////////////////////////////////////////////////////////////////////
FDisplayClusterViewportPostProcessOutputRemap::FDisplayClusterViewportPostProcessOutputRemap()
	: OutputRemapMesh(*(new FDisplayClusterRender_MeshComponent()))
	, ShadersAPI(IDisplayClusterShaders::Get())
{
}

FDisplayClusterViewportPostProcessOutputRemap::~FDisplayClusterViewportPostProcessOutputRemap()
{
	delete (&OutputRemapMesh);
}

bool FDisplayClusterViewportPostProcessOutputRemap::UpdateConfiguration_ExternalFile(const FString& InExternalFile)
{
	if (ExternalFile == InExternalFile)
	{
		return true;
	}

	UpdateConfiguration_Disabled();

	if (InExternalFile.IsEmpty())
	{
		return true;
	}

	// Support related paths
	FString FullPathFileName = DisplayClusterHelpers::filesystem::GetFullPathForConfigResource(InExternalFile);
	if (FullPathFileName.IsEmpty() || !FPaths::FileExists(FullPathFileName))
	{
		if (!bErrorCantFindFileOnce)
		{
			UE_LOG(LogDisplayClusterViewport, Error, TEXT("OutputRemap - Failed to find file '%s'"), *InExternalFile);
			bErrorCantFindFileOnce = true;
		}

		return false;
	}

	bErrorCantFindFileOnce = false;

	FDisplayClusterRender_MeshGeometry MeshGeometry;
	if (!MeshGeometry.LoadFromFile(FullPathFileName))
	{
		if (!bErrorFailLoadFromFileOnce)
		{
			UE_LOG(LogDisplayClusterViewport, Error, TEXT("OutputRemap - Failed to load ext mesh from file '%s'"), *FullPathFileName);
			bErrorFailLoadFromFileOnce = true;
		}

		return false;
	}

	bErrorFailLoadFromFileOnce = false;


	OutputRemapMesh.DataFunc = FDisplayClusterRender_MeshComponentProxyDataFunc::OutputRemapScreenSpace;
	OutputRemapMesh.UpdateDeffered(MeshGeometry);

	bIsEnabled = true;
	ExternalFile = InExternalFile;

	return true;
}

bool FDisplayClusterViewportPostProcessOutputRemap::UpdateConfiguration_StaticMesh(UStaticMesh* InStaticMesh)
{
	if (StaticMesh == InStaticMesh)
	{
		return true;
	}

	UpdateConfiguration_Disabled();

	if (InStaticMesh == nullptr)
	{
		return true;
	}

	OutputRemapMesh.DataFunc = FDisplayClusterRender_MeshComponentProxyDataFunc::OutputRemapScreenSpace;
	OutputRemapMesh.UpdateDeffered(InStaticMesh);

	bIsEnabled = true;
	StaticMesh = InStaticMesh;

	return true;
}

void FDisplayClusterViewportPostProcessOutputRemap::UpdateConfiguration_Disabled()
{
	StaticMesh = nullptr;
	ExternalFile.Empty();
	bIsEnabled = false;

	OutputRemapMesh.UpdateDeffered(nullptr);
}

void FDisplayClusterViewportPostProcessOutputRemap::PerformPostProcessFrame_RenderThread(FRHICommandListImmediate& RHICmdList, const TArray<FRHITexture2D*>* InFrameTargets, const TArray<FRHITexture2D*>* InAdditionalFrameTargets) const
{
	check(IsInRenderingThread());

	if (InFrameTargets && InAdditionalFrameTargets)
	{
		const FDisplayClusterRender_MeshComponentProxy* MeshProxy = OutputRemapMesh.GetProxy();
		if (MeshProxy && MeshProxy->IsValid_RenderThread())
		{
			for (int Index = 0; Index < InFrameTargets->Num(); Index++)
			{
				FRHITexture2D* InOutTexture = (*InFrameTargets)[Index];
				FRHITexture2D* TempTargetableTexture = (*InAdditionalFrameTargets)[Index];

				if (ShadersAPI.RenderPostprocess_OutputRemap(RHICmdList, InOutTexture, TempTargetableTexture, *MeshProxy))
				{
					//Copy remapped result back
					RHICmdList.CopyToResolveTarget(TempTargetableTexture, InOutTexture, FResolveParams());
				}
			}
		}
	}
}
