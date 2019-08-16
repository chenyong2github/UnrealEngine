// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	FXSystemSet.cpp: Internal redirector to several fx systems.
=============================================================================*/

#include "FXSystemSet.h"

FFXSystemInterface* FFXSystemSet::GetInterface(const FName& InName)
{
	for (FFXSystemInterface* FXSystem : FXSystems)
	{
		check(FXSystem);
		FXSystem = FXSystem->GetInterface(InName);
		if (FXSystem)
		{
			return FXSystem;
		}
	}
	return nullptr;
}

void FFXSystemSet::Tick(float DeltaSeconds)
{
	for (FFXSystemInterface* FXSystem : FXSystems)
	{
		check(FXSystem);
		FXSystem->Tick(DeltaSeconds);
	}
}

#if WITH_EDITOR

void FFXSystemSet::Suspend()
{
	for (FFXSystemInterface* FXSystem : FXSystems)
	{
		check(FXSystem);
		FXSystem->Suspend();
	}
}

void FFXSystemSet::Resume()
{
	for (FFXSystemInterface* FXSystem : FXSystems)
	{
		check(FXSystem);
		FXSystem->Resume();
	}
}

#endif // #if WITH_EDITOR

void FFXSystemSet::DrawDebug(FCanvas* Canvas)
{
	for (FFXSystemInterface* FXSystem : FXSystems)
	{
		check(FXSystem);
		FXSystem->DrawDebug(Canvas);
	}
}

void FFXSystemSet::AddVectorField(UVectorFieldComponent* VectorFieldComponent)
{
	for (FFXSystemInterface* FXSystem : FXSystems)
	{
		check(FXSystem);
		FXSystem->AddVectorField(VectorFieldComponent);
	}
}

void FFXSystemSet::RemoveVectorField(UVectorFieldComponent* VectorFieldComponent)
{
	for (FFXSystemInterface* FXSystem : FXSystems)
	{
		check(FXSystem);
		FXSystem->RemoveVectorField(VectorFieldComponent);
	}
}

void FFXSystemSet::UpdateVectorField(UVectorFieldComponent* VectorFieldComponent)
{
	for (FFXSystemInterface* FXSystem : FXSystems)
	{
		check(FXSystem);
		FXSystem->UpdateVectorField(VectorFieldComponent);
	}
}

void FFXSystemSet::PreInitViews(FRHICommandListImmediate& RHICmdList, bool bAllowGPUParticleUpdate)
{
	for (FFXSystemInterface* FXSystem : FXSystems)
	{
		check(FXSystem);
		FXSystem->PreInitViews(RHICmdList, bAllowGPUParticleUpdate);
	}
}

bool FFXSystemSet::UsesGlobalDistanceField() const
{
	for (FFXSystemInterface* FXSystem : FXSystems)
	{
		check(FXSystem);
		if (FXSystem->UsesGlobalDistanceField())
		{
			return true;
		}
	}
	return false;
}

void FFXSystemSet::PreRender(FRHICommandListImmediate& RHICmdList, const class FGlobalDistanceFieldParameterData* GlobalDistanceFieldParameterData, bool bAllowGPUParticleSceneUpdate)
{
	for (FFXSystemInterface* FXSystem : FXSystems)
	{
		check(FXSystem);
		FXSystem->PreRender(RHICmdList, GlobalDistanceFieldParameterData, bAllowGPUParticleSceneUpdate);
	}
}

void FFXSystemSet::PostRenderOpaque(
	FRHICommandListImmediate& RHICmdList,
	FRHIUniformBuffer* ViewUniformBuffer,
	const class FShaderParametersMetadata* SceneTexturesUniformBufferStruct,
	FRHIUniformBuffer* SceneTexturesUniformBuffer,
	bool bAllowGPUParticleUpdate)
{
	for (FFXSystemInterface* FXSystem : FXSystems)
	{
		check(FXSystem);
		FXSystem->PostRenderOpaque(
			RHICmdList,
			ViewUniformBuffer,
			SceneTexturesUniformBufferStruct,
			SceneTexturesUniformBuffer,
			bAllowGPUParticleUpdate
		);
	}
}

void FFXSystemSet::OnDestroy()
{
	for (FFXSystemInterface*& FXSystem : FXSystems)
	{
		check(FXSystem);
		FXSystem->OnDestroy();
	}

	FFXSystemInterface::OnDestroy();
}

FFXSystemSet::~FFXSystemSet()
{
	for (FFXSystemInterface* FXSystem : FXSystems)
	{
		check(FXSystem);
		delete FXSystem;
	}
}
