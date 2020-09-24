// Copyright Epic Games, Inc. All Rights Reserved.

#include "MovieSceneTracksComponentTypes.h"
#include "EntitySystem/BuiltInComponentTypes.h"
#include "EntitySystem/MovieSceneEntityManager.h"
#include "EntitySystem/MovieSceneEntitySystemLinker.h"
#include "EntitySystem/MovieSceneComponentRegistry.h"
#include "EntitySystem/MovieSceneBlenderSystem.h"
#include "EntitySystem/MovieScenePropertyComponentHandler.h"

#include "MovieSceneObjectBindingID.h"
#include "GameFramework/Actor.h"

#include "Misc/App.h"

namespace UE
{
namespace MovieScene
{

void ConvertOperationalProperty(const FIntermediate3DTransform& In, FEulerTransform& Out)
{
	Out.Location = In.GetTranslation();
	Out.Rotation = In.GetRotation();
	Out.Scale = In.GetScale();
}
void ConvertOperationalProperty(const FEulerTransform& In, FIntermediate3DTransform& Out)
{
	Out = FIntermediate3DTransform(In.Location, In.Rotation, In.Scale);
}

void ConvertOperationalProperty(const FIntermediate3DTransform& In, FTransform& Out)
{
	Out = FTransform(In.GetRotation().Quaternion(), In.GetTranslation(), In.GetScale());
}
void ConvertOperationalProperty(const FTransform& In, FIntermediate3DTransform& Out)
{
	FVector Location = In.GetTranslation();
	FRotator Rotation = In.GetRotation().Rotator();
	FVector Scale = In.GetScale3D();

	Out = FIntermediate3DTransform(Location, Rotation, Scale);
}


FIntermediate3DTransform GetComponentTransform(const UObject* Object)
{
	const USceneComponent* SceneComponent = CastChecked<const USceneComponent>(Object);
	FIntermediate3DTransform Result;
	ConvertOperationalProperty(SceneComponent->GetRelativeTransform(), Result);
	return Result;
}

void SetComponentTransform(USceneComponent* SceneComponent, const FIntermediate3DTransform& InTransform)
{
	// If this is a simulating component, teleport since sequencer takes over. 
	// Teleport will not have no velocity, but it's computed later by sequencer so that it will be correct for physics.
	// @todo: We would really rather not 
	AActor* Actor = SceneComponent->GetOwner();
	USceneComponent* RootComponent = Actor ? Actor->GetRootComponent() : nullptr;
	bool bIsSimulatingPhysics = RootComponent ? RootComponent->IsSimulatingPhysics() : false;

	FVector Translation = InTransform.GetTranslation();
	FRotator Rotation = InTransform.GetRotation();
	SceneComponent->SetRelativeLocationAndRotation(Translation, Rotation, false, nullptr, bIsSimulatingPhysics ? ETeleportType::ResetPhysics : ETeleportType::None);
	SceneComponent->SetRelativeScale3D(InTransform.GetScale());

	// Force the location and rotation values to avoid Rot->Quat->Rot conversions
	SceneComponent->SetRelativeLocation_Direct(Translation);
	SceneComponent->SetRelativeRotation_Direct(Rotation);
}

void SetComponentTransformAndVelocity(UObject* Object, const FIntermediate3DTransform& InTransform)
{
	InTransform.ApplyTo(CastChecked<USceneComponent>(Object));
}

void FIntermediate3DTransform::ApplyTo(USceneComponent* SceneComponent) const
{
	double DeltaTime = FApp::GetDeltaTime();
	if (DeltaTime <= 0)
	{
		SetComponentTransform(SceneComponent, *this);
	}
	else
	{
		/* Cache initial absolute position */
		FVector PreviousPosition = SceneComponent->GetComponentLocation();

		SetComponentTransform(SceneComponent, *this);

		/* Get current absolute position and set component velocity */
		FVector CurrentPosition = SceneComponent->GetComponentLocation();
		FVector ComponentVelocity = (CurrentPosition - PreviousPosition) / DeltaTime;
		SceneComponent->ComponentVelocity = ComponentVelocity;
	}
}

USceneComponent* FComponentAttachParamsDestination::ResolveAttachment(AActor* InParentActor) const
{
	if (SocketName != NAME_None)
	{
		if (ComponentName != NAME_None )
		{
			TInlineComponentArray<USceneComponent*> PotentialAttachComponents(InParentActor);
			for (USceneComponent* PotentialAttachComponent : PotentialAttachComponents)
			{
				if (PotentialAttachComponent->GetFName() == ComponentName && PotentialAttachComponent->DoesSocketExist(SocketName))
				{
					return PotentialAttachComponent;
				}
			}
		}
		else if (InParentActor->GetRootComponent()->DoesSocketExist(SocketName))
		{
			return InParentActor->GetRootComponent();
		}
	}
	else if (ComponentName != NAME_None )
	{
		TInlineComponentArray<USceneComponent*> PotentialAttachComponents(InParentActor);
		for (USceneComponent* PotentialAttachComponent : PotentialAttachComponents)
		{
			if (PotentialAttachComponent->GetFName() == ComponentName)
			{
				return PotentialAttachComponent;
			}
		}
	}

	if (InParentActor->GetDefaultAttachComponent())
	{
		return InParentActor->GetDefaultAttachComponent();
	}
	else
	{
		return InParentActor->GetRootComponent();
	}
}

void FComponentAttachParams::ApplyAttach(USceneComponent* ChildComponentToAttach, USceneComponent* NewAttachParent, const FName& SocketName) const
{
	if (ChildComponentToAttach->GetAttachParent() != NewAttachParent || ChildComponentToAttach->GetAttachSocketName() != SocketName)
	{
		FAttachmentTransformRules AttachmentRules(AttachmentLocationRule, AttachmentRotationRule, AttachmentScaleRule, false);

		ChildComponentToAttach->AttachToComponent(NewAttachParent, AttachmentRules, SocketName);
	}

	// Match the component velocity of the parent. If the attached child has any transformation, the velocity will be 
	// computed by the component transform system.
	if (ChildComponentToAttach->GetAttachParent())
	{
		ChildComponentToAttach->ComponentVelocity = ChildComponentToAttach->GetAttachParent()->GetComponentVelocity();
	}
}

void FComponentDetachParams::ApplyDetach(USceneComponent* ChildComponentToAttach, USceneComponent* NewAttachParent, const FName& SocketName) const
{
	// Detach if there was no pre-existing parent
	if (!NewAttachParent)
	{
		FDetachmentTransformRules DetachmentRules(DetachmentLocationRule, DetachmentRotationRule, DetachmentScaleRule, false);
		ChildComponentToAttach->DetachFromComponent(DetachmentRules);
	}
	else
	{
		ChildComponentToAttach->AttachToComponent(NewAttachParent, FAttachmentTransformRules::KeepRelativeTransform, SocketName);
	}
}


static bool GMovieSceneTracksComponentTypesDestroyed = false;
static TUniquePtr<FMovieSceneTracksComponentTypes> GMovieSceneTracksComponentTypes;

FMovieSceneTracksComponentTypes::FMovieSceneTracksComponentTypes()
{
	FComponentRegistry* ComponentRegistry = UMovieSceneEntitySystemLinker::GetComponents();

	// We purposefully do not use a preanimated token for component properties
	ComponentTransform.PropertyTag = ComponentRegistry->NewTag(TEXT("Component Transform"), EComponentTypeFlags::CopyToChildren);
	ComponentRegistry->NewComponentType(&ComponentTransform.InitialValue, TEXT("Initial Component Transform"), EComponentTypeFlags::Preserved);

	ComponentRegistry->NewPropertyType(Transform, TEXT("FTransform"));
	ComponentRegistry->NewPropertyType(EulerTransform, TEXT("FEulerTransform"));
	ComponentRegistry->NewPropertyType(Float, TEXT("float"));

	ComponentRegistry->NewComponentType(&QuaternionRotationChannel[0], TEXT("Quaternion Rotation Channel 0"));
	ComponentRegistry->NewComponentType(&QuaternionRotationChannel[1], TEXT("Quaternion Rotation Channel 1"));
	ComponentRegistry->NewComponentType(&QuaternionRotationChannel[2], TEXT("Quaternion Rotation Channel 2"));

	ComponentRegistry->NewComponentType(&AttachParent, TEXT("Attach Parent"));
	ComponentRegistry->NewComponentType(&AttachComponent, TEXT("Attachment Component"));
	ComponentRegistry->NewComponentType(&AttachParentBinding, TEXT("Attach Parent Binding"));

	ComponentRegistry->NewComponentType(&LevelVisibility, TEXT("Level Visibility"));

	FBuiltInComponentTypes* BuiltInComponents = FBuiltInComponentTypes::Get();

	// --------------------------------------------------------------------------------------------
	// Set up FTransform properties
	BuiltInComponents->PropertyRegistry.DefineCompositeProperty(Transform)
	.AddComposite<&FIntermediate3DTransform::T_X>(BuiltInComponents->FloatResult[0])
	.AddComposite<&FIntermediate3DTransform::T_Y>(BuiltInComponents->FloatResult[1])
	.AddComposite<&FIntermediate3DTransform::T_Z>(BuiltInComponents->FloatResult[2])
	.AddComposite<&FIntermediate3DTransform::R_X>(BuiltInComponents->FloatResult[3])
	.AddComposite<&FIntermediate3DTransform::R_Y>(BuiltInComponents->FloatResult[4])
	.AddComposite<&FIntermediate3DTransform::R_Z>(BuiltInComponents->FloatResult[5])
	.AddComposite<&FIntermediate3DTransform::S_X>(BuiltInComponents->FloatResult[6])
	.AddComposite<&FIntermediate3DTransform::S_Y>(BuiltInComponents->FloatResult[7])
	.AddComposite<&FIntermediate3DTransform::S_Z>(BuiltInComponents->FloatResult[8])
	.Commit();

	// --------------------------------------------------------------------------------------------
	// Set up FEulerTransform properties
	BuiltInComponents->PropertyRegistry.DefineCompositeProperty(EulerTransform)
	.AddComposite<&FIntermediate3DTransform::T_X>(BuiltInComponents->FloatResult[0])
	.AddComposite<&FIntermediate3DTransform::T_Y>(BuiltInComponents->FloatResult[1])
	.AddComposite<&FIntermediate3DTransform::T_Z>(BuiltInComponents->FloatResult[2])
	.AddComposite<&FIntermediate3DTransform::R_X>(BuiltInComponents->FloatResult[3])
	.AddComposite<&FIntermediate3DTransform::R_Y>(BuiltInComponents->FloatResult[4])
	.AddComposite<&FIntermediate3DTransform::R_Z>(BuiltInComponents->FloatResult[5])
	.AddComposite<&FIntermediate3DTransform::S_X>(BuiltInComponents->FloatResult[6])
	.AddComposite<&FIntermediate3DTransform::S_Y>(BuiltInComponents->FloatResult[7])
	.AddComposite<&FIntermediate3DTransform::S_Z>(BuiltInComponents->FloatResult[8])
	.Commit();

	// --------------------------------------------------------------------------------------------
	// Set up float properties
	BuiltInComponents->PropertyRegistry.DefineProperty(Float)
	.AddSoleChannel(BuiltInComponents->FloatResult[0])
	.SetCustomAccessors(&Accessors.Float)
	.Commit();

	// --------------------------------------------------------------------------------------------
	// Set up component transforms
	{
		Accessors.ComponentTransform.Add(USceneComponent::StaticClass(), "Transform", &GetComponentTransform, &SetComponentTransformAndVelocity);

		BuiltInComponents->PropertyRegistry.DefineCompositeProperty(ComponentTransform)
		.AddComposite<&FIntermediate3DTransform::T_X>(BuiltInComponents->FloatResult[0])
		.AddComposite<&FIntermediate3DTransform::T_Y>(BuiltInComponents->FloatResult[1])
		.AddComposite<&FIntermediate3DTransform::T_Z>(BuiltInComponents->FloatResult[2])
		.AddComposite<&FIntermediate3DTransform::R_X>(BuiltInComponents->FloatResult[3])
		.AddComposite<&FIntermediate3DTransform::R_Y>(BuiltInComponents->FloatResult[4])
		.AddComposite<&FIntermediate3DTransform::R_Z>(BuiltInComponents->FloatResult[5])
		.AddComposite<&FIntermediate3DTransform::S_X>(BuiltInComponents->FloatResult[6])
		.AddComposite<&FIntermediate3DTransform::S_Y>(BuiltInComponents->FloatResult[7])
		.AddComposite<&FIntermediate3DTransform::S_Z>(BuiltInComponents->FloatResult[8])
		.SetCustomAccessors(&Accessors.ComponentTransform)
		.Commit();
	}

	// --------------------------------------------------------------------------------------------
	// Set up quaternion rotation components
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(QuaternionRotationChannel); ++Index)
	{
		ComponentRegistry->Factories.DuplicateChildComponent(QuaternionRotationChannel[Index]);
		ComponentRegistry->Factories.DefineMutuallyInclusiveComponent(QuaternionRotationChannel[Index], BuiltInComponents->FloatResult[Index + 3]);
		ComponentRegistry->Factories.DefineMutuallyInclusiveComponent(QuaternionRotationChannel[Index], BuiltInComponents->EvalTime);
	}

	// --------------------------------------------------------------------------------------------
	// Set up attachment components
	ComponentRegistry->Factories.DefineChildComponent(AttachParentBinding, AttachParent);

	ComponentRegistry->Factories.DuplicateChildComponent(AttachParentBinding);
	ComponentRegistry->Factories.DuplicateChildComponent(AttachComponent);
}

FMovieSceneTracksComponentTypes::~FMovieSceneTracksComponentTypes()
{
}

void FMovieSceneTracksComponentTypes::Destroy()
{
	GMovieSceneTracksComponentTypes.Reset();
	GMovieSceneTracksComponentTypesDestroyed = true;
}

FMovieSceneTracksComponentTypes* FMovieSceneTracksComponentTypes::Get()
{
	if (!GMovieSceneTracksComponentTypes.IsValid())
	{
		check(!GMovieSceneTracksComponentTypesDestroyed);
		GMovieSceneTracksComponentTypes.Reset(new FMovieSceneTracksComponentTypes);
	}
	return GMovieSceneTracksComponentTypes.Get();
}


} // namespace MovieScene
} // namespace UE
