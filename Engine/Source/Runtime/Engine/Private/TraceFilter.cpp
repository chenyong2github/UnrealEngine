// Copyright Epic Games, Inc. All Rights Reserved.

#include "TraceFilter.h"

#if TRACE_FILTERING_ENABLED

#include "TraceFilters.h"
#include "HAL/IConsoleManager.h"
#include "Algo/Transform.h"
#include "Containers/Map.h"

#include "UObject/UObjectBase.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"
#include "Logging/LogMacros.h"
#include "ObjectTrace.h"

struct FTraceFilterObjectAnnotation
{
	FTraceFilterObjectAnnotation()
		: bIsTraceable(false)
	{}

	bool bIsTraceable;

	/** Determine if this annotation is default - required for annotations */
	FORCEINLINE bool IsDefault() const
	{
		return !bIsTraceable;
	}
};

FUObjectAnnotationSparse<FTraceFilterObjectAnnotation, true> GObjectFilterAnnotations;

DEFINE_LOG_CATEGORY_STATIC(TraceFiltering, Display, Display);

/** Console command allowing to debug the current state of GObjectFilterAnnotations, to see which objects are Traceable*/
FAutoConsoleCommand FlushFilterStateCommand(TEXT("TraceFilter.FlushState"), TEXT("Flushes the current trace filtering state to the output log."),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			TMap<const UWorld*, TArray<const AActor*>> WorldToActorMap;
			TMap<const AActor*, TArray<const UActorComponent*>> ActorToComponentMap;
			TSet<const UObject*> Objects;

			/** Collect object type information */
			const TMap<const UObjectBase*, FTraceFilterObjectAnnotation>& Annotations = GObjectFilterAnnotations.GetAnnotationMap();

			/** Retrieve all annotated objects */
			TArray<const UObjectBase*> BaseObjects;
			Annotations.GenerateKeyArray(BaseObjects);

			/** Filter each object according to its type */
			for (const UObjectBase* ObjectBase : BaseObjects)
			{
				const UObject* Object = (const UObject*)ObjectBase;
				if (const UWorld* World = Cast<UWorld>(Object))
				{
					WorldToActorMap.Add(World);
				}
			}

			for (const UObjectBase* ObjectBase : BaseObjects)
			{
				const UObject* Object = (const UObject*)ObjectBase;
				if (const AActor* Actor = Cast<AActor>(Object))
				{
					WorldToActorMap.FindChecked(Actor->GetWorld()).Add(Actor);
					ActorToComponentMap.Add(Actor);
				}
			}

			for (const UObjectBase* ObjectBase : BaseObjects)
			{
				const UObject* Object = (const UObject*)ObjectBase;
				if (const UActorComponent* Component = Cast<UActorComponent>(Object))
				{
					ActorToComponentMap.FindChecked(Component->GetOwner()).Add(Component);
				}
			}

			Algo::TransformIf(BaseObjects, Objects, 
				[](const UObjectBase* ObjectBase)
				{
					const UObject* Object = (const UObject*)ObjectBase;
					return !Cast<UWorld>(Object) && !Cast<AActor>(Object) && !Cast<UActorComponent>(Object);
				},
				[](const UObjectBase* ObjectBase)
				{
					const UObject* Object = (const UObject*)ObjectBase;
					return Object;
				}
			);

			/** Output collated data */
			FString OutputString;

			/** For each UWorld, output all AActors */
			for (const TPair<const UWorld*, TArray<const AActor*>>& WorldToActor : WorldToActorMap)
			{	
				OutputString += TEXT("\n");
				OutputString += WorldToActor.Key->GetName();
				OutputString += TEXT(" [UWorld]\n");

				/** For each AActor, output all UActorComponent */
				for (const AActor* Actor : WorldToActor.Value)
				{
					OutputString += TEXT("\t- ");
					OutputString += Actor->GetName();
					OutputString += TEXT(" [Actor]\n");

					TArray<const UActorComponent*> ActorComponents = ActorToComponentMap.FindChecked(Actor);
					for (const UActorComponent* Component : ActorComponents)
					{
						OutputString += TEXT("\t\t* ");
						OutputString += Component->GetName();
						OutputString += TEXT(" [Component]\n");
					}			
				}

				OutputString += TEXT("----------------------------------------------------\n");
			}			

			for (const UObject* Object : Objects)
			{
				OutputString += Object->GetName();
				OutputString += TEXT(" [Object]\n");
			}

			UE_LOG(TraceFiltering, Display, TEXT("%s"), *OutputString);
		})
	);

bool FTraceFilter::IsObjectTraceable(const UObject* InObject)
{
	return InObject ? GObjectFilterAnnotations.GetAnnotation(InObject).bIsTraceable : true;
}

void FTraceFilter::SetObjectIsTraceable(const UObject* InObject, bool bIsTraceable)
{
	ensure(InObject);

	if (bIsTraceable)
	{
		FTraceFilterObjectAnnotation Annotation;
		Annotation.bIsTraceable = true;
		GObjectFilterAnnotations.AddAnnotation(InObject, Annotation);

		TRACE_OBJECT(InObject);
	}
	else
	{
		GObjectFilterAnnotations.RemoveAnnotation(InObject);
	}
}

void FTraceFilter::MarkObjectTraceable(const UObject* InObject)
{
	ensure(InObject);

	FTraceFilterObjectAnnotation Annotation;
	Annotation.bIsTraceable = true;
	GObjectFilterAnnotations.AddAnnotation(InObject, Annotation);
}

void FTraceFilter::Init()
{
	FTraceActorFilter::Initialize();
	FTraceWorldFilter::Initialize();
}

void FTraceFilter::Destroy()
{
	GObjectFilterAnnotations.RemoveAllAnnotations();
	FTraceActorFilter::Destroy();
	FTraceWorldFilter::Destroy();
}

#endif // TRACE_FILTERING_ENABLED
