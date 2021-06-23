// Copyright Epic Games, Inc. All Rights Reserved.
#include "PerQualityLevelProperties.h"
#include "Serialization/Archive.h"
#include "Misc/ConfigCacheIni.h"

namespace QualityLevelProperty
{
	static TArray<FName> Names = { FName("Low"), FName("Medium"), FName("High"), FName("Epic"), FName("Cinematic") };

	FName QualityLevelToFName(int32 QL)
	{
		if (QL >= 0 && QL < static_cast<int32>(EQualityLevels::Num))
		{
			return Names[QL];
		}
		else
		{
			return NAME_None;
		}
		return Names[QL];
	}

	int32 FNameToQualityLevel(FName QL)
	{
		return Names.IndexOfByKey(QL);
	}
}

#if WITH_EDITOR
#include "Interfaces/ITargetPlatform.h"
#include "PlatformInfo.h"

static TMap<FString, FSupportedQualityLevelArray> SupportedQualitLevels;
static FCriticalSection CookCriticalSection;

template<typename _StructType, typename _ValueType, EName _BasePropertyName>
FSupportedQualityLevelArray FPerQualityLevelProperty<_StructType, _ValueType, _BasePropertyName>::GetPlatformGroupQualityLevels(const TCHAR* InGroupName) const
{
	FSupportedQualityLevelArray* CachedCookingQualitLevelInfo = nullptr;

	{
		FScopeLock ScopeLock(&CookCriticalSection);
		CachedCookingQualitLevelInfo = SupportedQualitLevels.Find(InGroupName);
		if (CachedCookingQualitLevelInfo)
		{
			return *CachedCookingQualitLevelInfo;
		}
	}

	FSupportedQualityLevelArray GroupQualitLevelsInfo;

	//filter entries to remove from the final list of platforms
	const TArray<FString> Filters = { TEXT("NoEditor"), TEXT("Client"), TEXT("Server"), TEXT("AllDesktop") };

	// Platform (group) names
	TMultiMap<FName, FName> GroupToPlatform;

	// sanitize all vanilla platform names
	// generate a list of all platform groups
	const TArray<FName>& SanitizedPlatformNameArray = PlatformInfo::GetAllVanillaPlatformNames().FilterByPredicate([&Filters, &GroupToPlatform](const FName& PlatformName)
		{
			for (const FString& Filter : Filters)
			{
				const int32 Position = PlatformName.ToString().Find(Filter);
				if (Position != INDEX_NONE)
				{
					return false;
				}
			}

			if (const PlatformInfo::FTargetPlatformInfo* PlatformInfo = PlatformInfo::FindPlatformInfo(PlatformName))
			{
				GroupToPlatform.AddUnique(PlatformInfo->DataDrivenPlatformInfo->PlatformGroupName, PlatformName);
			}
			return true;
		});


	TArray<FName> Platforms;
	GroupToPlatform.MultiFind(InGroupName, Platforms);

	for (FName& Platform : Platforms)
	{
		GroupQualitLevelsInfo.Append(GetSupportedQualityLevels(*Platform.ToString()));
	}
	
	// Cache the result
	{
		FScopeLock ScopeLock(&CookCriticalSection);
		CachedCookingQualitLevelInfo = &SupportedQualitLevels.Add(FString(InGroupName), GroupQualitLevelsInfo);
	}

	return *CachedCookingQualitLevelInfo;
}

template<typename _StructType, typename _ValueType, EName _BasePropertyName>
FSupportedQualityLevelArray FPerQualityLevelProperty<_StructType, _ValueType, _BasePropertyName>::GetSupportedQualityLevels(const TCHAR* InPlatformName) const
{
	FSupportedQualityLevelArray* CachedCookingQualitLevelInfo = nullptr;
	
	{
		FScopeLock ScopeLock(&CookCriticalSection);
		CachedCookingQualitLevelInfo = SupportedQualitLevels.Find(InPlatformName);
		if (CachedCookingQualitLevelInfo)
		{
			return *CachedCookingQualitLevelInfo;
		}
	}

	FSupportedQualityLevelArray CookingQualitLevelInfo;

	// check the Engine file
	FConfigFile EngineSettings;
	FConfigCacheIni::LoadLocalIniFile(EngineSettings, TEXT("Engine"), true, InPlatformName);

	int32 PropertyQualityLevel = -1;
	if (EngineSettings.GetInt(TEXT("SystemSettings"), *CVarName, PropertyQualityLevel))
	{
		CookingQualitLevelInfo.Add(PropertyQualityLevel);
	}

	// Load the scalability platform file
	FConfigFile ScalabilitySettings;
	FConfigCacheIni::LoadLocalIniFile(ScalabilitySettings, TEXT("Scalability"), true, InPlatformName);

	//check all possible quality levels specify in the scalability ini 
	for (int32 QualityLevel = 0; QualityLevel < (int32)QualityLevelProperty::EQualityLevels::Num; ++QualityLevel)
	{
		FString QualitLevelSectionName = Scalability::GetScalabilitySectionString(*ScalabilitySection, QualityLevel, (int32)QualityLevelProperty::EQualityLevels::Num);
		PropertyQualityLevel = -1;
		ScalabilitySettings.GetInt(*QualitLevelSectionName, *CVarName, PropertyQualityLevel);

		// add supported quality level to the property map
		if (PropertyQualityLevel != -1)
		{
			CookingQualitLevelInfo.Add(PropertyQualityLevel);
		}
	}

	// Cache the Scalability setting for this platform
	{
		FScopeLock ScopeLock(&CookCriticalSection);
		CachedCookingQualitLevelInfo = &SupportedQualitLevels.Add(FString(InPlatformName), CookingQualitLevelInfo);
	}

	return *CachedCookingQualitLevelInfo;
}

