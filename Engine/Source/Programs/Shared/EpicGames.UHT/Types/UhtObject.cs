// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Tables;
using EpicGames.UHT.Utils;

namespace EpicGames.UHT.Types
{
	[UhtEngineClass(Name = "Object")]
	public abstract class UhtObject : UhtType
	{
		public EInternalObjectFlags InternalObjectFlags = EInternalObjectFlags.None;
		public readonly int ObjectTypeIndex;

		/// <summary>
		/// The alternate object is used by the interface system where the native interface will
		/// update this setting to point to the UInterface derived companion object.
		/// </summary>
		public UhtObject? AlternateObject = null;

		/// <inheritdoc/>
		public override string EngineClassName { get => "Object"; }

		protected UhtObject(UhtSession Session) : base(Session)
		{
			ObjectTypeIndex = this.Session.GetNextObjectTypeIndex();
		}

		protected UhtObject(UhtType Outer, int LineNumber) : base(Outer, LineNumber)
		{
			ObjectTypeIndex = this.Session.GetNextObjectTypeIndex();
		}
	}
}
