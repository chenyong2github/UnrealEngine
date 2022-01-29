// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Net.Http;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace EpicGames.Horde.Auth
{
	/// <summary>
	/// Empty implementation of <see cref="IAuthProvider"/>
	/// </summary>
	public class NullAuthProvider<T> : IAuthProvider<T>
	{
		/// <inheritdoc/>
		public Task AddAuthorizationAsync(HttpRequestMessage Request, CancellationToken CancellationToken) => Task.CompletedTask;
	}
}
