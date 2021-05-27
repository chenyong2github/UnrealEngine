// Copyright Epic Games, Inc. All Rights Reserved.

#include "BaseGizmos/TransformGizmoUtil.h"

#include "ContextObjectStore.h"
#include "InteractiveToolsContext.h"
#include "InteractiveGizmoManager.h"
#include "InteractiveToolManager.h"

#include "BaseGizmos/AxisPositionGizmo.h"
#include "BaseGizmos/PlanePositionGizmo.h"
#include "BaseGizmos/AxisAngleGizmo.h"
#include "BaseGizmos/TransformGizmo.h"
#include "BaseGizmos/RepositionableTransformGizmo.h"

const FString UTransformGizmoContextObject::DefaultAxisPositionBuilderIdentifier = TEXT("Util_StandardXFormAxisTranslationGizmo");
const FString UTransformGizmoContextObject::DefaultPlanePositionBuilderIdentifier = TEXT("Util_StandardXFormPlaneTranslationGizmo");
const FString UTransformGizmoContextObject::DefaultAxisAngleBuilderIdentifier = TEXT("Util_StandardXFormAxisRotationGizmo");
const FString UTransformGizmoContextObject::DefaultThreeAxisTransformBuilderIdentifier = TEXT("Util_DefaultThreeAxisTransformBuilderIdentifier");
const FString UTransformGizmoContextObject::CustomThreeAxisTransformBuilderIdentifier = TEXT("Util_CustomThreeAxisTransformBuilderIdentifier");
const FString UTransformGizmoContextObject::CustomRepositionableThreeAxisTransformBuilderIdentifier = TEXT("Util_CustomRepositionableThreeAxisTransformBuilderIdentifier");


void UTransformGizmoContextObject::RegisterGizmosWithManager(UInteractiveToolManager* ToolManager)
{
	if (ensure(!bDefaultGizmosRegistered) == false)
	{
		return;
	}

	UInteractiveGizmoManager* GizmoManager = ToolManager->GetPairedGizmoManager();
	ToolManager->GetContextObjectStore()->AddContextObject(this);

	UAxisPositionGizmoBuilder* AxisTranslationBuilder = NewObject<UAxisPositionGizmoBuilder>();
	GizmoManager->RegisterGizmoType(DefaultAxisPositionBuilderIdentifier, AxisTranslationBuilder);

	UPlanePositionGizmoBuilder* PlaneTranslationBuilder = NewObject<UPlanePositionGizmoBuilder>();
	GizmoManager->RegisterGizmoType(DefaultPlanePositionBuilderIdentifier, PlaneTranslationBuilder);

	UAxisAngleGizmoBuilder* AxisRotationBuilder = NewObject<UAxisAngleGizmoBuilder>();
	GizmoManager->RegisterGizmoType(DefaultAxisAngleBuilderIdentifier, AxisRotationBuilder);

	UTransformGizmoBuilder* TransformBuilder = NewObject<UTransformGizmoBuilder>();
	TransformBuilder->AxisPositionBuilderIdentifier = DefaultAxisPositionBuilderIdentifier;
	TransformBuilder->PlanePositionBuilderIdentifier = DefaultPlanePositionBuilderIdentifier;
	TransformBuilder->AxisAngleBuilderIdentifier = DefaultAxisAngleBuilderIdentifier;
	GizmoManager->RegisterGizmoType(DefaultThreeAxisTransformBuilderIdentifier, TransformBuilder);

	GizmoActorBuilder = MakeShared<FTransformGizmoActorFactory>();

	UTransformGizmoBuilder* CustomThreeAxisBuilder = NewObject<UTransformGizmoBuilder>();
	CustomThreeAxisBuilder->AxisPositionBuilderIdentifier = DefaultAxisPositionBuilderIdentifier;
	CustomThreeAxisBuilder->PlanePositionBuilderIdentifier = DefaultPlanePositionBuilderIdentifier;
	CustomThreeAxisBuilder->AxisAngleBuilderIdentifier = DefaultAxisAngleBuilderIdentifier;
	CustomThreeAxisBuilder->GizmoActorBuilder = GizmoActorBuilder;
	GizmoManager->RegisterGizmoType(CustomThreeAxisTransformBuilderIdentifier, CustomThreeAxisBuilder);

	URepositionableTransformGizmoBuilder* CustomRepositionableThreeAxisBuilder = NewObject<URepositionableTransformGizmoBuilder>();
	CustomRepositionableThreeAxisBuilder->AxisPositionBuilderIdentifier = DefaultAxisPositionBuilderIdentifier;
	CustomRepositionableThreeAxisBuilder->PlanePositionBuilderIdentifier = DefaultPlanePositionBuilderIdentifier;
	CustomRepositionableThreeAxisBuilder->AxisAngleBuilderIdentifier = DefaultAxisAngleBuilderIdentifier;
	CustomRepositionableThreeAxisBuilder->GizmoActorBuilder = GizmoActorBuilder;
	GizmoManager->RegisterGizmoType(CustomRepositionableThreeAxisTransformBuilderIdentifier, CustomRepositionableThreeAxisBuilder);

	bDefaultGizmosRegistered = true;


}

