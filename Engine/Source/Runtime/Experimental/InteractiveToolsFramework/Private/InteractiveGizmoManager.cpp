// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


#include "InteractiveGizmoManager.h"
#include "InteractiveToolsContext.h"

#include "BaseGizmos/AxisPositionGizmo.h"
#include "BaseGizmos/PlanePositionGizmo.h"
#include "BaseGizmos/AxisAngleGizmo.h"
#include "BaseGizmos/TransformGizmo.h"


UInteractiveGizmoManager::UInteractiveGizmoManager()
{
	QueriesAPI = nullptr;
	TransactionsAPI = nullptr;
	InputRouter = nullptr;
}


void UInteractiveGizmoManager::Initialize(IToolsContextQueriesAPI* QueriesAPIIn, IToolsContextTransactionsAPI* TransactionsAPIIn, UInputRouter* InputRouterIn)
{
	this->QueriesAPI = QueriesAPIIn;
	this->TransactionsAPI = TransactionsAPIIn;
	this->InputRouter = InputRouterIn;
}


void UInteractiveGizmoManager::Shutdown()
{
	this->QueriesAPI = nullptr;

	TArray<FActiveGizmo> AllGizmos = ActiveGizmos;
	for (FActiveGizmo& ActiveGizmo : AllGizmos)
	{
		DestroyGizmo(ActiveGizmo.Gizmo);
	}
	ActiveGizmos.Reset();

	this->TransactionsAPI = nullptr;

	if (bDefaultGizmosRegistered)
	{
		DeregisterGizmoType(DefaultAxisPositionBuilderIdentifier);
		DeregisterGizmoType(DefaultPlanePositionBuilderIdentifier);
		DeregisterGizmoType(DefaultAxisAngleBuilderIdentifier);
		DeregisterGizmoType(DefaultThreeAxisTransformBuilderIdentifier);
	}
}



void UInteractiveGizmoManager::RegisterGizmoType(const FString& Identifier, UInteractiveGizmoBuilder* Builder)
{
	check(GizmoBuilders.Contains(Identifier) == false);
	GizmoBuilders.Add(Identifier, Builder );
}


bool UInteractiveGizmoManager::DeregisterGizmoType(const FString& BuilderIdentifier)
{
	if (GizmoBuilders.Contains(BuilderIdentifier) == false)
	{
		PostMessage(FString::Printf(TEXT("UInteractiveGizmoManager::DeregisterGizmoType: could not find requested type %s"), *BuilderIdentifier), EToolMessageLevel::Internal);
		return false;
	}
	GizmoBuilders.Remove(BuilderIdentifier);
	return true;
}




UInteractiveGizmo* UInteractiveGizmoManager::CreateGizmo(const FString& BuilderIdentifier, const FString& InstanceIdentifier, void* Owner)
{
	if ( GizmoBuilders.Contains(BuilderIdentifier) == false )
	{
		PostMessage(FString::Printf(TEXT("UInteractiveGizmoManager::CreateGizmo: could not find requested type %s"), *BuilderIdentifier), EToolMessageLevel::Internal);
		return nullptr;
	}
	UInteractiveGizmoBuilder* FoundBuilder = GizmoBuilders[BuilderIdentifier];

	// check if we have used this instance identifier
	for (FActiveGizmo& ActiveGizmo : ActiveGizmos)
	{
		if (ActiveGizmo.InstanceIdentifier == InstanceIdentifier)
		{
			PostMessage(FString::Printf(TEXT("UInteractiveGizmoManager::CreateGizmo: instance identifier %s already in use!"), *InstanceIdentifier), EToolMessageLevel::Internal);
			return nullptr;
		}
	}

	FToolBuilderState CurrentSceneState;
	QueriesAPI->GetCurrentSelectionState(CurrentSceneState);

	UInteractiveGizmo* NewGizmo = FoundBuilder->BuildGizmo(CurrentSceneState);
	if (NewGizmo == nullptr)
	{
		PostMessage(FString::Printf(TEXT("UInteractiveGizmoManager::CreateGizmo: BuildGizmo() returned null")), EToolMessageLevel::Internal);
		return nullptr;
	}

	NewGizmo->Setup();

	// register new active input behaviors
	InputRouter->RegisterSource(NewGizmo);

	PostInvalidation();

	FActiveGizmo ActiveGizmo = { NewGizmo, BuilderIdentifier, InstanceIdentifier, Owner };
	ActiveGizmos.Add(ActiveGizmo);

	return NewGizmo;
}



bool UInteractiveGizmoManager::DestroyGizmo(UInteractiveGizmo* Gizmo)
{
	int FoundIndex = -1;
	for ( int i = 0; i < ActiveGizmos.Num(); ++i )
	{
		if (ActiveGizmos[i].Gizmo == Gizmo)
		{
			FoundIndex = i;
			break;
		}
	}
	if (FoundIndex == -1)
	{
		return false;
	}

	InputRouter->ForceTerminateSource(Gizmo);

	Gizmo->Shutdown();

	InputRouter->DeregisterSource(Gizmo);

	ActiveGizmos.RemoveAt(FoundIndex);

	PostInvalidation();

	return true;
}




