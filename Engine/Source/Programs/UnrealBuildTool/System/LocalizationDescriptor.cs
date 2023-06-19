// Copyright Epic Games, Inc. All Rights Reserved.

using System.Linq;
using System.Diagnostics;
using EpicGames.Core;

namespace UnrealBuildTool
{
	/// <summary>
	/// 
	/// </summary>
	public enum LocalizationTargetDescriptorLoadingPolicy
	{
		/// <summary>
		/// 
		/// </summary>
		Never,

		/// <summary>
		/// 
		/// </summary>
		Always,

		/// <summary>
		/// 
		/// </summary>
		Editor,

		/// <summary>
		/// 
		/// </summary>
		Game,

		/// <summary>
		/// 
		/// </summary>
		PropertyNames,

		/// <summary>
		/// 
		/// </summary>
		ToolTips,
	};

	/// <summary>
	/// 
	/// </summary>
	[DebuggerDisplay("Name={Name}")]
	public class LocalizationTargetDescriptor
	{
		/// <summary>
		/// Name of this target
		/// </summary>
		public readonly string Name;

		/// <summary>
		/// When should the localization data associated with a target should be loaded?
		/// </summary>
		public LocalizationTargetDescriptorLoadingPolicy LoadingPolicy;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="InName">Name of the target</param>
		/// <param name="InLoadingPolicy">When should the localization data associated with a target should be loaded?</param>
		public LocalizationTargetDescriptor(string InName, LocalizationTargetDescriptorLoadingPolicy InLoadingPolicy)
		{
			Name = InName;
			LoadingPolicy = InLoadingPolicy;
		}

		/// <summary>
		/// Constructs a LocalizationTargetDescriptor from a Json object
		/// </summary>
		/// <param name="InObject"></param>
		/// <returns>The new localization target descriptor</returns>
		public static LocalizationTargetDescriptor FromJsonObject(JsonObject InObject)
		{
			return new LocalizationTargetDescriptor(InObject.GetStringField("Name"), InObject.GetEnumField<LocalizationTargetDescriptorLoadingPolicy>("LoadingPolicy"));
		}

		/// <summary>
		/// Write this target to a JsonWriter
		/// </summary>
		/// <param name="Writer">Writer to output to</param>
		void Write(JsonWriter Writer)
		{
			Writer.WriteObjectStart();
			Writer.WriteValue("Name", Name);
			Writer.WriteValue("LoadingPolicy", LoadingPolicy.ToString());
			Writer.WriteObjectEnd();
		}

		JsonObject ToJsonObject()
		{
			JsonObject localizationTargetObject= new JsonObject();
			localizationTargetObject.AddOrSetFieldValue("Name", Name);
			localizationTargetObject.AddOrSetFieldValue("LoadingPolicy", LoadingPolicy.ToString());
			
			return localizationTargetObject;
		}

		/// <summary>
		/// Write an array of target descriptors
		/// </summary>
		/// <param name="Writer">The Json writer to output to</param>
		/// <param name="Name">Name of the array</param>
		/// <param name="Targets">Array of targets</param>
		public static void WriteArray(JsonWriter Writer, string Name, LocalizationTargetDescriptor[]? Targets)
		{
			if (Targets != null && Targets.Length > 0)
			{
				Writer.WriteArrayStart(Name);
				foreach (LocalizationTargetDescriptor Target in Targets)
				{
					Target.Write(Writer);
				}
				Writer.WriteArrayEnd();
			}
		}

		/// <summary>
		/// Updates a JsonObject with an array of localization target descriptors.
		/// </summary>
		/// <param name="InObject">The Json object to update.</param>
		/// <param name="Name">Name of the array</param>
		/// <param name="Targets">Array of targets</param>
		public static void UpdateJson(JsonObject InObject, string Name, LocalizationTargetDescriptor[]? Targets)
		{
			if (Targets != null && Targets.Length > 0)
			{
				JsonObject[] JsonObjects = Targets.Select(X => X.ToJsonObject()).ToArray();
				InObject.AddOrSetFieldValue(Name, JsonObjects);
			}
		}
	}
}