void UTransformGizmoContextObject::DeregisterGizmosWithManager(UInteractiveToolManager* ToolManager)
{
	UInteractiveGizmoManager* GizmoManager = ToolManager->GetPairedGizmoManager();
	ToolManager->GetContextObjectStore()->RemoveContextObject(this);

	ensure(bDefaultGizmosRegistered);
	GizmoManager->DeregisterGizmoType(DefaultAxisPositionBuilderIdentifier);
	GizmoManager->DeregisterGizmoType(DefaultPlanePositionBuilderIdentifier);
	GizmoManager->DeregisterGizmoType(DefaultAxisAngleBuilderIdentifier);
	GizmoManager->DeregisterGizmoType(DefaultThreeAxisTransformBuilderIdentifier);
	GizmoManager->DeregisterGizmoType(CustomThreeAxisTransformBuilderIdentifier);
	GizmoManager->DeregisterGizmoType(CustomRepositionableThreeAxisTransformBuilderIdentifier);
	bDefaultGizmosRegistered = false;
}




bool UE::TransformGizmoUtil::RegisterTransformGizmoContextObject(UInteractiveToolsContext* ToolsContext)
{
	if (ensure(ToolsContext))
	{
		UTransformGizmoContextObject* Found = ToolsContext->ContextObjectStore->FindContext<UTransformGizmoContextObject>();
		if (Found == nullptr)
		{
			UTransformGizmoContextObject* GizmoHelper = NewObject<UTransformGizmoContextObject>(ToolsContext->ToolManager);
			if (ensure(GizmoHelper))
			{
				GizmoHelper->RegisterGizmosWithManager(ToolsContext->ToolManager);
				return true;
			}
			else
			{
				return false;
			}
		}
		return true;
	}
	return false;
}


bool UE::TransformGizmoUtil::DeregisterTransformGizmoContextObject(UInteractiveToolsContext* ToolsContext)
{
	if (ensure(ToolsContext))
	{
		UTransformGizmoContextObject* Found = ToolsContext->ContextObjectStore->FindContext<UTransformGizmoContextObject>();
		if (Found != nullptr)
		{
			Found->DeregisterGizmosWithManager(ToolsContext->ToolManager);
			ToolsContext->ContextObjectStore->RemoveContextObject(Found);
		}
		return true;
	}
	return false;
}



UTransformGizmo* UTransformGizmoContextObject::Create3AxisTransformGizmo(UInteractiveGizmoManager* GizmoManager, void* Owner, const FString& InstanceIdentifier)
{
	if (ensure(bDefaultGizmosRegistered))
	{
		UInteractiveGizmo* NewGizmo = GizmoManager->CreateGizmo(DefaultThreeAxisTransformBuilderIdentifier, InstanceIdentifier, Owner);
		ensure(NewGizmo);
		return Cast<UTransformGizmo>(NewGizmo);
	}
	return nullptr;
}
UTransformGizmo* UE::TransformGizmoUtil::Create3AxisTransformGizmo(UInteractiveGizmoManager* GizmoManager, void* Owner, const FString& InstanceIdentifier)
{
	if (ensure(GizmoManager))
	{
		UTransformGizmoContextObject* UseThis = GizmoManager->GetContextObjectStore()->FindContext<UTransformGizmoContextObject>();
		if (ensure(UseThis))
		{
			return UseThis->Create3AxisTransformGizmo(GizmoManager, Owner, InstanceIdentifier);
		}
	}
	return nullptr;
}
UTransformGizmo* UE::TransformGizmoUtil::Create3AxisTransformGizmo(UInteractiveToolManager* ToolManager, void* Owner, const FString& InstanceIdentifier)
{
	if (ensure(ToolManager))
	{
		return Create3AxisTransformGizmo(ToolManager->GetPairedGizmoManager(), Owner, InstanceIdentifier);
	}
	return nullptr;
}

