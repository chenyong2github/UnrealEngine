// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetData.h"
#include "MaterialLayersFunctions.generated.h"

#define LOCTEXT_NAMESPACE "MaterialLayersFunctions"

class FArchive;


UENUM()
enum EMaterialParameterAssociation
{
	LayerParameter,
	BlendParameter,
	GlobalParameter,
};

USTRUCT(BlueprintType)
struct ENGINE_API FMaterialParameterInfo
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=ParameterInfo)
	FName Name;

	/** Whether this is a global parameter, or part of a layer or blend */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=ParameterInfo)
	TEnumAsByte<EMaterialParameterAssociation> Association;

	/** Layer or blend index this parameter is part of. INDEX_NONE for global parameters. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=ParameterInfo)
	int32 Index;

#if WITH_EDITORONLY_DATA
	UPROPERTY(Transient)
	FSoftObjectPath ParameterLocation;
#endif

	FMaterialParameterInfo(const TCHAR* InName, EMaterialParameterAssociation InAssociation = EMaterialParameterAssociation::GlobalParameter, int32 InIndex = INDEX_NONE)
		: Name(InName)
		, Association(InAssociation)
		, Index(InIndex)
	{
	}
	FMaterialParameterInfo(FName InName = FName(), EMaterialParameterAssociation InAssociation = EMaterialParameterAssociation::GlobalParameter, int32 InIndex = INDEX_NONE)
	: Name(InName)
	, Association(InAssociation)
	, Index(InIndex)
	{
	}

	FString ToString() const
	{
		return *Name.ToString() + FString::FromInt(Association) + FString::FromInt(Index);
	}

	friend FArchive& operator<<(FArchive& Ar, FMaterialParameterInfo& Ref)
	{
		Ar << Ref.Name << Ref.Association << Ref.Index;
		return Ar;
	}
};

struct FHashedMaterialParameterInfo
{
	DECLARE_TYPE_LAYOUT(FHashedMaterialParameterInfo, NonVirtual);
public:
	FHashedMaterialParameterInfo(const FHashedName& InName = FHashedName(), EMaterialParameterAssociation InAssociation = EMaterialParameterAssociation::GlobalParameter, int32 InIndex = INDEX_NONE)
		: Name(InName)
		, Index(InIndex)
		, Association(InAssociation)
	{}

	FHashedMaterialParameterInfo(const TCHAR* InName, EMaterialParameterAssociation InAssociation = EMaterialParameterAssociation::GlobalParameter, int32 InIndex = INDEX_NONE)
		: Name(InName)
		, Index(InIndex)
		, Association(InAssociation)
	{}

	FHashedMaterialParameterInfo(const FName& InName, EMaterialParameterAssociation InAssociation = EMaterialParameterAssociation::GlobalParameter, int32 InIndex = INDEX_NONE)
		: Name(InName)
		, Index(InIndex)
		, Association(InAssociation)
	{}

	FHashedMaterialParameterInfo(const FMaterialParameterInfo& Rhs)
		: Name(Rhs.Name)
		, Index(Rhs.Index)
		, Association(Rhs.Association)
	{}

	FHashedMaterialParameterInfo(const FHashedMaterialParameterInfo& Rhs) = default;

	friend FArchive& operator<<(FArchive& Ar, FHashedMaterialParameterInfo& Ref)
	{
		Ar << Ref.Name << Ref.Association << Ref.Index;
		return Ar;
	}

	LAYOUT_FIELD(FHashedName, Name);
	LAYOUT_FIELD(int32, Index);
	LAYOUT_FIELD(TEnumAsByte<EMaterialParameterAssociation>, Association);
};

// For sorting/searching
FORCEINLINE bool operator<(const FHashedMaterialParameterInfo& Lhs, const FHashedMaterialParameterInfo& Rhs)
{
	if (Lhs.Association != Rhs.Association) return Lhs.Association < Rhs.Association;
	else if (Lhs.Index != Rhs.Index) return Lhs.Index < Rhs.Index;
	return Lhs.Name < Rhs.Name;
}

FORCEINLINE bool operator==(const FMaterialParameterInfo& Lhs, const FMaterialParameterInfo& Rhs)
{
	return Lhs.Name.IsEqual(Rhs.Name) && Lhs.Association == Rhs.Association && Lhs.Index == Rhs.Index;
}

FORCEINLINE bool operator!=(const FMaterialParameterInfo& Lhs, const FMaterialParameterInfo& Rhs)
{
	return !operator==(Lhs, Rhs);
}

FORCEINLINE bool operator==(const FHashedMaterialParameterInfo& Lhs, const FHashedMaterialParameterInfo& Rhs)
{
	return Lhs.Name == Rhs.Name && Lhs.Association == Rhs.Association && Lhs.Index == Rhs.Index;
}

FORCEINLINE bool operator!=(const FHashedMaterialParameterInfo& Lhs, const FHashedMaterialParameterInfo& Rhs)
{
	return !operator==(Lhs, Rhs);
}

FORCEINLINE bool operator==(const FMaterialParameterInfo& Lhs, const FHashedMaterialParameterInfo& Rhs)
{
	return FHashedName(Lhs.Name) == Rhs.Name && Lhs.Index == Rhs.Index && Lhs.Association == Rhs.Association;
}

FORCEINLINE bool operator!=(const FMaterialParameterInfo& Lhs, const FHashedMaterialParameterInfo& Rhs)
{
	return !operator==(Lhs, Rhs);
}

USTRUCT()
struct ENGINE_API FMaterialLayersFunctions
{
	GENERATED_USTRUCT_BODY()

	/** Serializable ID structure for FMaterialLayersFunctions which allows us to deterministically recompile shaders*/
	struct ID
	{
		TArray<FGuid> LayerIDs;
		TArray<FGuid> BlendIDs;
		TArray<bool> LayerStates;

		bool operator==(const ID& Reference) const;

		void SerializeForDDC(FArchive& Ar);

		void UpdateHash(FSHA1& HashState) const;

		//TODO: Investigate whether this is really required given it is only used by FMaterialShaderMapId AND that one also uses UpdateHash
		void AppendKeyString(FString& KeyString) const;
	};

	static const FGuid UninitializedParentGuid;
	static const FGuid NoParentGuid;
	static const FGuid BackgroundGuid;
		
	FMaterialLayersFunctions()
	{
		// Default to a non-blended "background" layer
		Layers.AddDefaulted();
		LayerStates.Add(true);
#if WITH_EDITOR
		FText LayerName = FText(LOCTEXT("Background", "Background"));
		LayerNames.Add(LayerName);
		RestrictToLayerRelatives.Add(false);
		// Use a consistent Guid for the background layer
		// This layer never needs to resolve, so doesn't need to be unique
		// Default constructor assigning different guids will break FStructUtils::AttemptToFindUninitializedScriptStructMembers
		LayerGuids.Add(BackgroundGuid);
		ParentLayerGuids.Add(NoParentGuid);
#endif
	}

	void Empty()
	{
		Layers.Empty();
		Blends.Empty();
		LayerStates.Empty();
#if WITH_EDITOR
		LayerNames.Empty();
		RestrictToLayerRelatives.Empty();
		RestrictToBlendRelatives.Empty();
		LayerGuids.Empty();
		ParentLayerGuids.Empty();
#endif
	}

	UPROPERTY(EditAnywhere, Category=MaterialLayers)
	TArray<class UMaterialFunctionInterface*> Layers;

	UPROPERTY(EditAnywhere, Category=MaterialLayers)
	TArray<class UMaterialFunctionInterface*> Blends;

	UPROPERTY(EditAnywhere, Category = MaterialLayers)
	TArray<bool> LayerStates;

