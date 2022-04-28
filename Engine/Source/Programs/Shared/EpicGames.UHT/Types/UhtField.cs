// Copyright Epic Games, Inc. All Rights Reserved.

namespace EpicGames.UHT.Types
{

	/// <summary>
	/// Represents a UField
	/// </summary>
	public abstract class UhtField : UhtObject
	{
		/// <inheritdoc/>
		public override string EngineClassName { get => "Field"; }

		/// <summary>
		/// Construct a new field
		/// </summary>
		/// <param name="Outer">Outer object</param>
		/// <param name="LineNumber">Line number of declaration</param>
		protected UhtField(UhtType Outer, int LineNumber) : base(Outer, LineNumber)
		{
		}
	}
}
