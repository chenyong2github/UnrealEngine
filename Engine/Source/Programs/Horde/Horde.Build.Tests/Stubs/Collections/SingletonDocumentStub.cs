// Copyright Epic Games, Inc. All Rights Reserved.

using Horde.Build.Models;
using Horde.Build.Utilities;
using System;
using System.Collections.Generic;
using System.Text;
using System.Threading.Tasks;

namespace Horde.Build.Tests.Stubs.Collections
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
