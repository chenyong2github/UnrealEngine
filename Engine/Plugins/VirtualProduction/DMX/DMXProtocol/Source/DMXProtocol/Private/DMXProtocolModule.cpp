// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXProtocolModule.h"
#include "Interfaces/IDMXProtocol.h"
#include "Interfaces/IDMXProtocolFactory.h"
#include "DMXProtocolSettings.h"

#if WITH_EDITOR
#include "ISettingsModule.h"
#include "IDetailCustomization.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "IDetailPropertyRow.h"
#include "DetailWidgetRow.h"
#include "SocketSubsystem.h"

#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SComboBox.h"
#endif

IMPLEMENT_MODULE( FDMXProtocolModule, DMXProtocol );
DEFINE_LOG_CATEGORY(LogDMXProtocol);

#define LOCTEXT_NAMESPACE "DMXProtocolModule"

/**  IDMXProtocol.h static declarations */
FOnNetworkInterfaceChanged IDMXProtocol::OnNetworkInterfaceChanged;

const TCHAR* FDMXProtocolModule::BaseModuleName = TEXT("DMXProtocol");
const FString FDMXProtocolModule::LocalHostIpAddress = TEXT("127.0.0.1");

const TMap<FName, IDMXProtocolPtr>& FDMXProtocolModule::GetProtocols() const
{
	return DMXProtocols;
}

#if WITH_EDITOR
class FDMXProtocolSettingsCustomization : public IDetailCustomization
{
public:
	FDMXProtocolSettingsCustomization()
	{
		LocalIpAddresses.Add(MakeShared<FString>(DefaultAddress));
		TArray<TSharedPtr<FInternetAddr>> Addresses;
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->GetLocalAdapterAddresses(Addresses);
		bool bFoundLocalHost = false;
		for (TSharedPtr<FInternetAddr> Address : Addresses)
		{
			LocalIpAddresses.Add(MakeShared<FString>(Address->ToString(false)));
			if (Address->ToString(false) == FDMXProtocolModule::LocalHostIpAddress)
			{
				bFoundLocalHost = true;
			}
		}
		if (!bFoundLocalHost)
		{
			LocalIpAddresses.Add(MakeShared<FString>(FDMXProtocolModule::LocalHostIpAddress));
		}
	}

	// IDetailCustomization interface
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override
	{
		
		InterfaceIpAddressProperty = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDMXProtocolSettings, InterfaceIPAddress));
		DetailBuilder.EditDefaultProperty(InterfaceIpAddressProperty)->CustomWidget()
			.NameContent()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("DMXProtocolSettingsCustomization", "Interface IP address"))
			]
			.ValueContent()
			[
				SNew(SComboBox<TSharedRef<FString>>)
				.OptionsSource(&LocalIpAddresses)
				.OnGenerateWidget(this, &FDMXProtocolSettingsCustomization::GenerateComboWidget)
				.InitiallySelectedItem(GetCurrentlySelectedIpAddress())
				.OnSelectionChanged(this, &FDMXProtocolSettingsCustomization::OnChangeSelection)
				.Content()
				[
					SNew(STextBlock).Text(this, &FDMXProtocolSettingsCustomization::GetTextAddressFromSettings)
				]
			];
	}
	//

	static TSharedRef<IDetailCustomization> MakeInstance()
	{
		return MakeShared<FDMXProtocolSettingsCustomization>();
	}

	TSharedRef<SWidget> GenerateComboWidget(TSharedRef<FString> InAddress)
	{
		return SNew(STextBlock).Text(FText::FromString(*InAddress));
	}

	void OnChangeSelection(TSharedPtr<FString> InAddress, ESelectInfo::Type InType)
	{
		InterfaceIpAddressProperty->SetValue(*InAddress);
	}

	FText GetTextAddressFromSettings() const
	{
		FString CurrentIpAddress = GetMutableDefault<UDMXProtocolSettings>()->InterfaceIPAddress;
		return FText::FromString(CurrentIpAddress);
	}

	TSharedPtr<FString> GetCurrentlySelectedIpAddress()
	{
		FString CurrentIpAddress = GetMutableDefault<UDMXProtocolSettings>()->InterfaceIPAddress;
		
		for (TSharedRef<FString> LocalIpAddress : LocalIpAddresses)
		{
			if (*LocalIpAddress == CurrentIpAddress)
			{
				return LocalIpAddress;
			}
		}

		return LocalIpAddresses[0];
	}

	TArray<TSharedRef<FString>> LocalIpAddresses;
	const FString DefaultAddress = TEXT("0.0.0.0");

	TSharedPtr<IPropertyHandle> InterfaceIpAddressProperty;
};
#endif

void FDMXProtocolModule::StartupModule()
{

#if WITH_EDITOR
	ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");

	// Register DMX Protocol global settings
	if (SettingsModule != nullptr)
	{	
		SettingsModule->RegisterSettings("Project", "Plugins", "DMX Plugin",
			LOCTEXT("ProjectSettings_Label", "DMX Plugin"),
			LOCTEXT("ProjectSettings_Description", "Configure DMX plugin global settings"),
			GetMutableDefault<UDMXProtocolSettings>()
		);

		FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyModule.RegisterCustomClassLayout(UDMXProtocolSettings::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FDMXProtocolSettingsCustomization::MakeInstance));
		PropertyModule.NotifyCustomizationModuleChanged();
	}
