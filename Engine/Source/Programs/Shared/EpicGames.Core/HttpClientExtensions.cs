// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Net;
using System.Net.Http;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Threading;
using System.Threading.Tasks;
using System.Web;

namespace EpicGames.Core
{
	/// <summary>
	/// Extension methods for consuming REST APIs via JSON objects
	/// </summary>
	public static class HttpClientExtensions
	{
		/// <summary>
		/// Gets a resource from an HTTP endpoint and parses it as a JSON object
		/// </summary>
		/// <typeparam name="TResponse">The object type to return</typeparam>
		/// <param name="Client">The http client instance</param>
		/// <param name="Url">The url to retrieve</param>
		/// <param name="CancellationToken">Cancels the request</param>
		/// <returns>New instance of the object</returns>
		public static async Task<TResponse> GetAsync<TResponse>(this HttpClient Client, string Url, CancellationToken CancellationToken)
		{
			using (HttpResponseMessage Response = await Client.GetAsync(Url, CancellationToken))
			{
				Response.EnsureSuccessStatusCode();
				return await ParseJsonContent<TResponse>(Response);
			}
		}

		/// <summary>
		/// Posts an object to an HTTP endpoint as a JSON object
		/// </summary>
		/// <typeparam name="TRequest">The object type to post</typeparam>
		/// <param name="Client">The http client instance</param>
		/// <param name="Url">The url to post to</param>
		/// <param name="Object">The object to post</param>
		/// <param name="CancellationToken">Cancels the request</param>
		/// <returns>Response message</returns>
		public static async Task<HttpResponseMessage> PostAsync<TRequest>(this HttpClient Client, string Url, TRequest Object, CancellationToken CancellationToken)
		{
			return await Client.PostAsync(Url, ToJsonContent(Object), CancellationToken);
		}

		/// <summary>
		/// Posts an object to an HTTP endpoint as a JSON object, and parses the response object
		/// </summary>
		/// <typeparam name="TResponse">The object type to return</typeparam>
		/// <typeparam name="TRequest">The object type to post</typeparam>
		/// <param name="Client">The http client instance</param>
		/// <param name="Url">The url to post to</param>
		/// <param name="Request">The object to post</param>
		/// <param name="CancellationToken">Cancels the request</param>
		/// <returns>The response parsed into the requested type</returns>
		public static async Task<TResponse> PostAsync<TResponse, TRequest>(this HttpClient Client, string Url, TRequest Request, CancellationToken CancellationToken)
		{
			using (HttpResponseMessage Response = await PostAsync(Client, Url, Request, CancellationToken))
			{
				Response.EnsureSuccessStatusCode();
				return await ParseJsonContent<TResponse>(Response);
			}
		}

		/// <summary>
		/// Puts an object to an HTTP endpoint as a JSON object
		/// </summary>
		/// <typeparam name="TRequest">The object type to post</typeparam>
		/// <param name="Client">The http client instance</param>
		/// <param name="Url">The url to post to</param>
		/// <param name="Object">The object to post</param>
		/// <param name="CancellationToken">Cancels the request</param>
		/// <returns>Response message</returns>
		public static async Task<HttpResponseMessage> PutAsync<TRequest>(this HttpClient Client, string Url, TRequest Object, CancellationToken CancellationToken)
		{
			return await Client.PutAsync(Url, ToJsonContent(Object), CancellationToken);
		}

		/// <summary>
		/// Puts an object to an HTTP endpoint as a JSON object, and parses the response object
		/// </summary>
		/// <typeparam name="TResponse">The object type to return</typeparam>
		/// <typeparam name="TRequest">The object type to post</typeparam>
		/// <param name="Client">The http client instance</param>
		/// <param name="Url">The url to post to</param>
		/// <param name="Request">The object to post</param>
		/// <param name="CancellationToken">Cancels the request</param>
		/// <returns>The response parsed into the requested type</returns>
		public static async Task<TResponse> PutAsync<TResponse, TRequest>(this HttpClient Client, string Url, TRequest Request, CancellationToken CancellationToken)
		{
			using (HttpResponseMessage Response = await PutAsync(Client, Url, Request, CancellationToken))
			{
				Response.EnsureSuccessStatusCode();
				return await ParseJsonContent<TResponse>(Response);
			}
		}

		/// <summary>
		/// Converts an object to a JSON http content object
		/// </summary>
		/// <typeparam name="T">Type of the object to parse</typeparam>
		/// <param name="Object">The object instance</param>
		/// <returns>Http content object</returns>
		private static HttpContent ToJsonContent<T>(T Object)
		{
			return new StringContent(JsonSerializer.Serialize<T>(Object), Encoding.UTF8, "application/json");
		}

		/// <summary>
		/// Parses a HTTP response as a JSON object
		/// </summary>
		/// <typeparam name="T">Type of the object to parse</typeparam>
		/// <param name="Message">The message received</param>
		/// <returns>Parsed object instance</returns>
		private static async Task<T> ParseJsonContent<T>(HttpResponseMessage Message)
		{
			byte[] Bytes = await Message.Content.ReadAsByteArrayAsync();
			return JsonSerializer.Deserialize<T>(Bytes);
		}
	}
}
