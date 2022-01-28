// Copyright Epic Games, Inc. All Rights Reserved.

#include "SaveContext.h"

#include "Serialization/PackageWriter.h"



TArray<ESaveRealm> FSaveContext::GetHarvestedRealmsToSave()
{
	TArray<ESaveRealm> HarvestedContextsToSave;
	if (IsCooking())
	{
		HarvestedContextsToSave.Add(ESaveRealm::Game);
		if (IsSaveOptional())
		{
			HarvestedContextsToSave.Add(ESaveRealm::Optional);
		}
	}
	else
	{
		HarvestedContextsToSave.Add(ESaveRealm::Editor);
	}
	return HarvestedContextsToSave;
}

void FSaveContext::MarkUnsaveable(UObject* InObject)
{
	if (IsUnsaveable(InObject))
	{
		InObject->SetFlags(RF_Transient);
	}

	// if this is the class default object, make sure it's not
	// marked transient for any reason, as we need it to be saved
	// to disk (unless it's associated with a transient generated class)
#if WITH_EDITORONLY_DATA
	ensureAlways(!InObject->HasAllFlags(RF_ClassDefaultObject | RF_Transient) || (InObject->GetClass()->ClassGeneratedBy != nullptr && InObject->GetClass()->HasAnyFlags(RF_Transient)));
#endif
}

bool FSaveContext::IsUnsaveable(UObject* InObject, bool bEmitWarning) const
{
	UObject* Obj = InObject;
	while (Obj)
	{
		// pending kill object are unsaveable
		if (!IsValidChecked(Obj))
		{
			return true;
		}

		// transient object are considered unsaveable if non native
		if (Obj->HasAnyFlags(RF_Transient) && !Obj->IsNative())
		{
			return true;
		}

		// if the object class is abstract, has been marked as deprecated, there is a newer version that exist, or the class is marked transient, then the object is unsaveable
		// @note: Although object instances of a transient class should definitely be unsaveable, it results in discrepancies with the old save algorithm and currently load problems
		if (Obj->GetClass()->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists /*| CLASS_Transient*/) 
			&& !Obj->HasAnyFlags(RF_ClassDefaultObject))
		{
			// only warn if the base object is fine but the outer is invalid. If an object is itself unsaveable, the old behavior is to ignore it
			if (bEmitWarning
				&& IsValidChecked(Obj)
				&& InObject->GetOutermost() == GetPackage()
				&& Obj != InObject)
			{
				UE_LOG(LogSavePackage, Warning, TEXT("%s has a deprecated or abstract class outer %s, so it will not be saved"), *InObject->GetFullName(), *Obj->GetFullName());
			}

			// there used to be a check for reference if the class had the CLASS_HasInstancedReference,
			// those reference were outer-ed to the object being flagged as unsaveable, making them unsaveable as well without having to look for them
			return true;
		}

		Obj = Obj->GetOuter();
	}
	return false;	
}
