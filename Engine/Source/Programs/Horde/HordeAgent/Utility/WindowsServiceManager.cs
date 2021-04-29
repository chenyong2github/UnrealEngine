// Copyright Epic Games, Inc. All Rights Reserved.

using Microsoft.Win32.SafeHandles;
using Serilog;
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;

namespace HordeAgent.Utility
{
	/// <summary>
	/// Native methods for manipulating services on Windows
	/// </summary>
	static partial class Native
	{
#pragma warning disable CS0649
		public class ServiceHandle : SafeHandleZeroOrMinusOneIsInvalid
		{
			public ServiceHandle()
				: base(true)
			{
			}

			public ServiceHandle(IntPtr Handle)
				: base(true)
			{
				SetHandle(Handle);
			}

			protected override bool ReleaseHandle()
			{
				return Native.CloseServiceHandle(handle);
			}
		}

		public const uint SC_MANAGER_ALL_ACCESS = 0xF003F;

		[DllImport("advapi32.dll", EntryPoint = "OpenSCManagerW", ExactSpelling = true, CharSet = CharSet.Unicode, SetLastError = true)]
		public static extern ServiceHandle OpenSCManager(string? machineName, string? databaseName, uint dwAccess);

		public const uint SERVICE_ALL_ACCESS = 0xf01ff;

		public const uint SERVICE_WIN32_OWN_PROCESS = 0x00000010;

		public const uint SERVICE_AUTO_START = 0x00000002;
		public const uint SERVICE_DEMAND_START = 0x00000003;

		public const uint SERVICE_ERROR_NORMAL = 0x00000001;

		[DllImport("advapi32.dll", SetLastError = true, CharSet = CharSet.Auto)]
		public static extern ServiceHandle CreateService(ServiceHandle hSCManager, string lpServiceName, string lpDisplayName, uint dwDesiredAccess, uint dwServiceType, uint dwStartType, uint dwErrorControl, string lpBinaryPathName, string? lpLoadOrderGroup, string? lpdwTagId, string? lpDependencies, string? lpServiceStartName, string? lpPassword);

		[DllImport("advapi32.dll", SetLastError = true, CharSet = CharSet.Auto)]
		public static extern ServiceHandle OpenService(ServiceHandle hSCManager, string lpServiceName, uint dwDesiredAccess);

		[DllImport("advapi32", SetLastError = true)]
		[return: MarshalAs(UnmanagedType.Bool)]
		public static extern bool StartService(ServiceHandle hService, int dwNumServiceArgs, string[]? lpServiceArgVectors);

		public const int ERROR_ACCESS_DENIED = 5;
		public const int ERROR_SERVICE_DOES_NOT_EXIST = 1060;
		public const int ERROR_SERVICE_NOT_ACTIVE = 1062;

		[DllImport("advapi32.dll", SetLastError = true)]
		[return: MarshalAs(UnmanagedType.Bool)]
		public static extern bool CloseServiceHandle(IntPtr hSCObject);

		[DllImport("advapi32.dll", SetLastError = true)]
		[return: MarshalAs(UnmanagedType.Bool)]
		public static extern bool DeleteService(ServiceHandle hService);

		public const int SC_STATUS_PROCESS_INFO = 0;

		public struct SERVICE_STATUS_PROCESS
		{
			public uint dwServiceType;
			public uint dwCurrentState;
			public uint dwControlsAccepted;
			public uint dwWin32ExitCode;
			public uint dwServiceSpecificExitCode;
			public uint dwCheckPoint;
			public uint dwWaitHint;
			public uint dwProcessId;
			public uint dwServiceFlags;
		}

		[DllImport("advapi32.dll", SetLastError = true)]
		[return: MarshalAs(UnmanagedType.Bool)]
		public static extern bool QueryServiceStatusEx(ServiceHandle hService, int InfoLevel, ref SERVICE_STATUS_PROCESS pBuffer, int cbBufSize, out int pcbBytesNeeded);

		public struct SERVICE_STATUS
		{
			public uint dwServiceType;
			public uint dwCurrentState;
			public uint dwControlsAccepted;
			public uint dwWin32ExitCode;
			public uint dwServiceSpecificExitCode;
			public uint dwCheckPoint;
			public uint dwWaitHint;
		}

