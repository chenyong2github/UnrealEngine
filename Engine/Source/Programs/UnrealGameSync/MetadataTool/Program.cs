// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Reflection;
using System.Threading.Tasks;
using Tools.DotNETCommon;
using System.Net;
using System.IO;
using System.Web.Script.Serialization;

namespace MetadataTool
{
	[AttributeUsage(AttributeTargets.Field)]
	class OptionalAttribute : Attribute
	{
	}

	class Program
	{
		class UserErrorException : Exception
		{
			public UserErrorException(string Message)
				: base(Message)
			{
			}
		}

		class MissingArgumentException : UserErrorException
		{
			public MissingArgumentException(string Message)
				: base(Message)
			{
			}
		}

		class Command
		{
			public string Name;
			public string Verb;
			public string Resource;
			public Type BodyType;
			public string[] OptionalParams;

			public Command(string Name, string Verb, string Resource, Type BodyType = null, string[] OptionalParams = null)
			{
				this.Name = Name;
				this.Verb = Verb;
				this.Resource = Resource;
				this.BodyType = BodyType;
				this.OptionalParams = OptionalParams;
			}
		}

		#pragma warning disable CS0649

		enum Outcome
		{
			Success = 1,
			Error = 2,
			Warning = 3,
		}

		class AddIssueBody
		{
			public string Project;
			public string Summary;
		}

		class UpdateBuildBody
		{
			[Optional] public int? Outcome;
		}

		class UpdateIssueBody
		{
			[Optional] public bool? Acknowledged;
			[Optional] public int? FixChange;
			[Optional] public string Owner;
			[Optional] public string NominatedBy;
			[Optional] public bool? Resolved;
			[Optional] public string Summary;
			[Optional] public string Url;
		}

		class AddBuildBody
		{
			public string Name;
			public string Url;
			public string Stream;
			public int Change;
			public Outcome Outcome;
		}

		class WatcherBody
		{
			public string UserName;
		}

		#pragma warning restore CS0649

		static Command[] Commands =
		{
			// Issues
			new Command("add-issue", "POST", "/api/issues", typeof(AddIssueBody)),
			new Command("get-issue", "GET", "/api/issues/{id}"),
			new Command("get-issues", "GET", "/api/issues", OptionalParams: new string[]{ "user" }),
			new Command("update-issue", "PUT", "/api/issues/{Id}", typeof(UpdateIssueBody)),

			// Builds
			new Command("add-build", "POST", "/api/issues/{Issue}/builds", typeof(AddBuildBody)),
			new Command("get-builds", "GET", "/api/issues/{Issue}/builds"),

			// Individual builds
			new Command("get-build", "GET", "/api/issuebuilds/{Build}"),
			new Command("update-build", "PUT", "/api/issuebuilds/{Build}", typeof(UpdateBuildBody)),

			// Watchers
			new Command("get-watchers", "GET", "/api/issues/{Issue}/watchers"),
			new Command("add-watcher", "POST", "/api/issues/{Issue}/watchers", typeof(WatcherBody)),
			new Command("remove-watcher", "DELETE", "/api/issues/{Issue}/watchers", typeof(WatcherBody))
		};

		static int Main(string[] Args)
		{
			try
			{
				bool bResult = InnerMain(Args);
				return bResult? 0 : 1;
			}
			catch(UserErrorException Ex)
			{
				Console.WriteLine("{0}", Ex.Message);
				return 1;
			}
		}

