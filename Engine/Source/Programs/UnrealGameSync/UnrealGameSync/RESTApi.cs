// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Net;
using System.Net.Mime;
using System.Text;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;

namespace UnrealGameSync
{
	class RestException : Exception
	{
		public RestException(string Method, string Uri, Exception InnerException)
			: base(String.Format("Error executing {0} {1}", Method, Uri), InnerException)
		{
		}

		public override string ToString()
		{
			return String.Format("{0}\n\n{1}", Message, InnerException!.ToString());
		}
	}

	public static class RESTApi
	{
		private static async Task<string> SendRequestInternal(string Url, string Method, string? RequestBody, CancellationToken CancellationToken)
		{
			HttpWebRequest Request = (HttpWebRequest)WebRequest.Create(Url);
			Request.ContentType = "application/json";
			Request.Method = Method;

			// Add json to request body
			if (!string.IsNullOrEmpty(RequestBody))
			{
				if (Method == "POST" || Method == "PUT")
				{
					byte[] bytes = Encoding.UTF8.GetBytes(RequestBody);
					using (Stream RequestStream = Request.GetRequestStream())
					{
						await RequestStream.WriteAsync(bytes, 0, bytes.Length, CancellationToken);
					}
				}
			}
			try
			{
				using (WebResponse Response = Request.GetResponse())
				{
					byte[] Data;
					using (MemoryStream Buffer = new MemoryStream())
					{
						await Response.GetResponseStream().CopyToAsync(Buffer, CancellationToken);
						Data = Buffer.ToArray();
					}
					return Encoding.UTF8.GetString(Data);
				}
			}
			catch (Exception Ex)
			{
				throw new RestException(Method, Request.RequestUri.ToString(), Ex);
			}
		}

		public static Task<string> PostAsync(string Url, string RequestBody, CancellationToken CancellationToken)
		{
			return SendRequestInternal(Url, "POST", RequestBody, CancellationToken);
		}

		public static Task<string> GetAsync(string Url, CancellationToken CancellationToken)
		{
			return SendRequestInternal(Url, "GET", null, CancellationToken);
		}

		public static async Task<T> GetAsync<T>(string Url, CancellationToken CancellationToken)
		{
			return JsonSerializer.Deserialize<T>(await GetAsync(Url, CancellationToken), Utility.DefaultJsonSerializerOptions)!;
		}

		public static Task<string> PutAsync<T>(string Url, T Object, CancellationToken CancellationToken)
		{
			return SendRequestInternal(Url, "PUT", JsonSerializer.Serialize(Object), CancellationToken);
		}
	}
}
