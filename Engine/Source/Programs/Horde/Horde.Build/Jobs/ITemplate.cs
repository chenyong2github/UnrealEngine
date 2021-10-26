// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using HordeServer.Api;
using HordeCommon;
using HordeServer.Utilities;
using MongoDB.Bson;
using MongoDB.Bson.Serialization.Attributes;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Text.Json.Serialization;
using System.Threading.Tasks;

namespace HordeServer.Models
{
	/// <summary>
	/// Base class for parameters used to configure templates via the new build dialog
	/// </summary>
	[BsonDiscriminator(RootClass = true)]
	[BsonKnownTypes(typeof(GroupParameter), typeof(TextParameter), typeof(ListParameter), typeof(BoolParameter))]
	public abstract class Parameter
	{
		/// <summary>
		/// Gets the default arguments for this parameter and its children
		/// </summary>
		/// <param name="DefaultArguments">List of default arguments</param>
		public abstract void GetDefaultArguments(List<string> DefaultArguments);

		/// <summary>
		/// Convert this parameter to data for serialization
		/// </summary>
		/// <returns>Serializable parameter data</returns>
		public abstract ParameterData ToData();
	}

	/// <summary>
	/// Used to group a number of other parameters
	/// </summary>
	public class GroupParameter : Parameter
	{
		/// <summary>
		/// Label to display next to this parameter
		/// </summary>
		public string Label { get; set; }

		/// <summary>
		/// How to display this group
		/// </summary>
		public GroupParameterStyle Style { get; set; }

		/// <summary>
		/// List of child parameters
		/// </summary>
		public List<Parameter> Children { get; set; }

		/// <summary>
		/// Private constructor for serialization
		/// </summary>
		private GroupParameter()
		{
			this.Label = null!;
			this.Children = null!;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Label">Name of the group</param>
		/// <param name="Style">How to display this group</param>
		/// <param name="Children">List of child parameters</param>
		public GroupParameter(string Label, GroupParameterStyle Style, List<Parameter> Children)
		{
			this.Label = Label;
			this.Style = Style;
			this.Children = Children;
		}

		/// <summary>
		/// Gets the default arguments for this parameter and its children
		/// </summary>
		/// <param name="DefaultArguments">List of default arguments</param>
		public override void GetDefaultArguments(List<string> DefaultArguments)
		{
			foreach (Parameter Child in Children)
			{
				Child.GetDefaultArguments(DefaultArguments);
			}
		}

		/// <summary>
		/// Convert this parameter to data for serialization
		/// </summary>
		/// <returns>Serializable parameter data</returns>
		public override ParameterData ToData()
		{
			return new GroupParameterData(Label, Style, Children.ConvertAll(x => x.ToData()));
		}
	}

	/// <summary>
	/// Free-form text entry parameter
	/// </summary>
	public sealed class TextParameter : Parameter
	{
		/// <summary>
		/// Label to display next to this parameter. Should default to the parameter name.
		/// </summary>
		public string Label { get; set; }

		/// <summary>
		/// Argument to add (will have the value of this field appended)
		/// </summary>
		public string Argument { get; set; }

		/// <summary>
		/// Default value for this argument
		/// </summary>
		public string Default { get; set; }

		/// <summary>
		/// Hint text to display when the field is empty
		/// </summary>
		public string? Hint { get; set; }

		/// <summary>
		/// Regex used to validate values entered into this text field.
		/// </summary>
		[BsonIgnoreIfNull]
		public string? Validation { get; set; }

		/// <summary>
		/// Message displayed to explain valid values if validation fails.
		/// </summary>
		[BsonIgnoreIfNull]
		public string? Description { get; set; }

		/// <summary>
		/// Tool tip text to display
		/// </summary>
		public string? ToolTip { get; set; }

		/// <summary>
		/// Private constructor for serialization
		/// </summary>
		private TextParameter()
		{
			Label = null!;
			Argument = null!;
			Default = String.Empty;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Label">Parameter to pass this value to the BuildGraph script with</param>
		/// <param name="Argument">Label to show next to the parameter</param>
		/// <param name="Default">Default value for this argument</param>
		/// <param name="Hint">Hint text to display</param>
		/// <param name="Validation">Regex used to validate entries</param>
		/// <param name="Description">Message displayed for invalid values</param>
		/// <param name="ToolTip">Tool tip text to display</param>
		public TextParameter(string Label, string Argument, string Default, string? Hint, string? Validation, string? Description, string? ToolTip)
		{
			this.Label = Label;
			this.Argument = Argument;
			this.Default = Default;
			this.Hint = Hint;
			this.Validation = Validation;
			this.Description = Description;
			this.ToolTip = ToolTip;
		}

		/// <summary>
		/// Gets the default arguments for this parameter and its children
		/// </summary>
		/// <param name="DefaultArguments">List of default arguments</param>
		public override void GetDefaultArguments(List<string> DefaultArguments)
		{
			DefaultArguments.Add(Argument + Default);
		}

		/// <summary>
		/// Convert this parameter to data for serialization
		/// </summary>
		/// <returns>Serializable parameter data</returns>
		public override ParameterData ToData()
		{
			return new TextParameterData(Label, Argument, Default, Hint, Validation, Description, ToolTip);
		}
	}

