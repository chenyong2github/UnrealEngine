// Copyright Epic Games, Inc. All Rights Reserved.

#include "SkeletalMeshEditorUtils.h"

#include "ContextObjectStore.h"
#include "InteractiveToolsContext.h"
#include "IPersonaToolkit.h"
#include "ISkeletalMeshEditor.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(SkeletalMeshEditorUtils)

bool UE::SkeletalMeshEditorUtils::RegisterEditorContextObject(UInteractiveToolsContext* ToolsContext)
{
	if (ensure(ToolsContext))
	{
		const USkeletalMeshEditorContextObject* Found = ToolsContext->ContextObjectStore->FindContext<USkeletalMeshEditorContextObject>();
		if (Found)
		{
			return true;
		}
		
		USkeletalMeshEditorContextObject* ContextObject = NewObject<USkeletalMeshEditorContextObject>(ToolsContext->ToolManager);
		if (ensure(ContextObject))
		{
			ContextObject->Register(ToolsContext->ToolManager);
			return true;
		}
	}
	return false;
}

bool UE::SkeletalMeshEditorUtils::UnregisterEditorContextObject(UInteractiveToolsContext* ToolsContext)
{
	if (ensure(ToolsContext))
	{
		USkeletalMeshEditorContextObject* Found = ToolsContext->ContextObjectStore->FindContext<USkeletalMeshEditorContextObject>();
		if (Found != nullptr)
		{
			Found->Unregister(ToolsContext->ToolManager);
			ToolsContext->ContextObjectStore->RemoveContextObject(Found);
		}
		return true;
	}
	return false;
}

USkeletalMeshEditorContextObject* UE::SkeletalMeshEditorUtils::GetEditorContextObject(UInteractiveToolsContext* ToolsContext)
{
	return ToolsContext->ContextObjectStore->FindContext<USkeletalMeshEditorContextObject>();
}

void USkeletalMeshEditorContextObject::Register(UInteractiveToolManager* InToolManager)
{
	if (ensure(!bRegistered) == false)
	{
		return;
	}

	InToolManager->GetContextObjectStore()->AddContextObject(this);
	bRegistered = true;
}

void USkeletalMeshEditorContextObject::Unregister(UInteractiveToolManager* InToolManager)
{
	ensure(bRegistered);
	
	InToolManager->GetContextObjectStore()->RemoveContextObject(this);

	Bindings.Reset();
	
	bRegistered = false;
}

void USkeletalMeshEditorContextObject::Init(const TWeakPtr<ISkeletalMeshEditor>& InEditor)
{
	Editor = InEditor;
	Bindings.Reset();
}

void USkeletalMeshEditorContextObject::HideSkeleton()
{
	if (!Editor.IsValid())
	{
		return;
	}

	const TSharedPtr<ISkeletalMeshEditor> SkeletalMeshEditor = Editor.Pin();
	UDebugSkelMeshComponent* SkeletalMeshComponent = SkeletalMeshEditor->GetPersonaToolkit()->GetPreviewMeshComponent();
	if (!SkeletalMeshComponent)
	{
		return;
	}
	
	SkeletonDrawMode = SkeletalMeshComponent->SkeletonDrawMode;
	SkeletalMeshComponent->SkeletonDrawMode = ESkeletonDrawMode::Hidden;
}

void USkeletalMeshEditorContextObject::ShowSkeleton()
{
	if (!Editor.IsValid())
	{
		return;
	}
	
	const TSharedPtr<ISkeletalMeshEditor> SkeletalMeshEditor = Editor.Pin();
	UDebugSkelMeshComponent* SkeletalMeshComponent = SkeletalMeshEditor->GetPersonaToolkit()->GetPreviewMeshComponent();
	if (!SkeletalMeshComponent)
	{
		return;
	}
	
	SkeletalMeshComponent->SkeletonDrawMode = SkeletonDrawMode;
	SkeletonDrawMode = ESkeletonDrawMode::Default;
}

void USkeletalMeshEditorContextObject::BindTo(ISkeletalMeshEditingInterface* InEditingInterface)
{
	if (!InEditingInterface || !Editor.IsValid())
	{
		return;
	}
	
	if (Bindings.Contains(InEditingInterface))
	{
		return;
	}
	
	TSharedPtr<ISkeletalMeshEditorBinding> Binding = Editor.Pin()->GetBinding();
	if (Binding.IsValid())
	{
		InEditingInterface->BindTo(Binding);
	
		FBindData BindData;
	
		// connect external interface to tool (ie skeletal mesh editor -> tool)
		BindData.ToToolNotifierHandle = Binding->GetNotifier().Delegate().AddLambda(
			[InEditingInterface](const TArray<FName>& BoneNames, const ESkeletalMeshNotifyType InNotifyType)
		{
			InEditingInterface->GetNotifier().HandleNotification(BoneNames, InNotifyType);
		});
	
		// connect too to external interface (ie tool -> skeletal mesh editor)
		BindData.FromToolNotifierHandle = InEditingInterface->GetNotifier().Delegate().AddLambda(
			[Binding](const TArray<FName>& BoneNames, const ESkeletalMeshNotifyType InNotifyType)
		{
			Binding->GetNotifier().HandleNotification(BoneNames, InNotifyType);
		});
		Bindings.Emplace(InEditingInterface, BindData);
	
		InEditingInterface->GetNotifier().HandleNotification(Binding->GetSelectedBones(), ESkeletalMeshNotifyType::BonesSelected);
	}
}

void USkeletalMeshEditorContextObject::UnbindFrom(ISkeletalMeshEditingInterface* InEditingInterface)
{
	if (!InEditingInterface)
	{
		return;
	}
	
	if (FBindData* BindData = Bindings.Find(InEditingInterface))
	{
		if (BindData->ToToolNotifierHandle.IsValid())
		{
			TSharedPtr<ISkeletalMeshEditorBinding> Binding = GetBinding();
			if (Binding.IsValid())
			{
				Binding->GetNotifier().Delegate().Remove(BindData->ToToolNotifierHandle);
			}
			BindData->ToToolNotifierHandle.Reset();
		}
	
		if (BindData->FromToolNotifierHandle.IsValid())
		{
			InEditingInterface->GetNotifier().Delegate().Remove(BindData->FromToolNotifierHandle);
			InEditingInterface->Unbind();
			
			BindData->FromToolNotifierHandle.Reset();
		}
	
		Bindings.Remove(InEditingInterface);
	}
}

TSharedPtr<ISkeletalMeshEditorBinding> USkeletalMeshEditorContextObject::GetBinding() const
{
	return Editor.IsValid() ? Editor.Pin()->GetBinding() : nullptr;
}