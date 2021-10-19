// Copyright Epic Games, Inc. All Rights Reserved.

#include "ControlRigControlActor.h"
#include "ControlRigGizmoLibrary.h"
#include "IControlRigObjectBinding.h"
#include "Sequencer/MovieSceneControlRigParameterTrack.h"

#define LOCTEXT_NAMESPACE "ControlRigControlActor"

AControlRigControlActor::AControlRigControlActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, bRefreshOnTick(true)
	, bIsSelectable(true)
	, ColorParameter(TEXT("Color"))
	, bCastShadows(false)
{
	ActorRootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("SceneComponent0"));

	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;

	if (WITH_EDITOR)
	{
		PrimaryActorTick.bStartWithTickEnabled = true;
		bAllowTickBeforeBeginPlay = true;
	}

	SetActorEnableCollision(false);
	

	Refresh();
}


AControlRigControlActor::~AControlRigControlActor()
{
	RemoveUnbindDelegate();
}

void AControlRigControlActor::RemoveUnbindDelegate()
{
	if (ControlRig)
	{
		if (!ControlRig->HasAllFlags(RF_BeginDestroyed))
		{
			if (TSharedPtr<IControlRigObjectBinding> Binding = ControlRig->GetObjectBinding())
			{
				if (OnUnbindDelegate.IsValid())
				{
					Binding->OnControlRigUnbind().Remove(OnUnbindDelegate);
					OnUnbindDelegate.Reset();
				}
			}
		}
	}
}

#if WITH_EDITOR

void AControlRigControlActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.Property &&
		(	PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(AControlRigControlActor, ActorToTrack) ||
			PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(AControlRigControlActor, ControlRigClass) ||
			PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(AControlRigControlActor, MaterialOverride) ||
			PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(AControlRigControlActor, ColorParameter) ||
			PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(AControlRigControlActor, bCastShadows)))
	{
		Clear();
		Refresh();
	}
}

#endif

void AControlRigControlActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bRefreshOnTick)
	{
		Refresh();
	}
}

void AControlRigControlActor::Clear()
{
	TArray<USceneComponent*> ChildComponents;
	if (ActorRootComponent)
	{
		ActorRootComponent->GetChildrenComponents(true, ChildComponents);
		for (USceneComponent* Child : ChildComponents)
		{
			if (UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Child))
			{
				Components.AddUnique(StaticMeshComponent);
			}
		}

		for (UStaticMeshComponent* Component : Components)
		{
			Component->DetachFromComponent(FDetachmentTransformRules::KeepRelativeTransform);
			Component->UnregisterComponent();
			Component->DestroyComponent();
		}
	}

	ControlNames.Reset();
	GizmoTransforms.Reset();
	Components.Reset();
	Materials.Reset();
}

void AControlRigControlActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	RemoveUnbindDelegate();
	ControlRig = nullptr;
	Super::EndPlay(EndPlayReason);
}

void AControlRigControlActor::Refresh()
{
	if (ActorToTrack == nullptr)
	{
		return;
	}

	if (ControlRig == nullptr)
	{
		for (TObjectIterator<UControlRig> Itr; Itr; ++Itr)
		{
			UControlRig* RigInstance = *Itr;
			if (ControlRigClass == nullptr || RigInstance->GetClass()->IsChildOf(ControlRigClass))
			{
				if (TSharedPtr<IControlRigObjectBinding> Binding = RigInstance->GetObjectBinding())
				{
					if (AActor* Actor = Binding->GetHostingActor())
					{
						if (Actor == ActorToTrack)
						{
							ControlRig = RigInstance;
							RemoveUnbindDelegate();
							OnUnbindDelegate =Binding->OnControlRigUnbind().AddLambda([ this ]( ) { this->Clear(); this->Refresh(); });
							break;
						}
					}
				}
			}
		}

		if (ControlRig == nullptr)
		{
			return;
		}

		UControlRigGizmoLibrary* GizmoLibrary = ControlRig->GetGizmoLibrary();
		if (GizmoLibrary == nullptr)
		{
			return;
		}

		// disable collision again
		SetActorEnableCollision(false);

		// prepare the material + color param
		UMaterialInterface* BaseMaterial = nullptr;
		if (MaterialOverride && !ColorParameter.IsEmpty())
		{
			BaseMaterial = MaterialOverride;
			ColorParameterName = *ColorParameter;
		}
		else
		{
			BaseMaterial = GizmoLibrary->DefaultMaterial.LoadSynchronous();
			ColorParameterName = GizmoLibrary->MaterialColorParameter;
		}

		const FRigControlHierarchy& ControlHierarchy = ControlRig->GetControlHierarchy();
		for (const FRigControl& Control : ControlHierarchy)
		{
			if (!Control.bGizmoEnabled)
			{
				continue;
			}

			switch (Control.ControlType)
			{
				case ERigControlType::Float:
				case ERigControlType::Integer:
				case ERigControlType::Vector2D:
				case ERigControlType::Position:
				case ERigControlType::Scale:
				case ERigControlType::Rotator:
				case ERigControlType::Transform:
				case ERigControlType::TransformNoScale:
				case ERigControlType::EulerTransform:
				{
					if (const FControlRigGizmoDefinition* GizmoDef = GizmoLibrary->GetGizmoByName(Control.GizmoName))
					{
						UStaticMesh* StaticMesh = GizmoDef->StaticMesh.LoadSynchronous();
						UStaticMeshComponent* Component = NewObject< UStaticMeshComponent>(ActorRootComponent);
						Component->SetStaticMesh(StaticMesh);
						Component->SetupAttachment(ActorRootComponent);
						Component->RegisterComponent();

						Component->bCastStaticShadow = bCastShadows;
						Component->bCastDynamicShadow = bCastShadows;

						UMaterialInstanceDynamic* MaterialInstance = UMaterialInstanceDynamic::Create(BaseMaterial, Component);
						Component->SetMaterial(0, MaterialInstance);

						ControlNames.Add(Control.Name);
						GizmoTransforms.Add(Control.GizmoTransform * GizmoDef->Transform);
						Components.Add(Component);
						Materials.Add(MaterialInstance);
					}
				}
				default:
				{
					break;
				}
			}
		}
	}

	for (int32 GizmoIndex = 0; GizmoIndex < ControlNames.Num(); GizmoIndex++)
	{
		const FRigControlHierarchy& ControlHierarchy = ControlRig->GetControlHierarchy();
		int32 ControlIndex = ControlHierarchy.GetIndex(ControlNames[GizmoIndex]);
		if (ControlIndex == INDEX_NONE)
		{
			Components[GizmoIndex]->SetVisibility(false);
			continue;
		}

		const FRigControl& Control = ControlHierarchy[ControlIndex];

		FTransform ControlTransform = ControlRig->GetControlGlobalTransform(ControlNames[GizmoIndex]);
		Components[GizmoIndex]->SetRelativeTransform(GizmoTransforms[GizmoIndex] * ControlTransform);
		Materials[GizmoIndex]->SetVectorParameterValue(ColorParameterName, FVector(Control.GizmoColor));
	}
}


#undef LOCTEXT_NAMESPACE
