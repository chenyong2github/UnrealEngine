// Copyright Epic Games, Inc. All Rights Reserved.

using Microsoft.Win32;
using System;
using System.IO;
using System.IO.Pipes;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading;
using System.Windows.Forms;

namespace UnrealGameSync
{
	enum AutomationRequestType
	{
		SyncProject = 0,
		FindProject = 1,
		OpenProject = 2,
		ExecCommand = 3,
		OpenIssue = 4,
	}

	class AutomationRequestInput
	{
		public AutomationRequestType Type;
		public byte[] Data;

		public AutomationRequestInput(AutomationRequestType Type, byte[] Data)
		{
			this.Type = Type;
			this.Data = Data;
		}

		public static AutomationRequestInput Read(Stream InputStream)
		{
			BinaryReader Reader = new BinaryReader(InputStream);
			
			int Type = Reader.ReadInt32();
			int InputSize = Reader.ReadInt32();
			byte[] Input = Reader.ReadBytes(InputSize);

			return new AutomationRequestInput((AutomationRequestType)Type, Input);
		}

		public void Write(Stream OutputStream)
		{
			BinaryWriter Writer = new BinaryWriter(OutputStream);

			Writer.Write((int)Type);
			Writer.Write(Data.Length);
			Writer.Write(Data);
		}
	}

	enum AutomationRequestResult
	{
		Ok,
		Invalid,
		Busy,
		Canceled,
		Error,
		NotFound
	}

	class AutomationRequestOutput
	{
		public AutomationRequestResult Result;
		public byte[] Data;

		public AutomationRequestOutput(AutomationRequestResult Result)
		{
			this.Result = Result;
			this.Data = new byte[0];
		}

		public AutomationRequestOutput(AutomationRequestResult Result, byte[] Data)
		{
			this.Result = Result;
			this.Data = Data;
		}

		public static AutomationRequestOutput Read(Stream InputStream)
		{
			using(BinaryReader Reader = new BinaryReader(InputStream))
			{
				AutomationRequestResult Result = (AutomationRequestResult)Reader.ReadInt32();
				int DataSize = Reader.ReadInt32();
				byte[] Data = Reader.ReadBytes(DataSize);
				return new AutomationRequestOutput(Result, Data);
			}
		}

		public void Write(Stream OutputStream)
		{
			using(BinaryWriter Writer = new BinaryWriter(OutputStream))
			{
				Writer.Write((int)Result);
				Writer.Write(Data.Length);
				Writer.Write(Data);
			}
		}
	}

	class AutomationRequest : IDisposable
	{
		public AutomationRequestInput Input;
		public AutomationRequestOutput Output;
		public ManualResetEventSlim Complete;

		public AutomationRequest(AutomationRequestInput Input)
		{
			this.Input = Input;
			this.Complete = new ManualResetEventSlim(false);
		}

		public void SetOutput(AutomationRequestOutput Output)
		{
			this.Output = Output;
			Complete.Set();
		}

		public void Dispose()
		{
			if(Complete != null)
			{
				Complete.Dispose();
				Complete = null;
			}
		}
	}

	class AutomationServer : IDisposable
	{
		TcpListener Listener;
		public const int DefaultPortNumber = 30422;

		NamedPipeServerStream IPCStream;
		static UnicodeEncoding StreamEncoding = new UnicodeEncoding();
		const string UGSChannel = @"\.\pipe\UGSChannel";

		Thread UriThread;
		Thread TcpThread;

		EventWaitHandle ShutdownEvent;		
		Action<AutomationRequest> PostRequest;

		bool bDisposing;
		TextWriter Log;
		string CommandLineUri;

		public AutomationServer(Action<AutomationRequest> PostRequest, TextWriter Log, string Uri)
		{
			try
			{
				ShutdownEvent = new ManualResetEvent(false);
				this.PostRequest = PostRequest;
				this.Log = Log;
				this.CommandLineUri = Uri;
				
				// IPC named pipe
				IPCStream = new NamedPipeServerStream(UGSChannel, PipeDirection.In, 1, PipeTransmissionMode.Message, PipeOptions.Asynchronous);

				// TCP listener setup
				int PortNumber = GetPortNumber();
				if (PortNumber > 0)
				{
					try
					{
						Listener = new TcpListener(IPAddress.Loopback, PortNumber);
						Listener.Start();
						TcpThread = new Thread(() => RunTcp());
						TcpThread.Start();

					}
					catch (Exception Ex)
					{
						Listener = null;
						Log.WriteLine("Unable to start automation server tcp listener: {0}", Ex.ToString());
					}
				}

				UriThread = new Thread(() => RunUri());
				UriThread.Start();
			}
			catch (Exception Ex)
			{
				Log.WriteLine("Unable to start automation server: {0}", Ex.ToString());
			}
		}

		public static void SetPortNumber(int PortNumber)
		{
			if(PortNumber <= 0)
			{
				Utility.DeleteRegistryKey(Registry.CurrentUser, "Software\\Epic Games\\UnrealGameSync", "AutomationPort");
			}
			else
			{
				Registry.SetValue("HKEY_CURRENT_USER\\Software\\Epic Games\\UnrealGameSync", "AutomationPort", PortNumber);
			}
		}