#endif // WITH_EDITOR

}

void FDMXProtocolModule::ShutdownModule()
{
	ShutdownAllDMXProtocols();

#if WITH_EDITOR
	FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyModule.UnregisterCustomClassLayout(UDMXProtocolSettings::StaticClass()->GetFName());
	// Unregister DMX Protocol global settings
	ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");
	if (SettingsModule)
	{
		SettingsModule->UnregisterSettings("Project", "Plugins", "DMX Protocol");
	}
#endif // WITH_EDITOR

}

static FName GetProtocolModuleName(const FString& ProtocolName)
{
	FName ModuleName;
	if (!ProtocolName.StartsWith(FDMXProtocolModule::BaseModuleName, ESearchCase::CaseSensitive))
	{
		ModuleName = FName(*(FDMXProtocolModule::BaseModuleName + ProtocolName));
	}
	else
	{
		ModuleName = FName(*ProtocolName);
	}

	return ModuleName;
}

static IModuleInterface* GetProtocolModule(const FString& SubsystemName)
{
	const FName ModuleName = GetProtocolModuleName(SubsystemName);
	FModuleManager& ModuleManager = FModuleManager::Get();
	check(ModuleManager.IsModuleLoaded(ModuleName));
	return ModuleManager.GetModule(ModuleName);
}

void FDMXProtocolModule::RegisterProtocol(const FName& FactoryName, IDMXProtocolFactory* Factory)
{
	if (!DMXFactories.Contains(FactoryName))
	{
		DMXFactories.Add(FactoryName, Factory);
	}
	else
	{
		UE_LOG_DMXPROTOCOL(Verbose, TEXT("Trying to add existing protocol %s"), *FactoryName.ToString());
	}
}

void FDMXProtocolModule::UnregisterProtocol(const FName& FactoryName)
{
	if (DMXFactories.Contains(FactoryName))
	{
		DMXFactories.Remove(FactoryName);
		ShutdownDMXProtocol(FactoryName);
	}
	else
	{
		UE_LOG_DMXPROTOCOL(Verbose, TEXT("Trying to remove unexisting protocol %s"), *FactoryName.ToString());
	}
}

FDMXProtocolModule& FDMXProtocolModule::Get()
{
	return FModuleManager::GetModuleChecked<FDMXProtocolModule>("DMXProtocol");
}


IDMXProtocolPtr FDMXProtocolModule::GetProtocol(const FName InProtocolName)
{
	IDMXProtocolPtr* DMXProtocolPtr = nullptr;
	if (!InProtocolName.IsNone())
	{
		DMXProtocolPtr = DMXProtocols.Find(InProtocolName);
		if (DMXProtocolPtr == nullptr)
		{
			IDMXProtocolFactory** DMXProtocolFactory = DMXFactories.Find(InProtocolName);

			if (DMXProtocolFactory != nullptr)
			{
				UE_LOG_DMXPROTOCOL(Log, TEXT("Creating protocol instance for: %s"), *InProtocolName.ToString());

				IDMXProtocolPtr NewProtocol = (*DMXProtocolFactory)->CreateProtocol(InProtocolName);
				if (NewProtocol.IsValid())
				{
					DMXProtocols.Add(InProtocolName, NewProtocol);
					DMXProtocolPtr = DMXProtocols.Find(InProtocolName);
				}
				else
				{
					bool* bNotedPreviously = DMXProtocolFailureNotes.Find(InProtocolName);
					if (!bNotedPreviously || !(*bNotedPreviously))
					{
						UE_LOG_DMXPROTOCOL(Verbose, TEXT("Unable to create Protocol %s"), *InProtocolName.ToString());
						DMXProtocolFailureNotes.Add(InProtocolName, true);
					}
				}
			}
		}
	}

	return (DMXProtocolPtr == nullptr) ? nullptr : *DMXProtocolPtr;
}

const TMap<FName, IDMXProtocolFactory*>& FDMXProtocolModule::GetProtocolFactories() const
{
	return DMXFactories;
}

void FDMXProtocolModule::ShutdownDMXProtocol(const FName& ProtocolName)
{
	if (!ProtocolName.IsNone())
	{
		IDMXProtocolPtr DMXProtocol;
		DMXProtocols.RemoveAndCopyValue(ProtocolName, DMXProtocol);
		if (DMXProtocol.IsValid())
		{
			DMXProtocol->Shutdown();
		}
		else
		{
			UE_LOG_DMXPROTOCOL(Verbose, TEXT("DMXProtocol instance %s not found, unable to destroy."), *ProtocolName.ToString());
		}
	}
}

void FDMXProtocolModule::ShutdownAllDMXProtocols()
{
	for (TMap<FName, IDMXProtocolPtr>::TIterator It = DMXProtocols.CreateIterator(); It; ++It)
	{
		It->Value->Shutdown();
	}
}

#undef LOCTEXT_NAMESPACE
