// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Interfaces/ITextureFormatManagerModule.h"
#include "Interfaces/ITextureFormat.h"
#include "Interfaces/ITextureFormatModule.h"
#include "TextureFormatManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogTextureFormatManager, Log, All);

/**
 * Module for the target platform manager
 */
class FTextureFormatManagerModule
	: public ITextureFormatManagerModule
{
public:

	/** Default constructor. */
	FTextureFormatManagerModule()
		: ModuleName(TEXT("TextureFormat"))
		, bForceCacheUpdate(true)
		, bIgnoreFirstDelegateCall(true)
	{
		// Calling a virtual function from a constructor, but with no expectation that a derived implementation of this
		// method would be called.  This is solely to avoid duplicating code in this implementation, not for polymorphism.
		FTextureFormatManagerModule::Invalidate();

		FModuleManager::Get().OnModulesChanged().AddRaw(this, &FTextureFormatManagerModule::ModulesChangesCallback);
	}

	/** Destructor. */
	virtual ~FTextureFormatManagerModule()
	{
		FModuleManager::Get().OnModulesChanged().RemoveAll(this);
	}


	virtual const TArray<const ITextureFormat*>& GetTextureFormats() override
	{
		static bool bInitialized = false;
		static TArray<const ITextureFormat*> Results;

		if (!bInitialized || bForceCacheUpdate)
		{
			bInitialized = true;
			Results.Empty(Results.Num());

			TArray<FName> Modules;

			FModuleManager::Get().FindModules(TEXT("*TextureFormat*"), Modules);

			if (!Modules.Num())
			{
				UE_LOG(LogTextureFormatManager, Error, TEXT("No texture formats found!"));
			}

			for (int32 Index = 0; Index < Modules.Num(); Index++)
			{
				if (Modules[Index] != ModuleName) // Avoid our own module when going through this list that was gathered by name
				{
					ITextureFormatModule* Module = FModuleManager::LoadModulePtr<ITextureFormatModule>(Modules[Index]);
					if (Module)
					{
						ITextureFormat* Format = Module->GetTextureFormat();
						if (Format != nullptr)
						{
							Results.Add(Format);
						}
					}
				}
			}
		}

		return Results;
	}

	virtual const ITextureFormat* FindTextureFormat(FName Name) override
	{
		const TArray<const ITextureFormat*>& TextureFormats = GetTextureFormats();

		for (int32 Index = 0; Index < TextureFormats.Num(); Index++)
		{
			TArray<FName> Formats;

			TextureFormats[Index]->GetSupportedFormats(Formats);

			for (int32 FormatIndex = 0; FormatIndex < Formats.Num(); FormatIndex++)
			{
				if (Formats[FormatIndex] == Name)
				{
					return TextureFormats[Index];
				}
			}
		}

		return nullptr;
	}

	virtual void Invalidate() override
	{
		bForceCacheUpdate = true;
		GetTextureFormats();
		bForceCacheUpdate = false;
	}

private:

	void ModulesChangesCallback(FName InModuleName, EModuleChangeReason ReasonForChange)
	{
		if (!bIgnoreFirstDelegateCall && (InModuleName != ModuleName) && InModuleName.ToString().Contains(TEXT("TextureFormat")))
		{
			Invalidate();
		}
		bIgnoreFirstDelegateCall = false;
	}

	FName ModuleName;

	// Flag to force reinitialization of all cached data. This is needed to have up-to-date caches
	// in case of a module reload of a TextureFormat-Module.
	bool bForceCacheUpdate;

	// Flag to avoid redunant reloads
	bool bIgnoreFirstDelegateCall;

};

IMPLEMENT_MODULE(FTextureFormatManagerModule, TextureFormat);

class ITextureFormatManagerModule* GetTextureFormatManager()
{
	static class ITextureFormatManagerModule* SingletonInterface = NULL;
	static bool bInitialized = false;
	if (!bInitialized)
	{
		check(IsInGameThread());
		bInitialized = true;
		SingletonInterface = FModuleManager::LoadModulePtr<ITextureFormatManagerModule>("TextureFormat");
	}
	return SingletonInterface;
}

class ITextureFormatManagerModule& GetTextureFormatManagerRef()
{
	class ITextureFormatManagerModule* SingletonInterface = GetTextureFormatManager();
	if (!SingletonInterface)
	{
		UE_LOG(LogInit, Fatal, TEXT("Texture format manager was requested, but not available."));
		CA_ASSUME( SingletonInterface != NULL );	// Suppress static analysis warning in unreachable code (fatal error)
	}
	return *SingletonInterface;
}