		public const int SERVICE_CONTROL_STOP = 1;

		[DllImport("advapi32.dll", SetLastError = true)]
		[return: MarshalAs(UnmanagedType.Bool)]
		public static extern bool ControlService(ServiceHandle hService, int dwControl, ref SERVICE_STATUS lpServiceStatus);

		public const int SERVICE_CONFIG_DESCRIPTION = 1;

		[StructLayout(LayoutKind.Sequential)]
		public struct SERVICE_DESCRIPTION
		{
			[MarshalAs(UnmanagedType.LPWStr)]
			public String lpDescription;
		}

		[DllImport("advapi32.dll", SetLastError = true, CharSet = CharSet.Auto)]
		[return: MarshalAs(UnmanagedType.Bool)]
		public static extern bool ChangeServiceConfig2(ServiceHandle hService, int dwInfoLevel, ref SERVICE_DESCRIPTION lpInfo);
#pragma warning restore CS0649
	}

	enum WindowsServiceStatus : uint
	{
		/// <summary>
		/// The service has stopped.
		/// </summary>
		Stopped = 1,

		/// <summary>
		/// The service is starting.
		/// </summary>
		Starting = 2,

		/// <summary>
		/// The service is stopping.
		/// </summary>
		Stopping = 3,

		/// <summary>
		/// The service is running
		/// </summary>
		Running = 4,

		/// <summary>
		/// The service is about to continue.
		/// </summary>
		Continuing = 5,

		/// <summary>
		/// The service is pausing
		/// </summary>
		Pausing = 6,

		/// <summary>
		/// The service is paused.
		/// </summary>
		Paused = 7,
	}

	/// <summary>
	/// Wrapper around 
	/// </summary>
	class WindowsService : IDisposable
	{
		/// <summary>
		/// Handle to the service
		/// </summary>
		Native.ServiceHandle Handle;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Handle">Handle to the service</param>
		public WindowsService(Native.ServiceHandle Handle)
		{
			this.Handle = Handle;
		}

		/// <summary>
		/// Determines if the service is valid
		/// </summary>
		public bool IsValid
		{
			get { return !Handle.IsInvalid; }
		}

		/// <summary>
		/// Gets the current service status
		/// </summary>
		/// <returns>Status code</returns>
		public WindowsServiceStatus GetStatus()
		{
			Native.SERVICE_STATUS_PROCESS Status = new Native.SERVICE_STATUS_PROCESS();
			if (!Native.QueryServiceStatusEx(Handle, Native.SC_STATUS_PROCESS_INFO, ref Status, Marshal.SizeOf(Status), out int BytesNeeded))
			{
				throw new Win32Exception(String.Format("Unable to query process status (0x{0:X8)}", Marshal.GetLastWin32Error()));
			}
			return (WindowsServiceStatus)Status.dwCurrentState;
		}

		/// <summary>
		/// Waits for the status to change
		/// </summary>
		/// <param name="TransitionStatus">Expected status while transitioning</param>
		/// <param name="MaxWaitTime">Maximum time to wait</param>
		public WindowsServiceStatus WaitForStatusChange(WindowsServiceStatus TransitionStatus, TimeSpan MaxWaitTime)
		{
			Stopwatch Timer = Stopwatch.StartNew();
			for(; ;)
			{
				WindowsServiceStatus Status = GetStatus();
				if (Status != TransitionStatus || Timer.Elapsed > MaxWaitTime)
				{
					return Status;
				}
				else
				{
					Thread.Sleep(1000);
				}
			}
		}

		/// <summary>
		/// Sets the service description
		/// </summary>
		/// <param name="Description">Description for the service</param>
		public void SetDescription(string Description)
		{
			Native.SERVICE_DESCRIPTION DescriptionData = new Native.SERVICE_DESCRIPTION();
			DescriptionData.lpDescription = Description;
			Native.ChangeServiceConfig2(Handle, Native.SERVICE_CONFIG_DESCRIPTION, ref DescriptionData);
		}

