// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Net;
using System.Net.Http;
using System.Text;
using System.Threading.Tasks;
using Newtonsoft.Json;
using static Gauntlet.HttpRequest;

// This is to suppress the WebRequest, HttpWebRequest, ServicePoint, and WebClient are obsolete warning/error.
#pragma warning disable SYSLIB0014

namespace Gauntlet
{
	public class RpcEntry
	{
		public string Name;
		public string Route;
		public string Verb;
		public string InputContentType;
		public List<RpcArgumentDesc> Args;
	}

	public class RpcArgumentDesc
	{
		public string Name;
		public string Type;
		public string Desc;
		public bool Optional;
	}

	public enum RpcTargetType
	{
		Client,
		Server,
		Editor
	}

	/// <summary>
	/// Base RPC Target class, usable for any process.
	/// RPC Target refers to an individual Unreal process (Client,Editor,Server) that is able to send and receive RPCs. A RPC Target contains 
	/// its own reusable HttpClient for communication with its associated Unreal process, as well as all of the RPC's that are available to 
	/// be called and their required parameters. It also contains a MessageBroker. This allows for messages to be subscribed to and a callback
	/// executed when that message is broadcast. 
	/// </summary>
	public class RpcTarget
	{
		public RpcTarget(string InIpAddress, int InPort, RpcTargetType InType = RpcTargetType.Client, string InName = "", int HttpRequestTimeoutInSeconds = 10)
		{
			HttpClient = new GauntletHttpClient
			{
				BaseAddress = new Uri(string.Format("http://{0}:{1}", InIpAddress, InPort)),
				Timeout = new TimeSpan(0, 0, 0, HttpRequestTimeoutInSeconds)
			};
			AvailableRpcs = new List<RpcEntry>();
			TargetType = InType;
			TargetName = InName;
		}
		protected GauntletHttpClient HttpClient;
		public readonly RpcTargetType TargetType;
		public string TargetName;
		public List<RpcEntry> AvailableRpcs;
		protected bool IsReadyForRpcs = false;

		public Uri BaseAddress
		{
			get
			{
				return HttpClient.BaseAddress;
			}
			set
			{
				HttpClient.BaseAddress = value;
			}
		}

		public RpcEntry GetRpc(string RpcName)
		{
			if (string.IsNullOrEmpty(RpcName))
			{
				return null;
			}

			foreach (var Rpc in AvailableRpcs)
			{
				if (String.Equals(Rpc.Name, RpcName, StringComparison.OrdinalIgnoreCase))
				{
					return Rpc;
				}
			}

			return null;
		}

		private string GetRpcRoute(string RpcName)
		{
			RpcEntry TargetRpc = GetRpc(RpcName);
			if (TargetRpc == null)
			{
				return String.Empty;
			}
			return TargetRpc.Route;
		}

		// calls a target's /listrpcs endpoint to enumerate all available endpoints for that target.
		public bool UpdateRpcRegistry()
		{
			try
			{
				LogInfo("Updating RPC Registry for {0}", TargetName);

				HttpResponseMessage Response = HttpClient.GetRequestAsync("listrpcs").GetAwaiter().GetResult();

				// Will throw exception if http request fails
				Response.EnsureSuccessStatusCode();

				// extract response data
				var JsonResponseString = Response.Content.ReadAsStringAsync().GetAwaiter().GetResult();
				AvailableRpcs = JsonConvert.DeserializeObject<List<RpcEntry>>(JsonResponseString);

				// request successful, but no RPC's could be parsed
				if (AvailableRpcs.Count < 1)
				{
					LogError("Request Successful, but could not find any RPCs in Response.");
				}
			}
			catch (Exception Ex)
			{
				LogError("Could not Update RPC Registry - http Call Failed. Reason: {0}", Ex.Message);
				// Not sure what to put here as RpcRegistry could quite possibly not be initialized by design.
				return false;
			}
			return true;
		}

