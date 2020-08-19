// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using Microsoft.Win32;
using System.Reflection;
using System.Threading;
using System.Collections.Specialized;
using System.Web;
using System.Linq;
using System.Diagnostics;
using System.Windows.Forms;
using System.Configuration;

namespace UnrealGameSync
{

	/// <summary>
	/// Uri handler result
	/// </summary>
	class UriResult
	{
		public bool Success = false;
		public string Error;
		public AutomationRequest Request = null;
	}

	/// <summary>
	/// Main handler for Uri requests
	/// </summary>
	static class UriHandler
	{
		public static UriResult HandleUri(string UriIn)
		{			
			try
			{
				Uri Uri = new Uri(UriIn);

				// Check if this is a registered uri request
				if (!Handlers.TryGetValue(Uri.Host, out MethodInfo Info))
				{
					return new UriResult() { Error = string.Format("Unknown Uri {0}", Uri.Host) };
				}

				NameValueCollection Query = HttpUtility.ParseQueryString(Uri.Query);

				List<object> Parameters = new List<object>();

				foreach (ParameterInfo Param in Info.GetParameters())
				{					
					string Value = Query.Get(Param.Name);

					if (Value == null)
					{
						if (!Param.HasDefaultValue)
						{
							return new UriResult() { Error = string.Format("Uri {0} is missing required parameter {1}", Uri.Host, Param.Name) };
						}

						Parameters.Add(Param.DefaultValue);
						continue;
					}

					if (Param.ParameterType == typeof(string))
					{
						Parameters.Add(Value);
					}
					else if (Param.ParameterType == typeof(bool))
					{
						if (Value.Equals("true", StringComparison.OrdinalIgnoreCase))
						{
							Parameters.Add(true);
						}
						else if (Value.Equals("false", StringComparison.OrdinalIgnoreCase))
						{
							Parameters.Add(false);
						}
						else
						{
							return new UriResult() { Error = string.Format("Uri {0} bool parameter {1} must be true or false", Uri.Host, Param.Name) };
						}
					}
					else if (Param.ParameterType == typeof(int) || Param.ParameterType == typeof(float))
					{
						float NumberValue;
						if (!float.TryParse(Value, out NumberValue))
						{
							return new UriResult() { Error = string.Format("Uri {0} invalid number parameter {1} : {2}", Uri.Host, Param.Name, Value) };
						}

						if (Param.ParameterType == typeof(int))
						{
							Parameters.Add(Convert.ToInt32(NumberValue));
						}
						else
						{
							Parameters.Add(NumberValue);
						}

					}

				}

				return Info.Invoke(null, Parameters.ToArray()) as UriResult;					
			}
			catch (Exception Ex)
			{
				return new UriResult() { Error = Ex.Message };
			}
		}

		public const string InstallHandlerArg = "-InstallHandler";
		public const string UninstallHandlerArg = "-UninstallHandler";
		public const string ElevatedArg = "-Elevated";