#if WITH_EDITORONLY_DATA
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
	 * Refers to the layer in the parent's LayerGuids list used to initialize this layer
	 * - Special value of 'NoParentGuid' means this layer was created in this material, not based on any layer in the parent
	 * - Special value of 'UninitializedParentGuid' means this data was serialized before these guids existed...layers with this value will attempt to match a parent layer with the same resources assigned
	 */
	UPROPERTY(EditAnywhere, Category = MaterialLayers)
	TArray<FGuid> ParentLayerGuids;

	/**
	 * List of Guids that exist in the parent material that have been explicitly deleted
	 * This is needed to distinguish these layers from newly added layers in the parent material
	 */
	UPROPERTY(EditAnywhere, Category = MaterialLayers)
	TArray<FGuid> DeletedParentLayerGuids;
#endif // WITH_EDITORONLY_DATA

	UPROPERTY()
	FString KeyString_DEPRECATED;

	void AppendBlendedLayer();

	void AddLayerCopy(const FMaterialLayersFunctions& Source, int32 SourceLayerIndex, const FGuid& ParentGuid);

	void InsertLayerCopy(const FMaterialLayersFunctions& Source, int32 SourceLayerIndex, const FGuid& ParentGuid, int32 LayerIndex);

	void RemoveBlendedLayerAt(int32 Index);

	void MoveBlendedLayer(int32 SrcLayerIndex, int32 DstLayerIndex);

#if WITH_EDITOR
	void UnlinkLayerFromParent(int32 Index);
	bool IsLayerLinkedToParent(int32 Index) const;
	void RelinkLayersToParent();
	bool HasAnyUnlinkedLayers() const;
#endif // WITH_EDITOR

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

#if WITH_EDITORONLY_DATA
	FText GetLayerName(int32 Counter) const
	{
		FText LayerName = FText::Format(LOCTEXT("LayerPrefix", "Layer {0}"), Counter);
		if (LayerNames.IsValidIndex(Counter))
		{
			LayerName = LayerNames[Counter];
		}
		return LayerName;
	}

	void CopyGuidsToParent();

	bool ResolveParent(const FMaterialLayersFunctions& Parent, TArray<int32>& OutRemapLayerIndices);

#endif // WITH_EDITORONLY_DATA

	const ID GetID() const;

	/** Lists referenced function packages in a string, intended for use as a static permutation identifier. */
	FString GetStaticPermutationString() const;

	void SerializeForDDC(FArchive& Ar);

	void PostSerialize(const FArchive& Ar);

	FORCEINLINE bool operator==(const FMaterialLayersFunctions& Other) const
	{
		if (Layers != Other.Layers || Blends != Other.Blends || LayerStates != Other.LayerStates)
		{
			return false;
		}
#if WITH_EDITORONLY_DATA
		if (ParentLayerGuids != Other.ParentLayerGuids || DeletedParentLayerGuids != Other.DeletedParentLayerGuids)
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