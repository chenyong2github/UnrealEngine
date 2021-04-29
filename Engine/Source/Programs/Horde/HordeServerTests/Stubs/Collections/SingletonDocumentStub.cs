// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Models;
using HordeServer.Utilities;
using System;
using System.Collections.Generic;
using System.Text;
using System.Threading.Tasks;

namespace HordeServerTests.Stubs.Collections
{
	class SingletonDocumentStub<T> : ISingletonDocument<T> where T : SingletonBase, new()
	{
		T Document = new T();

		public Task<T> GetAsync()
		{
			return Task.FromResult(Document);
		}

		public Task<bool> TryUpdateAsync(T Value)
		{
			Document = Value;
			return Task.FromResult(true);
		}
	}
}
