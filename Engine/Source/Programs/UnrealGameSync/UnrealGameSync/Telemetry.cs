// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net;
using System.Reflection;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Web;
using System.Web.Script.Serialization;

namespace UnrealGameSync
{
	/// <summary>
	/// Interface for a telemetry sink
	/// </summary>
	public interface ITelemetrySink : IDisposable
	{
		/// <summary>
		/// Sends a telemetry event with the given information
		/// </summary>
		/// <param name="EventName">Name of the event</param>
		/// <param name="Attributes">Arbitrary object to include in the payload</param>
		void SendEvent(string EventName, object Attributes);
	}

	/// <summary>
	/// Telemetry sink that discards all events
	/// </summary>
	class NullTelemetrySink : ITelemetrySink
	{
		/// <inheritdoc/>
		public void Dispose()
		{
		}

		/// <inheritdoc/>
		public void SendEvent(string EventName, object Attributes)
		{
		}
	}

	/// <summary>
	/// Epic internal telemetry sink using the data router
	/// </summary>
	class EpicTelemetrySink : ITelemetrySink
	{
		/// <summary>
		/// Combined url to post event streams to
		/// </summary>
		string Url;

		/// <summary>
		/// Lock used to modify the event queue
		/// </summary>
		object LockObject = new object();

		/// <summary>
		/// Whether a flush is queued
		/// </summary>
		bool bHasPendingFlush = false;

		/// <summary>
		/// List of pending events
		/// </summary>
		List<string> PendingEvents = new List<string>();

		/// <summary>
		/// The log writer to use
		/// </summary>
		TextWriter Log;

		/// <summary>
		/// Constructor
		/// </summary>
		public EpicTelemetrySink(string Url, TextWriter Log)
		{
			this.Url = Url;
			this.Log = Log;

			Log.WriteLine("Posting to URL: {0}", Url);
		}

		/// <inheritdoc/>
		public void Dispose()
		{
			Flush();
		}

		/// <inheritdoc/>
		public void Flush()
		{
			for (; ; )
			{
				lock (LockObject)
				{
					if (!bHasPendingFlush)
					{
						break;
					}
				}
				Thread.Sleep(10);
			}
		}

		/// <inheritdoc/>
		public void SendEvent(string EventName, object Attributes)
		{
			string AttributesText = new JavaScriptSerializer().Serialize(Attributes);
			if (AttributesText[0] != '{')
			{
				throw new Exception("Expected event data with named properties");
			}

			string EventText = AttributesText.Insert(1, String.Format("\"EventName\":\"{0}\",", HttpUtility.JavaScriptStringEncode(EventName)));
			lock (PendingEvents)
			{
				PendingEvents.Add(EventText);
				if (!bHasPendingFlush)
				{
					ThreadPool.QueueUserWorkItem(Obj => BackgroundFlush());
					bHasPendingFlush = true;
				}
			}
		}

		/// <summary>
		/// Synchronously sends a telemetry event
		/// </summary>
		void BackgroundFlush()
		{
			for (; ; )
			{
				try
				{
					// Generate the content for this event
					List<string> Events = new List<string>();
					lock (LockObject)
					{
						if (PendingEvents.Count == 0)
						{
							bHasPendingFlush = false;
							break;
						}

						Events.AddRange(PendingEvents);
						PendingEvents.Clear();
					}

					// Print all the events we're sending
					foreach (string Event in Events)
					{
						Log.WriteLine("Sending Event: {0}", Event);
					}

					// Convert the content to UTF8
					string ContentText = String.Format("{{\"Events\":[{0}]}}", String.Join(",", Events));
					byte[] Content = Encoding.UTF8.GetBytes(ContentText);

					// Post the event data
					HttpWebRequest Request = (HttpWebRequest)WebRequest.Create(Url);
					Request.Method = "POST";
					Request.ContentType = "application/json";
					Request.UserAgent = "ue/ugs";
					Request.Timeout = 5000;
					Request.ContentLength = Content.Length;
					Request.ContentType = "application/json";
					using (Stream RequestStream = Request.GetRequestStream())
					{
						RequestStream.Write(Content, 0, Content.Length);
					}

					// Wait for the response and dispose of it immediately
					using (HttpWebResponse Response = (HttpWebResponse)Request.GetResponse())
					{
						Log.WriteLine("Response: {0}", (int)Response.StatusCode);
					}
				}
				catch (WebException Ex)
				{
					// Handle errors. Any non-200 responses automatically generate a WebException.
					HttpWebResponse Response = (HttpWebResponse)Ex.Response;
					if (Response == null)
					{
						Log.WriteLine("Exception while attempting to send event: {0}", Ex.ToString());
					}
					else
					{
						string ResponseText;
						using (Stream ResponseStream = Response.GetResponseStream())
						{
							MemoryStream MemoryStream = new MemoryStream();
							ResponseStream.CopyTo(MemoryStream);
							ResponseText = Encoding.UTF8.GetString(MemoryStream.ToArray());
						}
						Log.WriteLine("EpicTelemetrySink: Failed to send analytics event. Code = {0}. Desc = {1}. Response = {2}.", (int)Response.StatusCode, Response.StatusDescription, ResponseText);
					}
				}
				catch (Exception Ex)
				{
					Log.WriteLine("Exception while attempting to send event: {0}", Ex.ToString());
				}
			}
		}
	}

	/// <summary>
	/// Global telemetry static class
	/// </summary>
	public static class Telemetry
	{
		/// <summary>
		/// The current telemetry provider
		/// </summary>
		public static ITelemetrySink ActiveSink
		{
			get; set;
		}

		/// <summary>
		/// Sends a telemetry event with the given information
		/// </summary>
		/// <param name="EventName">Name of the event</param>
		/// <param name="Attributes">Arbitrary object to include in the payload</param>
		public static void SendEvent(string EventName, object Attributes)
		{
			ActiveSink.SendEvent(EventName, Attributes);
		}
	}
}