TArray<UInteractiveGizmo*> UInteractiveGizmoManager::FindAllGizmosOfType(const FString& BuilderIdentifier)
{
	TArray<UInteractiveGizmo*> Found;
	for (int i = 0; i < ActiveGizmos.Num(); ++i)
	{
		if (ActiveGizmos[i].BuilderIdentifier == BuilderIdentifier)
		{
			Found.Add(ActiveGizmos[i].Gizmo);
		}
	}
	return Found;
}


void UInteractiveGizmoManager::DestroyAllGizmosOfType(const FString& BuilderIdentifier)
{
	TArray<UInteractiveGizmo*> ToRemove = FindAllGizmosOfType(BuilderIdentifier);

	for (int i = 0; i < ToRemove.Num(); ++i)
	{
		DestroyGizmo(ToRemove[i]);
	}
}


void UInteractiveGizmoManager::DestroyAllGizmosByOwner(void* Owner)
{
	TArray<UInteractiveGizmo*> Found;
	for ( const FActiveGizmo& ActiveGizmo : ActiveGizmos )
	{
		if (ActiveGizmo.Owner == Owner)
		{
			Found.Add(ActiveGizmo.Gizmo);
		}
	}
	for (UInteractiveGizmo* Gizmo : Found)
	{
		DestroyGizmo(Gizmo);
	}
}



UInteractiveGizmo* UInteractiveGizmoManager::FindGizmoByInstanceIdentifier(const FString& Identifier)
{
	for (int i = 0; i < ActiveGizmos.Num(); ++i)
	{
		if (ActiveGizmos[i].InstanceIdentifier == Identifier)
		{
			return ActiveGizmos[i].Gizmo;
		}
	}
	return nullptr;
}



void UInteractiveGizmoManager::Tick(float DeltaTime)
{
	for (FActiveGizmo& ActiveGizmo : ActiveGizmos)
	{
		ActiveGizmo.Gizmo->Tick(DeltaTime);
	}
}


void UInteractiveGizmoManager::Render(IToolsContextRenderAPI* RenderAPI)
{
	for (FActiveGizmo& ActiveGizmo : ActiveGizmos)
	{
		ActiveGizmo.Gizmo->Render(RenderAPI);
	}

}

void UInteractiveGizmoManager::PostMessage(const TCHAR* Message, EToolMessageLevel Level)
{
	TransactionsAPI->PostMessage(Message, Level);
}

void UInteractiveGizmoManager::PostMessage(const FString& Message, EToolMessageLevel Level)
{
	TransactionsAPI->PostMessage(*Message, Level);
}

void UInteractiveGizmoManager::PostInvalidation()
{
	TransactionsAPI->PostInvalidation();
}


void UInteractiveGizmoManager::BeginUndoTransaction(const FText& Description)
{
	TransactionsAPI->BeginUndoTransaction(Description);
}

void UInteractiveGizmoManager::EndUndoTransaction()
{
	TransactionsAPI->EndUndoTransaction();
}



void UInteractiveGizmoManager::EmitObjectChange(UObject* TargetObject, TUniquePtr<FChange> Change, const FText& Description)
{
	TransactionsAPI->AppendChange(TargetObject, MoveTemp(Change), Description );
}




FString UInteractiveGizmoManager::DefaultAxisPositionBuilderIdentifier = TEXT("StandardXFormAxisTranslationGizmo");
FString UInteractiveGizmoManager::DefaultPlanePositionBuilderIdentifier = TEXT("StandardXFormPlaneTranslationGizmo");
FString UInteractiveGizmoManager::DefaultAxisAngleBuilderIdentifier = TEXT("StandardXFormAxisRotationGizmo");
FString UInteractiveGizmoManager::DefaultThreeAxisTransformBuilderIdentifier = TEXT("DefaultThreeAxisTransformBuilderIdentifier");


void UInteractiveGizmoManager::RegisterDefaultGizmos()
{
	check(bDefaultGizmosRegistered == false);

	UAxisPositionGizmoBuilder* AxisTranslationBuilder = NewObject<UAxisPositionGizmoBuilder>();
	RegisterGizmoType(DefaultAxisPositionBuilderIdentifier, AxisTranslationBuilder);

	UPlanePositionGizmoBuilder* PlaneTranslationBuilder = NewObject<UPlanePositionGizmoBuilder>();
	RegisterGizmoType(DefaultPlanePositionBuilderIdentifier, PlaneTranslationBuilder);

	UAxisAngleGizmoBuilder* AxisRotationBuilder = NewObject<UAxisAngleGizmoBuilder>();
	RegisterGizmoType(DefaultAxisAngleBuilderIdentifier, AxisRotationBuilder);

	UTransformGizmoBuilder* TransformBuilder = NewObject<UTransformGizmoBuilder>();
	RegisterGizmoType(DefaultThreeAxisTransformBuilderIdentifier, TransformBuilder);

	bDefaultGizmosRegistered = true;
}

UTransformGizmo* UInteractiveGizmoManager::Create3AxisTransformGizmo(const FString& InstanceIdentifier, void* Owner)
{
	check(bDefaultGizmosRegistered);
	UInteractiveGizmo* NewGizmo = CreateGizmo(DefaultThreeAxisTransformBuilderIdentifier, InstanceIdentifier, Owner);
	check(NewGizmo);
	return Cast<UTransformGizmo>(NewGizmo);
}