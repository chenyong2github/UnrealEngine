// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NameTypes.h"
#include "Misc/Guid.h"
#include "UObject/FortniteMainBranchObjectVersion.h"
#include "UObject/RenderingObjectVersion.h"
#include "UObject/ReleaseObjectVersion.h"
#include "Materials/MaterialLayersFunctions.h"
#include "StaticParameterSet.generated.h"

class FSHA1;

/**
Base parameter properties
*/
USTRUCT()
struct FStaticParameterBase
{
	GENERATED_USTRUCT_BODY();

	UPROPERTY()
		FMaterialParameterInfo ParameterInfo;

	UPROPERTY()
		bool bOverride;

	UPROPERTY()
		FGuid ExpressionGUID;

	FStaticParameterBase() :
		bOverride(false),
		ExpressionGUID(0, 0, 0, 0)
	{ }

	FStaticParameterBase(const FMaterialParameterInfo& InInfo, bool InOverride, FGuid InGuid) :
		ParameterInfo(InInfo),
		bOverride(InOverride),
		ExpressionGUID(InGuid)
	{ }

	friend FArchive& operator<<(FArchive& Ar, FStaticParameterBase& P)
	{
		// This method should never be called, derived structures need to implement their own code (to retain compatibility) or call SerializeBase (for new classes)
		check(false);

		return Ar;
	}

	bool operator==(const FStaticParameterBase& Reference) const
	{
		return ParameterInfo == Reference.ParameterInfo && bOverride == Reference.bOverride && ExpressionGUID == Reference.ExpressionGUID;
	}

	void SerializeBase(FArchive& Ar)
	{
		Ar << ParameterInfo;
		Ar << bOverride;
		Ar << ExpressionGUID;
	}

	void UpdateHash(FSHA1& HashState) const
	{
		const FString ParameterName = ParameterInfo.ToString();
		HashState.Update((const uint8*)*ParameterName, ParameterName.Len() * sizeof(TCHAR));
		HashState.Update((const uint8*)&ExpressionGUID, sizeof(ExpressionGUID));
		uint8 Override = bOverride;
		HashState.Update((const uint8*)&Override, sizeof(Override));
	}

	void AppendKeyString(FString& KeyString) const
	{
		KeyString += ParameterInfo.ToString() + FString::FromInt(bOverride) + ExpressionGUID.ToString();
	}
};


/**
* Holds the information for a static switch parameter
*/
USTRUCT()
struct FStaticSwitchParameter : public FStaticParameterBase
{
	GENERATED_USTRUCT_BODY();

	UPROPERTY()
	bool Value;

	FStaticSwitchParameter() :
		FStaticParameterBase(),
		Value(false)
	{ }

	FStaticSwitchParameter(const FMaterialParameterInfo& InInfo, bool InValue, bool InOverride, FGuid InGuid) :
		FStaticParameterBase(InInfo, InOverride, InGuid),
		Value(InValue)
	{ }

	bool operator==(const FStaticSwitchParameter& Reference) const
	{
		return FStaticParameterBase::operator==(Reference) && Value == Reference.Value;
	}

	friend FArchive& operator<<(FArchive& Ar, FStaticSwitchParameter& P)
	{
		Ar.UsingCustomVersion(FRenderingObjectVersion::GUID);
		if (Ar.CustomVer(FRenderingObjectVersion::GUID) < FRenderingObjectVersion::MaterialAttributeLayerParameters)
		{
			Ar << P.ParameterInfo.Name;
		}
		else
		{
			Ar << P.ParameterInfo;
		}
		Ar << P.Value << P.bOverride << P.ExpressionGUID;
		return Ar;
	}

	void UpdateHash(FSHA1& HashState) const
	{
		FStaticParameterBase::UpdateHash(HashState);
		uint8 HashValue = Value;
		HashState.Update((const uint8*)&HashValue, sizeof(HashValue));
	}

	void AppendKeyString(FString& KeyString) const
	{
		FStaticParameterBase::AppendKeyString(KeyString);
		KeyString += FString::FromInt(Value);
	}
};

/**
* Holds the information for a static component mask parameter
*/
USTRUCT()
struct FStaticComponentMaskParameter : public FStaticParameterBase
{
	GENERATED_USTRUCT_BODY();

	UPROPERTY()
	bool R;
	
	UPROPERTY()
	bool G;

	UPROPERTY()
	bool B;

	UPROPERTY()
	bool A; 

	FStaticComponentMaskParameter() :
		FStaticParameterBase(),
		R(false),
		G(false),
		B(false),
		A(false)
	{ }

