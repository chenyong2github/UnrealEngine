// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading;
using Newtonsoft.Json;

#pragma warning disable SYSLIB0014

namespace Gauntlet
{
	public class IncomingRpcMessage
	{
		public string SenderId;
		public string Category;
		public string Payload;

		public IncomingRpcMessage() { }

		public IncomingRpcMessage(string InSenderId, string InCategory, string InPayload)
		{
			SenderId = InSenderId;
			Category = InCategory;
			Payload = InPayload;
		}

		public IncomingRpcMessage(IncomingRpcMessage CopyTarget)
			: this(CopyTarget.SenderId, CopyTarget.Category, CopyTarget.Payload)
		{ }
	}
	public class RpcTargetManager
	{
		protected int GauntletRpcListenPort = 23232;
		public RpcTargetManager() { }
		public RpcTargetManager(int InGauntletListenPort)
		{
			GauntletRpcListenPort = InGauntletListenPort;
		}

		// All currently managed Targets (processes)
		public List<RpcTarget> RpcTargets = new List<RpcTarget>();

		public string GetListenAddressAndPort()
		{
			string LocalIP = "127.0.0.1";
			IPHostEntry HostEntry = Dns.GetHostEntry(Dns.GetHostName());
			foreach (var TargetIp in HostEntry.AddressList)
			{
				if (TargetIp.AddressFamily == AddressFamily.InterNetwork)
				{
					LocalIP = TargetIp.ToString();
				}
			}
			return String.Format("{0}:{1}", LocalIP, GauntletRpcListenPort);
		}

		public class ResponseData
		{
			public string ResponseBody;
			public int ResponseCode;
			public ResponseData()
			{
				ResponseBody = "";
				ResponseCode = (int)HttpStatusCode.OK;
			}
		}

		public class RouteMapping
		{
			public string Path;
			public string Verb;
			public RouteMapping(string InPath, string InVerb = "post")
			{
				Path = InPath.ToLower();
				Verb = InVerb.ToLower();
			}

			public override bool Equals(object TargetObject)
			{
				return this.Equals(TargetObject as RouteMapping);
			}

			public bool Equals(RouteMapping InMap)
			{
				return (InMap.Path.ToLower() == Path.ToLower() && InMap.Verb.ToLower() == Verb.ToLower());
			}

			public override int GetHashCode()
			{
				string OverallString = string.Format("{0}", this);
				return OverallString.GetHashCode();
			}
			public override string ToString()
			{
				return string.Format("{0}|{1}", Path.ToLower(), Verb.ToLower()); ;
			}

		}

		protected HttpListener RequestListener;
		protected Thread HttpListenThread;
		public ConcurrentQueue<IncomingRpcMessage> IncomingMessageQueue = new ConcurrentQueue<IncomingRpcMessage>();
		protected Dictionary<RouteMapping, Delegate> HttpRoutes = new Dictionary<RouteMapping, Delegate>();
		private bool HasReceivedTerminationCall = false;
		public void ListenLoopThread()
		{

			RequestListener = new HttpListener();

			try
			{
				RequestListener.Prefixes.Add(String.Format("http://{0}/", GetListenAddressAndPort()));
				RegisterHttpRoute(new RouteMapping("/sendmessage"), HandleSendMessageEndpoint);
				RegisterHttpRoute(new RouteMapping("/updateip"), HandleUpdateIpEndpoint);
				RequestListener.Start();
			}
			catch (Exception Ex)
			{
				Log.Warning("Not able to open listener on external port unless running as admin. Error Message:" + Ex.Message);
				return;
			}
			Console.WriteLine(String.Format("Listening on port {0}...", GauntletRpcListenPort));

			// We always want to be listening for requests on this thread until we kill the loop.
			while (!HasReceivedTerminationCall && RequestListener.IsListening)
			{
				Log.Info("Listening Thread Started!");
				IAsyncResult ListenerContext = RequestListener.BeginGetContext(new AsyncCallback(ReceiveListenRequest), RequestListener);
				ListenerContext.AsyncWaitHandle.WaitOne();
			}

		}
		public void RegisterHttpRoute(RouteMapping Path, Delegate RouteDelegate, bool OverrideIfExists = false)
		{
			if (HttpRoutes.ContainsKey(Path))
			{
				if (OverrideIfExists)
				{
					Log.Info(string.Format("RpcTargetManager - Overwriting endpoint {0} | {1} with new delegate.", Path.Path, Path.Verb));
					HttpRoutes[Path] = RouteDelegate;
				}
				else
				{
					Log.Error(string.Format("RpcTargetManager - Unable to register endpoint {0} | {1}  as that route is already in use and bOverrideIfExists is set to false.", Path.Path, Path.Verb));
				}
			}
			else
			{
				Log.Info(string.Format("RpcTargetManager - Registering endpoint {0} with new delegate.", Path.ToString()));
				HttpRoutes.Add(Path, RouteDelegate);
			}
		}

