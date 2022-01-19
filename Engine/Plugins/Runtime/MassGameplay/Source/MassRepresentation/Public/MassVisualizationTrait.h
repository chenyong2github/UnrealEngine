// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "MassEntityTraitBase.h"
#include "MassRepresentationTypes.h"
#include "GameFramework/Actor.h"
#include "MassVisualizationTrait.generated.h"

class UMassRepresentationSubsystem;
class UMassProcessor;

UCLASS(meta=(DisplayName="Visualization"))
class MASSREPRESENTATION_API UMassVisualizationTrait : public UMassEntityTraitBase
{
	GENERATED_BODY()
public:
	UMassVisualizationTrait();

protected:

	virtual void BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, UWorld& World) const override;

	/** Instanced static mesh information for this agent */
	UPROPERTY(EditAnywhere, Category = "Mass|Visual")
	FStaticMeshInstanceVisualizationDesc StaticMeshInstanceDesc;

	/** Actor class of this agent when spawned in high resolution*/
	UPROPERTY(EditAnywhere, Category = "Mass|Visual")
	TSubclassOf<AActor> HighResTemplateActor;

	/** Actor class of this agent when spawned in low resolution*/
	UPROPERTY(EditAnywhere, Category = "Mass|Visual")
	TSubclassOf<AActor> LowResTemplateActor;

	/** Allow subclasses to override the representation subsystem to use */
	UPROPERTY()
	TSubclassOf<UMassRepresentationSubsystem> RepresentationSubsystemClass;

	// @todo the following property will be cut once new de/initializers are in
	/** Allow subclasses to override the representation fragment deinitializer */
	TSubclassOf<UMassProcessor> RepresentationFragmentDeinitializerClass;
};
