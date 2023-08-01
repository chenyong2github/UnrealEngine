// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXControlConsoleEditorLayouts.h"

#include "Algo/Find.h"
#include "DMXControlConsoleData.h"
#include "Layouts/DMXControlConsoleEditorGlobalLayoutBase.h"
#include "Layouts/DMXControlConsoleEditorGlobalLayoutDefault.h"
#include "Layouts/DMXControlConsoleEditorGlobalLayoutUser.h"
#include "Library/DMXLibrary.h"
#include "Models/DMXControlConsoleEditorModel.h"


UDMXControlConsoleEditorLayouts::UDMXControlConsoleEditorLayouts()
{
	DefaultLayout = CreateDefaultSubobject<UDMXControlConsoleEditorGlobalLayoutDefault>(TEXT("DefaultLayout"));
}

UDMXControlConsoleEditorGlobalLayoutUser* UDMXControlConsoleEditorLayouts::AddUserLayout(const FString& LayoutName)
{
	UDMXControlConsoleEditorGlobalLayoutUser* UserLayout = NewObject<UDMXControlConsoleEditorGlobalLayoutUser>(this, NAME_None, RF_Transactional);
	FString NewLayoutName = LayoutName;
	if (NewLayoutName.IsEmpty() || FindUserLayoutByName(NewLayoutName))
	{
		const int32 Index = UserLayouts.Num();
		NewLayoutName = FString::Format(TEXT("Layout {0}"), { Index });
	}
	UserLayout->SetLayoutName(NewLayoutName);

	UserLayouts.Add(UserLayout);

	return UserLayout;
}

void UDMXControlConsoleEditorLayouts::DeleteUserLayout(UDMXControlConsoleEditorGlobalLayoutUser* UserLayout)
{
	if (!ensureMsgf(UserLayout, TEXT("Invalid layout, cannot delete from '%s'."), *GetName()))
	{
		return;
	}

	if (!ensureMsgf(UserLayouts.Contains(UserLayout), TEXT("'%s' is not owner of '%s'. Cannot delete layout correctly."), *GetName(), *UserLayout->GetLayoutName()))
	{
		return;
	}

	UserLayouts.Remove(UserLayout);
}

UDMXControlConsoleEditorGlobalLayoutUser* UDMXControlConsoleEditorLayouts::FindUserLayoutByName(const FString& LayoutName) const
{
	const TObjectPtr<UDMXControlConsoleEditorGlobalLayoutUser>* UserLayout = Algo::FindByPredicate(UserLayouts, [LayoutName](const UDMXControlConsoleEditorGlobalLayoutUser* CustomLayout)
		{
			return IsValid(CustomLayout) && CustomLayout->GetLayoutName() == LayoutName;
		});

	return UserLayout ? UserLayout->Get() : nullptr;
}

void UDMXControlConsoleEditorLayouts::ClearUserLayouts()
{
	ActiveLayout = DefaultLayout;
	UserLayouts.Reset();
}

void UDMXControlConsoleEditorLayouts::SetActiveLayout(UDMXControlConsoleEditorGlobalLayoutBase* InLayout)
{
	if (InLayout)
	{
		ActiveLayout = InLayout;
	}
}

void UDMXControlConsoleEditorLayouts::UpdateDefaultLayout(const UDMXControlConsoleData* ControlConsoleData)
{
	if (DefaultLayout)
	{
		DefaultLayout->Modify();
		DefaultLayout->GenerateLayoutByControlConsoleData(ControlConsoleData);
	}
}

void UDMXControlConsoleEditorLayouts::SubscribeToFixturePatchDelegates()
{
	if (!DefaultLayout)
	{
		return;
	}

	if (!UDMXLibrary::GetOnEntitiesRemoved().IsBoundToObject(DefaultLayout))
	{
		UDMXLibrary::GetOnEntitiesRemoved().AddUObject(DefaultLayout, &UDMXControlConsoleEditorGlobalLayoutDefault::OnFixturePatchRemovedFromLibrary);
	}

	const UDMXControlConsoleEditorModel* EditorModel = GetDefault<UDMXControlConsoleEditorModel>();
	UDMXControlConsoleData* EditorConsoleData = EditorModel->GetEditorConsoleData();
	if (EditorConsoleData && !EditorConsoleData->GetOnFaderGroupAdded().IsBoundToObject(DefaultLayout))
	{
		EditorConsoleData->GetOnFaderGroupAdded().AddUObject(DefaultLayout, &UDMXControlConsoleEditorGlobalLayoutDefault::OnFaderGroupAddedToData, EditorConsoleData);
	}
}

void UDMXControlConsoleEditorLayouts::UnsubscribeFromFixturePatchDelegates()
{
	UDMXLibrary::GetOnEntitiesRemoved().RemoveAll(DefaultLayout);
	
	const UDMXControlConsoleEditorModel* EditorModel = GetDefault<UDMXControlConsoleEditorModel>();
	UDMXControlConsoleData* EditorConsoleData = EditorModel->GetEditorConsoleData();
	if (EditorConsoleData)
	{
		EditorConsoleData->GetOnFaderGroupAdded().RemoveAll(DefaultLayout);
	}
}