	/// <summary>
	/// Possible option for a list parameter
	/// </summary>
	public class ListParameterItem
	{
		/// <summary>
		/// Group to display this entry in
		/// </summary>
		[BsonIgnoreIfNull]
		public string? Group { get; set; }

		/// <summary>
		/// Text to display for this option.
		/// </summary>
		public string Text { get; set; }

		/// <summary>
		/// Argument to add with this parameter.
		/// </summary>
		[BsonIgnoreIfNull]
		public string? ArgumentIfEnabled { get; set; }

		/// <summary>
		/// Argument to add with this parameter.
		/// </summary>
		[BsonIgnoreIfNull]
		public string? ArgumentIfDisabled { get; set; }

		/// <summary>
		/// Whether this item is selected by default
		/// </summary>
		[BsonIgnoreIfDefault, BsonDefaultValue(false)]
		public bool Default { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Group">The group to put this parameter in</param>
		/// <param name="Text">Text to display for this option</param>
		/// <param name="ArgumentIfEnabled">Argument to add if this option is enabled</param>
		/// <param name="ArgumentIfDisabled">Argument to add if this option is disabled</param>
		/// <param name="Default">Whether this item is selected by default</param>
		public ListParameterItem(string? Group, string Text, string? ArgumentIfEnabled, string? ArgumentIfDisabled, bool Default)
		{
			this.Group = Group;
			this.Text = Text;
			this.ArgumentIfEnabled = ArgumentIfEnabled;
			this.ArgumentIfDisabled = ArgumentIfDisabled;
			this.Default = Default;
		}

		/// <summary>
		/// Convert this parameter to data for serialization
		/// </summary>
		/// <returns>Serializable parameter data</returns>
		public ListParameterItemData ToData()
		{
			return new ListParameterItemData(Group, Text, ArgumentIfEnabled, ArgumentIfDisabled, Default);
		}
	}

	/// <summary>
	/// Allows the user to select a value from a constrained list of choices
	/// </summary>
	public class ListParameter : Parameter
	{
		/// <summary>
		/// Label to display next to this parameter.
		/// </summary>
		public string Label { get; set; }

		/// <summary>
		/// Style of picker parameter to use
		/// </summary>
		public ListParameterStyle Style { get; set; }

		/// <summary>
		/// List of values to display in the list
		/// </summary>
		public List<ListParameterItem> Items { get; set; }

		/// <summary>
		/// Tool tip text to display
		/// </summary>
		public string? ToolTip { get; set; }

		/// <summary>
		/// Private constructor for serialization
		/// </summary>
		private ListParameter()
		{
			this.Label = null!;
			this.Items = null!;
		}

		/// <summary>
		/// List of possible values
		/// </summary>
		/// <param name="Label">Label to show next to this parameter</param>
		/// <param name="Style">Style of list parameter to use</param>
		/// <param name="Entries">Entries for this list</param>
		/// <param name="ToolTip">Tool tip text to display</param>
		public ListParameter(string Label, ListParameterStyle Style, List<ListParameterItem> Entries, string? ToolTip)
		{
			this.Label = Label;
			this.Style = Style;
			this.Items = Entries;
			this.ToolTip = ToolTip;
		}

		/// <summary>
		/// Gets the default arguments for this parameter and its children
		/// </summary>
		/// <param name="DefaultArguments">List of default arguments</param>
		public override void GetDefaultArguments(List<string> DefaultArguments)
		{
			foreach(ListParameterItem Item in Items)
			{
				if (Item.Default)
				{
					if (Item.ArgumentIfEnabled != null)
					{
						DefaultArguments.Add(Item.ArgumentIfEnabled);
					}
				}
				else
				{
					if (Item.ArgumentIfDisabled != null)
					{
						DefaultArguments.Add(Item.ArgumentIfDisabled);
					}
				}
			}
		}

