// Copyright Epic Games, Inc. All Rights Reserved.

#include "SceneImporter.h"

#include "DatasmithRuntimeUtils.h"

#include "DatasmithAreaLightActor.h"
#include "DatasmithSceneFactory.h"
#include "DatasmithUtils.h"
#include "IDatasmithSceneElements.h"

#include "Components/ChildActorComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/LightComponent.h"
#include "Components/LightmassPortalComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/Blueprint.h"
#include "Engine/TextureLightProfile.h"
#include "Math/Quat.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace DatasmithRuntime
{
	extern const FString TexturePrefix;
	extern const FString MaterialPrefix;
	extern const FString MeshPrefix;
	
	USceneComponent* ImportAreaLightComponent(FActorData& ActorData, IDatasmithAreaLightElement* AreaLightElement, USceneComponent* Parent);

	USceneComponent* CreateSceneComponent(FActorData& ActorData, UClass* Class, USceneComponent* Parent);

	template<typename T>
	T* CreateSceneComponent(FActorData& ActorData, USceneComponent* Parent)
	{
		return Cast<T>(CreateSceneComponent(ActorData, T::StaticClass(), Parent));
	}

	bool FSceneImporter::ProcessLightActorData(FActorData& ActorData, IDatasmithLightActorElement* LightActorElement)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FSceneImporter::ProcessLightActorData);

		if (ActorData.HasState(EAssetState::Processed))
		{
			return true;
		}

		if (LightActorElement->GetUseIes() && FCString::Strlen(LightActorElement->GetIesTexturePathName()) > 0)
		{
			if (FSceneGraphId* ElementIdPtr = AssetElementMapping.Find(TexturePrefix + LightActorElement->GetIesTexturePathName()))
			{
				FActionTaskFunction AssignTextureFunc = [this](UObject* Object, const FReferencer& Referencer) -> EActionResult::Type
				{
					return this->AssignProfileTexture(Referencer, Cast<UTextureLightProfile>(Object));
				};

				ProcessTextureData(*ElementIdPtr);

				AddToQueue(NONASYNC_QUEUE, { AssignTextureFunc, *ElementIdPtr, true, { EDataType::Actor, ActorData.ElementId, 0 } });
			}
		}

		FActionTaskFunction CreateLightFunc = [this](UObject* Object, const FReferencer& Referencer) -> EActionResult::Type
		{
			return this->CreateLightComponent(Referencer.GetId());
		};

		AddToQueue(NONASYNC_QUEUE, { CreateLightFunc, { EDataType::Actor, ActorData.ElementId, 0 } });
		TasksToComplete |= EWorkerTask::LightComponentCreate;

		ActorData.SetState(EAssetState::Processed);

		return true;
	}

	EActionResult::Type FSceneImporter::AssignProfileTexture(const FReferencer& Referencer, UTextureLightProfile* TextureProfile)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FSceneImporter::AssignProfileTexture);

		if (TextureProfile == nullptr)
		{
			ensure(Referencer.Type == EDataType::Actor);
			return EActionResult::Failed;
		}

		const FSceneGraphId ActorId = Referencer.GetId();
		ensure(ActorDataList.Contains(ActorId));

		FActorData& ActorData = ActorDataList[ActorId];

		if (!ActorData.HasState(EAssetState::Completed))
		{
			return EActionResult::Retry;
		}

		if (ULightComponent* LightComponent = ActorData.GetObject<ULightComponent>())
		{
			LightComponent->IESTexture = TextureProfile;
		}
		else if (UChildActorComponent* ChildActorComponent = ActorData.GetObject<UChildActorComponent>())
		{
			if (ADatasmithAreaLightActor* LightShapeActor = Cast< ADatasmithAreaLightActor >(ChildActorComponent->GetChildActor()))
			{
				LightShapeActor->IESTexture = TextureProfile;
			}
		}
		else
		{
			ensure(false);
			return EActionResult::Failed;
		}

		return EActionResult::Succeeded;
	}

	EActionResult::Type FSceneImporter::CreateLightComponent(FSceneGraphId ActorId)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FSceneImporter::CreateLightComponent);

		FActorData& ActorData = ActorDataList[ActorId];

		IDatasmithLightActorElement* LightElement = static_cast<IDatasmithLightActorElement*>(Elements[ActorId].Get());

		USceneComponent* LightComponent = nullptr;

		if ( LightElement->IsA(EDatasmithElementType::AreaLight) )
		{
			IDatasmithAreaLightElement* AreaLightElement = static_cast<IDatasmithAreaLightElement*>(LightElement);
			LightComponent = ImportAreaLightComponent( ActorData, AreaLightElement, RootComponent.Get() );
		}
		else if ( LightElement->IsA( EDatasmithElementType::LightmassPortal ) )
		{
			// #ue_datasmithruntime: What happens on update?
			LightComponent = CreateSceneComponent< ULightmassPortalComponent >(ActorData, RootComponent.Get());

			LightComponent->SetRelativeTransform(ActorData.WorldTransform);
		}
		else if ( LightElement->IsA( EDatasmithElementType::DirectionalLight ) )
		{
			UDirectionalLightComponent* DirectionalLightComponent = CreateSceneComponent< UDirectionalLightComponent >(ActorData, RootComponent.Get());

			SetupLightComponent( ActorData, DirectionalLightComponent, LightElement );

			LightComponent = DirectionalLightComponent;
		}
		else if ( LightElement->IsA( EDatasmithElementType::SpotLight ) || LightElement->IsA( EDatasmithElementType::PointLight ) )
		{

			if ( LightElement->IsA( EDatasmithElementType::SpotLight ) )
			{
				USpotLightComponent* SpotLightComponent = CreateSceneComponent< USpotLightComponent >(ActorData, RootComponent.Get());

				if (SpotLightComponent)
				{
					IDatasmithSpotLightElement* SpotLightElement = static_cast<IDatasmithSpotLightElement*>(LightElement);

					SpotLightComponent->InnerConeAngle = SpotLightElement->GetInnerConeAngle();
					SpotLightComponent->OuterConeAngle = SpotLightElement->GetOuterConeAngle();

					SetupLightComponent( ActorData, SpotLightComponent, LightElement );
				}

				LightComponent = SpotLightComponent;
			}
			else if ( LightElement->IsA( EDatasmithElementType::PointLight ) )
			{
				UPointLightComponent* PointLightComponent = CreateSceneComponent< UPointLightComponent >(ActorData, RootComponent.Get());

				if (PointLightComponent)
				{
					IDatasmithPointLightElement* PointLightElement = static_cast<IDatasmithPointLightElement*>(LightElement);

					switch ( PointLightElement->GetIntensityUnits() )
					{
					case EDatasmithLightUnits::Candelas:
						PointLightComponent->IntensityUnits = ELightUnits::Candelas;
						break;
					case EDatasmithLightUnits::Lumens:
						PointLightComponent->IntensityUnits = ELightUnits::Lumens;
						break;
					default:
						PointLightComponent->IntensityUnits = ELightUnits::Unitless;
						break;
					}

					if ( PointLightElement->GetSourceRadius() > 0.f )
					{
						PointLightComponent->SourceRadius = PointLightElement->GetSourceRadius();
					}

					if ( PointLightElement->GetSourceLength() > 0.f )
					{
						PointLightComponent->SourceLength = PointLightElement->GetSourceLength();
					}

					if ( PointLightElement->GetAttenuationRadius() > 0.f )
					{
						PointLightComponent->AttenuationRadius = PointLightElement->GetAttenuationRadius();
					}

					SetupLightComponent( ActorData, PointLightComponent, LightElement );
				}

				LightComponent = PointLightComponent;
			}
		}

		ActorData.AddState(EAssetState::Completed);

		return LightComponent ? EActionResult::Succeeded : EActionResult::Failed;
	}

	USceneComponent* CreateSceneComponent(FActorData& ActorData, UClass* Class, USceneComponent* Parent)
	{
		USceneComponent* SceneComponent = ActorData.GetObject<USceneComponent>();

		if (SceneComponent == nullptr)
		{
			SceneComponent = NewObject< USceneComponent >(Parent->GetOwner(), Class, NAME_None);
			if (SceneComponent == nullptr)
			{
				return nullptr;
			}

			SceneComponent->SetMobility(EComponentMobility::Movable);

			SceneComponent->AttachToComponent(Parent, FAttachmentTransformRules::KeepRelativeTransform);
			SceneComponent->RegisterComponentWithWorld(Parent->GetOwner()->GetWorld());

			ActorData.Object = TWeakObjectPtr<UObject>(SceneComponent);
		}

		if (SceneComponent->GetAttachParent() != Parent)
		{
			SceneComponent->Rename(nullptr, Parent->GetOwner(), REN_NonTransactional | REN_DontCreateRedirectors);
			SceneComponent->AttachToComponent(Parent, FAttachmentTransformRules::KeepRelativeTransform);
		}

		return SceneComponent;
	}

	EDatasmithAreaLightActorType GetLightActorTypeForLightType( const EDatasmithAreaLightType LightType )
	{
		EDatasmithAreaLightActorType LightActorType = EDatasmithAreaLightActorType::Point;

		switch ( LightType )
		{
		case EDatasmithAreaLightType::Spot:
			LightActorType = EDatasmithAreaLightActorType::Spot;
			break;

		case EDatasmithAreaLightType::Point:
			LightActorType = EDatasmithAreaLightActorType::Point;
			break;

		case EDatasmithAreaLightType::IES_DEPRECATED:
			LightActorType = EDatasmithAreaLightActorType::Point;
			break;

		case EDatasmithAreaLightType::Rect:
			LightActorType = EDatasmithAreaLightActorType::Rect;
			break;
		}

		return LightActorType;
	}

	USceneComponent* ImportAreaLightComponent( FActorData& ActorData, IDatasmithAreaLightElement* AreaLightElement, USceneComponent* Parent )
	{
		USceneComponent* SceneComponent = nullptr;

		FSoftObjectPath LightShapeBlueprintRef = FSoftObjectPath( TEXT("/DatasmithContent/Datasmith/DatasmithArealight.DatasmithArealight") );
		UBlueprint* LightShapeBlueprint = Cast< UBlueprint >( LightShapeBlueprintRef.TryLoad() );

		if ( LightShapeBlueprint )
		{
			UChildActorComponent* ChildActorComponent = ActorData.GetObject<UChildActorComponent>();

			if (ChildActorComponent == nullptr)
			{
				ChildActorComponent = CreateSceneComponent< UChildActorComponent >(ActorData, Parent);

				ChildActorComponent->SetChildActorClass( TSubclassOf< AActor > ( LightShapeBlueprint->GeneratedClass ) );
				ChildActorComponent->CreateChildActor();
			}

			ChildActorComponent->SetRelativeTransform(ActorData.WorldTransform);

			ADatasmithAreaLightActor* LightShapeActor = Cast< ADatasmithAreaLightActor >( ChildActorComponent->GetChildActor() );

			if ( LightShapeActor )
			{
#if WITH_EDITOR
				LightShapeActor->SetActorLabel( AreaLightElement->GetLabel() );
#endif

				LightShapeActor->UnregisterAllComponents( true );

				LightShapeActor->LightType = GetLightActorTypeForLightType( AreaLightElement->GetLightType() );
				LightShapeActor->LightShape = (EDatasmithAreaLightActorShape)AreaLightElement->GetLightShape();
				LightShapeActor->Dimensions = FVector2D( AreaLightElement->GetLength(), AreaLightElement->GetWidth() );
				LightShapeActor->Color = AreaLightElement->GetColor();
				LightShapeActor->Intensity = AreaLightElement->GetIntensity();
				LightShapeActor->IntensityUnits = (ELightUnits)AreaLightElement->GetIntensityUnits();

				if ( AreaLightElement->GetUseTemperature() )
				{
					LightShapeActor->Temperature = AreaLightElement->GetTemperature();
				}

				if ( AreaLightElement->GetUseIes() )
				{
					LightShapeActor->bUseIESBrightness = AreaLightElement->GetUseIesBrightness();
					LightShapeActor->IESBrightnessScale = AreaLightElement->GetIesBrightnessScale();
					LightShapeActor->Rotation = AreaLightElement->GetIesRotation().Rotator();
				}

				if ( AreaLightElement->GetSourceRadius() > 0.f )
				{
					LightShapeActor->SourceRadius = AreaLightElement->GetSourceRadius();
				}

				if ( AreaLightElement->GetSourceLength() > 0.f )
				{
					LightShapeActor->SourceLength = AreaLightElement->GetSourceLength();
				}

				if ( AreaLightElement->GetAttenuationRadius() > 0.f )
				{
					LightShapeActor->AttenuationRadius = AreaLightElement->GetAttenuationRadius();
				}

				LightShapeActor->RegisterAllComponents();

				LightShapeActor->RerunConstructionScripts();

				SceneComponent = ChildActorComponent;
			}
		}

		return SceneComponent;
	}

	void FSceneImporter::SetupLightComponent( FActorData& ActorData, ULightComponent* LightComponent, IDatasmithLightActorElement* LightElement )
	{
		if ( !LightComponent )
		{
			return;
		}

		LightComponent->SetVisibility(LightElement->IsEnabled());
		LightComponent->Intensity = LightElement->GetIntensity();
		LightComponent->CastShadows = true;
		LightComponent->LightColor = LightElement->GetColor().ToFColor( true );
		LightComponent->bUseTemperature = LightElement->GetUseTemperature();
		LightComponent->Temperature = LightElement->GetTemperature();

		// #ue_datasmithruntime: material function not supported yet
		//if ( LightElement->GetLightFunctionMaterial().IsValid() )
		//{
		//	FString BaseName = LightElement->GetLightFunctionMaterial()->GetName();
		//	FString MaterialName = FPaths::Combine( MaterialsFolderPath, BaseName + TEXT(".") + BaseName );
		//	UMaterialInterface* Material = Cast< UMaterialInterface >( FSoftObjectPath( *MaterialName ).TryLoad() );

		//	if ( Material )
		//	{
		//		LightComponent->LightFunctionMaterial = Material;
		//	}
		//}

		if (UPointLightComponent* PointLightComponent = Cast<UPointLightComponent>(LightComponent))
		{
			if ( LightElement->GetUseIes() ) // For IES lights that are not area lights, the IES rotation should be baked into the light transform
			{
				PointLightComponent->bUseIESBrightness = LightElement->GetUseIesBrightness();
				PointLightComponent->IESBrightnessScale = LightElement->GetIesBrightnessScale();

				LightElement->SetRotation( LightElement->GetRotation() * LightElement->GetIesRotation() );

				// Compute parent transform
				FTransform ParentTransform = ActorData.RelativeTransform.Inverse() * ActorData.WorldTransform;

				// Update relative transform
				ActorData.RelativeTransform = LightElement->GetRelativeTransform();

				// Update world transform
				ActorData.WorldTransform = ActorData.RelativeTransform * ParentTransform;
			}
		}

		LightComponent->UpdateColorAndBrightness();

		LightComponent->SetRelativeTransform(ActorData.WorldTransform);

		if (LightElement->GetTagsCount() > 0)
		{
			LightComponent->ComponentTags.Reserve(LightElement->GetTagsCount());
			for (int32 Index = 0; Index < LightElement->GetTagsCount(); ++Index)
			{
				LightComponent->ComponentTags.Add(LightElement->GetTag(Index));
			}
		}
	}
} // End of namespace DatasmithRuntime