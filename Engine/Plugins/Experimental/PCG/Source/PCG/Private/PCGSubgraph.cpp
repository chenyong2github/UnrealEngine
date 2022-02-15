// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGSubgraph.h"
#include "PCGGraph.h"
#include "PCGComponent.h"

void UPCGBaseSubgraphSettings::PostLoad()
{
	Super::PostLoad();

#if WITH_EDITOR
	if (UPCGGraph* Subgraph = GetSubgraph())
	{
		Subgraph->OnGraphChangedDelegate.AddUObject(this, &UPCGSubgraphSettings::OnSubgraphChanged);
	}
#endif
}

void UPCGBaseSubgraphSettings::BeginDestroy()
{
#if WITH_EDITOR
	if (UPCGGraph* Subgraph = GetSubgraph())
	{
		Subgraph->OnGraphChangedDelegate.RemoveAll(this);
	}
#endif

	Super::BeginDestroy();
}

#if WITH_EDITOR
void UPCGBaseSubgraphSettings::PreEditChange(FProperty* PropertyAboutToChange)
{
	if (PropertyAboutToChange && IsStructuralProperty(PropertyAboutToChange->GetFName()))
	{
		if (UPCGGraph* Subgraph = GetSubgraph())
		{
			Subgraph->OnGraphChangedDelegate.RemoveAll(this);
		}
	}

	Super::PreEditChange(PropertyAboutToChange);
}

void UPCGBaseSubgraphSettings::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.Property && IsStructuralProperty(PropertyChangedEvent.Property->GetFName()))
	{
		if (UPCGGraph* Subgraph = GetSubgraph())
		{
			Subgraph->OnGraphChangedDelegate.AddUObject(this, &UPCGSubgraphSettings::OnSubgraphChanged);
		}
	}
}

void UPCGBaseSubgraphSettings::OnSubgraphChanged(UPCGGraph* InGraph, bool bIsStructural)
{
	if (InGraph == GetSubgraph())
	{
		if (bIsStructural)
		{
			OnStructuralSettingsChangedDelegate.Broadcast(this);
		}
		else
		{
			OnSettingsChangedDelegate.Broadcast(this);
		}
	}
}

#endif // WITH_EDITOR

UPCGNode* UPCGSubgraphSettings::CreateNode() const
{
	return NewObject<UPCGSubgraphNode>();
}

#if WITH_EDITOR
bool UPCGSubgraphSettings::IsStructuralProperty(const FName& InPropertyName) const
{
	return (InPropertyName == GET_MEMBER_NAME_CHECKED(UPCGSubgraphSettings, Subgraph)) || Super::IsStructuralProperty(InPropertyName);
}

TArray<FName> UPCGSubgraphSettings::GetTrackedActorTags() const
{
	return Subgraph ? Subgraph->GetTrackedActorTags() : TArray<FName>();
}
#endif

FPCGElementPtr UPCGSubgraphSettings::CreateElement() const
{
	return MakeShared<FPCGSubgraphElement>();
}

TObjectPtr<UPCGGraph> UPCGSubgraphNode::GetSubgraph() const
{
	TObjectPtr<UPCGSubgraphSettings> Settings = Cast<UPCGSubgraphSettings>(DefaultSettings);
	return Settings ? Settings->Subgraph : nullptr;
}

void UPCGSubgraphNode::PostLoad()
{
	Super::PostLoad();

#if WITH_EDITOR
	if (UPCGSubgraphSettings* SubgraphSettings = Cast<UPCGSubgraphSettings>(DefaultSettings))
	{
		SubgraphSettings->OnStructuralSettingsChangedDelegate.AddUObject(this, &UPCGSubgraphNode::OnStructuralSettingsChanged);
	}
#endif
}

void UPCGSubgraphNode::BeginDestroy()
{
#if WITH_EDITOR
	if (UPCGSubgraphSettings* SubgraphSettings = Cast<UPCGSubgraphSettings>(DefaultSettings))
	{
		SubgraphSettings->OnStructuralSettingsChangedDelegate.RemoveAll(this);
	}
#endif

	Super::BeginDestroy();
}

