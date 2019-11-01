// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterPlayerInput.h"
#include "Components/InputComponent.h"

#include "Cluster/IPDisplayClusterClusterManager.h"
#include "DisplayClusterGlobals.h"
#include "DisplayClusterHelpers.h"
#include "DisplayClusterLog.h"
#include "DisplayClusterUtils/DisplayClusterTypesConverter.h"


UDisplayClusterPlayerInput::UDisplayClusterPlayerInput()
	: Super()
{
}

void UDisplayClusterPlayerInput::ProcessInputStack(const TArray<UInputComponent*>& InputComponentStack, const float DeltaTime, const bool bGamePaused)
{
	UE_LOG(LogDisplayClusterGame, Verbose, TEXT("Processing input stack..."));

	static IPDisplayClusterClusterManager* const ClusterMgr = GDisplayCluster->GetPrivateClusterMgr();
	if (ClusterMgr && ClusterMgr->IsCluster())
	{
		if (ClusterMgr->IsMaster())
		{
			// Build master auxiliary data
			BuildDelegatesMap(InputComponentStack, true);
			// Start procssing the stack
			Super::ProcessInputStack(InputComponentStack, DeltaTime, bGamePaused);
		}
		else
		{
			// Build slave auxiliary data
			BuildDelegatesMap(InputComponentStack, false);
			// Get native input data from master
			TMap<FString, FString> NativeInputData;
			ClusterMgr->SyncNativeInput(NativeInputData);
			// Process native input data got from master
			ImportNativeInputData(NativeInputData, InputComponentStack);
		}
	}
}

void UDisplayClusterPlayerInput::ProcessDelegatesFilter(const TArray<UInputComponent*>& InputComponentStack, TArray<FAxisDelegateDetails>& AxisDelegates, TArray<FVectorAxisDelegateDetails>& VectorAxisDelegates, TArray<FDelegateDispatchDetails>& NonAxisDelegates)
{
	UE_LOG(LogDisplayClusterGame, Verbose, TEXT("Processing input delegates..."));

	static IPDisplayClusterClusterManager* const ClusterMgr = GDisplayCluster->GetPrivateClusterMgr();

	// Serialize native input data
	TMap<FString, FString> NativeInputData;
	ExportNativeInputData(NativeInputData, InputComponentStack, AxisDelegates, VectorAxisDelegates, NonAxisDelegates);
	// Provide data to slave nodes
	ClusterMgr->ProvideNativeInputData(NativeInputData);
}

