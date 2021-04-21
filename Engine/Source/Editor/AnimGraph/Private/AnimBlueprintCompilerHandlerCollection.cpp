// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimBlueprintCompilerHandlerCollection.h"
#include "AnimBlueprintCompilerCreationContext.h"

/** All of our registered handler factories */
static TMap<FName, TFunction<TUniquePtr<IAnimBlueprintCompilerHandler>(IAnimBlueprintCompilerCreationContext&)>> HandlerFactories;

void IAnimBlueprintCompilerHandlerCollection::RegisterHandler(FName InName, TFunction<TUniquePtr<IAnimBlueprintCompilerHandler>(IAnimBlueprintCompilerCreationContext&)> InFunction)
{
	HandlerFactories.Add(InName, InFunction);
}

void IAnimBlueprintCompilerHandlerCollection::UnregisterHandler(FName InName)
{
	HandlerFactories.Remove(InName);
}

void FAnimBlueprintCompilerHandlerCollection::Initialize(FAnimBlueprintCompilerContext* InCompilerContext)
{
	FAnimBlueprintCompilerCreationContext CreationContext(InCompilerContext);

	// Create all of the registered handlers
	for(const TPair<FName, TFunction<TUniquePtr<IAnimBlueprintCompilerHandler>(IAnimBlueprintCompilerCreationContext&)>>& HandlerFactoryPair : HandlerFactories)
	{
		Handlers.Add(HandlerFactoryPair.Key, HandlerFactoryPair.Value(CreationContext));
	}
}

IAnimBlueprintCompilerHandler* FAnimBlueprintCompilerHandlerCollection::GetHandlerByName(FName InName) const
{
	if(const TUniquePtr<IAnimBlueprintCompilerHandler>* FoundHandler = Handlers.Find(InName))
	{
		return (*FoundHandler).Get();
	}

	return nullptr;
}