		public void DeregisterHttpRoute(RouteMapping Path)
		{
			if (HttpRoutes.ContainsKey(Path))
			{
				Log.Info(string.Format("RpcTargetManager - Deregistering endpoint {0}", Path));
				HttpRoutes.Remove(Path);
			}
			else
			{
				Log.Info(string.Format("RpcTargetManager - Failed to deregister nonexistant endpoint {0}", Path));
			}
		}

		public virtual void HandleUpdateIpEndpoint(string MessageBody, ResponseData CallResponse)
		{
			CallResponse.ResponseCode = (int)HttpStatusCode.OK;
			Dictionary<string, string> IncomingMessage = JsonConvert.DeserializeObject<Dictionary<string, string>>(MessageBody);
			if (!IncomingMessage.ContainsKey("newip") || !IncomingMessage.ContainsKey("target"))
			{
				CallResponse.ResponseCode = (int)HttpStatusCode.BadRequest;
				CallResponse.ResponseBody = "UpdateTargetIp: Request requires both newip and target values in body";
				return;
			}
			else if (IPAddress.Parse(IncomingMessage["newip"]) == null)
			{
				CallResponse.ResponseCode = (int)HttpStatusCode.BadRequest;
				CallResponse.ResponseBody = "UpdateTargetIp: newip is invalid";
				return;
			}

			RpcTarget DesiredTarget = GetRpcTarget(IncomingMessage["target"]);
			if (DesiredTarget == null)
			{
				CallResponse.ResponseCode = (int)HttpStatusCode.BadRequest;
				CallResponse.ResponseBody = "UpdateTargetIp: Target not found";
				return;
			}

			DesiredTarget.BaseAddress = new Uri(IncomingMessage["newip"]);
			CallResponse.ResponseBody = String.Format("UpdateTargetIp: Target {0} IP updated to {1}", DesiredTarget.TargetName, DesiredTarget.BaseAddress);
			Log.Info(CallResponse.ResponseBody);
		}

		public virtual void HandleSendMessageEndpoint(string MessageBody, ResponseData CallResponse)
		{
			CallResponse.ResponseCode = (int)HttpStatusCode.OK;
			IncomingRpcMessage IncomingMessage = JsonConvert.DeserializeObject<IncomingRpcMessage>(MessageBody);
			if (IncomingMessage != null)
			{
				IncomingMessageQueue.Enqueue(IncomingMessage);
				CallResponse.ResponseBody = string.Format("New message enqueued! Body: {0}", IncomingMessage.ToString());
				Log.Info(CallResponse.ResponseBody);
			}
			else
			{
				CallResponse.ResponseBody = "Body could not be processed. Contents: " + MessageBody;
				CallResponse.ResponseCode = (int)HttpStatusCode.UnprocessableEntity;
				Log.Error(CallResponse.ResponseBody);
			}
		}

