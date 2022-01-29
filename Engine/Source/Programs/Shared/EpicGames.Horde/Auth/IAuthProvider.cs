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
	/// Interface for an authorization provider.
	/// </summary>
	public interface IAuthProvider
	{
		/// <summary>
		/// Add authorization headers to the given request
		/// </summary>
		/// <param name="Request">Request to modify</param>
		/// <param name="CancellationToken">Cancellation token for the request</param>
		public Task AddAuthorizationAsync(HttpRequestMessage Request, CancellationToken CancellationToken);
	}

	/// <summary>
	/// Typed authorization provider. For use with dependency injection.
	/// </summary>
	/// <typeparam name="T"></typeparam>
	public interface IAuthProvider<T> : IAuthProvider
	{
	}

	/// <summary>
	/// Implementation of <see cref="IAuthProvider{T}"/> which wraps an underlying auth provider implementation
	/// </summary>
	/// <typeparam name="T">Nominal type for the implementation</typeparam>
	public class TypedAuthProvider<T> : IAuthProvider<T>
	{
		public IAuthProvider AuthProvider;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="AuthProvider"></param>
		public TypedAuthProvider(IAuthProvider AuthProvider)
		{
			this.AuthProvider = AuthProvider;
		}

		/// <inheritdoc/>
		public Task AddAuthorizationAsync(HttpRequestMessage Request, CancellationToken CancellationToken)
		{
			return AuthProvider.AddAuthorizationAsync(Request, CancellationToken);
		}
	}
}
