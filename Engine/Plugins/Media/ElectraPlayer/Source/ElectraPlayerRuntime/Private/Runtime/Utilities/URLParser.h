// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PlayerCore.h"


namespace Electra
{

	class IURLParser
	{
	public:
		static IURLParser* Create();
		virtual ~IURLParser() = default;

		virtual UEMediaError ParseURL(const FString& URL) = 0;

		virtual FString GetPath() const = 0;
		virtual void GetPathComponents(TArray<FString>& OutPathComponents) const = 0;

		virtual FString ResolveWith(const FString& RelativeURL) const = 0;

	protected:
		IURLParser() = default;
		IURLParser(const IURLParser&) = delete;
	};


} // namespace Electra