		static bool InnerMain(string[] Args)
		{
			List<string> RemainingArgs = new List<string>(Args);

			// Get the server URL
			string ServerUrl = ParseOptionalParam(RemainingArgs, "Server") ?? Environment.GetEnvironmentVariable("UGS_METADATA_SERVER_URL");
			if(RemainingArgs.Count == 0)
			{
				PrintCommands();
				return true;
			}

			// Register all the commands
			Command Command = Commands.FirstOrDefault(x => String.Compare(x.Name, RemainingArgs[0], StringComparison.OrdinalIgnoreCase) == 0);
			if(Command == null)
			{
				Console.WriteLine("Unknown command '{0}'", RemainingArgs[0]);
				return false;
			}
			RemainingArgs.RemoveAt(0);

			// Handle help for this command
			if(ParseOption(RemainingArgs, "-Help"))
			{
				PrintCommandHelp(Command);
				return false;
			}

			// Make sure we have the server parameter
			if(String.IsNullOrEmpty(ServerUrl))
			{
				Console.WriteLine("Missing -Server=... argument.");
				Console.WriteLine();
				PrintCommandHelp(Command);
				return false;
			}

			// Try to execute the command
			try
			{
				// Create the command url
				StringBuilder CommandUrl = new StringBuilder();
				CommandUrl.AppendFormat("{0}{1}", ServerUrl, Command.Resource);

				// Replace all the arguments that are embedded into the resource name
				foreach(string ResourceArg in ParseArgumentNamesFromResource(Command.Resource))
				{
					string Value = ParseRequiredParam(RemainingArgs, ResourceArg);
					CommandUrl.Replace("{" + ResourceArg + "}", Value);
				}

				// Add all the required and optional parameters
				List<string> QueryParams = new List<string>();
				if(Command.OptionalParams != null)
				{
					foreach(string OptionalParam in Command.OptionalParams)
					{
						string Value = ParseOptionalParam(RemainingArgs, OptionalParam);
						if(Value != null)
						{
							QueryParams.Add(String.Format("{0}={1}", OptionalParam, Value));
						}
					}
				}

				// Append the parameters to the URL
				for(int Idx = 0; Idx < QueryParams.Count; Idx++)
				{
					if(Idx == 0)
					{
						CommandUrl.Append("?");
					}
					else
					{
						CommandUrl.Append("&");
					}
					CommandUrl.Append(QueryParams[Idx]);
				}

				// Parse additional options for the message body
				string BodyText = null;
				if(Command.BodyType != null)
				{
					object BodyObject = ParseObject(RemainingArgs, Command.BodyType);
					BodyText = new JavaScriptSerializer().Serialize(BodyObject);
				}

				// Make sure there are no unused arguments
				if(RemainingArgs.Count > 0)
				{
					foreach(string RemainingArg in RemainingArgs)
					{
						Console.WriteLine("Unused argument: {0}", RemainingArg);
					}
					return false;
				}

				// Create the request
				HttpWebRequest Request = (HttpWebRequest)WebRequest.Create(CommandUrl.ToString());
				Request.ContentType = "application/json";
				Request.Method = Command.Verb;
				if (BodyText != null)
				{
					byte[] BodyData = Encoding.UTF8.GetBytes(BodyText);
					using (Stream RequestStream = Request.GetRequestStream())
					{
						RequestStream.Write(BodyData, 0, BodyData.Length);
					}
				}

				// Read the response
				HttpWebResponse Response = (HttpWebResponse)Request.GetResponse();
				Console.WriteLine("Response: {0} ({1})", (int)Response.StatusCode, Response.StatusDescription);
				using (StreamReader ResponseReader = new StreamReader(Response.GetResponseStream(), Encoding.Default))
				{
					string ResponseContent = ResponseReader.ReadToEnd();
					if(!String.IsNullOrEmpty(ResponseContent))
					{
						Console.WriteLine(Json.Format(ResponseContent));
					}
				}
				return true;
			}
			catch(CommandLineArgumentException Ex)
			{
				Console.WriteLine("{0}", Ex.Message);
				Console.WriteLine();
				PrintCommandHelp(Command);
				return false;
			}
		}

		static void PrintCommands()
		{
			Console.WriteLine("SYNTAX:");
			Console.WriteLine("  MetadataTool.exe -Server=http://HostName:Port <Command> <Arguments...>");
			Console.WriteLine();
			Console.WriteLine("Global Options:");
			Console.WriteLine("  -Server=<url>     Specifies the endpoint for the metadata server (as http://hostname:port)");
			Console.WriteLine();
			Console.WriteLine("Available commands:");
			foreach(Command Command in Commands)
			{
				Console.WriteLine("  {0}", Command.Name);
			}
		}

		static void PrintCommandHelp(Command Command)
		{
			Console.WriteLine("Arguments for command '{0}':", Command.Name);

			foreach(string ArgumentName in ParseArgumentNamesFromResource(Command.Resource))
			{
				Console.WriteLine("  -{0}=...", ArgumentName);
			}
			if(Command.OptionalParams != null)
			{
				foreach(string ArgumentName in Command.OptionalParams)
				{
					Console.WriteLine("  -{0}=... (optional)", ArgumentName);
				}
			}
			if(Command.BodyType != null)
			{
				foreach(FieldInfo Field in Command.BodyType.GetFields(BindingFlags.Instance | BindingFlags.Public))
				{
					if(IsOptionalObjectField(Field))
					{
						Console.WriteLine("  -{0}={1} (optional)", Field.Name, GetFieldValueDescription(Field.FieldType));
					}
					else
					{
						Console.WriteLine("  -{0}={1}", Field.Name, GetFieldValueDescription(Field.FieldType));
					}
				}
			}
		}