		/// <summary>
		/// Handle URI passed in via command lines
		/// </summary>
		public static bool ProcessCommandLine(string[] Args, bool FirstInstance, EventWaitHandle ActivateEvent = null)
		{
			if (Args.Any(x => x.Equals(InstallHandlerArg, StringComparison.OrdinalIgnoreCase)))
			{
				if (Args.Any(x => x.Equals(ElevatedArg, StringComparison.OrdinalIgnoreCase)))
				{
					ProtocolHandlerUtils.InstallElevated();
				}
				else
				{
					ProtocolHandlerUtils.Install();
				}
				return true;
			}
			else if (Args.Any(x => x.Equals(UninstallHandlerArg, StringComparison.OrdinalIgnoreCase)))
			{
				if (Args.Any(x => x.Equals(ElevatedArg, StringComparison.OrdinalIgnoreCase)))
				{
					ProtocolHandlerUtils.UninstallElevated();
				}
				else
				{
					ProtocolHandlerUtils.Uninstall();
				}
				return true;
			}
			else
			{
				string UriIn = string.Empty;
				for (int Idx = 0; Idx < Args.Length; Idx++)
				{
					const string Prefix = "-uri=";
					if (Args[Idx].StartsWith(Prefix, StringComparison.OrdinalIgnoreCase))
					{
						UriIn = Args[Idx].Substring(Prefix.Length);
					}
				}

				if (UriIn == string.Empty)
				{
					return false;
				}

				Uri Uri;
				try
				{
					Uri = new Uri(UriIn);
				}
				catch
				{
					MessageBox.Show(String.Format("Invalid URI: {0}", UriIn));
					return true;
				}

				MethodInfo Handler;
				if (!Handlers.TryGetValue(Uri.Host, out Handler))
				{
					MessageBox.Show(String.Format("Unknown action from URI request ('{0}')", Uri.Host));
					return true;
				}

				UriHandlerAttribute Attribute = Handler.GetCustomAttribute<UriHandlerAttribute>();

				// handle case where we terminate after invoking handler
				if (Attribute.Terminate)
				{
					UriResult Result = HandleUri(UriIn);
					if (!Result.Success)
					{
						MessageBox.Show(Result.Error);
					}
					return true;
				}

				if (!FirstInstance)
				{
					if (ActivateEvent != null)
					{
						ActivateEvent.Set();
					}

					// send to main UGS process using IPC
					AutomationServer.SendUri(UriIn);
					return true;
				}

				// we're in the main UGS process, which was also launched, defer handling to after main window is created
				return false;
			}
		}


		static UriHandler()
		{
			foreach (Type Type in Assembly.GetExecutingAssembly().GetTypes())
			{
				List<MethodInfo> HandlerMethods = new List<MethodInfo> (Type.GetMethods().Where(MethodInfo => MethodInfo.GetCustomAttribute<UriHandlerAttribute>() != null));
				foreach (MethodInfo MethodInfo in HandlerMethods)
				{
					if (!MethodInfo.IsStatic)
					{
						throw new Exception(string.Format("UriHandler method {0} must be static", MethodInfo.Name));
					}

					if (MethodInfo.ReturnType != typeof(UriResult))
					{
						throw new Exception(string.Format("UriHandler method {0} must return UriResult type", MethodInfo.Name));
					}

					if (Handlers.ContainsKey(MethodInfo.Name))
					{
						throw new Exception(string.Format("UriHandler method {0} clashes with another handler", MethodInfo.Name));
					}

					foreach (ParameterInfo ParameterInfo in MethodInfo.GetParameters())
					{
						Type ParameterType = ParameterInfo.ParameterType;
						if (ParameterType != typeof(bool) && ParameterType != typeof(string) && ParameterType != typeof(float) && ParameterType != typeof(int))
						{
							throw new Exception(string.Format("UriHandler method parameter {0} must be bool, string, int, or float", ParameterInfo.Name));
						}
					}

					Handlers[MethodInfo.Name] = MethodInfo;
				}
			}
		}

		/// <summary>
		/// All available handler infos
		/// </summary>
		static readonly Dictionary<string, MethodInfo> Handlers = new Dictionary<string, MethodInfo>(StringComparer.OrdinalIgnoreCase);

	}

	/// <summary>
	/// Method attribute for Uri handlers
	/// </summary>
	[AttributeUsage(AttributeTargets.Method)]
	class UriHandlerAttribute : Attribute
	{
		public bool Terminate;

		public UriHandlerAttribute(bool Terminate = false)
		{
			this.Terminate = Terminate;			
		}
	}

	enum ProtocolHandlerState
	{
		Unknown,
		Installed,
		NotInstalled,
	}

	/// <summary>
	/// Development utilities to register protocol binary, this or a variation should go in installer/updater
	/// Needs administrator to edit registry
	/// </summary>
	static class ProtocolHandlerUtils
	{
		class RegistrySetting
		{
			public RegistryKey RootKey;
			public string KeyName;
			public string ValueName;
			public string Value;

