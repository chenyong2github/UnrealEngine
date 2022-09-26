// Copyright Epic Games, Inc. All Rights Reserved.

#include "CustomizableObjectIdentifier.h"


FCustomizableObjectIdPair::FCustomizableObjectIdPair(FString ObjectGroupName, FString ObjectName)
	: CustomizableObjectGroupName(ObjectGroupName)
	, CustomizableObjectName(ObjectName)
{
}

FCustomizableObjectIdentifier::FCustomizableObjectIdentifier()
{
}

FCustomizableObjectIdentifier::FCustomizableObjectIdentifier(FString ObjectGroupName, FString ObjectName)
	: CustomizableObjectGroupName(ObjectGroupName)
	, CustomizableObjectName(ObjectName)
{
}