		public void ReceiveListenRequest(IAsyncResult ListenResult)
		{
			HttpListener RequestListener = (HttpListener)ListenResult.AsyncState;
			if (HasReceivedTerminationCall)
			{
				return;
			}
			HttpListenerContext ListenerContext = RequestListener.EndGetContext(ListenResult);
			HttpListenerRequest ListenerRequest = ListenerContext.Request;


			RouteMapping CallPath = new RouteMapping(ListenerContext.Request.Url.LocalPath, ListenerContext.Request.HttpMethod);
			Log.Info(string.Format("In ReceiveListenRequest callback for {0} | {1}", CallPath.Path, CallPath.Verb));
			ResponseData CallResponse = new ResponseData();
			CallResponse.ResponseBody = "This endpoint is not handled";

			foreach (KeyValuePair<RouteMapping, Delegate> RouteEntry in HttpRoutes)
			{
				if (CallPath.Equals(RouteEntry.Key))
				{
					using (HttpListenerResponse ListenerResponse = ListenerContext.Response)
					{
						ListenerResponse.Headers.Set("Content-Type", "text/plain");

						using (Stream ResponseStream = ListenerResponse.OutputStream)
						{
							StreamReader RequestReader = new StreamReader(ListenerRequest.InputStream, ListenerRequest.ContentEncoding);
							string RequestBody = RequestReader.ReadToEnd();
							RouteEntry.Value.DynamicInvoke(RequestBody, CallResponse);
							ListenerContext.Response.StatusCode = CallResponse.ResponseCode;
							byte[] ResponseBuffer = Encoding.UTF8.GetBytes(CallResponse.ResponseBody);
							ListenerResponse.ContentLength64 = ResponseBuffer.Length;
							ResponseStream.Write(ResponseBuffer, 0, ResponseBuffer.Length);
						}
					}
					return;
				}
			}
			using (HttpListenerResponse ListenerResponse = ListenerContext.Response)
			{
				ListenerResponse.Headers.Set("Content-Type", "text/plain");

				using (Stream ResponseStream = ListenerResponse.OutputStream)
				{
					ListenerContext.Response.StatusCode = (int)HttpStatusCode.NotFound;
					byte[] ResponseBuffer = Encoding.UTF8.GetBytes(CallResponse.ResponseBody);
					ListenerResponse.ContentLength64 = ResponseBuffer.Length;
					ResponseStream.Write(ResponseBuffer, 0, ResponseBuffer.Length);
				}
			}
		}

		public List<IncomingRpcMessage> GetNewMessages()
		{
			List<IncomingRpcMessage> NewMessages = new List<IncomingRpcMessage>();
			if (IncomingMessageQueue.Count == 0)
			{
				return NewMessages;
			}
			foreach (IncomingRpcMessage NewMessage in IncomingMessageQueue)
			{
				NewMessages.Add(new IncomingRpcMessage(NewMessage));
			}

			IncomingMessageQueue.Clear();
			return NewMessages;
		}

		public void StartListenThread()
		{
			if (HttpListenThread == null)
			{
				HttpListenThread = new Thread(ListenLoopThread);
				HttpListenThread.Start();
			}
		}

		public void ShutdownListenThread()
		{
			if (HttpListenThread != null)
			{
				HasReceivedTerminationCall = true;
				RequestListener.Abort();
			}
		}

		~RpcTargetManager()
		{
			if (HttpListenThread != null && HttpListenThread.ThreadState == ThreadState.Running)
			{
				HasReceivedTerminationCall = true;
				RequestListener.Abort();
			}
		}

		public void AddNewRpcTarget(string IpAddress = "localhost", int Port = 11223, RpcTargetType InType = RpcTargetType.Client, string InName = "")
		{
			if (GetRpcTarget(IpAddress, Port) == null && GetRpcTarget(InName) == null)
			{
				RpcTarget NewTarget;

				// Create instance of child class so we can cast freely back to child class if needed.
				switch (InType)
				{
					case RpcTargetType.Client:
						NewTarget = new ClientRpcTarget(IpAddress, Port, InType, InName);
						break;
					case RpcTargetType.Server:
						NewTarget = new ServerRpcTarget(IpAddress, Port, InType, InName);
						break;
					case RpcTargetType.Editor:
						NewTarget = new EditorRpcTarget(IpAddress, Port, InType, InName);
						break;
					default:
						throw new TestException("Unexpected RpcTargetType encountered! Value: {0}", InType.ToString());
				}

				RpcTargets.Add(NewTarget);
			}
		}

		public RpcTarget GetRpcTarget(string InIpAddress, int InPort)
		{
			foreach (RpcTarget Target in RpcTargets)
			{
				if (Target.BaseAddress.ToString() == InIpAddress && Target.BaseAddress.Port == InPort)
				{
					return Target;
				}
			}
			return null;
		}
		public RpcTarget GetRpcTarget(string InTargetName)
		{
			foreach (RpcTarget Registry in RpcTargets)
			{
				if (Registry.TargetName == InTargetName)
				{
					return Registry;
				}
			}
			return null;
		}

		/// <summary>
		/// Allows for getting the RPC Target by type - warning, this will only give you the first instance if there are more than one.
		/// If you need more specific handling use the name or ip/port overloads.
		/// </summary>
		/// <param name="TargetType"></param>
		/// <returns></returns>
		public RpcTarget GetRpcTarget(RpcTargetType TargetType)
		{
			foreach (RpcTarget Registry in RpcTargets)
			{
				if (Registry.TargetType == TargetType)
				{
					return Registry;
				}
			}
			return null;
		}
	}
}
