// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusResourceDescription.h"

#include "OptimusDeformer.h"
#include "OptimusHelpers.h"


UOptimusDeformer* UOptimusResourceDescription::GetOwningDeformer() const
{
	const UOptimusResourceContainer* Container = CastChecked<UOptimusResourceContainer>(GetOuter());
	return Container ? CastChecked<UOptimusDeformer>(Container->GetOuter()) : nullptr;
}


#if WITH_EDITOR
void UOptimusResourceDescription::PostEditChangeProperty(
	FPropertyChangedEvent& PropertyChangedEvent
	)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	
	const FName PropertyName = PropertyChangedEvent.GetPropertyName();

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UOptimusResourceDescription, ResourceName))
	{
		UOptimusDeformer *Deformer = GetOwningDeformer();
		if (ensure(Deformer))
		{
			// Rename the object itself and update the nodes. A lot of this is covered by
			// UOptimusDeformer::RenameResource but since we're inside of a transaction, which
			// has already taken a snapshot of this object, we have to do the remaining 
			// operations on this object under the transaction scope.
			ResourceName = Optimus::GetUniqueNameForScope(GetOuter(), ResourceName);
			Rename(*ResourceName.ToString(), nullptr);
			
			constexpr bool bForceChange = true;
			Deformer->RenameResource(this, ResourceName, bForceChange);
		}
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(FOptimusDataTypeRef, TypeName))
	{
		UOptimusDeformer *Deformer = GetOwningDeformer();
		if (ensure(Deformer))
		{
			// Set the resource type again, so that we can remove any links that are now
			// type-incompatible.
			constexpr bool bForceChange = true;
			Deformer->SetResourceDataType(this, DataType, bForceChange);
		}
	}
}


void UOptimusResourceDescription::PreEditUndo()
{
	Super::PreEditUndo();

	ResourceNameForUndo = ResourceName;
}


void UOptimusResourceDescription::PostEditUndo()
{
	Super::PostEditUndo();

	if (ResourceNameForUndo != ResourceName)
	{
		const UOptimusDeformer *Deformer = GetOwningDeformer();
		Deformer->Notify(EOptimusGlobalNotifyType::ResourceRenamed, this);
	}
}

#endif