template<typename _StructType, typename _ValueType, EName _BasePropertyName>
void FPerQualityLevelProperty<_StructType, _ValueType, _BasePropertyName>::StripQualtiyLevelForCooking(const TCHAR* InPlatformName)
{
	_StructType* This = StaticCast<_StructType*>(this);
	if (This->PerQuality.Num() > 0)
	{
		FSupportedQualityLevelArray CookQualityLevelInfo = This->GetSupportedQualityLevels(InPlatformName);

		int32 LowestQualityLevel = (int32)QualityLevelProperty::EQualityLevels::Num;

		// remove unsupported quality levels 
		for (TMap<int32, int32>::TIterator It(This->PerQuality); It; ++It)
		{
			if(!CookQualityLevelInfo.Contains(It.Key()))
			{
				It.RemoveCurrent();
			}
			else
			{
				LowestQualityLevel = (It.Key() < LowestQualityLevel) ? It.Key() : LowestQualityLevel;
			}
		}

		//if found supported platforms, put the lowest quality level in Default
		if (LowestQualityLevel != (int32)QualityLevelProperty::EQualityLevels::Num)
		{
			This->Default = This->GetValue(LowestQualityLevel);
		}
	}
}

template<typename _StructType, typename _ValueType, EName _BasePropertyName>
bool FPerQualityLevelProperty<_StructType, _ValueType, _BasePropertyName>::IsQualityLevelValid(int32 QualityLevel) const
{
	const _StructType* This = StaticCast<const _StructType*>(this);
	int32* Value = (int32*)This->PerQuality.Find(QualityLevel);

	if (Value != nullptr)
	{
		return true;
	}
	else
	{
		return false;
	}
}
#endif

/** Serializer to cook out the most appropriate platform override */
template<typename _StructType, typename _ValueType, EName _BasePropertyName>
ENGINE_API FArchive& operator<<(FArchive& Ar, FPerQualityLevelProperty<_StructType, _ValueType, _BasePropertyName>& Property)
{
	bool bCooked = false;
	_StructType* This = StaticCast<_StructType*>(&Property);

#if WITH_EDITOR
	if (Ar.IsCooking())
	{
		bCooked = true;
		const FDataDrivenPlatformInfo& PlatformInfo = Ar.CookingTarget()->GetPlatformInfo();
		This->StripQualtiyLevelForCooking(*(PlatformInfo.PlatformGroupName.ToString()));
	}
#endif
	{
		Ar << bCooked;
		Ar << This->Default;
		Ar << This->PerQuality;
	}
	return Ar;
}

/** Serializer to cook out the most appropriate platform override */
template<typename _StructType, typename _ValueType, EName _BasePropertyName>
ENGINE_API void operator<<(FStructuredArchive::FSlot Slot, FPerQualityLevelProperty<_StructType, _ValueType, _BasePropertyName>& Property)
{
	FArchive& UnderlyingArchive = Slot.GetUnderlyingArchive();
	FStructuredArchive::FRecord Record = Slot.EnterRecord();

	bool bCooked = false;
	_StructType* This = StaticCast<_StructType*>(&Property);

#if WITH_EDITOR
	if (UnderlyingArchive.IsCooking())
	{
		bCooked = true;
		This->StripQualtiyLevelForCooking(*(UnderlyingArchive.CookingTarget()->GetPlatformInfo().PlatformGroupName.ToString()));
	}
#endif
	{
		Record << SA_VALUE(TEXT("bCooked"), bCooked);
		Record << SA_VALUE(TEXT("Value"), This->Default);
		Record << SA_VALUE(TEXT("PerQuality"), This->PerQuality);
	}
}
// 
template ENGINE_API FArchive& operator<<(FArchive&, FPerQualityLevelProperty<FPerQualityLevelInt, int32, NAME_IntProperty>&);
template ENGINE_API void operator<<(FStructuredArchive::FSlot Slot, FPerQualityLevelProperty<FPerQualityLevelInt, int32, NAME_IntProperty>&);

#if WITH_EDITOR
template FSupportedQualityLevelArray FPerQualityLevelProperty<FPerQualityLevelInt, int32, NAME_IntProperty>::GetSupportedQualityLevels(const TCHAR* InPlatformName) const;
template FSupportedQualityLevelArray FPerQualityLevelProperty<FPerQualityLevelInt, int32, NAME_IntProperty>::GetPlatformGroupQualityLevels(const TCHAR* InGroupName) const;
template void FPerQualityLevelProperty<FPerQualityLevelInt, int32, NAME_IntProperty>::StripQualtiyLevelForCooking(const TCHAR* InPlatformName);
template bool FPerQualityLevelProperty<FPerQualityLevelInt, int32, NAME_IntProperty>::IsQualityLevelValid(int32 QualityLevel) const;
#endif