		private bool RpcArgsAreValid(RpcEntry RpcDefinition, Dictionary<string, object> InArgsToValidate)
		{
			//  no args to validate
			if (RpcDefinition.Args == null)
			{
				return true;
			}

			foreach (RpcArgumentDesc ArgDesc in RpcDefinition.Args)
			{
				if (!InArgsToValidate.ContainsKey(ArgDesc.Name) && !ArgDesc.Optional)
				{
					LogError(string.Format("Rpc {0} lists required argument {1} of type {2} which was not provided. Please either repair argument list or update Rpc Definition.",
						RpcDefinition.Name, ArgDesc.Name, ArgDesc.Type));
					continue;
				}
				object ArgValue = InArgsToValidate[ArgDesc.Name];
				if (ArgValue == null)
				{
					LogError(string.Format("Rpc {0} argument {1} of expected type {2} is null. If null is a possible value please make value optional in Rpc instead.",
							RpcDefinition.Name, ArgDesc.Name, ArgDesc.Type));
					continue;
				}
				bool CastSucceeded = true;
				switch (ArgDesc.Type.ToLower())
				{
					case "bool":
						{
							CastSucceeded = ArgValue is bool;
							break;
						}
					case "int":
						{
							CastSucceeded = ArgValue is int;
							break;
						}
					case "float":
						{
							CastSucceeded = ArgValue is float;
							break;
						}
					default:
						{
							// If listed object type is something not handled in this switch, either add a new handler or we basically just
							// accept that it is a var type that is not going to be automatically linted. Or it's a string which serializing will automatically handle.
							break;
						}
				}

				if (!CastSucceeded)
				{
					LogError(string.Format("Rpc {0} unable to cast arg {1} value \"{2}\" to defined type {3}. Please either repair argument list or update Rpc Definition.",
						RpcDefinition.Name, ArgDesc.Name, ArgValue, ArgDesc.Type));
					return false;
				}
			}
			return true;
		}

		/// <summary>
		/// Calls the requested RPC ID with no body.
		/// </summary>
		/// <param name="RpcId"></param>
		/// <param name="HttpFailureThrowsException"></param>
		/// <returns>Http response as a string</returns>
		public async Task<string> CallRpcAsync(string RpcId, bool HttpFailureThrowsException = true)
		{
			return await CallRpcAsync(RpcId, new Dictionary<string, object>(), HttpFailureThrowsException);
		}

		/// <summary>
		/// Calls the requested RPC ID and attaches passed in args as the request body.
		/// </summary>
		/// <param name="RpcId"></param>
		/// <param name="InArgs"></param>
		/// <param name="TimeoutInMilliseconds"></param>
		/// <param name="HttpFailureThrowsException"></param>
		/// <returns>Http response as a string</returns>
		public async Task<string> CallRpcAsync(string RpcId, Dictionary<string, object> InArgs, bool HttpFailureThrowsException = true)
		{
			try
			{
				RpcEntry RpcDetails = GetRpc(RpcId);

				if (RpcDetails == null)
				{
					throw new TestException("RPC Not Found.");
				}

				if (InArgs == null)
				{
					InArgs = new Dictionary<string, object>();
				}

				if (!RpcArgsAreValid(RpcDetails, InArgs))
				{
					return string.Empty;
				}

				using StringContent JsonContent = new(System.Text.Json.JsonSerializer.Serialize(InArgs),
					Encoding.ASCII, "application/json");

				var RpcVerb = RpcDetails.Verb == "1" ? HttpMethod.Get : HttpMethod.Post;

				HttpRequestMessage RequestMessage = new HttpRequestMessage(RpcVerb, RpcDetails.Route);
				RequestMessage.Content = JsonContent;

				using HttpResponseMessage HttpResponse = await HttpClient.SendRequestAsync(RequestMessage);

				try
				{
					// Will throw exception if http request fails
					HttpResponse.EnsureSuccessStatusCode();
				}
				catch (Exception)
				{
					string FailureMessage = string.Format("Http Call Unsuccessful! Request Uri: {0}{1}, Response Code: {2}, Reason: {3}",
						HttpClient.BaseAddress, RpcDetails.Route, HttpResponse.StatusCode, HttpResponse.ReasonPhrase);
					if (HttpFailureThrowsException)
					{
						LogError(FailureMessage);
						throw new HttpListenerException((int)HttpResponse.StatusCode, FailureMessage);
					}
					else
					{
						LogWarning(FailureMessage);
					}
				}

				string JsonResponse = await HttpResponse.Content.ReadAsStringAsync();

				// return the response as a string - the caller will need to know how to serialize it into what it needs.
				return JsonResponse;
			}
			catch (HttpRequestException Hx)
			{
				throw new TestException(LogError("RPC call failed! Process may have crashed or become unresponsive. RPC ID: {0}, Message: {1}, Stack Trace: {2}",
					RpcId, Hx.Message, Hx.StackTrace));
			}
			catch (TestException)
			{
				throw;
			}
			catch (Exception Ex)
			{
				throw new TestException(LogError("Error calling RPC! RPC ID: {0}, Message: {1}, Stack Trace: {2}",
					RpcId, Ex.Message, Ex.StackTrace));
			}
		}

