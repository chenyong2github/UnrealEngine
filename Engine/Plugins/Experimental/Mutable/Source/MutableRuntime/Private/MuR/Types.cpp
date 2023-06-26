// Copyright Epic Games, Inc. All Rights Reserved.


#include "MuR/Types.h"
#include "MuR/Serialisation.h"
#include "Hash/CityHash.h"


FExternalImageID::FExternalImageID(const FString& InName)
{
	Name = InName;
	NameHash = CityHash64(reinterpret_cast<const char*>(*Name),Name.Len()*sizeof(FString::ElementType));
}

void FExternalImageID::Serialise(mu::OutputArchive& arch) const
{
	arch << Name;
}

void FExternalImageID::Unserialise(mu::InputArchive& arch)
{
	arch >> Name;
	NameHash = CityHash64(reinterpret_cast<const char*>(*Name), Name.Len() * sizeof(FString::ElementType));
}