#if WITH_EDITOR
void UPCGSubgraphNode::PreEditChange(FProperty* PropertyAboutToChange)
{
	if (PropertyAboutToChange && PropertyAboutToChange->GetFName() == GET_MEMBER_NAME_CHECKED(UPCGNode, DefaultSettings))
	{
		if (UPCGSubgraphSettings* SubgraphSettings = Cast<UPCGSubgraphSettings>(DefaultSettings))
		{
			SubgraphSettings->OnStructuralSettingsChangedDelegate.RemoveAll(this);
		}
	}

	Super::PreEditChange(PropertyAboutToChange);
}

void UPCGSubgraphNode::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	// Implementation note:
	// We must process structural changes before the parent class' otherwise the graph might be rescheduled with its tasks
	// before it is appropriately dirtied (which will trigger a recompilation)
	if (PropertyChangedEvent.Property && PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UPCGNode, DefaultSettings))
	{
		if (UPCGSubgraphSettings* SubgraphSettings = Cast<UPCGSubgraphSettings>(DefaultSettings))
		{
			SubgraphSettings->OnStructuralSettingsChangedDelegate.AddUObject(this, &UPCGSubgraphNode::OnStructuralSettingsChanged);

			// Changing the default settings should trigger immediately a structural change
			OnStructuralSettingsChanged(DefaultSettings);
		}
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

void UPCGSubgraphNode::OnStructuralSettingsChanged(UPCGSettings* InSettings)
{
	if (InSettings == DefaultSettings)
	{
		OnNodeStructuralSettingsChangedDelegate.Broadcast(this);
	}
}
#endif // WITH_EDITOR

FPCGContextPtr FPCGSubgraphElement::Initialize(const FPCGDataCollection& InputData, UPCGComponent* SourceComponent)
{
	TSharedPtr<FPCGSubgraphContext> Context = MakeShared<FPCGSubgraphContext>();
	Context->InputData = InputData;
	Context->SourceComponent = SourceComponent;

	return StaticCastSharedPtr<FPCGContext>(Context);
}

bool FPCGSubgraphElement::ExecuteInternal(FPCGContextPtr InContext) const
{
	TSharedPtr<FPCGSubgraphContext> Context = StaticCastSharedPtr<FPCGSubgraphContext>(InContext);

	const UPCGSubgraphNode* SubgraphNode = Cast<const UPCGSubgraphNode>(Context->Node);
	if (SubgraphNode && SubgraphNode->bDynamicGraph)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FPCGSubgraphElement::Execute);

		if (!Context->bScheduledSubgraph)
		{
			const UPCGSubgraphSettings* Settings = Context->GetInputSettings<UPCGSubgraphSettings>();
			check(Settings);
			UPCGGraph* Subgraph = Settings->Subgraph;

			UPCGSubsystem* Subsystem = Context->SourceComponent->GetSubsystem();

			if (Subsystem && Subgraph)
			{
				// Dispatch graph to execute with the given information we have
				// using this node's task id as additional inputs
				FPCGTaskId SubgraphTaskId = Subsystem->ScheduleGraph(Subgraph, Context->SourceComponent, MakeShared<FPCGInputForwardingElement>(Context->InputData), {});

				Context->SubgraphTaskId = SubgraphTaskId;
				Context->bScheduledSubgraph = true;
				Context->bIsPaused = true;

				// add a trivial task after the output task that wakes up this task
				Subsystem->ScheduleGeneric([Context]() {
					// Wake up the current task
					Context->bIsPaused = false;
					return true;
					}, { Context->SubgraphTaskId });

				return false;
			}
			else
			{
				// Job cannot run; cancel
				Context->OutputData.bCancelExecution = true;
				return true;
			}
		}
		else if (Context->bIsPaused)
		{
			// Should not happen once we skip it in the graph executor
			return false;
		}
		else
		{
			// when woken up, get the output data from the subgraph
			// and copy it to the current context output data, and finally return true
			UPCGSubsystem* Subsystem = Context->SourceComponent->GetSubsystem();
			if (Subsystem)
			{
				ensure(Subsystem->GetOutputData(Context->SubgraphTaskId, Context->OutputData));
			}
			else
			{
				// Job cannot run, cancel
				Context->OutputData.bCancelExecution = true;
			}

			return true;
		}
	}
	else
	{
		Context->OutputData = Context->InputData;
		return true;
	}
}

FPCGInputForwardingElement::FPCGInputForwardingElement(const FPCGDataCollection& InputToForward)
	: Input(InputToForward)
{

}

bool FPCGInputForwardingElement::ExecuteInternal(FPCGContextPtr Context) const
{
	Context->OutputData = Input;
	return true;
}