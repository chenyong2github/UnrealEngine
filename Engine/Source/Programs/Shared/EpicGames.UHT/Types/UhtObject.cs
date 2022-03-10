// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Tables;
using EpicGames.UHT.Utils;

namespace EpicGames.UHT.Types
{
	/// <summary>
	/// Represents a UObject in the engine
	/// </summary>
	[UhtEngineClass(Name = "Object")]
	public abstract class UhtObject : UhtType
	{

		/// <summary>
		/// Internal object flags.
		/// </summary>
		public EInternalObjectFlags InternalObjectFlags = EInternalObjectFlags.None;

		/// <summary>
		/// Unique index of the object
		/// </summary>
		public readonly int ObjectTypeIndex;

		/// <summary>
		/// The alternate object is used by the interface system where the native interface will
		/// update this setting to point to the UInterface derived companion object.
		/// </summary>
		public UhtObject? AlternateObject = null;

		/// <inheritdoc/>
		public override string EngineClassName { get => "Object"; }

		/// <summary>
		/// Construct a new instance of the object
		/// </summary>
		/// <param name="Session">Session of the object</param>
		protected UhtObject(UhtSession Session) : base(Session)
		{
			ObjectTypeIndex = this.Session.GetNextObjectTypeIndex();
		}

		/// <summary>
		/// Construct a new instance of the object
		/// </summary>
		/// <param name="Outer">Outer object</param>
		/// <param name="LineNumber">Line number where object is defined</param>
		protected UhtObject(UhtType Outer, int LineNumber) : base(Outer, LineNumber)
		{
			ObjectTypeIndex = this.Session.GetNextObjectTypeIndex();
		}
	}
}
