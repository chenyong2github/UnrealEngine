// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "NiagaraRendererLights.h"
#include "ParticleResources.h"
#include "NiagaraDataSet.h"
#include "NiagaraStats.h"
#include "NiagaraVertexFactory.h"
#include "Engine/Engine.h"

#include "NiagaraRendererLights.h"


DECLARE_CYCLE_STAT(TEXT("Generate Particle Lights"), STAT_NiagaraGenLights, STATGROUP_Niagara);

struct FNiagaraDynamicDataLights : public FNiagaraDynamicDataBase
{
	FNiagaraDynamicDataLights(const FNiagaraEmitterInstance* InEmitter)
		: FNiagaraDynamicDataBase(InEmitter)
	{
	}
	
	TArray<FNiagaraRendererLights::SimpleLightData> LightArray;
};

//////////////////////////////////////////////////////////////////////////


FNiagaraRendererLights::FNiagaraRendererLights(ERHIFeatureLevel::Type FeatureLevel, const UNiagaraRendererProperties *InProps, const FNiagaraEmitterInstance* Emitter)
	: FNiagaraRenderer(FeatureLevel, InProps, Emitter)
{
	bHasLights = true;
}

FPrimitiveViewRelevance FNiagaraRendererLights::GetViewRelevance(const FSceneView* View, const FNiagaraSceneProxy *SceneProxy)const
{
	FPrimitiveViewRelevance Result;
	Result.bDrawRelevance = false;
	Result.bShadowRelevance = false;
	Result.bDynamicRelevance = false;
	Result.bOpaqueRelevance = false;
	Result.bHasSimpleLights = true;
	//MaterialRelevance.SetPrimitiveViewRelevance(Result);
	return Result;
}

/** Update render data buffer from attributes */
FNiagaraDynamicDataBase* FNiagaraRendererLights::GenerateDynamicData(const FNiagaraSceneProxy* Proxy, const UNiagaraRendererProperties* InProperties, const FNiagaraEmitterInstance* Emitter) const
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraGenLights);

	//Bail if we don't have the required attributes to render this emitter.
	const UNiagaraLightRendererProperties* Properties = CastChecked<const UNiagaraLightRendererProperties>(InProperties);
	FNiagaraDataSet& Data = Emitter->GetData();
	FNiagaraDataBuffer& ParticleData = Data.GetCurrentDataChecked();
	FNiagaraDynamicDataLights* DynamicData = new FNiagaraDynamicDataLights(Emitter);
	if (!Properties)
	{
		return DynamicData;
	}

	//I'm not a great fan of pulling scalar components out to a structured vert buffer like this.
	//TODO: Experiment with a new VF that reads the data directly from the scalar layout.
	FNiagaraDataSetAccessor<FVector> PosAccessor(Data, Properties->PositionBinding.DataSetVariable);
	FNiagaraDataSetAccessor<FLinearColor> ColAccessor(Data, Properties->ColorBinding.DataSetVariable);
	FNiagaraDataSetAccessor<float> RadiusAccessor(Data, Properties->RadiusBinding.DataSetVariable);
	FNiagaraDataSetAccessor<float> ExponentAccessor(Data, Properties->LightExponentBinding.DataSetVariable);
	FNiagaraDataSetAccessor<float> ScatteringAccessor(Data, Properties->VolumetricScatteringBinding.DataSetVariable);
	FNiagaraDataSetAccessor<int32> EnabledAccessor(Data, Properties->LightRenderingEnabledBinding.DataSetVariable);

	const FMatrix& LocalToWorldMatrix = Proxy->GetLocalToWorld();
	FVector DefaultColor = FVector(Properties->ColorBinding.DefaultValueIfNonExistent.GetValue<FLinearColor>());
	FVector DefaultPos = FVector4(LocalToWorldMatrix.GetOrigin());
	float DefaultRadius = Properties->RadiusBinding.DefaultValueIfNonExistent.GetValue<float>();
	float DefaultScattering = Properties->VolumetricScatteringBinding.DefaultValueIfNonExistent.GetValue<float>();

	for (uint32 ParticleIndex = 0; ParticleIndex < ParticleData.GetNumInstances(); ParticleIndex++)
	{
		bool ShouldRenderParticleLight = !Properties->bOverrideRenderingEnabled || EnabledAccessor.GetSafe(ParticleIndex, true);
		float LightRadius = RadiusAccessor.GetSafe(ParticleIndex, DefaultRadius) * Properties->RadiusScale;
		if (ShouldRenderParticleLight && LightRadius > 0)
		{
			SimpleLightData LightData;
			LightData.LightEntry.Radius = LightRadius;
			LightData.LightEntry.Color = FVector(ColAccessor.GetSafe(ParticleIndex, DefaultColor)) + Properties->ColorAdd;
			LightData.LightEntry.Exponent = Properties->bUseInverseSquaredFalloff ? 0 : ExponentAccessor.GetSafe(ParticleIndex, 1.0f);
			LightData.LightEntry.bAffectTranslucency = Properties->bAffectsTranslucency;
			LightData.LightEntry.VolumetricScatteringIntensity = ScatteringAccessor.GetSafe(ParticleIndex, DefaultScattering);
			LightData.PerViewEntry.Position = PosAccessor.GetSafe(ParticleIndex, DefaultPos);
			if (bLocalSpace)
			{
				LightData.PerViewEntry.Position = LocalToWorldMatrix.TransformPosition(LightData.PerViewEntry.Position);
			}

			DynamicData->LightArray.Add(LightData);
		}
	}

	return DynamicData;
}

void FNiagaraRendererLights::GatherSimpleLights(FSimpleLightArray& OutParticleLights)const
{
	FNiagaraDynamicDataLights* DynamicData = (FNiagaraDynamicDataLights*)DynamicDataRender;
	if (DynamicData)
	{
		int32 LightCount = DynamicData->LightArray.Num();

		OutParticleLights.InstanceData.Reserve(OutParticleLights.InstanceData.Num() + LightCount);
		OutParticleLights.PerViewData.Reserve(OutParticleLights.PerViewData.Num() + LightCount);

		for (FNiagaraRendererLights::SimpleLightData &LightData : DynamicData->LightArray)
		{
			// When not using camera-offset, output one position for all views to share. 
			OutParticleLights.PerViewData.Add(LightData.PerViewEntry);

			// Add an entry for the light instance.
			OutParticleLights.InstanceData.Add(LightData.LightEntry);
		}
	}
}