			public RegistrySetting(RegistryKey RootKey, string KeyName, string ValueName, string Value)
			{
				this.RootKey = RootKey;
				this.KeyName = KeyName;
				this.ValueName = ValueName;
				this.Value = Value;
			}
		}

		static List<RegistrySetting> GetRegistrySettings()
		{
			string ApplicationPath = Assembly.GetExecutingAssembly().Location;

			List<RegistrySetting> Keys = new List<RegistrySetting>();
			Keys.Add(new RegistrySetting(Registry.ClassesRoot, "UGS", null, "URL:UGS Protocol"));
			Keys.Add(new RegistrySetting(Registry.ClassesRoot, "UGS", "URL Protocol", ""));
			Keys.Add(new RegistrySetting(Registry.ClassesRoot, "UGS\\DefaultIcon", null, String.Format("\"{0}\",0", ApplicationPath)));
			Keys.Add(new RegistrySetting(Registry.ClassesRoot, "UGS\\shell\\open\\command", null, String.Format("\"{0}\" -uri=\"%1\"", ApplicationPath)));
			return Keys;
		}

		public static ProtocolHandlerState GetState()
		{
			try
			{
				bool bHasAny = false;
				bool bHasAll = true;

				List<RegistrySetting> Keys = GetRegistrySettings();
				foreach (IGrouping<RegistryKey, RegistrySetting> RootKeyGroup in Keys.GroupBy(x => x.RootKey))
				{
					foreach (IGrouping<string, RegistrySetting> KeyNameGroup in RootKeyGroup.GroupBy(x => x.KeyName))
					{
						using (RegistryKey RegistryKey = RootKeyGroup.Key.OpenSubKey(KeyNameGroup.Key))
						{
							if (RegistryKey == null)
							{
								bHasAll = false;
							}
							else
							{
								bHasAll &= KeyNameGroup.All(x => (RegistryKey.GetValue(x.ValueName) as string) == x.Value);
								bHasAny = true;
							}
						}
					}
				}

				return bHasAll? ProtocolHandlerState.Installed : bHasAny? ProtocolHandlerState.Unknown : ProtocolHandlerState.NotInstalled;
			}
			catch
			{
				return ProtocolHandlerState.Unknown;
			}
		}

		public static void Install()
		{
			RunElevated(String.Format("{0} {1}", UriHandler.InstallHandlerArg, UriHandler.ElevatedArg));
		}

		public static void InstallElevated()
		{
			try
			{
				List<RegistrySetting> Keys = GetRegistrySettings();
				foreach (IGrouping<RegistryKey, RegistrySetting> RootKeyGroup in Keys.GroupBy(x => x.RootKey))
				{
					foreach (IGrouping<string, RegistrySetting> KeyNameGroup in RootKeyGroup.GroupBy(x => x.KeyName))
					{
						using (RegistryKey RegistryKey = RootKeyGroup.Key.CreateSubKey(KeyNameGroup.Key))
						{
							foreach (RegistrySetting Setting in KeyNameGroup)
							{
								RegistryKey.SetValue(Setting.ValueName, Setting.Value);
							}
						}
					}
				}
			}
			catch (Exception Ex)
			{
				MessageBox.Show(String.Format("Unable to register protocol handler: {0}", Ex));
			}
		}

		public static void Uninstall()
		{
			RunElevated(String.Format("{0} {1}", UriHandler.UninstallHandlerArg, UriHandler.ElevatedArg));
		}

		public static void UninstallElevated()
		{
			try
			{
				Registry.ClassesRoot.DeleteSubKeyTree("UGS", false);
			}
			catch (Exception Ex)
			{
				MessageBox.Show(String.Format("Unable to register protocol handler: {0}", Ex));
			}
		}

		private static void RunElevated(string Arguments)
		{
			using (Process Process = new Process())
			{
				Process.StartInfo.FileName = Assembly.GetExecutingAssembly().Location;
				Process.StartInfo.Arguments = Arguments;
				Process.StartInfo.Verb = "runas";
				Process.StartInfo.UseShellExecute = true;
				Process.Start();
				Process.WaitForExit();
			}
		}
	}
}



