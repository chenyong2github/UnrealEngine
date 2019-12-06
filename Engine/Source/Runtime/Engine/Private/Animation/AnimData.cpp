// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	AnimData.cpp: Anim data template code and related.
=============================================================================*/ 

#include "Animation/AnimClassData.h"
#include "UObject/UnrealTypePrivate.h"
#include "UObject/CoreObjectVersion.h"

void UAnimClassData::Serialize(FArchive& Ar)
{
	Ar.UsingCustomVersion(FCoreObjectVersion::GUID);

	Super::Serialize(Ar);

	if (Ar.CustomVer(FCoreObjectVersion::GUID) >= FCoreObjectVersion::FProperties)
	{
		Ar << AnimNodeProperties;
		Ar << LinkedAnimGraphNodeProperties;
		Ar << LinkedAnimLayerNodeProperties;
		Ar << PreUpdateNodeProperties;
		Ar << DynamicResetNodeProperties;
		Ar << StateMachineNodeProperties;
		Ar << InitializationNodeProperties;
	}
#if WITH_EDITORONLY_DATA
	else if (Ar.IsLoading())
	{
		for (UStructProperty* Prop : AnimNodeProperties_DEPRECATED)
		{
			AnimNodeProperties.Add(Prop);
		}
		for (UStructProperty* Prop : LinkedAnimGraphNodeProperties_DEPRECATED)
		{
			LinkedAnimGraphNodeProperties.Add(Prop);
		}
		for (UStructProperty* Prop : LinkedAnimLayerNodeProperties_DEPRECATED)
		{
			LinkedAnimLayerNodeProperties.Add(Prop);
		}
		for (UStructProperty* Prop : PreUpdateNodeProperties_DEPRECATED)
		{
			PreUpdateNodeProperties.Add(Prop);
		}
		for (UStructProperty* Prop : DynamicResetNodeProperties_DEPRECATED)
		{
			DynamicResetNodeProperties.Add(Prop);
		}
		for (UStructProperty* Prop : StateMachineNodeProperties_DEPRECATED)
		{
			StateMachineNodeProperties.Add(Prop);
		}
		for (UStructProperty* Prop : InitializationNodeProperties_DEPRECATED)
		{
			InitializationNodeProperties.Add(Prop);
		}

	}
#endif // WITH_EDITORONLY_DATA
}