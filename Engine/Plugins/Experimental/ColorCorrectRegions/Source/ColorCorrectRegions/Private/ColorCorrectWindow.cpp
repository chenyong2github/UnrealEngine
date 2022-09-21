// Copyright Epic Games, Inc. All Rights Reserved.

#include "ColorCorrectWindow.h"
#include "Components/BillboardComponent.h"
#include "CoreMinimal.h"
#include "Materials/Material.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "UObject/ConstructorHelpers.h"

ENUM_RANGE_BY_COUNT(EColorCorrectWindowType, EColorCorrectWindowType::MAX)

AColorCorrectWindow::AColorCorrectWindow(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, WindowType(EColorCorrectWindowType::Square)
{
	PositionalParams.DistanceFromCenter = 300.f;
	PositionalParams.Longitude = 0.f;
	PositionalParams.Latitude = 30.f;
	PositionalParams.Spin = 0.f;
	PositionalParams.Pitch = 0.f;
	PositionalParams.Yaw = 0.5;
	PositionalParams.RadialOffset = -0.5f;
	PositionalParams.Scale = FVector2D(1.f);
	
	UMaterial* Material = LoadObject<UMaterial>(NULL, TEXT("/ColorCorrectRegions/Materials/M_ColorCorrectRegionTransparentPreview.M_ColorCorrectRegionTransparentPreview"), NULL, LOAD_None, NULL);
	const TArray<UStaticMesh*> StaticMeshes =
	{
		Cast<UStaticMesh>(StaticLoadObject(UStaticMesh::StaticClass(), NULL, TEXT("/Engine/BasicShapes/Plane"))),
		Cast<UStaticMesh>(StaticLoadObject(UStaticMesh::StaticClass(), NULL, TEXT("/Engine/BasicShapes/Cylinder")))
	};
	for (EColorCorrectWindowType CCWType : TEnumRange<EColorCorrectWindowType>())
	{
		UStaticMeshComponent* MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(*UEnum::GetValueAsString(CCWType));
		MeshComponents.Add(MeshComponent);
		MeshComponent->SetupAttachment(RootComponent);
		MeshComponent->SetStaticMesh(StaticMeshes[static_cast<uint8>(CCWType)]);
		MeshComponent->SetWorldScale3D(FVector(1., 1., 0.001));
		MeshComponent->SetMaterial(0, Material);
		MeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		MeshComponent->SetCollisionProfileName(TEXT("OverlapAll"));
	}
	SetMeshVisibilityForWindowType();

#if WITH_METADATA
	CreateIcon();
#endif
}

#if WITH_EDITOR
void AColorCorrectWindow::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	const FName PropertyName = PropertyChangedEvent.GetPropertyName();

	if (PropertyName == GET_MEMBER_NAME_CHECKED(AColorCorrectWindow, WindowType))
	{
		SetMeshVisibilityForWindowType();
	}
	else
	{
		const FStructProperty* StructProperty = CastField<FStructProperty>(PropertyChangedEvent.MemberProperty);
		const bool bIsOrientation = StructProperty ? StructProperty->Struct == FDisplayClusterPositionalParams::StaticStruct() : false;
	
		if (bIsOrientation)
		{
			UpdateStageActorTransform();
			// Updates MU in real-time. Skip our method as the positional coordinates are already correct.
			AActor::PostEditMove(PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive);
		}
		else if (
			PropertyName == USceneComponent::GetRelativeLocationPropertyName() ||
			PropertyName == USceneComponent::GetRelativeRotationPropertyName() ||
			PropertyName == USceneComponent::GetRelativeScale3DPropertyName())
		{
			bNotifyOnParamSetter = false;
			UpdatePositionalParamsFromTransform();
			bNotifyOnParamSetter = true;
		}
	}
}

void AColorCorrectWindow::PostEditMove(bool bFinished)
{
	Super::PostEditMove(bFinished);

	bNotifyOnParamSetter = false;
	UpdatePositionalParamsFromTransform();
	bNotifyOnParamSetter = true;
}
#endif //WITH_EDITOR


