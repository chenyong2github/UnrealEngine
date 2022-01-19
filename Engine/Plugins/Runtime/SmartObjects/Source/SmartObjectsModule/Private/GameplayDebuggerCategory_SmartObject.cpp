// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameplayDebuggerCategory_SmartObject.h"
#include "SmartObjectSubsystem.h"
#include "Math/ColorList.h"
#include "Engine/World.h"

#if WITH_GAMEPLAY_DEBUGGER && WITH_SMARTOBJECT_DEBUG

FGameplayDebuggerCategory_SmartObject::FGameplayDebuggerCategory_SmartObject()
{
	bShowOnlyWithDebugActor = false;
}

TSharedRef<FGameplayDebuggerCategory> FGameplayDebuggerCategory_SmartObject::MakeInstance()
{
	return MakeShareable(new FGameplayDebuggerCategory_SmartObject());
}

void FGameplayDebuggerCategory_SmartObject::CollectData(APlayerController* OwnerPC, AActor* DebugActor)
{
	UWorld* World = GetDataWorld(OwnerPC, DebugActor);
	check(World);

	USmartObjectSubsystem* Subsystem = World->GetSubsystem<USmartObjectSubsystem>();
	if (Subsystem == nullptr)
	{
		AddTextLine(FString::Printf(TEXT("{Red}SmartObjectSubsystem instance is missing")));
		return;
	}

	const uint32 NumRuntimeObjects = Subsystem->DebugGetNumRuntimeObjects();
	const uint32 NumRegisteredComponents = Subsystem->DebugGetNumRegisteredComponents();

	ASmartObjectCollection* MainCollection = Subsystem->GetMainCollection();
	const uint32 NumCollectionEntries = MainCollection != nullptr ? MainCollection->GetEntries().Num() : 0;
	AddTextLine(FString::Printf(TEXT("{White}Collection entries = {Green}%d\n{White}Runtime objects = {Green}%s\n{White}Registered components = {Green}%s"),
		NumCollectionEntries, *LexToString(NumRuntimeObjects), *LexToString(NumRegisteredComponents)));

	FVector ViewLocation = FVector::ZeroVector;
	FVector ViewDirection = FVector::ForwardVector;
	bool bApplyCulling = GetViewPoint(OwnerPC, ViewLocation, ViewDirection);

	FColor DebugColor = FColor::Yellow;

	const FColor FreeColor = FColorList::Grey;
	const FColor ClaimedColor = FColor::Yellow;
	const FColor OccupiedColor = FColor::Red;

	const TMap<FSmartObjectSlotHandle, FSmartObjectSlotClaimState>& Entries = Subsystem->DebugGetRuntimeSlots();
	for (auto& LookupEntry : Entries)
	{
		const FSmartObjectSlotHandle SlotHandle = LookupEntry.Key;
		const FSmartObjectSlotClaimState& SlotState = LookupEntry.Value;

		FSmartObjectSlotView View = Subsystem->GetSlotView(LookupEntry.Key);

		const FSmartObjectSlotTransform& SlotTransform = View.GetStateData<FSmartObjectSlotTransform>();
		FTransform Transform = SlotTransform.GetTransform();
		if (bApplyCulling && !IsLocationInViewCone(ViewLocation, ViewDirection, Transform.GetLocation()))
		{
			continue;
		}

		constexpr float DebugArrowThickness = 2.f;
		constexpr float DebugCircleRadius = 40.f;
		constexpr float DebugArrowHeadSize = 10.f;
#if WITH_EDITORONLY_DATA
		DebugColor = View.GetDefinition().DEBUG_DrawColor;
#endif
		const FVector Pos = Transform.GetLocation() + FVector(0.0f, 0.0f, 25.0f);
		const FVector Dir = Transform.GetRotation().GetForwardVector();

		FColor StateColor = FColor::Silver;
		switch (SlotState.GetState())
		{
		case ESmartObjectSlotState::Free:		StateColor = FreeColor;		break;
		case ESmartObjectSlotState::Claimed:	StateColor = ClaimedColor;	break;
		case ESmartObjectSlotState::Occupied:	StateColor = OccupiedColor;	break;
		default:
			ensureMsgf(false, TEXT("Unsupported value: %s"), *UEnum::GetValueAsString(SlotState.GetState()));
		}

		AddShape(FGameplayDebuggerShape::MakeCircle(Pos, FVector::UpVector, DebugCircleRadius, DebugColor));
		AddShape(FGameplayDebuggerShape::MakeCircle(Pos, FVector::UpVector, 0.75f * DebugCircleRadius, /* Thickness */5.f, StateColor));
		AddShape(FGameplayDebuggerShape::MakeArrow(Pos, Pos + Dir * 2.0f * DebugCircleRadius, DebugArrowHeadSize, DebugArrowThickness, DebugColor));
	}
}

#endif // WITH_GAMEPLAY_DEBUGGER && WITH_SMARTOBJECT_DEBUG
