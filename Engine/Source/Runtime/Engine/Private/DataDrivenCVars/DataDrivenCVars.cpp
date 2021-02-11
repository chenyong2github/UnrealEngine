// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataDrivenCVars/DataDrivenCVars.h"
#include "HAL/ConsoleManager.h"
#include "Engine/Engine.h"

FDataDrivenConsoleVariable::~FDataDrivenConsoleVariable()
{
	UnRegister();
}

void FDataDrivenConsoleVariable::Register()
{
	if (!Name.IsEmpty())
	{
		IConsoleVariable* CVarToAdd = IConsoleManager::Get().FindConsoleVariable(*Name);
		if (CVarToAdd == nullptr)
		{
			if (Type == FDataDrivenCVarType::CVarInt)
			{
				CVarToAdd = IConsoleManager::Get().RegisterConsoleVariable(*Name, DefaultValueInt, TEXT("RuntimeConsoleVariables"), ECVF_Default | ECVF_Scalability);
			}
			else if (Type == FDataDrivenCVarType::CVarBool)
			{
				CVarToAdd = IConsoleManager::Get().RegisterConsoleVariable(*Name, DefaultValueBool, TEXT("RuntimeConsoleVariables"), ECVF_Default | ECVF_Scalability);
			}
			else
			{
				CVarToAdd = IConsoleManager::Get().RegisterConsoleVariable(*Name, DefaultValueFloat, TEXT("RuntimeConsoleVariables"), ECVF_Default | ECVF_Scalability);
			}
		}
		CVarToAdd->SetOnChangedCallback(FConsoleVariableDelegate::CreateStatic(UDataDrivenConsoleVariableSettings::OnDataDrivenChange));
		ShadowName = Name;
		ShadowType = Type;
	}
}

void FDataDrivenConsoleVariable::UnRegister(bool bUseShadowName)
{
	IConsoleVariable* CVarToRemove = IConsoleManager::Get().FindConsoleVariable(bUseShadowName  ? *ShadowName : *Name);
	if (CVarToRemove)
	{
		FConsoleVariableDelegate NullCallback;
		CVarToRemove->SetOnChangedCallback(NullCallback);
		IConsoleManager::Get().UnregisterConsoleObject(CVarToRemove, false);
	}
}

#if WITH_EDITOR
void FDataDrivenConsoleVariable::Refresh()
{
	if (ShadowName != Name)
	{
		// unregister old cvar name
		if (!ShadowName.IsEmpty())
		{
			UnRegister(true);
		}
		ShadowName = Name;
	}
	else if (ShadowType != Type)
	{
		UnRegister(true);
		ShadowType = Type;
	}

	// make sure the cvar is registered
	Register();
}
#endif

void UDataDrivenConsoleVariableSettings::PostInitProperties()
{
	Super::PostInitProperties();

	for (FDataDrivenConsoleVariable& CVar : CVarsArray)
	{
		CVar.Register();
	}
}

void UDataDrivenConsoleVariableSettings::OnDataDrivenChange(IConsoleVariable* CVar)
{
	UDataDrivenCVarEngineSubsystem* Subsystem = GEngine->GetEngineSubsystem<UDataDrivenCVarEngineSubsystem>();
	if (Subsystem)
	{
		FConsoleManager& ConsoleManager = (FConsoleManager&)IConsoleManager::Get();
		Subsystem->OnDataDrivenCVarDelegate.Broadcast(ConsoleManager.FindConsoleObjectName(CVar));
	}
}

#if WITH_EDITOR
void UDataDrivenConsoleVariableSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	for (FDataDrivenConsoleVariable& CVar : CVarsArray)
	{
		CVar.Refresh();
	}
}
#endif // #if WITH_EDITOR

FName UDataDrivenConsoleVariableSettings::GetCategoryName() const
{
	return FName(TEXT("Engine"));
}
