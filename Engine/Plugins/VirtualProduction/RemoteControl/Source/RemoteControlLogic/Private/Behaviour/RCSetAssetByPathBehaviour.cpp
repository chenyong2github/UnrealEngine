// Copyright Epic Games, Inc. All Rights Reserved.

#include "Behaviour/RCSetAssetByPathBehaviour.h"

#include "Backends/CborStructDeserializerBackend.h"
#include "Backends/CborStructSerializerBackend.h"
#include "Components/MeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Controller/RCController.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "IRemoteControlModule.h"
#include "IRemoteControlPropertyHandle.h"
#include "Materials/MaterialInstance.h"
#include "RCVirtualProperty.h"
#include "RCVirtualPropertyContainer.h"
#include "RemoteControlPreset.h"
#include "StructSerializer.h"

URCSetAssetByPathBehaviour::URCSetAssetByPathBehaviour()
{
	PropertyInContainer = CreateDefaultSubobject<URCVirtualPropertyContainerBase>(FName("VirtualPropertyInContainer"));
}

void URCSetAssetByPathBehaviour::Initialize()
{
	PropertyInContainer->AddProperty(SetAssetByPathBehaviourHelpers::TargetProperty, URCController::StaticClass(), EPropertyBagPropertyType::String);
	PropertyInContainer->AddProperty(SetAssetByPathBehaviourHelpers::DefaultProperty, URCController::StaticClass(), EPropertyBagPropertyType::String);
	
	Super::Initialize();
}

bool URCSetAssetByPathBehaviour::SetAssetByPath(const FString& AssetPath, const FString& TargetPropertyString, const FString& DefaultString)
{
	URCController* Controller = ControllerWeakPtr.Get();
	
	if (AssetPath.IsEmpty() || !Controller)
	{
		return false;
	}
	
	FString ControllerString;
	if (!Controller->GetValueString(ControllerString))
	{
		return false;
	}
	
	FSoftObjectPath MainObjectRef(AssetPath + ControllerString);
	
	if (UObject* Object = MainObjectRef.TryLoad())
	{
		return SetAsset(Object, TargetPropertyString);
	}
	
	if (!DefaultString.IsEmpty())
	{
		FSoftObjectPath DefaultObjectRef(AssetPath + DefaultString);
		
		return SetAsset(DefaultObjectRef.TryLoad(), TargetPropertyString);
	}
	
	return false;
}

