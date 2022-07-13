// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

class UReplicationSystem;
class UObjectReplicationBridge;

namespace UE::Net
{
	class FNetTokenStoreState;
	class FStringTokenStore;

	struct FNetObjectResolveContext
	{
		FNetTokenStoreState* RemoteNetTokenStoreState;
	};

	namespace Private
	{
		class FNetExportContext;
	}
}