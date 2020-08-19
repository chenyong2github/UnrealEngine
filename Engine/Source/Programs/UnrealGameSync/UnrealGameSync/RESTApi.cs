// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using System.Web.Script.Serialization;

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
			return String.Format("{0}\n\n{1}", Message, InnerException.ToString());
		}
	}

	public static class RESTApi
	{
		private static string SendRequestInternal(string URI, string Resource, string Method, string RequestBody = null, params string[] QueryParams)
		{
			// set up the query string
			StringBuilder TargetURI = new StringBuilder(string.Format("{0}/api/{1}", URI, Resource));
			if (QueryParams.Length != 0)
			{
				TargetURI.Append("?");
				for (int Idx = 0; Idx < QueryParams.Length; Idx++)
				{
					TargetURI.Append(QueryParams[Idx]);
					if (Idx != QueryParams.Length - 1)
					{
						TargetURI.Append("&");
					}
				}
			}

			HttpWebRequest Request = (HttpWebRequest)WebRequest.Create(TargetURI.ToString());
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
						RequestStream.Write(bytes, 0, bytes.Length);
					}
				}
			}
			try
			{
				using (WebResponse Response = Request.GetResponse())
				{
					string ResponseContent;
					using (StreamReader ResponseReader = new StreamReader(Response.GetResponseStream(), Encoding.UTF8))
					{
						ResponseContent = ResponseReader.ReadToEnd();
					}
					return ResponseContent;
				}
			}
			catch (Exception Ex)
			{
				throw new RestException(Method, Request.RequestUri.ToString(), Ex);
			}
		}

		public static string POST(string URI, string Resource, string RequestBody = null, params string[] QueryParams)
		{
			return SendRequestInternal(URI, Resource, "POST", RequestBody, QueryParams);
		}

		public static string GET(string URI, string Resource, params string[] QueryParams)
		{
			return SendRequestInternal(URI, Resource, "GET", null, QueryParams);
		}

		public static T GET<T>(string URI, string Resource, params string[] QueryParams)
		{
			return new JavaScriptSerializer { MaxJsonLength = 86753090 }.Deserialize<T>(SendRequestInternal(URI, Resource, "GET", null, QueryParams));
		}

		public static string PUT<T>(string URI, string Resource, T Object, params string[] QueryParams)
		{
			return SendRequestInternal(URI, Resource, "PUT", new JavaScriptSerializer().Serialize(Object), QueryParams);
		}
	}
}
