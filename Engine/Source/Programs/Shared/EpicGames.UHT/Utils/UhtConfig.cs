// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Tokenizer;

namespace EpicGames.UHT.Utils
{

	/// <summary>
	/// Describes how pointers will generate warnings or errors
	/// </summary>
	public enum UhtPointerMemberBehavior
	{

		/// <summary>
		/// An error will be generated.
		/// </summary>
		Disallow,

		/// <summary>
		/// Ignore the pointer
		/// </summary>
		AllowSilently,

		/// <summary>
		/// Log a warning about the pointer
		/// </summary>
		AllowAndLog,
	};

	/// <summary>
	/// Interface for accessing configuration data.  Since UnrealBuildTool depends on EpicGames.UHT and all 
	/// of the configuration support exists in UBT, configuration data must be accessed through an interface.
	/// </summary>
	public interface IUhtConfig
	{
		/// <summary>
		/// Default version of generated code. Defaults to oldest possible, unless specified otherwise in config.
		/// </summary>
		public EGeneratedCodeVersion DefaultGeneratedCodeVersion { get; }

		/// <summary>
		/// Pointer warning for native pointers in the engine
		/// </summary>
		public UhtPointerMemberBehavior EngineNativePointerMemberBehavior { get; }

		/// <summary>
		/// Pointer warning for object pointers in the engine
		/// </summary>
		public UhtPointerMemberBehavior EngineObjectPtrMemberBehavior { get; }

		/// <summary>
		/// Pointer warning for native pointers outside the engine
		/// </summary>
		public UhtPointerMemberBehavior NonEngineNativePointerMemberBehavior { get; }

		/// <summary>
		/// Pointer warning for object pointers outside the engine
		/// </summary>
		public UhtPointerMemberBehavior NonEngineObjectPtrMemberBehavior { get; }

		/// <summary>
		/// If true setters and getters will be automatically (without specifying their function names on a property) parsed and generated if a function with matching signature is found
		/// </summary>
		public bool bAllowAutomaticSettersAndGetters { get; }

		/// <summary>
		/// If the token references a remapped identifier, update the value in the token 
		/// </summary>
		/// <param name="Token">Token to be remapped</param>
		public void RedirectTypeIdentifier(ref UhtToken Token);

		/// <summary>
		/// Return the remapped key or the existing key
		/// </summary>
		/// <param name="Key">Key to be remapped.</param>
		/// <param name="NewKey">Resulting key name</param>
		/// <returns>True if the key has been remapped and has changed.</returns>
		public bool RedirectMetaDataKey(string Key, out string NewKey);

		/// <summary>
		/// Test to see if the given units are valid.
		/// </summary>
		/// <param name="Units">Units to test</param>
		/// <returns>True if the units are valid, false if not</returns>
		public bool IsValidUnits(StringView Units);

		/// <summary>
		/// Test to see if the structure name should be using a "T" prefix.
		/// </summary>
		/// <param name="Name">Name of the structure to test without any prefix.</param>
		/// <returns>True if the structure should have a "T" prefix.</returns>
		public bool IsStructWithTPrefix(StringView Name);

		/// <summary>
		/// Test to see if the given macro has a parameter count as part of the name.
		/// </summary>
		/// <param name="DelegateMacro">Macro to test</param>
		/// <returns>-1 if the macro does not contain a parameter count.  The number of parameters minus one.</returns>
		public int FindDelegateParameterCount(StringView DelegateMacro);

		/// <summary>
		/// Get the parameter count string associated with the given index
		/// </summary>
		/// <param name="Index">Index from a prior call to FindDelegateParameterCount or -1.</param>
		/// <returns>Parameter count string or an empty string if Index is -1.</returns>
		public StringView GetDelegateParameterCountString(int Index);

		/// <summary>
		/// Test to see if the exporter is enabled
		/// </summary>
		/// <param name="Name">Name of the exporter</param>
		/// <returns>True if the exporter is enabled, false if not</returns>
		public bool IsExporterEnabled(string Name);
	}

	/// <summary>
	/// Helper class to house the IUhtConfig instance
	/// </summary>
	public static class UhtConfig
	{
		private static IUhtConfig? InstanceInternal = null;

		/// <summary>
		/// Get/Set the instance of IUhtConfig
		/// </summary>
		public static IUhtConfig Instance 
		{ 
			get
			{
				if (UhtConfig.InstanceInternal == null)
				{
					throw new UhtIceException("UhtConfig instance has not been set");
				}
				return UhtConfig.InstanceInternal;
			}

			set
			{
				UhtConfig.InstanceInternal = value;
			}
		}
	}
}