bool URCSetAssetByPathBehaviour::SetAsset(UObject* SetterObject, const FString& PropertyString)
{
	URCController* Controller = ControllerWeakPtr.Get();
	if (!SetterObject || PropertyString.IsEmpty() || !Controller)
	{
		return false;
	}
	
	const FName TargetPropertyLabel = FName(*PropertyString);
	TWeakObjectPtr<URemoteControlPreset> PresetPtr = Controller->PresetWeakPtr;
	TArray<TWeakPtr<FRemoteControlProperty>> ExposedProperties = PresetPtr->GetExposedEntities<FRemoteControlProperty>();
	TArray<TWeakPtr<FRemoteControlEntity>> ExposedEntities = PresetPtr->GetExposedEntities<FRemoteControlEntity>();
	
	TWeakPtr<FRemoteControlEntity>* ExposedEntityPtr = ExposedEntities.FindByPredicate([TargetPropertyLabel](const TWeakPtr<FRemoteControlEntity>& Entity) { return Entity.Pin()->GetLabel() == TargetPropertyLabel;});
	TWeakPtr<FRemoteControlProperty>* ExposedPropertyPtr = ExposedProperties.FindByPredicate([TargetPropertyLabel](const TWeakPtr<FRemoteControlProperty>& Property){ return Property.Pin()->GetLabel() == TargetPropertyLabel; });
	
	if (ExposedPropertyPtr)
	{
		TWeakPtr<FRemoteControlProperty> ExposedProperty = *ExposedPropertyPtr;
		if (AssetClass == UStaticMesh::StaticClass() && SetterObject->IsA(UStaticMesh::StaticClass()))
		{
			UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(ExposedProperty.Pin()->GetBoundObject());
			StaticMeshComponent->SetStaticMesh(Cast<UStaticMesh>(SetterObject));
			
			return true;
		}
		
		if (AssetClass == UMaterial::StaticClass() && SetterObject->IsA(UMaterial::StaticClass()))
		{
			if (UMeshComponent* MaterialOwnerComponent = Cast<UMeshComponent>(ExposedProperty.Pin()->GetBoundObject()))
			{
				MaterialOwnerComponent->SetMaterial(0, Cast<UMaterial>(SetterObject));
			}
			
			return true;
		}
		
		if (GetSupportedClasses().Contains(AssetClass) && SetterObject->IsA(AssetClass))
		{
			FRCObjectReference ObjectRef;
			TSharedPtr<FRemoteControlProperty> RemoteControlProperty = ExposedProperty.Pin();
			ObjectRef.Property = RemoteControlProperty->GetProperty();
			ObjectRef.Access = ExposedProperty.Pin()->GetPropertyHandle()->ShouldGenerateTransaction() ? ERCAccess::WRITE_TRANSACTION_ACCESS : ERCAccess::WRITE_ACCESS;
			ObjectRef.PropertyPathInfo = RemoteControlProperty->FieldPathInfo.ToString();
		
			for (UObject* Object : RemoteControlProperty->GetBoundObjects())
			{
				if (IRemoteControlModule::Get().ResolveObjectProperty(ObjectRef.Access, Object, ObjectRef.PropertyPathInfo, ObjectRef))
				{
					/**
					 * Setting with Pointer and using Serialization of itself afterwards as a workaround with the Texture Asset not updating in the world.
					 */
					FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(ExposedProperty.Pin()->GetProperty());
					if (!ObjectProperty)
					{
						return false;
					}

					FProperty* Property = RemoteControlProperty->GetProperty();
					uint8* ValueAddress = Property->ContainerPtrToValuePtr<uint8>(ObjectRef.ContainerAdress);
					ObjectProperty->SetObjectPropertyValue(ValueAddress, SetterObject);
					
					TArray<uint8> Buffer;
					FMemoryReader Reader(Buffer);
					FCborStructDeserializerBackend DeserializerBackend(Reader);

					IRemoteControlModule::Get().SetObjectProperties(ObjectRef, DeserializerBackend, ERCPayloadType::Cbor, Buffer);
					
					return true;
				}
			}
		}
		
		return false;
	}
	
	if (ExposedEntityPtr)
	{
		TWeakPtr<FRemoteControlEntity> ExposedEntity = *ExposedEntityPtr;
		if (AssetClass == UBlueprint::StaticClass() && SetterObject->IsA(UBlueprint::StaticClass()))
		{

			AActor* OldActor = Cast<AActor>(ExposedEntity.Pin()->GetBoundObject());
			UWorld* World = OldActor->GetWorld();
			
			OldActor->UnregisterAllComponents();
			
			FVector OldLocation = OldActor->GetActorLocation();
			FRotator OldRotation = OldActor->GetActorRotation();
			const FName OldActorName = OldActor->GetFName();
			
			FActorSpawnParameters SpawnParams;
			SpawnParams.Name = OldActorName;
			
			const FName OldActorReplacedNamed = MakeUniqueObjectName(OldActor->GetOuter(), OldActor->GetClass(), *FString::Printf(TEXT("%s_REPLACED"), *OldActor->GetFName().ToString()));
			OldActor->Rename(*OldActorReplacedNamed.ToString(), OldActor->GetOuter(), REN_DoNotDirty | REN_DontCreateRedirectors | REN_ForceNoResetLoaders);

			if (AActor* NewActor = World->SpawnActor(Cast<UBlueprint>(SetterObject)->GeneratedClass, &OldLocation, &OldRotation, SpawnParams))
			{
				World->DestroyActor(OldActor);
				
				return true;
			}

			OldActor->UObject::Rename(*OldActorName.ToString(), OldActor->GetOuter(), REN_DoNotDirty | REN_DontCreateRedirectors | REN_ForceNoResetLoaders);
			OldActor->RegisterAllComponents();
		}
	}
	
	return false;
}

TArray<UClass*> URCSetAssetByPathBehaviour::GetSupportedClasses() const
{
	static TArray<UClass*> SupportedClasses =
	{
		UStaticMesh::StaticClass(),
		UMaterial::StaticClass(),
		UTexture::StaticClass(),
		UBlueprint::StaticClass(),
	};

	return SupportedClasses;
}
