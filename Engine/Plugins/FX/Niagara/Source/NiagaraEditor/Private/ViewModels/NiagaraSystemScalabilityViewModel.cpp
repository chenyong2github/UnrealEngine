// Copyright Epic Games, Inc. All Rights Reserved.

#include "ViewModels/NiagaraSystemScalabilityViewModel.h"

#include "ViewModels/NiagaraSystemViewModel.h"
#include "NiagaraSystem.h"
#include "ISequencerModule.h"
#include "NiagaraSystemToolkit.h"
#include "DeviceProfiles/DeviceProfile.h"
#include "Modules/ModuleManager.h"
#include "ViewModels/NiagaraEmitterHandleViewModel.h"

#define LOCTEXT_NAMESPACE "NiagaraSystemScalabilityViewModel"

UNiagaraSystemScalabilityViewModel::UNiagaraSystemScalabilityViewModel()
	: SystemViewModel(nullptr)
{
}

void UNiagaraSystemScalabilityViewModel::Initialize(TSharedRef<FNiagaraSystemViewModel> InSystemViewModel)
{
	SystemViewModel = InSystemViewModel;

	PreviewPlatforms = MakeShared<FNiagaraPlatformSet>(FNiagaraPlatformSet::CreateQualityLevelMask(3));
}

bool UNiagaraSystemScalabilityViewModel::IsValid() const
{
	return SystemViewModel != nullptr;
}

bool UNiagaraSystemScalabilityViewModel::IsActive() const
{
	return GetSystemViewModel().Pin()->GetWorkflowMode() == FNiagaraSystemToolkit::ScalabilityModeName;
}

void UNiagaraSystemScalabilityViewModel::UpdatePreviewDeviceProfile(UDeviceProfile* DeviceProfile)
{
	if(DeviceProfile)
	{
		PreviewDeviceProfile = DeviceProfile;
	}
	else
	{
		PreviewDeviceProfile.Reset();
	}

	FNiagaraPlatformSet::InvalidateCachedData();
	GetSystemViewModel().Pin()->GetSystem().UpdateScalability();
}

void UNiagaraSystemScalabilityViewModel::UpdatePreviewQualityLevel(int32 QualityLevel, bool bEnabled)
{
	PreviewPlatforms->QualityLevelMask = 0;
	PreviewPlatforms->SetEnabledForEffectQuality(QualityLevel, bEnabled);

	FNiagaraPlatformSet::InvalidateCachedData();
	GetSystemViewModel().Pin()->GetSystem().UpdateScalability();
}

// void UNiagaraSystemScalabilityViewModel::NavigateToScalabilityProperty(UObject* Object, FName PropertyName)
// {
// 	TArray<UNiagaraOverviewNode*> OverviewNodes;
// 	GetSystemViewModel().Pin()->GetOverviewGraphViewModel()->GetGraph()->GetNodesOfClass<UNiagaraOverviewNode>(OverviewNodes);
// 	
// 	for(UNiagaraOverviewNode* Node : OverviewNodes)
// 	{
// 		if(FNiagaraEmitterHandle* EmitterHandle = Node->TryGetEmitterHandle())
// 		{
// 			if(Object == EmitterHandle->GetInstance())
// 			{
// 				GetSystemViewModel().Pin()->GetOverviewGraphViewModel()->GetNodeSelection()->SetSelectedObject(Node);
// 				break;
// 			}
// 		}
// 		else if(Object == Node->GetOwningSystem())
// 		{
// 			GetSystemViewModel().Pin()->GetOverviewGraphViewModel()->GetNodeSelection()->SetSelectedObject(Node);
// 			break;
// 		}
// 	}
//
// 	ScalabilityPropertySelectedDelegate.Broadcast(PropertyName);
// }

#undef LOCTEXT_NAMESPACE // NiagaraSystemScalabilityViewModel
