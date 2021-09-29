// Copyright Epic Games, Inc. All Rights Reserved.

#include "DebugEntityTemplateBuilder.h"
#include "MassDebuggerSubsystem.h"
#include "MassCommonFragments.h"
#include "MassDebugVisualizationComponent.h"
#include "MassEntityTemplateRegistry.h"
#include "MassMovementFragments.h"
#include "MassTranslatorRegistry.h"
#include "Engine/World.h"

namespace UE::Mass::DebugEntityTemplateBuilder
{
	void BuildTemplate(const UWorld* World, const FInstancedStruct& InStructInstance, FMassEntityTemplateBuildContext& BuildContext)
	{
		const FMassSpawnProps& Data = InStructInstance.Get<FMassSpawnProps>();

		for (const FInstancedStruct& Fragment : Data.AdditionalDataFragments)
		{
			BuildContext.AddFragmentWithDefaultInitializer(Fragment);
		}

		// the following needs to be always there for mesh vis to work. Adding following fragments after already
		// adding Data.AdditionalDataFragments to let user configure the fragments first. Calling OutFragments.Add()
		// won't override any fragments that are already there
		BuildContext.AddFragmentWithDefaultInitializer<FDataFragment_Transform>();
		BuildContext.AddFragmentWithDefaultInitializer<FDataFragment_NavLocation>();
		BuildContext.AddFragmentWithDefaultInitializer<FMassVelocityFragment>();

#if WITH_EDITORONLY_DATA
		const UStaticMesh* const DebugMesh = Data.DebugShape.Mesh;
#else
	const UStaticMesh* const DebugMesh = nullptr;
#endif

		if (DebugMesh)
		{
#if WITH_EDITORONLY_DATA
			FSimDebugVisComponent& DebugVisFragment = BuildContext.AddFragmentWithDefaultInitializer_GetRef<FSimDebugVisComponent>();
			UMassDebuggerSubsystem* Debugger = UWorld::GetSubsystem<UMassDebuggerSubsystem>(World);
			if (ensure(Debugger))
			{
				UMassDebugVisualizationComponent* DebugVisComponent = Debugger->GetVisualizationComponent();
				if (ensure(DebugVisComponent))
				{
					DebugVisFragment.VisualType = DebugVisComponent->AddDebugVisType(Data.DebugShape);
				}
			}

			BuildContext.AddDefaultInitializer<FSimDebugVisComponent>();
#endif // WITH_EDITORONLY_DATA
		}
			// add fragments needed whenever we have debugging capabilities
			// @todo could be useful to distinguish the manually added fragments from the ones we've auto-added. The template could store this info
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		else // Data.DebugShape.Mesh == null
		{
			BuildContext.AddFragmentWithDefaultInitializer<FDataFragment_DebugVis>();
			BuildContext.AddFragmentWithDefaultInitializer<FDataFragment_AgentRadius>();
		}
#endif // if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	}

	void Register()
	{
		UMassEntityTemplateRegistry::FindOrAdd(*FMassSpawnProps::StaticStruct()).BindStatic(&BuildTemplate);
	}
} // namespace UE::Mass::DebugEntityTemplateBuilder

