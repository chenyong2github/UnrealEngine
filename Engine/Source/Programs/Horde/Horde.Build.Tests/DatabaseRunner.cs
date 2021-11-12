// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.NetworkInformation;
using System.Net.Sockets;
using System.Reflection;
using System.Runtime.InteropServices;
using Castle.Core.Internal;
using EpicGames.Core;

namespace HordeServerTests
{
	public abstract class DatabaseRunner
	{
		private readonly string Name;
		private readonly string BinName;
		private readonly int DefaultPort;
		private readonly bool ReuseProcess;
		private readonly bool PrintStdOut;
		private readonly bool PrintStdErr;
		protected readonly string TempDir;
		private Process? Proc;
		protected int Port { get; private set; } = -1;

		protected DatabaseRunner(string Name, string BinName, int DefaultPort, bool ReuseProcess, bool PrintStdOut = false, bool PrintStdErr = true)
		{
			this.Name = Name;
			this.BinName = BinName;
			this.DefaultPort = DefaultPort;
			this.ReuseProcess = ReuseProcess;
			this.PrintStdOut = PrintStdOut;
			this.PrintStdErr = PrintStdErr;
			TempDir = GetTemporaryDirectory();
		}

		protected abstract string GetArguments();
		
		public void Start()
		{
			if (ReuseProcess && !IsPortAvailable(DefaultPort))
			{
				Console.WriteLine($"Re-using already running {Name} process!");
				Port = DefaultPort;
				return;
			}

			if (!RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
			{
				Console.WriteLine($"Unable to find a running {Name} process to use during testing!");
				Console.WriteLine("This is required on any non-Windows as the runner can only start Windows binaries!");
				Console.WriteLine($"Please ensure {BinName} is running on the default port {DefaultPort}.");
				throw new Exception("Failed finding process to re-use! See stdout for info.");
			}
			
			if (Proc != null)
			{
				return;
			}

			Port = GetAvailablePort();
			
			Process P = new Process();
			if (PrintStdOut)
			{
				P.OutputDataReceived += (_, Args) => Console.WriteLine("{0} stdout: {1}", Name, Args.Data);	
			}
			if (PrintStdErr)
			{
				P.ErrorDataReceived += (_, Args) => Console.WriteLine("{0} stderr: {1}", Name, Args.Data);
			}
				
			P.StartInfo.FileName = GetBinaryPath();
			P.StartInfo.WorkingDirectory = TempDir;
			P.StartInfo.Arguments = GetArguments();
			P.StartInfo.UseShellExecute = false;
			P.StartInfo.CreateNoWindow = true;
			P.StartInfo.RedirectStandardOutput = true;
			P.StartInfo.RedirectStandardError = true;

			if (!P.Start())
			{
				throw new Exception("Process start failed!");
			}
			P.BeginOutputReadLine();
			P.BeginErrorReadLine();

			// Try detect when main .NET process exits and kill the runner
			AppDomain.CurrentDomain.ProcessExit += (Sender, EventArgs) =>
			{
				Console.WriteLine("Main process exiting!");
				Stop();
			};

			Proc = P;
		}
		
		public void Stop()
		{
			if (Proc != null)
			{
				Proc.Kill(true);
				
				// Waiting for exit blocks excessively even though the kill was sent. Anti-virus interfering?
				// Process eventually shuts down but takes 2-3 min in Redis case. ReuseProcess flags circumvents this.
				//Proc.WaitForExit();
				Proc = null;
				DeleteDirectory(TempDir);
			}
		}

		public (string Host, int Port) GetListenAddress()
		{
			return ("localhost", Port);
		}
		
		private string GetBinaryPath()
		{
			FileReference File = new FileReference(new Uri(Assembly.GetExecutingAssembly().Location).LocalPath);
			FileReference BinPath = FileReference.Combine(File.Directory, BinName);
			return BinPath.FullName;
		}
		
		private string GetTemporaryDirectory()
		{
			string Temp = Path.Join(Path.GetTempPath(), $"horde-{Name}-"  + Path.GetRandomFileName());
			Directory.CreateDirectory(Temp);
			return Temp;
		}
		
		private static int GetAvailablePort()
		{
			TcpListener Listener = new TcpListener(IPAddress.Loopback, 0);
			Listener.Start();
			int Port = ((IPEndPoint)Listener.LocalEndpoint).Port;
			Listener.Stop();
			return Port;
		}

		private static bool IsPortAvailable(int Port)
		{
			if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
			{
				IPGlobalProperties IpGlobalProperties = IPGlobalProperties.GetIPGlobalProperties();

				IPEndPoint[] Listeners = IpGlobalProperties.GetActiveTcpListeners();
				if (Listeners.Any(x => x.Port == Port))
				{
					return false;
				}

				return true;
			}
			else
			{
				TcpListener? ListenerAny = null;
				TcpListener? ListenerLoopback = null;
				try
				{
					ListenerAny = new TcpListener(IPAddress.Loopback, Port);
					ListenerAny.Start();
					ListenerLoopback = new TcpListener(IPAddress.Any, Port);
					ListenerLoopback.Start();
					return true;
				}
				catch (SocketException)
				{
				}
				finally
				{
					ListenerAny?.Stop();
					ListenerLoopback?.Stop();
				}

				return false;
			}
		}
		
		private static void DeleteDirectory(string Path)
		{
			DirectoryInfo Dir = new DirectoryInfo(Path) { Attributes = FileAttributes.Normal };
			foreach (var Info in Dir.GetFileSystemInfos("*", SearchOption.AllDirectories))
			{
				Info.Attributes = FileAttributes.Normal;
			}
			Dir.Delete(true);
		}
	}

	public class MongoDbRunnerLocal : DatabaseRunner
	{
		public MongoDbRunnerLocal() : base("mongodb", "ThirdParty/Mongo/mongod.exe", 27017, true)
		{
		}

		protected override string GetArguments()
		{
			return $"--dbpath {TempDir} --noauth --quiet --port {Port}";
		}

		public string GetConnectionString()
		{
			(var Host, int ListenPort) = GetListenAddress();
			return $"mongodb://{Host}:{ListenPort}";
		}
	}
	
	public class RedisRunner : DatabaseRunner
	{
		public RedisRunner() : base("redis", "ThirdParty/Redis/redis-server.exe", 6379, true)
		{
		}

		protected override string GetArguments()
		{
			return $"--port {Port} --save \"\" --appendonly no";
		}
	}
}