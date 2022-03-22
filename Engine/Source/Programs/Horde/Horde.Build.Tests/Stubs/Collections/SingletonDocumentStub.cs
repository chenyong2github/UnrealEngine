// Copyright Epic Games, Inc. All Rights Reserved.

using Horde.Build.Models;
using Horde.Build.Utilities;
using System.Threading.Tasks;

namespace Horde.Build.Tests.Stubs.Collections
{
	class SingletonDocumentStub<T> : ISingletonDocument<T> where T : SingletonBase, new()
	{
		T _document = new T();

		public Task<T> GetAsync()
		{
			return Task.FromResult(_document);
		}

		public Task<bool> TryUpdateAsync(T value)
		{
			_document = value;
			return Task.FromResult(true);
		}
	}
}