UTransformGizmo* UTransformGizmoContextObject::CreateCustomTransformGizmo(UInteractiveGizmoManager* GizmoManager, ETransformGizmoSubElements Elements, void* Owner, const FString& InstanceIdentifier)
{
	if (ensure(bDefaultGizmosRegistered))
	{
		GizmoActorBuilder->EnableElements = Elements;
		UInteractiveGizmo* NewGizmo = GizmoManager->CreateGizmo(CustomThreeAxisTransformBuilderIdentifier, InstanceIdentifier, Owner);
		ensure(NewGizmo);
		return Cast<UTransformGizmo>(NewGizmo);
	}
	return nullptr;
}
UTransformGizmo* UE::TransformGizmoUtil::CreateCustomTransformGizmo(UInteractiveGizmoManager* GizmoManager, ETransformGizmoSubElements Elements, void* Owner, const FString& InstanceIdentifier)
{
	if (ensure(GizmoManager))
	{
		UTransformGizmoContextObject* UseThis = GizmoManager->GetContextObjectStore()->FindContext<UTransformGizmoContextObject>();
		if (ensure(UseThis))
		{
			return UseThis->CreateCustomTransformGizmo(GizmoManager, Elements, Owner, InstanceIdentifier);
		}
	}
	return nullptr;
}
UTransformGizmo* UE::TransformGizmoUtil::CreateCustomTransformGizmo(UInteractiveToolManager* ToolManager, ETransformGizmoSubElements Elements, void* Owner, const FString& InstanceIdentifier)
{
	if (ensure(ToolManager))
	{
		return CreateCustomTransformGizmo(ToolManager->GetPairedGizmoManager(), Elements, Owner, InstanceIdentifier);
	}
	return nullptr;
}


UTransformGizmo* UTransformGizmoContextObject::CreateCustomRepositionableTransformGizmo(UInteractiveGizmoManager* GizmoManager, ETransformGizmoSubElements Elements, void* Owner, const FString& InstanceIdentifier)
{
	if (ensure(bDefaultGizmosRegistered))
	{
		GizmoActorBuilder->EnableElements = Elements;
		UInteractiveGizmo* NewGizmo = GizmoManager->CreateGizmo(CustomRepositionableThreeAxisTransformBuilderIdentifier, InstanceIdentifier, Owner);
		ensure(NewGizmo);
		return Cast<UTransformGizmo>(NewGizmo);
	}
	return nullptr;
}
UTransformGizmo* UE::TransformGizmoUtil::CreateCustomRepositionableTransformGizmo(UInteractiveGizmoManager* GizmoManager, ETransformGizmoSubElements Elements, void* Owner, const FString& InstanceIdentifier)
{
	if (ensure(GizmoManager))
	{
		UTransformGizmoContextObject* UseThis = GizmoManager->GetContextObjectStore()->FindContext<UTransformGizmoContextObject>();
		if (ensure(UseThis))
		{
			return UseThis->CreateCustomRepositionableTransformGizmo(GizmoManager, Elements, Owner, InstanceIdentifier);
		}
	}
	return nullptr;
}
UTransformGizmo* UE::TransformGizmoUtil::CreateCustomRepositionableTransformGizmo(UInteractiveToolManager* ToolManager, ETransformGizmoSubElements Elements, void* Owner, const FString& InstanceIdentifier)
{
	if (ensure(ToolManager))
	{
		return CreateCustomRepositionableTransformGizmo(ToolManager->GetPairedGizmoManager(), Elements, Owner, InstanceIdentifier);
	}
	return nullptr;
}