		/// <summary>
		/// Calls the requested RPC ID with no body. Synchronous version of CallRpcAsync for ease of use in non-async functions.
		/// </summary>
		/// <param name="RpcId"></param>
		/// <param name="HttpFailureThrowsException"></param>
		/// <returns>Http response as a string</returns>
		public string CallRpc(string RpcId, bool HttpFailureThrowsException = true)
		{
			return CallRpcAsync(RpcId, new Dictionary<string, object>(), HttpFailureThrowsException).GetAwaiter().GetResult();
		}

		/// <summary>
		/// Calls the requested RPC ID and attaches passed in args as the request body.
		/// </summary>
		/// <param name="RpcId"></param>
		/// <param name="InArgs"></param>
		/// <param name="TimeoutInMilliseconds"></param>
		/// <param name="HttpFailureThrowsException"></param>
		/// <returns>Http response as a string</returns>
		public string CallRpc(string RpcId, Dictionary<string, object> InArgs, bool HttpFailureThrowsException = true)
		{

			return CallRpcAsync(RpcId, InArgs, HttpFailureThrowsException).GetAwaiter().GetResult();

		}

		// Logging Helpers that prepends the TargetName to the log message to help identify which process it came from
		// returns the formatted message
		public string LogInfo(string Format, params object[] Args)
		{
			return LogMessage(Microsoft.Extensions.Logging.LogLevel.Information, Format, Args);
		}

		public string LogWarning(string Format, params object[] Args)
		{
			return LogMessage(Microsoft.Extensions.Logging.LogLevel.Warning, Format, Args);
		}

		public string LogError(string Format, params object[] Args)
		{
			return LogMessage(Microsoft.Extensions.Logging.LogLevel.Error, Format, Args);
		}
		public string LogVerbose(string Format, params object[] Args)
		{
			return LogMessage(Microsoft.Extensions.Logging.LogLevel.Debug, Format, Args);
		}

		public string LogMessage(Microsoft.Extensions.Logging.LogLevel LogLevel, string Format, params object[] Args)
		{
			// Prepend TargetName as long as it exists.
			if (!string.IsNullOrEmpty(TargetName))
			{
				Format = string.Format("{0} - {1}", TargetName, Format);
			}

			switch (LogLevel)
			{
				case Microsoft.Extensions.Logging.LogLevel.Warning:
					Log.Warning(Format, Args);
					break;
				case Microsoft.Extensions.Logging.LogLevel.Error:
					Log.Error(Format, Args);
					break;
				case Microsoft.Extensions.Logging.LogLevel.Debug:
					Log.Verbose(Format, Args);
					break;
				default:
					Log.Info(Format, Args);
					break;
			}

			return string.Format(Format, Args);
		}
	}

	/// <summary>
	/// To be used with remote Editor processes. Enables auto filtering of the Action Library
	/// </summary>
	public class EditorRpcTarget : RpcTarget
	{
		public EditorRpcTarget(string InIpAddress, int InPort, RpcTargetType InType = RpcTargetType.Editor, string InName = "")
			: base(InIpAddress, InPort, InType, InName)
		{
		}

	}
	/// <summary>
	/// To be used with remote Server processes. Enables auto filtering of the Action Library
	/// </summary>
	public class ServerRpcTarget : RpcTarget
	{
		public ServerRpcTarget(string InIpAddress, int InPort, RpcTargetType InType = RpcTargetType.Server, string InName = "")
			: base(InIpAddress, InPort, InType, InName)
		{
		}
	}
	/// <summary>
	/// To be used with remote Client processes. Enables auto filtering of the Action Library
	/// </summary>
	public class ClientRpcTarget : RpcTarget
	{
		public ClientRpcTarget(string InIpAddress, int InPort, RpcTargetType InType = RpcTargetType.Client, string InName = "")
			: base(InIpAddress, InPort, InType, InName)
		{
		}
	}
}
