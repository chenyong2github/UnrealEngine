// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IRivermaxManager.h"


namespace UE::RivermaxCore::Private
{
	class FRivermaxManager : public UE::RivermaxCore::IRivermaxManager
	{
	public:
		FRivermaxManager();
		~FRivermaxManager();

	public:
		//~ Begin IRivermaxManager interface
		virtual bool IsInitialized() const override;
		//~ End IRivermaxManager interface

	private:
		bool LoadRivermaxLibrary();

	private:
		bool bIsInitialized = false;
		void* LibraryHandle = nullptr;
	};
}


