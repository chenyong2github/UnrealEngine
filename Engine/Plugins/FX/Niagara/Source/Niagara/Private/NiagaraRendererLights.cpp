// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraRendererLights.h"
#include "ParticleResources.h"
#include "NiagaraDataSet.h"
#include "NiagaraDataSetAccessor.h"
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
	// todo - for platforms where we know we can't support deferred shading we can just set this to false
	bHasLights = true;
}

FPrimitiveViewRelevance FNiagaraRendererLights::GetViewRelevance(const FSceneView* View, const FNiagaraSceneProxy *SceneProxy)const
{
	FPrimitiveViewRelevance Result;
	Result.bDrawRelevance = bHasLights;
	Result.bShadowRelevance = false;
	Result.bDynamicRelevance = false;
	Result.bOpaque = false;
	Result.bHasSimpleLights = bHasLights;

	return Result;
}

/** Update render data buffer from attributes */
FNiagaraDynamicDataBase* FNiagaraRendererLights::GenerateDynamicData(const FNiagaraSceneProxy* Proxy, const UNiagaraRendererProperties* InProperties, const FNiagaraEmitterInstance* Emitter) const
{
	// particle (simple) lights are only supported with deferred shading
	if (!bHasLights || Proxy->GetScene().GetShadingPath() != EShadingPath::Deferred)
	{
		return nullptr;
	}

	SCOPE_CYCLE_COUNTER(STAT_NiagaraGenLights);

	//Bail if we don't have the required attributes to render this emitter.
	const UNiagaraLightRendererProperties* Properties = CastChecked<const UNiagaraLightRendererProperties>(InProperties);
	FNiagaraDataSet& Data = Emitter->GetData();
	FNiagaraDataBuffer* DataToRender = Data.GetCurrentData();
	if (DataToRender == nullptr)
	{
		return nullptr;
	}

	FNiagaraDynamicDataLights* DynamicData = new FNiagaraDynamicDataLights(Emitter);

	//I'm not a great fan of pulling scalar components out to a structured vert buffer like this.
	//TODO: Experiment with a new VF that reads the data directly from the scalar layout.
	const auto PositionReader = FNiagaraDataSetAccessor<FVector>::CreateReader(Data, Properties->PositionBinding.DataSetVariable.GetName());
	const auto ColorReader = FNiagaraDataSetAccessor<FLinearColor>::CreateReader(Data, Properties->ColorBinding.DataSetVariable.GetName());
	const auto RadiusReader = FNiagaraDataSetAccessor<float>::CreateReader(Data, Properties->RadiusBinding.DataSetVariable.GetName());
	const auto ExponentReader = FNiagaraDataSetAccessor<float>::CreateReader(Data, Properties->LightExponentBinding.DataSetVariable.GetName());
	const auto ScatteringReader = FNiagaraDataSetAccessor<float>::CreateReader(Data, Properties->VolumetricScatteringBinding.DataSetVariable.GetName());
	const auto EnabledReader = FNiagaraDataSetAccessor<FNiagaraBool>::CreateReader(Data, Properties->LightRenderingEnabledBinding.DataSetVariable.GetName());

	const FMatrix& LocalToWorldMatrix = Proxy->GetLocalToWorld();
	const FLinearColor DefaultColor = Properties->ColorBinding.DefaultValueIfNonExistent.GetValue<FLinearColor>();
	const FVector DefaultPos = LocalToWorldMatrix.GetOrigin();
	const float DefaultRadius = Properties->RadiusBinding.DefaultValueIfNonExistent.GetValue<float>();
	const float DefaultScattering = Properties->VolumetricScatteringBinding.DefaultValueIfNonExistent.GetValue<float>();
	const FNiagaraBool DefaultEnabled(true);

	for (uint32 ParticleIndex = 0; ParticleIndex < DataToRender->GetNumInstances(); ParticleIndex++)
	{
		bool ShouldRenderParticleLight = EnabledReader.GetSafe(ParticleIndex, DefaultEnabled).GetValue();
		float LightRadius = RadiusReader.GetSafe(ParticleIndex, DefaultRadius) * Properties->RadiusScale;
		if (ShouldRenderParticleLight && LightRadius > 0)
		{
			SimpleLightData& LightData = DynamicData->LightArray.AddDefaulted_GetRef();

			LightData.LightEntry.Radius = LightRadius;
			LightData.LightEntry.Color = FVector(ColorReader.GetSafe(ParticleIndex, DefaultColor)) + Properties->ColorAdd;
			LightData.LightEntry.Exponent = Properties->bUseInverseSquaredFalloff ? 0 : ExponentReader.GetSafe(ParticleIndex, 1.0f);
			LightData.LightEntry.bAffectTranslucency = Properties->bAffectsTranslucency;
			LightData.LightEntry.VolumetricScatteringIntensity = ScatteringReader.GetSafe(ParticleIndex, DefaultScattering);
			LightData.PerViewEntry.Position = PositionReader.GetSafe(ParticleIndex, DefaultPos);
			if (bLocalSpace)
			{
				LightData.PerViewEntry.Position = LocalToWorldMatrix.TransformPosition(LightData.PerViewEntry.Position);
			}
		}
	}

	return DynamicData;
}

void FNiagaraRendererLights::GatherSimpleLights(FSimpleLightArray& OutParticleLights)const
{
	if (const FNiagaraDynamicDataLights* DynamicData = static_cast<const FNiagaraDynamicDataLights*>(DynamicDataRender))
	{
		const int32 LightCount = DynamicData->LightArray.Num();

		OutParticleLights.InstanceData.Reserve(OutParticleLights.InstanceData.Num() + LightCount);
		OutParticleLights.PerViewData.Reserve(OutParticleLights.PerViewData.Num() + LightCount);

		for (const FNiagaraRendererLights::SimpleLightData &LightData : DynamicData->LightArray)
		{
			// When not using camera-offset, output one position for all views to share. 
			OutParticleLights.PerViewData.Add(LightData.PerViewEntry);

			// Add an entry for the light instance.
			OutParticleLights.InstanceData.Add(LightData.LightEntry);
		}
	}
}