		/// <summary>
		/// Starts the service
		/// </summary>
		public void Start()
		{
			if (!Native.StartService(Handle, 0, null))
			{
				throw new Win32Exception(String.Format("Unable to start service (error 0x{0:X8})", Marshal.GetLastWin32Error()));
			}
		}

		/// <summary>
		/// Stops the service
		/// </summary>
		public void Stop()
		{
			Native.SERVICE_STATUS Status = new Native.SERVICE_STATUS();
			if (!Native.ControlService(Handle, Native.SERVICE_CONTROL_STOP, ref Status))
			{
				int Error = Marshal.GetLastWin32Error();
				if (Error != Native.ERROR_SERVICE_NOT_ACTIVE)
				{
					throw new Win32Exception(String.Format("Unable to stop service (error 0x{0:X8})", Error));
				}
			}
		}

		/// <summary>
		/// Deletes the service
		/// </summary>
		public void Delete()
		{
			if (!Native.DeleteService(Handle))
			{
				throw new Win32Exception(String.Format("Unable to delete service (error 0x{0:X8})", Marshal.GetLastWin32Error()));
			}
		}

		/// <summary>
		/// Dispose of the service handle
		/// </summary>
		public void Dispose()
		{
			Handle.Close();
		}
	}

	/// <summary>
	/// Helper functionality for manipulating Windows services
	/// </summary>
	class WindowsServiceManager : IDisposable
	{
		/// <summary>
		/// Native handle to the service manager
		/// </summary>
		Native.ServiceHandle ServiceManagerHandle;

		/// <summary>
		/// Constructor. Opens a handle to the service manager.
		/// </summary>
		public WindowsServiceManager()
		{
			ServiceManagerHandle = Native.OpenSCManager(null, null, Native.SC_MANAGER_ALL_ACCESS);
			if (ServiceManagerHandle.IsInvalid)
			{
				int ErrorCode = Marshal.GetLastWin32Error();
				if (ErrorCode == Native.ERROR_ACCESS_DENIED)
				{
					throw new Win32Exception("Unable to open service manager (access denied). Check you're running as administrator.");
				}
				else
				{
					throw new Win32Exception(String.Format("Unable to open service manager (0x{0:X8)).", ErrorCode));
				}
			}
		}

		/// <summary>
		/// Dispose of this object
		/// </summary>
		public void Dispose()
		{
			ServiceManagerHandle.Close();
		}

		/// <summary>
		/// Opens a service with the given name
		/// </summary>
		/// <param name="ServiceName">Name of the service</param>
		/// <returns>New service wrapper</returns>
		public WindowsService Open(string ServiceName)
		{
			Native.ServiceHandle ServiceHandle = Native.OpenService(ServiceManagerHandle, ServiceName, Native.SERVICE_ALL_ACCESS);
			if (ServiceHandle.IsInvalid)
			{
				int ErrorCode = Marshal.GetLastWin32Error();
				if (ErrorCode != Native.ERROR_SERVICE_DOES_NOT_EXIST)
				{
					throw new Win32Exception("Unable to open handle to service");
				}
			}
			return new WindowsService(ServiceHandle);
		}

		/// <summary>
		/// Creates a service with the given settings
		/// </summary>
		/// <param name="Name">Name of the service</param>
		/// <param name="DisplayName">Display name</param>
		/// <param name="CommandLine">Command line to use when starting the service</param>
		/// <param name="UserName">Username to run this service as</param>
		/// <param name="Password">Password for the account the service is to run under</param>
		/// <returns>New service instance</returns>
		public WindowsService Create(string Name, string DisplayName, string CommandLine, string? UserName, string? Password)
		{
			Native.ServiceHandle NewServiceHandle = Native.CreateService(ServiceManagerHandle, Name, DisplayName, Native.SERVICE_ALL_ACCESS, Native.SERVICE_WIN32_OWN_PROCESS, Native.SERVICE_AUTO_START, Native.SERVICE_ERROR_NORMAL, CommandLine, null, null, null, UserName, Password);
			if (NewServiceHandle.IsInvalid)
			{
				throw new Win32Exception(String.Format("Unable to create service (0x{0:X8})", Marshal.GetLastWin32Error()));
			}
			return new WindowsService(NewServiceHandle);
		}
	}
}
