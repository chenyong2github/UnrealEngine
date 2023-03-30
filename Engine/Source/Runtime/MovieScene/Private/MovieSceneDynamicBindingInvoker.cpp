// Copyright Epic Games, Inc. All Rights Reserved.

#include "MovieSceneDynamicBindingInvoker.h"

#include "CoreMinimal.h"
#include "Engine/World.h"
#include "Evaluation/MovieSceneEvaluationTemplateInstance.h"
#include "IMovieScenePlayer.h"
#include "MovieSceneDynamicBinding.h"

bool FMovieSceneDynamicBindingInvoker::ResolveDynamicBinding(IMovieScenePlayer& Player, UMovieSceneSequence* Sequence, const FMovieSceneSequenceID& SequenceID, const FGuid& InGuid, const FMovieSceneDynamicBinding& DynamicBinding, TArray<UObject*, TInlineAllocator<1>>& OutObjects)
{
	UObject* Object = ResolveDynamicBinding(Player, Sequence, SequenceID, InGuid, DynamicBinding);
	if (Object != nullptr)
	{
		OutObjects.Add(Object);

		// We have successfully called the dynamic binding function, don't use the default behavior.
		return false;
	}

	// No valid object found, fallback to default behavior.
	return true;
}

UObject* FMovieSceneDynamicBindingInvoker::ResolveDynamicBinding(IMovieScenePlayer& Player, UMovieSceneSequence* Sequence, const FMovieSceneSequenceID& SequenceID, const FGuid& InGuid, const FMovieSceneDynamicBinding& DynamicBinding)
{
	if (!ensure(Sequence))
	{
		// Sequence is somehow null... fallback to default behavior.
		return nullptr;
	}

	UFunction* DynamicBindingFunc = DynamicBinding.Function.Get();
	if (!DynamicBindingFunc)
	{
		// No dynamic binding, fallback to default behavior.
		return nullptr;
	}

	UObject* DirectorInstance = Player.GetEvaluationTemplate().GetOrCreateDirectorInstance(SequenceID, Player);
	if (!DirectorInstance)
	{
#if !NO_LOGGING
		UE_LOG(LogMovieScene, Warning, 
				TEXT("%s: Failed to resolve dynamic binding '%s' because no director instance was available."), 
				*Sequence->GetName(), *DynamicBindingFunc->GetName());
#endif
		// Fallback to default behavior.
		return nullptr;
	}

#if WITH_EDITOR
	const static FName NAME_CallInEditor(TEXT("CallInEditor"));

	UWorld* World = DirectorInstance->GetWorld();
	const bool bIsGameWorld = World && World->IsGameWorld();

	if (!bIsGameWorld && !DynamicBindingFunc->HasMetaData(NAME_CallInEditor))
	{
		UE_LOG(LogMovieScene, Verbose,
				TEXT("%s: Refusing to resolve dynamic binding '%s' in editor world because function '%s' has 'Call in Editor' set to false."),
				*Sequence->GetName(), *LexToString(InGuid), *DynamicBindingFunc->GetName());
		// Fallback to default behavior.
		return nullptr;
	}
#endif // WITH_EDITOR

	UE_LOG(LogMovieScene, VeryVerbose,
			TEXT("%s: Resolving dynamic binding '%s' with function '%s'."),
			*Sequence->GetName(), *LexToString(InGuid), *DynamicBindingFunc->GetName());

	FMovieSceneDynamicBindingResolveParams ResolveParams;
	ResolveParams.ObjectBindingID = InGuid;
	ResolveParams.Sequence = Sequence;
	ResolveParams.RootSequence = Player.GetEvaluationTemplate().GetRootSequence();
	UObject* Object = InvokeDynamicBinding(DirectorInstance, DynamicBinding, ResolveParams);

	return Object;
}

UObject* FMovieSceneDynamicBindingInvoker::InvokeDynamicBinding(UObject* DirectorInstance, const FMovieSceneDynamicBinding& DynamicBinding, const FMovieSceneDynamicBindingResolveParams& ResolveParams)
{
	// Parse all function parameters.
	UFunction* DynamicBindingFunc = DynamicBinding.Function.Get();
	check(DynamicBindingFunc);
	uint8* Parameters = (uint8*)FMemory_Alloca(DynamicBindingFunc->ParmsSize + DynamicBindingFunc->MinAlignment);
	Parameters = Align(Parameters, DynamicBindingFunc->MinAlignment);

	// Initialize parameters.
	FMemory::Memzero(Parameters, DynamicBindingFunc->ParmsSize);

	FObjectPropertyBase* ReturnProp = nullptr;
	for (TFieldIterator<FProperty> It(DynamicBindingFunc); It; ++It)
	{
		FProperty* LocalProp = *It;
		checkSlow(LocalProp);
		if (!LocalProp->HasAnyPropertyFlags(CPF_ZeroConstructor))
		{
			LocalProp->InitializeValue_InContainer(Parameters);
		}

		if (LocalProp->HasAnyPropertyFlags(CPF_ReturnParm))
		{
			ensure(ReturnProp == nullptr);
			ReturnProp = CastFieldChecked<FObjectPropertyBase>(LocalProp);
		}
	}
	
	// Set the resolve parameter struct if we need to pass it to the function.
	if (FProperty* ResolveParamsProp = DynamicBinding.ResolveParamsProperty.Get())
	{
		ResolveParamsProp->SetValue_InContainer(Parameters, &ResolveParams);
	}

	// Invoke the function.
	DirectorInstance->ProcessEvent(DynamicBindingFunc, Parameters);

	// Grab the result value.
	UObject* ResultObject = nullptr;
	if (ensure(ReturnProp))
	{
		ResultObject = ReturnProp->GetObjectPropertyValue_InContainer(Parameters);
	}

	// Destroy parameters.
	for (TFieldIterator<FProperty> It(DynamicBindingFunc); It; ++It)
	{
		It->DestroyValue_InContainer(Parameters);
	}

	return ResultObject;
}

