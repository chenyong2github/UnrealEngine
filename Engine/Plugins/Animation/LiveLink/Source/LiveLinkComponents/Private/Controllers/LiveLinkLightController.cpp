// Copyright Epic Games, Inc. All Rights Reserved.

#include "Controllers/LiveLinkLightController.h"

#include "ILiveLinkClient.h"
#include "Roles/LiveLinkLightRole.h"
#include "Roles/LiveLinkLightTypes.h"

#include "Components/LightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Features/IModularFeatures.h"
#include "GameFramework/Actor.h"

#if WITH_EDITOR
#include "Kismet2/ComponentEditorUtils.h"
#endif


void ULiveLinkLightController::OnEvaluateRegistered()
{
	AActor* OuterActor = GetOuterActor();
	TransformData.CheckForError(OuterActor ? OuterActor->GetFName() : NAME_None, Cast<USceneComponent>(ComponentToControl.GetComponent(OuterActor)));
}


void ULiveLinkLightController::Tick(float DeltaTime, const FLiveLinkSubjectRepresentation& SubjectRepresentation)
{
	if (ULightComponent* LightComponent = Cast<ULightComponent>(ComponentToControl.GetComponent(GetOuterActor())))
	{
		ILiveLinkClient& LiveLinkClient = IModularFeatures::Get().GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);

		FLiveLinkSubjectFrameData SubjectData;
		if (LiveLinkClient.EvaluateFrame_AnyThread(SubjectRepresentation.Subject, SubjectRepresentation.Role, SubjectData))
		{
			FLiveLinkLightStaticData* StaticData = SubjectData.StaticData.Cast<FLiveLinkLightStaticData>();
			FLiveLinkLightFrameData* FrameData = SubjectData.FrameData.Cast<FLiveLinkLightFrameData>();

			if (StaticData && FrameData)
			{
				TransformData.ApplyTransform(LightComponent, FrameData->Transform);

				if (StaticData->bIsTemperatureSupported) { LightComponent->SetTemperature(FrameData->Temperature); }
				if (StaticData->bIsIntensitySupported) { LightComponent->SetIntensity(FrameData->Intensity); }
				if (StaticData->bIsLightColorSupported) { LightComponent->SetLightColor(FrameData->LightColor); }

				if (UPointLightComponent* PointLightComponent = Cast<UPointLightComponent>(LightComponent))
				{
					if (StaticData->bIsAttenuationRadiusSupported) { PointLightComponent->SetAttenuationRadius(FrameData->AttenuationRadius); }
					if (StaticData->bIsSourceRadiusSupported) { PointLightComponent->SetSourceRadius(FrameData->SourceRadius); }
					if (StaticData->bIsSoftSourceRadiusSupported) { PointLightComponent->SetSoftSourceRadius(FrameData->SoftSourceRadius); }
					if (StaticData->bIsSourceLenghtSupported) { PointLightComponent->SetSourceLength(FrameData->SourceLength); }

					if (USpotLightComponent* SpotlightComponent = Cast<USpotLightComponent>(LightComponent))
					{
						if (StaticData->bIsInnerConeAngleSupported) { SpotlightComponent->SetInnerConeAngle(FrameData->InnerConeAngle); }
						if (StaticData->bIsOuterConeAngleSupported) { SpotlightComponent->SetOuterConeAngle(FrameData->OuterConeAngle); }
					}
				}
			}
		}
	}
}


bool ULiveLinkLightController::IsRoleSupported(const TSubclassOf<ULiveLinkRole>& RoleToSupport)
{
	return RoleToSupport->IsChildOf(ULiveLinkLightRole::StaticClass());
}


#if WITH_EDITOR
void ULiveLinkLightController::InitializeInEditor()
{
	if (AActor* Actor = GetOuterActor())
	{
		if (ULightComponent* LightComponent = Actor->FindComponentByClass<ULightComponent>())
		{
			ComponentToControl = FComponentEditorUtils::MakeComponentReference(Actor, LightComponent);
		}
	}
}
#endif
