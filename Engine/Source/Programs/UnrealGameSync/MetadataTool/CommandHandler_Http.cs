// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net;
using System.Reflection;
using System.Text;
using System.Threading.Tasks;
using System.Web.Script.Serialization;
using Tools.DotNETCommon;

namespace MetadataTool
{
	class CommandHandler_Http : CommandHandler
	{
		public string Verb;
		public string Resource;
		public Type BodyType;
		public string[] OptionalParams;

		public CommandHandler_Http(string Name, string Verb, string Resource, Type BodyType = null, string[] OptionalParams = null)
			: base(Name)
		{
			this.Verb = Verb;
			this.Resource = Resource;
			this.BodyType = BodyType;
			this.OptionalParams = OptionalParams;
		}

		public override void Exec(CommandLineArguments Arguments)
		{
			// Parse the server URL
			string ServerUrl = Arguments.GetStringOrDefault("-Server=", null) ?? Environment.GetEnvironmentVariable("UGS_METADATA_SERVER_URL");
			if (String.IsNullOrEmpty(ServerUrl))
			{
				throw new CommandLineArgumentException("Missing -Server=... argument");
			}

			// Create the command url
			StringBuilder CommandUrl = new StringBuilder();
			CommandUrl.AppendFormat("{0}{1}", ServerUrl, Resource);

			// Replace all the arguments that are embedded into the resource name
			foreach (string ResourceArg in ParseArgumentNamesFromResource(Resource))
			{
				string Value = Arguments.GetString(String.Format("-{0}=", ResourceArg));
				CommandUrl.Replace("{" + ResourceArg + "}", Value);
			}

			// Add all the required and optional parameters
			List<string> QueryParams = new List<string>();
			if (OptionalParams != null)
			{
				foreach (string OptionalParam in OptionalParams)
				{
					string Value = Arguments.GetStringOrDefault(String.Format("-{0}=", OptionalParam), null);
					if (Value != null)
					{
						QueryParams.Add(String.Format("{0}={1}", OptionalParam, Value));
					}
				}
			}

			// Append the parameters to the URL
			for (int Idx = 0; Idx < QueryParams.Count; Idx++)
			{
				if (Idx == 0)
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
			if (BodyType != null)
			{
				object BodyObject = ParseObject(Arguments, BodyType);
				BodyText = new JavaScriptSerializer().Serialize(BodyObject);
			}

			// Make sure there are no unused arguments
			Arguments.CheckAllArgumentsUsed();

			// Create the request
			HttpWebRequest Request = (HttpWebRequest)WebRequest.Create(CommandUrl.ToString());
			Request.ContentType = "application/json";
			Request.Method = Verb;
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
				if (!String.IsNullOrEmpty(ResponseContent))
				{
					Console.WriteLine(Json.Format(ResponseContent));
				}
			}
		}

		public override void Help()
		{
			foreach (string ArgumentName in ParseArgumentNamesFromResource(Resource))
			{
				Console.WriteLine("  -{0}=...", ArgumentName);
			}
			if (OptionalParams != null)
			{
				foreach (string ArgumentName in OptionalParams)
				{
					Console.WriteLine("  -{0}=... (optional)", ArgumentName);
				}
			}
			if (BodyType != null)
			{
				foreach (FieldInfo Field in BodyType.GetFields(BindingFlags.Instance | BindingFlags.Public))
				{
					if (IsOptionalObjectField(Field))
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

		static IEnumerable<string> ParseArgumentNamesFromResource(string Resource)
		{
			for (int ArgIdx = Resource.IndexOf('{'); ArgIdx != -1; ArgIdx = Resource.IndexOf('{', ArgIdx))
			{
				int ArgEndIdx = Resource.IndexOf('}', ArgIdx + 1);
				yield return Resource.Substring(ArgIdx + 1, ArgEndIdx - (ArgIdx + 1));
				ArgIdx = ArgEndIdx + 1;
			}
		}

		static object ParseObject(CommandLineArguments Arguments, Type ObjectType)
		{
			object Instance = Activator.CreateInstance(ObjectType);
			foreach (FieldInfo Field in Instance.GetType().GetFields(BindingFlags.Instance | BindingFlags.Public))
			{
				string Value = Arguments.GetStringOrDefault(String.Format("-{0}=", Field.Name), null);
				if (Value == null)
				{
					if (!IsOptionalObjectField(Field))
					{
						throw new CommandLineArgumentException(String.Format("Missing -{0}=... argument", Field.Name));
					}
				}
				else
				{
					Type FieldType = Field.FieldType;
					if (FieldType.IsGenericType && FieldType.GetGenericTypeDefinition() == typeof(Nullable<>))
					{
						FieldType = FieldType.GetGenericArguments()[0];
					}

					if (FieldType == typeof(string))
					{
						Field.SetValue(Instance, Value);
					}
					else if (FieldType == typeof(int))
					{
						int IntValue;
						if (!int.TryParse(Value, out IntValue))
						{
							throw new CommandLineArgumentException(String.Format("Invalid value for '{0}' - {1} is not an integer.", Field.Name, Value));
						}
						Field.SetValue(Instance, IntValue);
					}
					else if (FieldType == typeof(bool))
					{
						bool BoolValue;
						if (!bool.TryParse(Value, out BoolValue))
						{
							throw new CommandLineArgumentException(String.Format("Invalid value for '{0}' - {1} is not a boolean.", Field.Name, Value));
						}
						Field.SetValue(Instance, BoolValue);
					}
					else if (FieldType.IsEnum)
					{
						object EnumValue;
						try
						{
							EnumValue = Enum.Parse(FieldType, Value, true);
						}
						catch (ArgumentException)
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
			if (Field.GetCustomAttribute<OptionalAttribute>() != null)
			{
				return true;
			}
			if (Field.FieldType.IsGenericType && Field.FieldType.GetGenericTypeDefinition() == typeof(Nullable<>))
			{
				return true;
			}
			return false;
		}

		static string GetFieldValueDescription(Type FieldType)
		{
			if (FieldType.IsEnum)
			{
				return String.Join("|", Enum.GetNames(FieldType));
			}
			else
			{
				return "...";
			}
		}
	}
}
