// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetData.h"
#include "MaterialTypes.h"
#include "MaterialLayersFunctions.generated.h"

#define LOCTEXT_NAMESPACE "MaterialLayersFunctions"

class FArchive;

UENUM()
enum class EMaterialLayerLinkState : uint8
{
	Uninitialized = 0u, // Saved with previous engine version
	LinkedToParent, // Layer should mirror changes from parent material
	UnlinkedFromParent, // Layer is based on parent material, but should not mirror changes
	NotFromParent, // Layer was created locally in this material, not in parent
};

USTRUCT()
struct ENGINE_API FMaterialLayersFunctions
{
	GENERATED_USTRUCT_BODY()

#if WITH_EDITOR
	/** Serializable ID structure for FMaterialLayersFunctions which allows us to deterministically recompile shaders*/
	struct ID
	{
		TArray<FGuid> LayerIDs;
		TArray<FGuid> BlendIDs;
		TArray<bool> LayerStates;

		bool operator==(const ID& Reference) const;
		inline bool operator!=(const ID& Reference) const { return !operator==(Reference); }

		void SerializeForDDC(FArchive& Ar);

		friend ID& operator<<(FArchive& Ar, ID& Ref)
		{
			Ref.SerializeForDDC(Ar);
			return Ref;
		}

		void UpdateHash(FSHA1& HashState) const;

		//TODO: Investigate whether this is really required given it is only used by FMaterialShaderMapId AND that one also uses UpdateHash
		void AppendKeyString(FString& KeyString) const;
	};
#endif // WITH_EDITOR

	static const FGuid BackgroundGuid;
		
	FMaterialLayersFunctions() = default;

	void Empty()
	{
		Layers.Empty();
		Blends.Empty();
#if WITH_EDITOR
		LayerStates.Empty();
		LayerNames.Empty();
		RestrictToLayerRelatives.Empty();
		RestrictToBlendRelatives.Empty();
		LayerGuids.Empty();
		LayerLinkStates.Empty();
#endif
	}

	UPROPERTY(EditAnywhere, Category=MaterialLayers)
	TArray<TObjectPtr<class UMaterialFunctionInterface>> Layers;

	UPROPERTY(EditAnywhere, Category=MaterialLayers)
	TArray<TObjectPtr<class UMaterialFunctionInterface>> Blends;

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, Category = MaterialLayers)
	TArray<bool> LayerStates;

	UPROPERTY(EditAnywhere, Category = MaterialLayers)
	TArray<FText> LayerNames;

	UPROPERTY(EditAnywhere, Category = MaterialLayers)
	TArray<bool> RestrictToLayerRelatives;

	UPROPERTY(EditAnywhere, Category = MaterialLayers)
	TArray<bool> RestrictToBlendRelatives;

	/** Guid that identifies each layer in this stack */
	UPROPERTY(EditAnywhere, Category = MaterialLayers)
	TArray<FGuid> LayerGuids;

	/**
	 * State of each layer's link to parent material
	 */
	UPROPERTY(EditAnywhere, Category = MaterialLayers)
	TArray<EMaterialLayerLinkState> LayerLinkStates;

	/**
	 * List of Guids that exist in the parent material that have been explicitly deleted
	 * This is needed to distinguish these layers from newly added layers in the parent material
	 */
	UPROPERTY(EditAnywhere, Category = MaterialLayers)
	TArray<FGuid> DeletedParentLayerGuids;
#endif // WITH_EDITORONLY_DATA

	inline bool IsEmpty() const { return Layers.Num() == 0; }

	void AddDefaultBackgroundLayer()
	{
		// Default to a non-blended "background" layer
		Layers.AddDefaulted();
#if WITH_EDITORONLY_DATA
		LayerStates.Add(true);
		FText LayerName = FText(LOCTEXT("Background", "Background"));
		LayerNames.Add(LayerName);
		RestrictToLayerRelatives.Add(false);
		// Use a consistent Guid for the background layer
		// Default constructor assigning different guids will break FStructUtils::AttemptToFindUninitializedScriptStructMembers
		LayerGuids.Add(BackgroundGuid);
		LayerLinkStates.Add(EMaterialLayerLinkState::NotFromParent);
#endif // WITH_EDITORONLY_DATA
	}

	int32 AppendBlendedLayer();

	int32 AddLayerCopy(const FMaterialLayersFunctions& Source, int32 SourceLayerIndex, bool bVisible, EMaterialLayerLinkState LinkState);

	void InsertLayerCopy(const FMaterialLayersFunctions& Source, int32 SourceLayerIndex, EMaterialLayerLinkState LinkState, int32 LayerIndex);

	void RemoveBlendedLayerAt(int32 Index);

	void MoveBlendedLayer(int32 SrcLayerIndex, int32 DstLayerIndex);

#if WITH_EDITOR
	const ID GetID() const;

	/** Gets a string representation of the ID */
	FString GetStaticPermutationString() const;

	void UnlinkLayerFromParent(int32 Index);
	bool IsLayerLinkedToParent(int32 Index) const;
	void RelinkLayersToParent();
	bool HasAnyUnlinkedLayers() const;

	void ToggleBlendedLayerVisibility(int32 Index)
	{
		check(LayerStates.IsValidIndex(Index));
		LayerStates[Index] = !LayerStates[Index];
	}

	void SetBlendedLayerVisibility(int32 Index, bool InNewVisibility)
	{
		check(LayerStates.IsValidIndex(Index));
		LayerStates[Index] = InNewVisibility;
	}

	bool GetLayerVisibility(int32 Index) const
	{
		check(LayerStates.IsValidIndex(Index));
		return LayerStates[Index];
	}

	FText GetLayerName(int32 Counter) const
	{
		FText LayerName = FText::Format(LOCTEXT("LayerPrefix", "Layer {0}"), Counter);
		if (LayerNames.IsValidIndex(Counter))
		{
			LayerName = LayerNames[Counter];
		}
		return LayerName;
	}

	bool MatchesParent(const FMaterialLayersFunctions& Parent) const;

	void LinkAllLayersToParent();

	bool ResolveParent(const FMaterialLayersFunctions& Parent, TArray<int32>& OutRemapLayerIndices);

	void SerializeLegacy(FArchive& Ar);
#endif // WITH_EDITOR

	void PostSerialize(const FArchive& Ar);

	FORCEINLINE bool operator==(const FMaterialLayersFunctions& Other) const
	{
		if (Layers != Other.Layers || Blends != Other.Blends)
		{
			return false;
		}
#if WITH_EDITORONLY_DATA
		if (LayerStates != Other.LayerStates || LayerLinkStates != Other.LayerLinkStates || DeletedParentLayerGuids != Other.DeletedParentLayerGuids)
		{
			return false;
		}
#endif // WITH_EDITORONLY_DATA
		return true;
	}

	FORCEINLINE bool operator!=(const FMaterialLayersFunctions& Other) const
	{
		return !operator==(Other);
	}
};

template<>
struct TStructOpsTypeTraits<FMaterialLayersFunctions> : TStructOpsTypeTraitsBase2<FMaterialLayersFunctions>
{
	enum { WithPostSerialize = true };
};

#undef LOCTEXT_NAMESPACE