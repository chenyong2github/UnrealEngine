// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"

#include "LiveLinkRole.h"

#include "LiveLinkSubjectSettings.generated.h"

class ULiveLinkFrameInterpolationProcessor;
class ULiveLinkFramePreProcessor;
class ULiveLinkFrameTranslator;


// Base class for live link subject settings
UCLASS()
class LIVELINKINTERFACE_API ULiveLinkSubjectSettings : public UObject
{
public:
	GENERATED_BODY()

	/** List of available preprocessor the subject will use. */
	UPROPERTY(EditAnywhere, Instanced, Category = "LiveLink", meta = (DisplayName = "Pre Processors"))
	TArray<ULiveLinkFramePreProcessor*> PreProcessors;

	/** The interpolation processor the subject will use. */
	UPROPERTY(EditAnywhere, Instanced, Category = "LiveLink", meta = (DisplayName = "Interpolation"))
	ULiveLinkFrameInterpolationProcessor* InterpolationProcessor;

	/** List of available translator the subject can use. */
	UPROPERTY(EditAnywhere, Instanced, Category = "LiveLink", meta = (DisplayName = "Translators"))
	TArray<ULiveLinkFrameTranslator*> Translators;

	UPROPERTY()
	TSubclassOf<ULiveLinkRole> Role;

	/** Last FrameRate estimated by the subject. If in Timecode mode, this will come directly from the QualifiedFrameTime. */
	UPROPERTY(VisibleAnywhere, Category="LiveLink")
	FFrameRate FrameRate;
	
	/** If enabled, rebroadcast this subject */
	UPROPERTY(EditAnywhere, Category = "LiveLink")
    bool bRebroadcastSubject;

public:
	//~ Begin UObject interface
#if WITH_EDITOR
	virtual void PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent) override;
#endif
	//~ End UObject interface
};