	FStaticComponentMaskParameter(const FMaterialParameterInfo& InInfo, bool InR, bool InG, bool InB, bool InA, bool InOverride, FGuid InGuid) :
		FStaticParameterBase(InInfo, InOverride, InGuid),
		R(InR),
		G(InG),
		B(InB),
		A(InA)
	{ }

	bool operator==(const FStaticComponentMaskParameter& Reference) const
	{
		return FStaticParameterBase::operator==(Reference) && R == Reference.R && G == Reference.G && B == Reference.B && A == Reference.A;
	}

	friend FArchive& operator<<(FArchive& Ar, FStaticComponentMaskParameter& P)
	{
		Ar.UsingCustomVersion(FRenderingObjectVersion::GUID);
		if (Ar.CustomVer(FRenderingObjectVersion::GUID) < FRenderingObjectVersion::MaterialAttributeLayerParameters)
		{
			Ar << P.ParameterInfo.Name;
		}
		else
		{
			Ar << P.ParameterInfo;
		}
		Ar << P.R << P.G << P.B << P.A << P.bOverride << P.ExpressionGUID;
		return Ar;
	}

	void UpdateHash(FSHA1& HashState) const
	{
		FStaticParameterBase::UpdateHash(HashState);
		uint8 Values[4];
		Values[0] = R;
		Values[1] = G;
		Values[2] = B;
		Values[3] = A;
		HashState.Update((const uint8*)&Values, sizeof(Values));
	}

	void AppendKeyString(FString& KeyString) const
	{
		FStaticParameterBase::AppendKeyString(KeyString);
		KeyString += FString::FromInt(R);
		KeyString += FString::FromInt(G);
		KeyString += FString::FromInt(B);
		KeyString += FString::FromInt(A);
	}
};

/**
* Holds the information for a static switch parameter
*/
USTRUCT()
struct FStaticTerrainLayerWeightParameter : public FStaticParameterBase
{
	GENERATED_USTRUCT_BODY();

	UPROPERTY()
	int32 WeightmapIndex;

	UPROPERTY()
	bool bWeightBasedBlend;

	FStaticTerrainLayerWeightParameter() :
		FStaticParameterBase(),
		WeightmapIndex(INDEX_NONE),
		bWeightBasedBlend(true)
	{ }

	FStaticTerrainLayerWeightParameter(const FMaterialParameterInfo& InInfo, int32 InWeightmapIndex, bool InOverride, FGuid InGuid, bool InWeightBasedBlend) :
		FStaticParameterBase(InInfo, InOverride, InGuid),
		WeightmapIndex(InWeightmapIndex),
		bWeightBasedBlend(InWeightBasedBlend)
	{ }

	bool operator==(const FStaticTerrainLayerWeightParameter& Reference) const
	{
		return FStaticParameterBase::operator==(Reference) && WeightmapIndex == Reference.WeightmapIndex && bWeightBasedBlend == Reference.bWeightBasedBlend;
	}

	friend FArchive& operator<<(FArchive& Ar, FStaticTerrainLayerWeightParameter& P)
	{
		Ar.UsingCustomVersion(FRenderingObjectVersion::GUID);
		if (Ar.CustomVer(FRenderingObjectVersion::GUID) < FRenderingObjectVersion::MaterialAttributeLayerParameters)
		{
			Ar << P.ParameterInfo.Name;
		}
		else
		{
			Ar << P.ParameterInfo;
		}
		Ar.UsingCustomVersion(FFortniteMainBranchObjectVersion::GUID);
		if (Ar.CustomVer(FFortniteMainBranchObjectVersion::GUID) >= FFortniteMainBranchObjectVersion::StaticParameterTerrainLayerWeightBlendType)
		{
			Ar << P.bWeightBasedBlend;
		}
		Ar << P.WeightmapIndex<< P.bOverride << P.ExpressionGUID;
		return Ar;
	}

	void UpdateHash(FSHA1& HashState) const
	{
		FStaticParameterBase::UpdateHash(HashState);
		int32 Values[2];
		Values[0] = WeightmapIndex;
		Values[1] = bWeightBasedBlend;
		HashState.Update((const uint8*)&Values, sizeof(Values));
	}

	void AppendKeyString(FString& KeyString) const
	{
		FStaticParameterBase::AppendKeyString(KeyString);
		KeyString += FString::FromInt(WeightmapIndex);
		KeyString += FString::FromInt(bWeightBasedBlend);
	}
};


/**
* Holds the information for a static material layers parameter
*/
USTRUCT()
struct FStaticMaterialLayersParameter : public FStaticParameterBase
{
	GENERATED_USTRUCT_BODY();

	struct ID
	{
		FStaticParameterBase ParameterID;
		FMaterialLayersFunctions::ID Functions;