		static string GetFieldValueDescription(Type FieldType)
		{
			if(FieldType.IsEnum)
			{
				return String.Join("|", Enum.GetNames(FieldType));
			}
			else
			{
				return "...";
			}
		}

		static IEnumerable<string> ParseArgumentNamesFromResource(string Resource)
		{
			for(int ArgIdx = Resource.IndexOf('{'); ArgIdx != -1; ArgIdx = Resource.IndexOf('{', ArgIdx))
			{
				int ArgEndIdx = Resource.IndexOf('}', ArgIdx + 1);
				yield return Resource.Substring(ArgIdx + 1, ArgEndIdx - (ArgIdx + 1));
				ArgIdx = ArgEndIdx + 1;
			}
		}

		static object ParseObject(List<string> CommandArgs, Type ObjectType)
		{
			object Instance = Activator.CreateInstance(ObjectType);
			foreach(FieldInfo Field in Instance.GetType().GetFields(BindingFlags.Instance | BindingFlags.Public))
			{
				string Value = ParseOptionalParam(CommandArgs, Field.Name);
				if(Value == null)
				{
					if(!IsOptionalObjectField(Field))
					{
						throw new CommandLineArgumentException(String.Format("Missing -{0}=... argument", Field.Name));
					}
				}
				else
				{
					Type FieldType = Field.FieldType;
					if(FieldType.IsGenericType && FieldType.GetGenericTypeDefinition() == typeof(Nullable<>))
					{
						FieldType = FieldType.GetGenericArguments()[0];
					}

					if(FieldType == typeof(string))
					{
						Field.SetValue(Instance, Value);
					}
					else if(FieldType == typeof(int))
					{
						int IntValue;
						if(!int.TryParse(Value, out IntValue))
						{
							throw new CommandLineArgumentException(String.Format("Invalid value for '{0}' - {1} is not an integer.", Field.Name, Value));
						}
						Field.SetValue(Instance, IntValue);
					}
					else if(FieldType == typeof(bool))
					{
						bool BoolValue;
						if(!bool.TryParse(Value, out BoolValue))
						{
							throw new CommandLineArgumentException(String.Format("Invalid value for '{0}' - {1} is not a boolean.", Field.Name, Value));
						}
						Field.SetValue(Instance, BoolValue);
					}
					else if(FieldType.IsEnum)
					{
						object EnumValue;
						try
						{
							EnumValue = Enum.Parse(FieldType, Value, true);
						}
						catch(ArgumentException)
						{
							throw new CommandLineArgumentException(String.Format("Invalid value for '{0}' - should be {1}", Field.Name, String.Join("/", Enum.GetNames(FieldType))));
						}
						Field.SetValue(Instance, EnumValue);
					}
					else
					{
						throw new NotImplementedException(String.Format("Unsupported type '{0}'", FieldType));
					}
				}
			}
			return Instance;
		}

		static bool IsOptionalObjectField(FieldInfo Field)
		{
			if(Field.GetCustomAttribute<OptionalAttribute>() != null)
			{
				return true;
			}
			if(Field.FieldType.IsGenericType && Field.FieldType.GetGenericTypeDefinition() == typeof(Nullable<>))
			{
				return true;
			}
			return false;
		}

		static bool ParseOption(List<string> RemainingArgs, string Option)
		{
			for(int Idx = 0; Idx < RemainingArgs.Count; Idx++)
			{
				if(RemainingArgs[Idx].Equals(Option, StringComparison.OrdinalIgnoreCase))
				{
					RemainingArgs.RemoveAt(Idx);
					return true;
				}
			}
			return false;
		}

		static string ParseRequiredParam(List<string> RemainingArgs, string Name)
		{
			string Result = ParseOptionalParam(RemainingArgs, Name);
			if(Result == null)
			{
				throw new CommandLineArgumentException(String.Format("Missing -{0}=... argument", Name));
			}
			return Result;
		}

		static string ParseOptionalParam(List<string> RemainingArgs, string Name)
		{
			for(int Idx = 0; Idx < RemainingArgs.Count; Idx++)
			{
				string RemainingArg = RemainingArgs[Idx];
				if(RemainingArg.Length >= Name.Length + 2 && RemainingArg[0] == '-' && RemainingArg[Name.Length + 1] == '=' && String.Compare(RemainingArg, 1, Name, 0, Name.Length, StringComparison.OrdinalIgnoreCase) == 0)
				{
					string Value = RemainingArgs[Idx].Substring(Name.Length + 2);
					RemainingArgs.RemoveAt(Idx);
					return Value;
				}
			}
			return null;
		}
	}
}