#if WITH_METADATA
void AColorCorrectWindow::CreateIcon()
{
	// Create billboard component
	if (GIsEditor && !IsRunningCommandlet())
	{
		// Structure to hold one-time initialization

		struct FConstructorStatics
		{
			ConstructorHelpers::FObjectFinderOptional<UTexture2D> SpriteTextureObject;
			FName ID_ColorCorrectRegion;
			FText NAME_ColorCorrectRegion;

			FConstructorStatics()
				: SpriteTextureObject(TEXT("/ColorCorrectRegions/Icons/S_ColorCorrectWindowIcon"))
				, ID_ColorCorrectRegion(TEXT("Color Correct Window"))
				, NAME_ColorCorrectRegion(NSLOCTEXT("SpriteCategory", "ColorCorrectWindow", "Color Correct Window"))
			{
			}
		};

		static FConstructorStatics ConstructorStatics;

		SpriteComponent = CreateEditorOnlyDefaultSubobject<UBillboardComponent>(TEXT("Color Correct Window Icon"));

		if (SpriteComponent)
		{
			SpriteComponent->Sprite = ConstructorStatics.SpriteTextureObject.Get();
			SpriteComponent->SpriteInfo.Category = ConstructorStatics.ID_ColorCorrectRegion;
			SpriteComponent->SpriteInfo.DisplayName = ConstructorStatics.NAME_ColorCorrectRegion;
			SpriteComponent->SetIsVisualizationComponent(true);
			SpriteComponent->SetRelativeLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);
			SpriteComponent->SetMobility(EComponentMobility::Movable);
			SpriteComponent->bHiddenInGame = true;
			SpriteComponent->bIsScreenSizeScaled = true;

			SpriteComponent->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepRelativeTransform);
		}
	}

}
#endif 

void AColorCorrectWindow::SetMeshVisibilityForWindowType()
{
	for (EColorCorrectWindowType CCWType : TEnumRange<EColorCorrectWindowType>())
	{
		uint8 TypeIndex = static_cast<uint8>(CCWType);

		if (CCWType == WindowType)
		{
			MeshComponents[TypeIndex]->SetVisibility(true, true);
		}
		else
		{
			MeshComponents[TypeIndex]->SetVisibility(false, true);
		}
	}
}

#define NOTIFY_PARAM_SETTER()\
	if (bNotifyOnParamSetter)\
	{\
		UpdateStageActorTransform();\
	}\

void AColorCorrectWindow::SetLongitude(double InValue)
{
	PositionalParams.Longitude = InValue;
	NOTIFY_PARAM_SETTER()
}

double AColorCorrectWindow::GetLongitude() const
{
	return PositionalParams.Longitude;
}

void AColorCorrectWindow::SetLatitude(double InValue)
{
	PositionalParams.Latitude = InValue;
	NOTIFY_PARAM_SETTER()
}

double AColorCorrectWindow::GetLatitude() const
{
	return PositionalParams.Latitude;
}

void AColorCorrectWindow::SetDistanceFromCenter(double InValue)
{
	PositionalParams.DistanceFromCenter = InValue;
	NOTIFY_PARAM_SETTER()
}

double AColorCorrectWindow::GetDistanceFromCenter() const
{
	return PositionalParams.DistanceFromCenter;
}

void AColorCorrectWindow::SetSpin(double InValue)
{
	PositionalParams.Spin = InValue;
	NOTIFY_PARAM_SETTER()
}

double AColorCorrectWindow::GetSpin() const
{
	return PositionalParams.Spin;
}

void AColorCorrectWindow::SetPitch(double InValue)
{
	PositionalParams.Pitch = InValue;
	NOTIFY_PARAM_SETTER()
}

double AColorCorrectWindow::GetPitch() const
{
	return PositionalParams.Pitch;
}

void AColorCorrectWindow::SetYaw(double InValue)
{
	PositionalParams.Yaw = InValue;
	NOTIFY_PARAM_SETTER()
}

double AColorCorrectWindow::GetYaw() const
{
	return PositionalParams.Yaw;
}

void AColorCorrectWindow::SetRadialOffset(double InValue)
{
	PositionalParams.RadialOffset = InValue;
	NOTIFY_PARAM_SETTER()
}

double AColorCorrectWindow::GetRadialOffset() const
{
	return PositionalParams.RadialOffset;
}

void AColorCorrectWindow::SetScale(const FVector2D& InScale)
{
	PositionalParams.Scale = InScale;
	NOTIFY_PARAM_SETTER()
}

FVector2D AColorCorrectWindow::GetScale() const
{
	return PositionalParams.Scale;
}

void AColorCorrectWindow::SetOrigin(const FTransform& InOrigin)
{
	Origin = InOrigin;
	NOTIFY_PARAM_SETTER()
}

FTransform AColorCorrectWindow::GetOrigin() const
{
	return Origin;
}

void AColorCorrectWindow::SetPositionalParams(const FDisplayClusterPositionalParams& InParams)
{
	PositionalParams = InParams;
	NOTIFY_PARAM_SETTER()
}

FDisplayClusterPositionalParams AColorCorrectWindow::GetPositionalParams() const
{
	return PositionalParams;
}

#undef NOTIFY_PARAM_SETTER
