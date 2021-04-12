// Copyright Epic Games, Inc. All Rights Reserved.

#include "MLAdapterSettings.h"

#define GET_CONFIG_VALUE(a) (GetDefault<UMLAdapterSettings>()->a)

UMLAdapterSettings::UMLAdapterSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DefautAgentClass = UMLAdapterAgent::StaticClass();
	ManagerClass = UMLAdapterManager::StaticClass();
	SessionClass = UMLAdapterSession::StaticClass();
}

TSubclassOf<UMLAdapterManager> UMLAdapterSettings::GetManagerClass()
{ 
	const FSoftClassPath LocalClassName = GET_CONFIG_VALUE(ManagerClass);
	TSubclassOf<UMLAdapterManager> LocalClass = LocalClassName.ResolveClass();
	return LocalClass;
}

TSubclassOf<UMLAdapterSession> UMLAdapterSettings::GetSessionClass()
{
	const FSoftClassPath LocalClassName = GET_CONFIG_VALUE(SessionClass);
	TSubclassOf<UMLAdapterSession> LocalClass = LocalClassName.ResolveClass();
	return LocalClass;
}

TSubclassOf<UMLAdapterAgent> UMLAdapterSettings::GetAgentClass()
{
	const FSoftClassPath LocalClassName = GET_CONFIG_VALUE(DefautAgentClass);
	TSubclassOf<UMLAdapterAgent> LocalClass = LocalClassName.ResolveClass();
	return LocalClass;
}

#if WITH_EDITOR
void UMLAdapterSettings::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif // WITH_EDITOR

#undef GET_CONFIG_VALUE