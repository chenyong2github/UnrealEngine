// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TemplateSequencePlayer.h"
#include "IMovieScenePlaybackClient.h"
#include "MovieSceneBindingOwnerInterface.h"
#include "GameFramework/Actor.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "UObject/SoftObjectPath.h"
#include "TemplateSequenceActor.generated.h"

class UTemplateSequence;

USTRUCT(BlueprintType)
struct FTemplateSequenceBindingOverrideData
{
	GENERATED_BODY()

	FTemplateSequenceBindingOverrideData()
		: bOverridesDefault(true)
	{
	}

	UPROPERTY(EditAnywhere, Category = "Binding")
	TWeakObjectPtr<UObject> Object;

	UPROPERTY(EditAnywhere, Category = "Binding")
	bool bOverridesDefault;
};

UCLASS(hideCategories = (Rendering, Physics, LOD, Activation, Input))
class TEMPLATESEQUENCE_API ATemplateSequenceActor
	: public AActor
	, public IMovieScenePlaybackClient
{
public:

	GENERATED_BODY()

	ATemplateSequenceActor(const FObjectInitializer& ObjectInitializer);

public:

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Playback", meta = (ShowOnlyInnerProperties))
	FMovieSceneSequencePlaybackSettings PlaybackSettings;

	UPROPERTY(Instanced, Transient, Replicated, BlueprintReadOnly, BlueprintGetter = GetSequencePlayer, Category = "Playback", meta = (ExposeFunctionCategories = "Game|Cinematic"))
	UTemplateSequencePlayer* SequencePlayer;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "General", meta = (AllowedClasses = "TemplateSequence"))
	FSoftObjectPath TemplateSequence;

	UPROPERTY(BlueprintReadOnly, Category = "General")
	FTemplateSequenceBindingOverrideData BindingOverride;

public:

	UFUNCTION(BlueprintCallable, Category = "Game|Cinematic")
	UTemplateSequence* GetSequence() const;

	UFUNCTION(BlueprintCallable, Category = "Game|Cinematic")
	UTemplateSequence* LoadSequence() const;

	UFUNCTION(BlueprintCallable, Category = "Game|Cinematic")
	void SetSequence(UTemplateSequence* InSequence);

	UFUNCTION(BlueprintGetter)
	UTemplateSequencePlayer* GetSequencePlayer() const;

	UFUNCTION(BlueprintCallable, Category = "Game|Cinematic|Bindings")
	void SetBinding(AActor* Actor);

protected:

	//~ Begin IMovieScenePlaybackClient interface
	virtual bool RetrieveBindingOverrides(const FGuid& InBindingId, FMovieSceneSequenceID InSequenceID, TArray<UObject*, TInlineAllocator<1>>& OutObjects) const override;
	virtual UObject* GetInstanceData() const override;
	//~ End IMovieScenePlaybackClient interface

	//~ Begin AActor interface
	virtual void Tick(float DeltaSeconds) override;
	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End AActor interface

	//~ Begin UObject interface
	virtual void PostInitProperties() override;
	//~ End UObject interface

public:

	void InitializePlayer();

#if WITH_EDITOR
	virtual bool GetReferencedContentObjects(TArray<UObject*>& Objects) const override;
#endif

private:

	void OnSequenceLoaded(const FName& PackageName, UPackage* Package, EAsyncLoadingResult::Type Result);
};
