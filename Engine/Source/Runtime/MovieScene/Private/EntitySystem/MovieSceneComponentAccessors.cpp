// Copyright Epic Games, Inc. All Rights Reserved.

#include "EntitySystem/MovieSceneComponentAccessors.h"
#include "EntitySystem/MovieSceneComponentRegistry.h"
#include "EntitySystem/MovieSceneEntityManager.h"


namespace UE
{
namespace MovieScene
{

#if UE_MOVIESCENE_ENTITY_DEBUG

void AccessorToString(const FRead* In, FEntityManager* EntityManager, FString& OutString)
{
	const FComponentTypeInfo& ComponentTypeInfo = EntityManager->GetComponents()->GetComponentTypeChecked(In->ComponentType);
	OutString += FString::Printf(TEXT("\n\tRead: %s %s"), *ComponentTypeInfo.DebugInfo->DebugName, ComponentTypeInfo.DebugInfo->DebugTypeName);
}
void AccessorToString(const FWrite* In, FEntityManager* EntityManager, FString& OutString)
{
	const FComponentTypeInfo& ComponentTypeInfo = EntityManager->GetComponents()->GetComponentTypeChecked(In->ComponentType);
	OutString += FString::Printf(TEXT("\n\tWrite: %s %s"), *ComponentTypeInfo.DebugInfo->DebugName, ComponentTypeInfo.DebugInfo->DebugTypeName);
}
void AccessorToString(const FReadOptional* In, FEntityManager* EntityManager, FString& OutString)
{
	if (In->ComponentType)
	{
		const FComponentTypeInfo& ComponentTypeInfo = EntityManager->GetComponents()->GetComponentTypeChecked(In->ComponentType);
		OutString += FString::Printf(TEXT("\n\tRead (Optional): %s %s"), *ComponentTypeInfo.DebugInfo->DebugName, ComponentTypeInfo.DebugInfo->DebugTypeName);
	}
}
void AccessorToString(const FWriteOptional* In, FEntityManager* EntityManager, FString& OutString)
{
	if (In->ComponentType)
	{
		const FComponentTypeInfo& ComponentTypeInfo = EntityManager->GetComponents()->GetComponentTypeChecked(In->ComponentType);
		OutString += FString::Printf(TEXT("\n\tWrite (Optional): %s %s"), *ComponentTypeInfo.DebugInfo->DebugName, ComponentTypeInfo.DebugInfo->DebugTypeName);
	}
}
void AccessorToString(const FReadEntityIDs*, FEntityManager* EntityManager, FString& OutString)
{
	OutString += TEXT("\n\tRead: Entity IDs");
}
void OneOfAccessorToString(const FReadOptional* In, FEntityManager* EntityManager, FString& OutString)
{
	if (In->ComponentType)
	{
		const FComponentTypeInfo& ComponentTypeInfo = EntityManager->GetComponents()->GetComponentTypeChecked(In->ComponentType);
		OutString = FString::Printf(TEXT("%s %s"), *ComponentTypeInfo.DebugInfo->DebugName, ComponentTypeInfo.DebugInfo->DebugTypeName);
	}
}

#endif // UE_MOVIESCENE_ENTITY_DEBUG


} // namespace MovieScene
} // namespace UE