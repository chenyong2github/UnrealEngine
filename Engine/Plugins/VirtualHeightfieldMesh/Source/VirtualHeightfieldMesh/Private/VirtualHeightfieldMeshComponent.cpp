// Copyright Epic Games, Inc. All Rights Reserved.

#include "VirtualHeightfieldMeshComponent.h"

#include "Engine/World.h"
#include "VirtualHeightfieldMeshSceneProxy.h"
#include "VT/RuntimeVirtualTexture.h"

UVirtualHeightfieldMeshComponent::UVirtualHeightfieldMeshComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bNeverDistanceCull = true;
#if WITH_EDITORONLY_DATA
	bEnableAutoLODGeneration = false;
#endif // WITH_EDITORONLY_DATA

	Mobility = EComponentMobility::Static;
}

bool UVirtualHeightfieldMeshComponent::IsVisible() const
{
	return
		Super::IsVisible() &&
		RuntimeVirtualTexture != nullptr &&
		RuntimeVirtualTexture->GetMaterialType() == ERuntimeVirtualTextureMaterialType::WorldHeight &&
		UseVirtualTexturing(GetScene() ? GetScene()->GetFeatureLevel() : ERHIFeatureLevel::SM5);
}

FPrimitiveSceneProxy* UVirtualHeightfieldMeshComponent::CreateSceneProxy()
{
	return new FVirtualHeightfieldMeshSceneProxy(this);
}

FBoxSphereBounds UVirtualHeightfieldMeshComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	return FBoxSphereBounds(FBox(FVector(0.f, 0.f, 0.f), FVector(1.f, 1.f, 1.f))).TransformBy(LocalToWorld);
}

void UVirtualHeightfieldMeshComponent::GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials) const
{
	if (Material != nullptr)
	{
		OutMaterials.Add(Material);
	}
}

#if WITH_EDITOR

void UVirtualHeightfieldMeshComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	static FName MinMaxTextureName = GET_MEMBER_NAME_CHECKED(UVirtualHeightfieldMeshComponent, MinMaxTexture);
	static FName NumOcclusionLodsName = GET_MEMBER_NAME_CHECKED(UVirtualHeightfieldMeshComponent, NumOcclusionLods);
	if (PropertyChangedEvent.Property && (PropertyChangedEvent.Property->GetFName() == MinMaxTextureName || PropertyChangedEvent.Property->GetFName() == NumOcclusionLodsName))
	{
		BuildOcclusionData();
	}
}

void UVirtualHeightfieldMeshComponent::BuildOcclusionData()
{
	NumBuiltOcclusionLods = 0;
	BuiltOcclusionData.Reset();
	
	if (MinMaxTexture != nullptr && NumOcclusionLods > 0)
	{
		if (MinMaxTexture->Source.IsValid() && MinMaxTexture->Source.GetFormat() == TSF_BGRA8)
		{
			const int32 NumMinMaxTextureMips = MinMaxTexture->Source.GetNumMips();

			// Clamp NumBuiltOcclusionLods to give a maximum 341 occlusion volumes.
			NumBuiltOcclusionLods = FMath::Min(NumOcclusionLods, 5);
			NumBuiltOcclusionLods = FMath::Min(NumBuiltOcclusionLods, NumMinMaxTextureMips);

			// Reserve the expected entries assuming square mips.
			const int32 NumEntries = ((1 << (2 * NumBuiltOcclusionLods)) - 1) / 3;
			BuiltOcclusionData.Reserve(NumEntries);
		
			// Iterate the MinMaxTexture mips and extract min/max values to store in a flat array.
			const int32 BaseMipIndex = NumMinMaxTextureMips - NumBuiltOcclusionLods;
			for (int32 MipIndex = BaseMipIndex; MipIndex < NumMinMaxTextureMips; ++MipIndex)
			{
				TArray64<uint8> MipData;
				if (MinMaxTexture->Source.GetMipData(MipData, MipIndex))
				{
					for (int32 Index = 0; Index < MipData.Num(); Index += 4)
					{
						float Min = (float)(MipData[Index + 2] * 256 + MipData[Index + 3]) / 65535.f;
						float Max = (float)(MipData[Index + 0] * 256 + MipData[Index + 1]) / 65535.f;
						BuiltOcclusionData.Add(FVector2D(Min, Max));
					}
				}
			}

			// Check assumption of square mips, and disable occlusion if not true.
			ensure(NumEntries == BuiltOcclusionData.Num());
			if (NumEntries != BuiltOcclusionData.Num())
			{
				NumBuiltOcclusionLods = 0;
				BuiltOcclusionData.Reset();
			}
		}
	}
}

#endif