void UDisplayClusterPlayerInput::BuildDelegatesMap(const TArray<UInputComponent*>& InputComponentStack, bool IsMaster)
{
	CleanDelegateMap(IsMaster);

	// Master related logic
	if (IsMaster)
	{
		for (UInputComponent* IC : InputComponentStack)
		{
			// Cache axis delegates
			for (const FInputAxisBinding& AxisBinding : IC->AxisBindings)
			{
				void* Addr1 = (void*)&AxisBinding.AxisDelegate.GetDelegate();
				void* Addr2 = (void*)&AxisBinding.AxisDelegate.GetDynamicDelegate();

				if (!AxisDelegateNames.Contains(Addr1))
				{
					AxisDelegateNames.Emplace(Addr1);
				}

				AxisDelegateNames[Addr1].Emplace(Addr2, AxisBinding.AxisName);
			}

			// Cache gesture delegates
			for (const FInputGestureBinding& GestureBinding : IC->GestureBindings)
			{
				void* Addr1 = (void*)&GestureBinding.GestureDelegate.GetDelegate();
				void* Addr2 = (void*)&GestureBinding.GestureDelegate.GetDynamicDelegate();

				if (!GestureDelegateNames.Contains(Addr1))
				{
					GestureDelegateNames.Emplace(Addr1);
				}

				GestureDelegateNames[Addr1].Emplace(Addr2, GestureBinding.GestureKey.GetFName());
			}

			// Cache vector axis delegates
			for (const FInputVectorAxisBinding& VectorAxisBinding : IC->VectorAxisBindings)
			{
				void* Addr1 = (void*)&VectorAxisBinding.AxisDelegate.GetDelegate();
				void* Addr2 = (void*)&VectorAxisBinding.AxisDelegate.GetDynamicDelegate();

				if (!VectorAxisDelegateNames.Contains(Addr1))
				{
					VectorAxisDelegateNames.Emplace(Addr1);
				}

				VectorAxisDelegateNames[Addr1].Emplace(Addr2, VectorAxisBinding.AxisKey.GetFName());
			}
		}
	}
	// Slave related logic
	else
	{
		for (UInputComponent* IC : InputComponentStack)
		{
			// Cache axis delegates
			for (const FInputAxisBinding& AxisBinding : IC->AxisBindings)
			{
				AxisDelegateInstances.Emplace(AxisBinding.AxisName, &AxisBinding.AxisDelegate);
			}

			// Cache action delegates
			const int32 ActionBindingsAmount = IC->GetNumActionBindings();
			for (int i = 0; i < ActionBindingsAmount; ++i)
			{
				const FInputActionBinding& ActionBinding = IC->GetActionBinding(i);
				const FName ActionName = ActionBinding.GetActionName();
				if (!ActionDelegateInstances.Contains(ActionName))
				{
					ActionDelegateInstances.Emplace(ActionName);
				}

				const EInputEvent InputEvent = ActionBinding.KeyEvent.GetValue();
				if (!ActionDelegateInstances[ActionName].Contains(InputEvent))
				{
					ActionDelegateInstances[ActionName].Emplace(InputEvent);
				}

				ActionDelegateInstances[ActionName][InputEvent] = &ActionBinding.ActionDelegate;
			}

			// Cache gesture delegates
			for (const FInputGestureBinding& GestureBinding : IC->GestureBindings)
			{
				const FName GestureKey = GestureBinding.GestureKey.GetFName();
				GestureDelegateInstances.Emplace(GestureKey, &GestureBinding.GestureDelegate);
			}

			// Cache vector axis delegates
			for (const FInputVectorAxisBinding& VectorAxisBinding : IC->VectorAxisBindings)
			{
				const FName AxisKey = VectorAxisBinding.AxisKey.GetFName();
				VectorAxisDelegateInstances.Emplace(AxisKey, &VectorAxisBinding.AxisDelegate);
			}
		}
	}

	// For any node type
	for (UInputComponent* IC : InputComponentStack)
	{
		// Cache touch delegates
		for (const FInputTouchBinding& TouchBinding : IC->TouchBindings)
		{
			const EInputEvent InputEvent = TouchBinding.KeyEvent.GetValue();
			TouchDelegateInstances.Emplace(InputEvent, &TouchBinding.TouchDelegate);
		}
	}
}

void UDisplayClusterPlayerInput::CleanDelegateMap(bool IsMaster)
{
	if (IsMaster)
	{
		AxisDelegateNames.Reset();
		GestureDelegateNames.Reset();
		VectorAxisDelegateNames.Reset();
	}
	else
	{
		AxisDelegateInstances.Reset();
		ActionDelegateInstances.Reset();
		GestureDelegateInstances.Reset();
		VectorAxisDelegateInstances.Reset();
	}

	TouchDelegateInstances.Reset();
}

void UDisplayClusterPlayerInput::ExportNativeInputData(TMap<FString, FString>& InputData, const TArray<UInputComponent*>& InputComponentStack, TArray<FAxisDelegateDetails>& AxisDelegates, TArray<FVectorAxisDelegateDetails>& VectorAxisDelegates, TArray<FDelegateDispatchDetails>& NonAxisDelegates)
{
	UE_LOG(LogDisplayClusterGame, Verbose, TEXT("Exporting native input data..."));

	InputData.Reset();

	InputData.Emplace(FString("action"), SerializeActionData(InputComponentStack, NonAxisDelegates));
	InputData.Emplace(FString("axis"),   SerializeAxisData(InputComponentStack,   AxisDelegates));
	InputData.Emplace(FString("vector"), SerializeVectorAxisData(InputComponentStack, VectorAxisDelegates));

	UE_LOG(LogDisplayClusterGame, Verbose, TEXT("[EXP] Native output action: %s"), *InputData[FString("action")]);
	UE_LOG(LogDisplayClusterGame, Verbose, TEXT("[EXP] Native output axis  : %s"), *InputData[FString("axis")]);
	UE_LOG(LogDisplayClusterGame, Verbose, TEXT("[EXP] Native output vector: %s"), *InputData[FString("vector")]);
}

void UDisplayClusterPlayerInput::ImportNativeInputData(const TMap<FString, FString>& InputData, const TArray<UInputComponent*>& InputComponentStack)
{
	UE_LOG(LogDisplayClusterGame, Verbose, TEXT("Importing native input data..."));

	check(InputData.Num() == 3);

	UE_LOG(LogDisplayClusterGame, Verbose, TEXT("[IMP] Native output action: %s"), *InputData[FString("action")]);
	UE_LOG(LogDisplayClusterGame, Verbose, TEXT("[IMP] Native output axis  : %s"), *InputData[FString("axis")]);
	UE_LOG(LogDisplayClusterGame, Verbose, TEXT("[IMP] Native output vector: %s"), *InputData[FString("vector")]);

	DeserializeAndProcessActionInput    (InputData[FString("action")]);
	DeserializeAndProcessAxisInput      (InputData[FString("axis")]);
	DeserializeAndProcessVectorAxisInput(InputData[FString("vector")]);
}

