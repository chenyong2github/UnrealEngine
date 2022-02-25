// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Serialization;
using Microsoft.Extensions.Options;
using System;
using System.Collections.Generic;
using System.ComponentModel.DataAnnotations;
using System.Net;
using System.Net.Http;
using System.Text;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;

namespace EpicGames.Horde.Auth
{
	/// <summary>
	/// Options for authenticating particular requests
	/// </summary>
	public interface ITokenAuthOptions
	{
		/// <summary>
		/// Bearer token for auth
		/// </summary>
		string Token { get; }
	}

	/// <summary>
	/// Http message handler which adds an OAuth authorization header using a cached/periodically refreshed bearer token
	/// </summary>
	public class TokenHandler<T> : HttpClientHandler
	{
		ITokenAuthOptions Options;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Options"></param>
		public TokenHandler(ITokenAuthOptions Options)
		{
			this.Options = Options;
		}

		/// <inheritdoc/>
		protected override async Task<HttpResponseMessage> SendAsync(HttpRequestMessage Request, CancellationToken CancellationToken)
		{
			Request.Headers.Add("Authorization", $"Bearer {Options.Token}");
			return await base.SendAsync(Request, CancellationToken);
		}
	}
}
