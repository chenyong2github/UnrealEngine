// Copyright Epic Games, Inc. All Rights Reserved.

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
	FNiagaraDataBuffer& ParticleData = Data.GetCurrentDataChecked();
	FNiagaraDynamicDataLights* DynamicData = new FNiagaraDynamicDataLights(Emitter);
	if (!Properties)
	{
		return DynamicData;
	}

	//I'm not a great fan of pulling scalar components out to a structured vert buffer like this.
	//TODO: Experiment with a new VF that reads the data directly from the scalar layout.
	FNiagaraDataSetAccessor<FVector> PosAccessor(Data, Properties->PositionBinding.DataSetVariable);
	FNiagaraDataSetAccessor<FNiagaraDataConversions<FLinearColor>> ColAccessor;
	if (Data.HasVariable(Properties->ColorBinding.DataSetVariable.GetName()))
	{
		ColAccessor = FNiagaraDataSetAccessor<FNiagaraDataConversions<FLinearColor>>(Data, Properties->ColorBinding.DataSetVariable.GetName());
	}
	FNiagaraDataSetAccessor<FNiagaraDataConversions<float>> RadiusAccessor;
	if (Data.HasVariable(Properties->RadiusBinding.DataSetVariable.GetName()))
	{
		RadiusAccessor = FNiagaraDataSetAccessor<FNiagaraDataConversions<float>>(Data, Properties->RadiusBinding.DataSetVariable.GetName());
	}
	FNiagaraDataSetAccessor<FNiagaraDataConversions<float>> ExponentAccessor;
	if (Data.HasVariable(Properties->LightExponentBinding.DataSetVariable.GetName()))
	{
		ExponentAccessor = FNiagaraDataSetAccessor<FNiagaraDataConversions<float>>(Data, Properties->LightExponentBinding.DataSetVariable.GetName());
	}
	FNiagaraDataSetAccessor<FNiagaraDataConversions<float>> ScatteringAccessor;
	if (Data.HasVariable(Properties->VolumetricScatteringBinding.DataSetVariable.GetName()))
	{
		ScatteringAccessor = FNiagaraDataSetAccessor<FNiagaraDataConversions<float>>(Data, Properties->VolumetricScatteringBinding.DataSetVariable.GetName());
	}
	FNiagaraDataSetAccessor<int32> EnabledAccessor;
	if (Data.HasVariable(Properties->LightRenderingEnabledBinding.DataSetVariable.GetName()))
	{
		EnabledAccessor = FNiagaraDataSetAccessor<int32>(Data, Properties->LightRenderingEnabledBinding.DataSetVariable);
	}

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
			SimpleLightData& LightData = DynamicData->LightArray.AddDefaulted_GetRef();

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