FString UDisplayClusterPlayerInput::SerializeAxisData(const TArray<UInputComponent*>& InputComponentStack, const TArray<FAxisDelegateDetails>& AxisDelegates)
{
	FString OutData;
	OutData.Reserve(512);

	for (const FAxisDelegateDetails& Details : AxisDelegates)
	{
		const void* const Addr1 = (void*)&Details.Delegate.GetDelegate();
		const void* const Addr2 = (void*)&Details.Delegate.GetDynamicDelegate();

		if (AxisDelegateNames.Contains(Addr1) && AxisDelegateNames[Addr1].Contains(Addr2))
		{
			OutData += FString::Printf(TEXT("%s=%f;"), *AxisDelegateNames[Addr1][Addr2].ToString(), Details.Value);
		}
	}

	return OutData;
}

FString UDisplayClusterPlayerInput::SerializeVectorAxisData(const TArray<UInputComponent*>& InputComponentStack, const TArray<FVectorAxisDelegateDetails>& VectorAxisDelegates)
{
	FString OutData;
	OutData.Reserve(512);

	for (const FVectorAxisDelegateDetails& Details : VectorAxisDelegates)
	{
		void* Addr1 = (void*)&Details.Delegate.GetDelegate();
		void* Addr2 = (void*)&Details.Delegate.GetDynamicDelegate();

		if (VectorAxisDelegateNames.Contains(Addr1) && VectorAxisDelegateNames[Addr1].Contains(Addr2))
		{
			OutData += FString::Printf(TEXT("%s=%s;"), *VectorAxisDelegateNames[Addr1][Addr2].ToString(), *FDisplayClusterTypesConverter::ToString(Details.Value));
		}
	}

	return OutData;
}

FString UDisplayClusterPlayerInput::SerializeActionData(const TArray<UInputComponent*>& InputComponentStack, const TArray<FDelegateDispatchDetails>& NonAxisDelegates)
{
	FString OutData;
	OutData.Reserve(512);

	for (const FDelegateDispatchDetails& Details : NonAxisDelegates)
	{
		if (Details.SourceAction)
		{
			const FString ActionName = Details.SourceAction->GetActionName().ToString();
			if (Details.ActionDelegate.IsBound())
			{
				OutData += FString::Printf(TEXT("key=%s|%s|%d;"), *ActionName, *Details.Chord.Key.GetFName().ToString(), (int)Details.SourceAction->KeyEvent.GetValue());
			}
			else if (Details.TouchDelegate.IsBound())
			{
				OutData += FString::Printf(TEXT("touch=%d|%s|%d;"), (int)Details.KeyEvent.GetValue(), *FDisplayClusterTypesConverter::ToString(Details.TouchLocation), (int)Details.FingerIndex);
			}
			else if (Details.GestureDelegate.IsBound())
			{
				const void* const Addr1 = (void*)&Details.GestureDelegate.GetDelegate();
				const void* const Addr2 = (void*)&Details.GestureDelegate.GetDynamicDelegate();

				if (GestureDelegateNames.Contains(Addr1) && GestureDelegateNames[Addr1].Contains(Addr2))
				{
					OutData += FString::Printf(TEXT("gesture=%s|%f;"), *GestureDelegateNames[Addr1][Addr2].ToString(), Details.GestureValue);
				}
			}
		}
	}

	return OutData;
}

void UDisplayClusterPlayerInput::DeserializeAndProcessAxisInput(const FString& Data)
{
	const TArray<FString> AxisRecords = DisplayClusterHelpers::str::StrToArray<FString>(Data, FString(";"));
	for (const FString& AxisRecord : AxisRecords)
	{
		if (AxisRecord.Len() > 0)
		{
			const TArray<FString> AxisParams = DisplayClusterHelpers::str::StrToArray<FString>(AxisRecord, FString(TEXT("=")));
			check(AxisParams.Num() == 2);

			const FName AxisName = *AxisParams[0];
			if (AxisDelegateInstances.Contains(AxisName))
			{
				const float AxisValue = FDisplayClusterTypesConverter::FromString<float>(AxisParams[1]);
				UE_LOG(LogDisplayClusterGame, Verbose, TEXT("Native input - axis %s, value %f"), *AxisName.ToString(), AxisValue);
				AxisDelegateInstances[AxisName]->Execute(AxisValue);
			}
		}
	}
}