		/// <summary>
		/// Convert this parameter to data for serialization
		/// </summary>
		/// <returns>Serializable parameter data</returns>
		public override ParameterData ToData()
		{
			return new ListParameterData(Label, Style, Items.ConvertAll(x => x.ToData()), ToolTip);
		}
	}

	/// <summary>
	/// Allows the user to toggle an option on or off
	/// </summary>
	public class BoolParameter : Parameter
	{
		/// <summary>
		/// Label to display next to this parameter.
		/// </summary>
		public string Label { get; set; }

		/// <summary>
		/// Value if enabled
		/// </summary>
		[BsonIgnoreIfNull]
		public string? ArgumentIfEnabled { get; set; }

		/// <summary>
		/// Value if disabled
		/// </summary>
		[BsonIgnoreIfNull]
		public string? ArgumentIfDisabled { get; set; }

		/// <summary>
		/// Whether this option should be enabled by default
		/// </summary>
		[BsonIgnoreIfDefault, BsonDefaultValue(false)]
		public bool Default { get; set; }

		/// <summary>
		/// Tool tip text to display
		/// </summary>
		public string? ToolTip { get; set; }

		/// <summary>
		/// Private constructor for serialization
		/// </summary>
		private BoolParameter()
		{
			Label = null!;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Label">Label to display for this parameter</param>
		/// <param name="ArgumentIfEnabled">Value if enabled</param>
		/// <param name="ArgumentIfDisabled">Value if disabled</param>
		/// <param name="Default">Default value for this argument</param>
		/// <param name="ToolTip">Tool tip text to display</param>
		public BoolParameter(string Label, string? ArgumentIfEnabled, string? ArgumentIfDisabled, bool Default, string? ToolTip)
		{
			this.Label = Label;
			this.ArgumentIfEnabled = ArgumentIfEnabled;
			this.ArgumentIfDisabled = ArgumentIfDisabled;
			this.Default = Default;
			this.ToolTip = ToolTip;
		}

		/// <summary>
		/// Gets the default arguments for this parameter and its children
		/// </summary>
		/// <param name="DefaultArguments">List of default arguments</param>
		public override void GetDefaultArguments(List<string> DefaultArguments)
		{
			string? DefaultArgument = Default ? ArgumentIfEnabled : ArgumentIfDisabled;
			if (!String.IsNullOrEmpty(DefaultArgument))
			{
				DefaultArguments.Add(DefaultArgument);
			}
		}
		/// <summary>
		/// Convert this parameter to data for serialization
		/// </summary>
		/// <returns><see cref="BoolParameterData"/> instance</returns>
		public override ParameterData ToData()
		{
			return new BoolParameterData(Label, ArgumentIfEnabled, ArgumentIfDisabled, Default, ToolTip);
		}
	}

	/// <summary>
	/// Document describing a job template. These objects are considered immutable once created and uniquely referenced by hash, in order to de-duplicate across all job runs.
	/// </summary>
	public interface ITemplate
	{
		/// <summary>
		/// Hash of this template
		/// </summary>
		public ContentHash Id { get; }

		/// <summary>
		/// Name of the template.
		/// </summary>
		public string Name { get; }

		/// <summary>
		/// Priority of this job
		/// </summary>
		public Priority? Priority { get; }

		/// <summary>
		/// Whether to allow preflights for this job type
		/// </summary>
		public bool AllowPreflights { get; }

		/// <summary>
		/// Agent type to use for parsing the job state
		/// </summary>
		public string? InitialAgentType { get; }

		/// <summary>
		/// Path to a file within the stream to submit to generate a new changelist for jobs
		/// </summary>
		public string? SubmitNewChange { get; }

		/// <summary>
		/// Optional predefined user-defined properties for this job
		/// </summary>
		public IReadOnlyList<string> Arguments { get; }

		/// <summary>
		/// Parameters for this template
		/// </summary>
		public IReadOnlyList<Parameter> Parameters { get; }
	}

	/// <summary>
	/// Extension methods for templates
	/// </summary>
	public static class TemplateExtensions
	{
		/// <summary>
		/// Gets the arguments for default options in this template. Does not include the standard template arguments.
		/// </summary>
		/// <returns>List of default arguments</returns>
		public static List<string> GetDefaultArguments(this ITemplate Template)
		{
			List<string> DefaultArguments = new List<string>(Template.Arguments);
			foreach (Parameter Parameter in Template.Parameters)
			{
				Parameter.GetDefaultArguments(DefaultArguments);
			}
			return DefaultArguments;
		}
	}
}
