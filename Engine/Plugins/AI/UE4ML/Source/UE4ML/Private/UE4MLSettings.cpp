// Copyright Epic Games, Inc. All Rights Reserved.

#include "UE4MLSettings.h"

#define GET_CONFIG_VALUE(a) (GetDefault<UUE4MLSettings>()->a)

UUE4MLSettings::UUE4MLSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DefautAgentClass = U4MLAgent::StaticClass();
	ManagerClass = U4MLManager::StaticClass();
	SessionClass = U4MLSession::StaticClass();
}

TSubclassOf<U4MLManager> UUE4MLSettings::GetManagerClass()
{ 
	const FSoftClassPath LocalClassName = GET_CONFIG_VALUE(ManagerClass);
	TSubclassOf<U4MLManager> LocalClass = LocalClassName.ResolveClass();
	return LocalClass;
}

TSubclassOf<U4MLSession> UUE4MLSettings::GetSessionClass()
{
	const FSoftClassPath LocalClassName = GET_CONFIG_VALUE(SessionClass);
	TSubclassOf<U4MLSession> LocalClass = LocalClassName.ResolveClass();
	return LocalClass;
}

TSubclassOf<U4MLAgent> UUE4MLSettings::GetAgentClass()
{
	const FSoftClassPath LocalClassName = GET_CONFIG_VALUE(DefautAgentClass);
	TSubclassOf<U4MLAgent> LocalClass = LocalClassName.ResolveClass();
	return LocalClass;
}

#if WITH_EDITOR
void UUE4MLSettings::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif // WITH_EDITOR

#undef GET_CONFIG_VALUE