void UDisplayClusterPlayerInput::DeserializeAndProcessVectorAxisInput(const FString& Data)
{
	const TArray<FString> VectorRecords = DisplayClusterHelpers::str::StrToArray<FString>(Data, FString(";"));
	for (const FString& VectorRecord : VectorRecords)
	{
		if (VectorRecord.Len() > 0)
		{
			const TArray<FString> VectorParams = DisplayClusterHelpers::str::StrToArray<FString>(VectorRecord, FString(TEXT("=")));
			check(VectorParams.Num() == 2);

			FName VectorAxisName = *VectorParams[0];
			if (VectorAxisDelegateInstances.Contains(VectorAxisName))
			{
				FVector VectorValue = FDisplayClusterTypesConverter::FromString<FVector>(VectorParams[1]);
				UE_LOG(LogDisplayClusterGame, Verbose, TEXT("Native input - vector axis %s, value %f"), *VectorAxisName.ToString(), *VectorValue.ToString());
				VectorAxisDelegateInstances[VectorAxisName]->Execute(VectorValue);
			}
		}
	}
}

void UDisplayClusterPlayerInput::DeserializeAndProcessActionInput(const FString& Data)
{
	const TArray<FString> ActionRecords = DisplayClusterHelpers::str::StrToArray<FString>(Data, FString(";"));
	for (const FString& ActionRecord : ActionRecords)
	{
		if(ActionRecord.Len() > 0)
		{
			const TArray<FString> ActionParams = DisplayClusterHelpers::str::StrToArray<FString>(ActionRecord, FString(TEXT("=")));
			check(ActionParams.Num() == 2);
			const TArray<FString> ActionParamsEx = DisplayClusterHelpers::str::StrToArray<FString>(ActionParams[1], FString(TEXT("|")));

			if (ActionParams[0].Equals(FString("key"), ESearchCase::IgnoreCase))
			{
				check(ActionParamsEx.Num() == 3);

				const FName ActionName = *ActionParamsEx[0];
				if (ActionDelegateInstances.Contains(ActionName))
				{
					const EInputEvent InputEvent = (EInputEvent)FDisplayClusterTypesConverter::FromString<int>(ActionParamsEx[2]);
					if (ActionDelegateInstances[ActionName].Contains(InputEvent))
					{
						const FKey ActionKey(*ActionParamsEx[1]);

						UE_LOG(LogDisplayClusterGame, Verbose, TEXT("Native input - key action %s, key %s, input event %d"), *ActionName.ToString(), *ActionKey.ToString(), (int)InputEvent);

						ActionDelegateInstances[ActionName][InputEvent]->Execute(ActionKey);
					}
				}
			}
			else if (ActionParams[0].Equals(FString("touch"), ESearchCase::IgnoreCase))
			{
				check(ActionParamsEx.Num() == 3);

				const EInputEvent InputEvent = (EInputEvent)FDisplayClusterTypesConverter::FromString<int>(ActionParamsEx[0]);
				if (TouchDelegateInstances.Contains(InputEvent))
				{
					const FVector           TouchLocation = FDisplayClusterTypesConverter::FromString<FVector>(ActionParamsEx[1]);
					const ETouchIndex::Type FingerIndex   = (ETouchIndex::Type)FDisplayClusterTypesConverter::FromString<int>(ActionParamsEx[2]);

					UE_LOG(LogDisplayClusterGame, Verbose, TEXT("Native input - touch action, finger index %d, touch location %s, input event %d"), (int)FingerIndex, *TouchLocation.ToString(), (int)InputEvent);

					TouchDelegateInstances[InputEvent]->Execute(FingerIndex, TouchLocation);
				}
			}
			else if (ActionParams[0].Equals(FString("gesture"), ESearchCase::IgnoreCase))
			{
				check(ActionParamsEx.Num() == 2);

				const FName GestureKey = *ActionParamsEx[0];
				if (GestureDelegateInstances.Contains(GestureKey))
				{
					const float GestureValue = FDisplayClusterTypesConverter::FromString<float>(ActionParamsEx[1]);

					UE_LOG(LogDisplayClusterGame, Verbose, TEXT("Native input - gesture action, key %s, value %f"), *GestureKey.ToString(), GestureValue);

					GestureDelegateInstances[GestureKey]->Execute(GestureValue);
				}
			}
		}
	}
}
