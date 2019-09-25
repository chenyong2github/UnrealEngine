// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ControlRigGizmoLibrary.h"

#define LOCTEXT_NAMESPACE "ControlRigGizmoLibrary"

UControlRigGizmoLibrary::UControlRigGizmoLibrary()
{
}

#if WITH_EDITOR

// UObject interface
void UControlRigGizmoLibrary::PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.Property->GetName() == TEXT("GizmoName"))
	{
		UProperty* MemberProperty = PropertyChangedEvent.PropertyChain.GetHead()->GetValue();
		if (MemberProperty->GetName() == TEXT("DefaultGizmo"))
		{
			DefaultGizmo.GizmoName = TEXT("Gizmo");
			GetUpdatedNameList(true);
		}
		else if(MemberProperty->GetName() == TEXT("Gizmos"))
		{
			if (Gizmos.Num() == 0)
			{
				return;
			}

			int32 GizmoIndexEdited = PropertyChangedEvent.GetArrayIndex(TEXT("Gizmos"));
			if (Gizmos.IsValidIndex(GizmoIndexEdited))
			{
				TArray<FName> Names;
				Names.Add(DefaultGizmo.GizmoName);
				for (int32 GizmoIndex = 0; GizmoIndex < Gizmos.Num(); GizmoIndex++)
				{
					if (GizmoIndex != GizmoIndexEdited)
					{
						Names.Add(Gizmos[GizmoIndex].GizmoName);
					}
				}

				FName DesiredName = Gizmos[GizmoIndexEdited].GizmoName;
				FString Name = DesiredName.ToString();
				int32 Suffix = 0;
				while (Names.Contains(*Name))
				{
					Suffix++;
					Name = FString::Printf(TEXT("%s_%d"), *DesiredName.ToString(), Suffix);
				}
				Gizmos[GizmoIndexEdited].GizmoName = *Name;
			}
			GetUpdatedNameList(true);
		}
	}
	else if (PropertyChangedEvent.Property->GetName() == TEXT("Gizmos"))
	{
		TArray<FName> Names;
		Names.Add(DefaultGizmo.GizmoName);
		for (int32 GizmoIndex = 0; GizmoIndex < Gizmos.Num(); GizmoIndex++)
		{
			FName DesiredName = Gizmos[GizmoIndex].GizmoName;
			FString Name = DesiredName.ToString();
			int32 Suffix = 0;
			while (Names.Contains(*Name))
			{
				Suffix++;
				Name = FString::Printf(TEXT("%s_%d"), *DesiredName.ToString(), Suffix);
			}
			Gizmos[GizmoIndex].GizmoName = *Name;

			Names.Add(Gizmos[GizmoIndex].GizmoName);
		}
		GetUpdatedNameList(true);
	}
}

#endif

const FControlRigGizmoDefinition* UControlRigGizmoLibrary::GetGizmoByName(const FName& InName, bool bUseDefaultIfNotFound) const
{
	if (InName == DefaultGizmo.GizmoName)
	{
		return &DefaultGizmo;
	}

	for (int32 GizmoIndex = 0; GizmoIndex < Gizmos.Num(); GizmoIndex++)
	{
		if (Gizmos[GizmoIndex].GizmoName == InName)
		{
			return &Gizmos[GizmoIndex];
		}
	}

	if (bUseDefaultIfNotFound)
	{
		return &DefaultGizmo;
	}

	return nullptr;
}

const TArray<FName> UControlRigGizmoLibrary::GetUpdatedNameList(bool bReset)
{
	if (bReset)
	{
		NameList.Reset();
	}

	if (NameList.Num() != Gizmos.Num())
	{
		NameList.Reset();
		for (const FControlRigGizmoDefinition& Gizmo : Gizmos)
		{
			NameList.Add(Gizmo.GizmoName);
		}
		NameList.Sort(FNameLexicalLess());
	}

	return NameList;
}

#undef LOCTEXT_NAMESPACE