		public static int GetPortNumber()
		{
			object PortValue = Registry.GetValue("HKEY_CURRENT_USER\\Software\\Epic Games\\UnrealGameSync", "AutomationPort", null);
			if (PortValue != null && PortValue is int)
			{
				return (int)PortValue;
			}
			else
			{
				return -1;
			}
		}

		void RunUri()
		{
			// Handle main process command line URI request
			if (!string.IsNullOrEmpty(CommandLineUri))
			{
				HandleUri(CommandLineUri);
			}

			for (;;)
			{
				try
				{

					IAsyncResult IPCResult = IPCStream.BeginWaitForConnection(null, null);

					int WaitResult = WaitHandle.WaitAny(new WaitHandle[] { ShutdownEvent, IPCResult.AsyncWaitHandle });

					// Shutting down
					if (WaitResult == 0)
					{
						break;
					}

					try
					{
						IPCStream.EndWaitForConnection(IPCResult);

						Log.WriteLine("Accepted Uri connection");

						// Read URI
						string Uri = ReadString(IPCStream);

						Log.WriteLine("Received Uri: {0}", Uri);

						IPCStream.Disconnect();

						HandleUri(Uri);

					}
					catch (Exception Ex)
					{
						Log.WriteLine("Exception: {0}", Ex.ToString());
					}
				}
				catch (Exception Ex)
				{
					if (!bDisposing)
					{
						Log.WriteLine("Exception: {0}", Ex.ToString());
					}
				}
			}
		}


		void RunTcp()
		{
			
			for (;;)
			{
				try
				{

					IAsyncResult TCPResult = Listener.BeginAcceptTcpClient(null, null);

					int WaitResult = WaitHandle.WaitAny(new WaitHandle[] { ShutdownEvent, TCPResult.AsyncWaitHandle });

					// Shutting down
					if (WaitResult == 0)
					{
						break;
					}

					try
					{
						TcpClient Client = Listener.EndAcceptTcpClient(TCPResult);

						Log.WriteLine("Accepted connection from {0}", Client.Client.RemoteEndPoint);

						NetworkStream Stream = Client.GetStream();

						AutomationRequestInput Input = AutomationRequestInput.Read(Stream);
						Log.WriteLine("Received input: {0} (+{1} bytes)", Input.Type, Input.Data.Length);

						AutomationRequestOutput Output;
						using (AutomationRequest Request = new AutomationRequest(Input))
						{
							PostRequest(Request);
							Request.Complete.Wait();
							Output = Request.Output;
						}

						Output.Write(Stream);
						Log.WriteLine("Sent output: {0} (+{1} bytes)", Output.Result, Output.Data.Length);
					}
					catch (Exception Ex)
					{
						Log.WriteLine("Exception: {0}", Ex.ToString());
					}
					finally
					{
						TCPResult = null;
						Log.WriteLine("Closed connection.");
					}
				}
				catch (Exception Ex)
				{
					if (!bDisposing)
					{
						Log.WriteLine("Exception: {0}", Ex.ToString());
					}
				}
			}
		}

		void HandleUri(string Uri)
		{
			try
			{
				UriResult Result = UriHandler.HandleUri(Uri);
				if (!Result.Success)
				{
					if (!string.IsNullOrEmpty(Result.Error))
					{
						MessageBox.Show(String.Format("Error handling uri: {0}", Result.Error));
					}

					return;
				}

				if (Result.Request != null)
				{
					PostRequest(Result.Request);
					Result.Request.Complete.Wait();
					Result.Request.Dispose();
				}
			}
			catch { }
		}

		/// <summary>
		/// Sends UGS scope URI from secondary process to main for handling
		/// </summary>		
		public static void SendUri(string Uri)
		{
			using (NamedPipeClientStream ClientStream = new NamedPipeClientStream(".", UGSChannel, PipeDirection.Out, PipeOptions.None))
			{
				try
				{
					ClientStream.Connect(5000);
					WriteString(ClientStream, Uri);
				}
				catch (Exception)
				{

				}
			}
		}

		static string ReadString(Stream Stream)
		{
			int Len = Stream.ReadByte() * 256;
			Len += Stream.ReadByte();
			byte[] InBuffer = new byte[Len];
			Stream.Read(InBuffer, 0, Len);

			return StreamEncoding.GetString(InBuffer);
		}

		static void WriteString(Stream Stream, string Output)
		{
			byte[] OutBuffer = StreamEncoding.GetBytes(Output);

			int Len = OutBuffer.Length;

			if (Len > ushort.MaxValue)
			{
				Len = ushort.MaxValue;
			}

			Stream.WriteByte((byte)(Len / 256));
			Stream.WriteByte((byte)(Len & 255));
			Stream.Write(OutBuffer, 0, Len);
			Stream.Flush();
		}


		public void Dispose()
		{
			const int Timeout = 5000;
			bDisposing = true;
			ShutdownEvent.Set();

			if (UriThread != null)
			{
				if (!UriThread.Join(Timeout))
				{
					try { UriThread.Abort(); } catch { }
				}

				UriThread = null;
			}

			if (TcpThread != null)
			{
				if (!TcpThread.Join(Timeout))
				{
					try { TcpThread.Abort(); } catch { }
				}

				TcpThread = null;
			}


			// clean up IPC stream
			if (IPCStream != null)
			{
				IPCStream.Dispose();
			}

			if (Listener != null)
			{
				Listener.Stop();
				Listener = null;
			}

		}
	}
}