		friend FArchive& operator<<(FArchive& Ar, ID& P)
		{
			P.ParameterID.SerializeBase(Ar);
			P.Functions.SerializeForDDC(Ar);
			return Ar;
		}

		bool operator==(const ID& Reference) const
		{
			return ParameterID == Reference.ParameterID && Functions == Reference.Functions;
		}

		void UpdateHash(FSHA1& HashState) const
		{
			ParameterID.UpdateHash(HashState);
			Functions.UpdateHash(HashState);
		}

		void AppendKeyString(FString& KeyString) const
		{
			ParameterID.AppendKeyString(KeyString);
			Functions.AppendKeyString(KeyString);
		}
	};

	UPROPERTY()
	FMaterialLayersFunctions Value;

	FStaticMaterialLayersParameter() :
		FStaticParameterBase()
	{ }

	FStaticMaterialLayersParameter(const FMaterialParameterInfo& InInfo, const FMaterialLayersFunctions& InValue, bool InOverride, FGuid InGuid) :
		FStaticParameterBase(InInfo, InOverride, InGuid),
		Value(InValue)
	{ }
	
	const ID GetID() const;

	UMaterialFunctionInterface* GetParameterAssociatedFunction(const FMaterialParameterInfo& InParameterInfo) const;
	void GetParameterAssociatedFunctions(const FMaterialParameterInfo& InParameterInfo, TArray<UMaterialFunctionInterface*>& AssociatedFunctions) const;
	
	void AppendKeyString(FString& InKeyString) const
	{
		InKeyString += ParameterInfo.ToString() + ExpressionGUID.ToString() + Value.GetStaticPermutationString();
	}

	friend FArchive& operator<<(FArchive& Ar, FStaticMaterialLayersParameter& P)
	{
		Ar << P.ParameterInfo << P.bOverride << P.ExpressionGUID;
		
		Ar.UsingCustomVersion(FReleaseObjectVersion::GUID);
		if (Ar.CustomVer(FReleaseObjectVersion::GUID) >= FReleaseObjectVersion::MaterialLayersParameterSerializationRefactor)
		{
			P.Value.SerializeForDDC(Ar);
		}
		return Ar;
	}
};

/** Contains all the information needed to identify a single permutation of static parameters. */
USTRUCT()
struct FStaticParameterSet
{
	GENERATED_USTRUCT_BODY();

	/** An array of static switch parameters in this set */
	UPROPERTY()
	TArray<FStaticSwitchParameter> StaticSwitchParameters;

	/** An array of static component mask parameters in this set */
	UPROPERTY()
	TArray<FStaticComponentMaskParameter> StaticComponentMaskParameters;

	/** An array of terrain layer weight parameters in this set */
	UPROPERTY()
	TArray<FStaticTerrainLayerWeightParameter> TerrainLayerWeightParameters;

	/** An array of function call parameters in this set */
	UPROPERTY()
	TArray<FStaticMaterialLayersParameter> MaterialLayersParameters;

	/** 
	* Checks if this set contains any parameters
	* 
	* @return	true if this set has no parameters
	*/
	bool IsEmpty() const
	{
		return StaticSwitchParameters.Num() == 0 && StaticComponentMaskParameters.Num() == 0 && TerrainLayerWeightParameters.Num() == 0 && MaterialLayersParameters.Num() == 0;
	}

	void Serialize(FArchive& Ar)
	{
		Ar.UsingCustomVersion(FRenderingObjectVersion::GUID);
		Ar.UsingCustomVersion(FReleaseObjectVersion::GUID);
		// Note: FStaticParameterSet is saved both in packages (UMaterialInstance) and the DDC (FMaterialShaderMap)
		// Backwards compatibility only works with FStaticParameterSet's stored in packages.  
		// You must bump MATERIALSHADERMAP_DERIVEDDATA_VER as well if changing the serialization of FStaticParameterSet.
		Ar << StaticSwitchParameters;
		Ar << StaticComponentMaskParameters;
		Ar << TerrainLayerWeightParameters;
		if (Ar.CustomVer(FReleaseObjectVersion::GUID) >= FReleaseObjectVersion::MaterialLayersParameterSerializationRefactor)
		{
			Ar << MaterialLayersParameters;
		}
	}

	/** 
	* Tests this set against another for equality
	* 
	* @param ReferenceSet	The set to compare against
	* @return				true if the sets are equal
	*/
	bool operator==(const FStaticParameterSet& ReferenceSet) const;

	bool operator!=(const FStaticParameterSet& ReferenceSet) const
	{
		return !(*this == ReferenceSet);
	}

	bool Equivalent(const FStaticParameterSet& ReferenceSet) const;

private:
	void SortForEquivalent();
};
