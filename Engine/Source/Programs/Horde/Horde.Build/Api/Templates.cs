// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using HordeCommon;
using HordeServer.Models;
using HordeServer.Utilities;
using System;
using System.Collections.Generic;
using System.ComponentModel.DataAnnotations;
using System.Linq;
using System.Text.Json.Serialization;
using System.Threading.Tasks;

namespace HordeServer.Api
{
	/// <summary>
	/// Base class for template parameters
	/// </summary>
	[JsonKnownTypes(typeof(GroupParameterData), typeof(TextParameterData), typeof(ListParameterData), typeof(BoolParameterData))]
	public abstract class ParameterData
	{
		/// <summary>
		/// Convert to a parameter object
		/// </summary>
		/// <returns><see cref="Parameter"/> object</returns>
		public abstract Parameter ToModel();
	}

	/// <summary>
	/// Describes how to render a group parameter
	/// </summary>
	public enum GroupParameterStyle
	{
		/// <summary>
		/// Separate tab on the form
		/// </summary>
		Tab,

		/// <summary>
		/// Section with heading
		/// </summary>
		Section,
	}

	/// <summary>
	/// Used to group a number of other parameters
	/// </summary>
	[JsonDiscriminator("Group")]
	public class GroupParameterData : ParameterData
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
		public List<ParameterData> Children { get; set; }

		/// <summary>
		/// Private constructor for serialization
		/// </summary>
		private GroupParameterData()
		{
			Label = null!;
			Children = null!;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Label">Name of the group</param>
		/// <param name="Style">How to display this group</param>
		/// <param name="Children">List of child parameters</param>
		public GroupParameterData(string Label, GroupParameterStyle Style, List<ParameterData> Children)
		{
			this.Label = Label;
			this.Style = Style;
			this.Children = Children;
		}

		/// <summary>
		/// Converts this data to a model object
		/// </summary>
		/// <returns>New <see cref="GroupParameter"/> object</returns>
		public override Parameter ToModel()
		{
			return new GroupParameter(Label, Style, Children.ConvertAll(x => x.ToModel()));
		}
	}

	/// <summary>
	/// Free-form text entry parameter
	/// </summary>
	[JsonDiscriminator("Text")]
	public class TextParameterData : ParameterData
	{
		/// <summary>
		/// Name of the parameter associated with this parameter.
		/// </summary>
		public string Label { get; set; }

		/// <summary>
		/// Argument to pass to the executor
		/// </summary>
		public string Argument { get; set; }

		/// <summary>
		/// Default value for this argument
		/// </summary>
		public string Default { get; set; }

		/// <summary>
		/// Hint text for this parameter
		/// </summary>
		public string? Hint { get; set; }

		/// <summary>
		/// Regex used to validate this parameter
		/// </summary>
		public string? Validation { get; set; }

		/// <summary>
		/// Message displayed if validation fails, informing user of valid values.
		/// </summary>
		public string? ValidationError { get; set; }

		/// <summary>
		/// Tool-tip text to display
		/// </summary>
		public string? ToolTip { get; set; }

		/// <summary>
		/// Private constructor for serialization
		/// </summary>
		private TextParameterData()
		{
			Label = null!;
			Argument = null!;
			Default = String.Empty;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Label">Label to show next to the parameter</param>
		/// <param name="Argument">Argument to pass this value with</param>
		/// <param name="Default">Default value for this parameter</param>
		/// <param name="Hint">Hint text to display for this parameter</param>
		/// <param name="Validation">Regex used to validate entries</param>
		/// <param name="ValidationError">Message displayed to explain validation issues</param>
		/// <param name="ToolTip">Tool tip text to display</param>
		public TextParameterData(string Label, string Argument, string Default, string? Hint, string? Validation, string? ValidationError, string? ToolTip)
		{
			this.Label = Label;
			this.Argument = Argument;
			this.Default = Default;
			this.Hint = Hint;
			this.Validation = Validation;
			this.ValidationError = ValidationError;
			this.ToolTip = ToolTip;
		}

		/// <summary>
		/// Converts this data to a model object
		/// </summary>
		/// <returns>New <see cref="TextParameter"/> object</returns>
		public override Parameter ToModel()
		{
			return new TextParameter(Label, Argument, Default, Hint, Validation, ValidationError, ToolTip);
		}
	}

	/// <summary>
	/// Style of list parameter
	/// </summary>
	public enum ListParameterStyle
	{
		/// <summary>
		/// Regular drop-down list. One item is always selected.
		/// </summary>
		List,

		/// <summary>
		/// Drop-down list with checkboxes
		/// </summary>
		MultiList,

		/// <summary>
		/// Tag picker from list of options
		/// </summary>
		TagPicker,
	}

	/// <summary>
	/// Possible option for a list parameter
	/// </summary>
	public class ListParameterItemData
	{
		/// <summary>
		/// Optional group heading to display this entry under, if the picker style supports it.
		/// </summary>
		public string? Group { get; set; }

		/// <summary>
		/// Name of the parameter associated with this list.
		/// </summary>
		public string Text { get; set; }

		/// <summary>
		/// Argument to pass with this parameter.
		/// </summary>
		public string? ArgumentIfEnabled { get; set; }

		/// <summary>
		/// Argument to pass with this parameter.
		/// </summary>
		public string? ArgumentIfDisabled { get; set; }

		/// <summary>
		/// Whether this item is selected by default
		/// </summary>
		public bool Default { get; set; }

		/// <summary>
		/// Private constructor for serialization
		/// </summary>
		private ListParameterItemData()
		{
			Text = null!;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Group">The group to put this parameter in</param>
		/// <param name="Text">Text to display for this option</param>
		/// <param name="ArgumentIfEnabled">Argument to pass for this item if it's enabled</param>
		/// <param name="ArgumentIfDisabled">Argument to pass for this item if it's enabled</param>
		/// <param name="Default">Whether this item is selected by default</param>
		public ListParameterItemData(string? Group, string Text, string? ArgumentIfEnabled, string? ArgumentIfDisabled, bool Default)
		{
			this.Group = Group;
			this.Text = Text;
			this.ArgumentIfEnabled = ArgumentIfEnabled;
			this.ArgumentIfDisabled = ArgumentIfDisabled;
			this.Default = Default;
		}

		/// <summary>
		/// Converts this data to a model object
		/// </summary>
		/// <returns>New <see cref="ListParameterItem"/> object</returns>
		public ListParameterItem ToModel()
		{
			return new ListParameterItem(Group, Text, ArgumentIfEnabled, ArgumentIfDisabled, Default);
		}
	}

	/// <summary>
	/// Allows the user to select a value from a constrained list of choices
	/// </summary>
	[JsonDiscriminator("List")]
	public class ListParameterData : ParameterData
	{
		/// <summary>
		/// Label to display next to this parameter. Defaults to the parameter name.
		/// </summary>
		public string Label { get; set; }

		/// <summary>
		/// The type of list parameter
		/// </summary>
		public ListParameterStyle Style { get; set; }

		/// <summary>
		/// List of values to display in the list
		/// </summary>
		public List<ListParameterItemData> Items { get; set; }

		/// <summary>
		/// Tool tip text to display
		/// </summary>
		public string? ToolTip { get; set; }

		/// <summary>
		/// Private constructor
		/// </summary>
		private ListParameterData()
		{
			Label = null!;
			Items = null!;
		}

		/// <summary>
		/// List of possible values
		/// </summary>
		/// <param name="Label">Label to show next to this parameter</param>
		/// <param name="Style">Type of picker to show</param>
		/// <param name="Items">Entries for this list</param>
		/// <param name="ToolTip">Tool tip text to display</param>
		public ListParameterData(string Label, ListParameterStyle Style, List<ListParameterItemData> Items, string? ToolTip)
		{
			this.Label = Label;
			this.Style = Style;
			this.Items = Items;
			this.ToolTip = ToolTip;
		}

		/// <summary>
		/// Converts this data to a model object
		/// </summary>
		/// <returns>New <see cref="ListParameter"/> object</returns>
		public override Parameter ToModel()
		{
			return new ListParameter(Label, Style, Items.ConvertAll(x => x.ToModel()), ToolTip);
		}
	}

	/// <summary>
	/// Allows the user to toggle an option on or off
	/// </summary>
	[JsonDiscriminator("Bool")]
	public class BoolParameterData : ParameterData
	{
		/// <summary>
		/// Name of the parameter associated with this parameter.
		/// </summary>
		public string Label { get; set; }

		/// <summary>
		/// Value if enabled
		/// </summary>
		public string? ArgumentIfEnabled { get; set; }

		/// <summary>
		/// Value if disabled
		/// </summary>
		public string? ArgumentIfDisabled { get; set; }

		/// <summary>
		/// Whether this argument is enabled by default
		/// </summary>
		public bool Default { get; set; }

		/// <summary>
		/// Tool tip text to display
		/// </summary>
		public string? ToolTip { get; set; }

		/// <summary>
		/// Private constructor for serialization
		/// </summary>
		private BoolParameterData()
		{
			Label = null!;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Label">Label to show next to this parameter</param>
		/// <param name="ArgumentIfEnabled">Value if enabled</param>
		/// <param name="ArgumentIfDisabled">Value if disabled</param>
		/// <param name="Default">Whether this option is enabled by default</param>
		/// <param name="ToolTip">The tool tip text to display</param>
		public BoolParameterData(string Label, string? ArgumentIfEnabled, string? ArgumentIfDisabled, bool Default, string? ToolTip)
		{
			this.Label = Label;
			this.ArgumentIfEnabled = ArgumentIfEnabled;
			this.ArgumentIfDisabled = ArgumentIfDisabled;
			this.Default = Default;
			this.ToolTip = ToolTip;
		}

		/// <summary>
		/// Converts this data to a model object
		/// </summary>
		/// <returns>New <see cref="BoolParameter"/> object</returns>
		public override Parameter ToModel()
		{
			return new BoolParameter(Label, ArgumentIfEnabled, ArgumentIfDisabled, Default, ToolTip);
		}
	}

	/// <summary>
	/// Parameters to create a new template
	/// </summary>
	public class CreateTemplateRequest
	{
		/// <summary>
		/// Name for the new template
		/// </summary>
		[Required]
		public string Name { get; set; } = null!;

		/// <summary>
		/// Default priority for this job
		/// </summary>
		public Priority? Priority { get; set; }

		/// <summary>
		/// Whether to allow preflights of this template
		/// </summary>
		public bool AllowPreflights { get; set; } = true;

		/// <summary>
		/// Initial agent type to parse the buildgraph script on
		/// </summary>
		public string? InitialAgentType { get; set; }

		/// <summary>
		/// Path to a file within the stream to submit to generate a new changelist for jobs
		/// </summary>
		public string? SubmitNewChange { get; set; }

		/// <summary>
		/// Fixed arguments for the new job
		/// </summary>
		public List<string> Arguments { get; set; } = new List<string>();

		/// <summary>
		/// Parameters for this template
		/// </summary>
		public List<ParameterData> Parameters { get; set; } = new List<ParameterData>();
	}

	/// <summary>
	/// Response describing a template
	/// </summary>
	public class GetTemplateResponseBase
	{
		/// <summary>
		/// Name of the template
		/// </summary>
		public string Name { get; set; }

		/// <summary>
		/// Default priority for this job
		/// </summary>
		public Priority? Priority { get; set; }

		/// <summary>
		/// Whether to allow preflights of this template
		/// </summary>
		public bool AllowPreflights { get; set; }

		/// <summary>
		/// The initial agent type to parse the BuildGraph script on
		/// </summary>
		public string? InitialAgentType { get; set; }

		/// <summary>
		/// Path to a file within the stream to submit to generate a new changelist for jobs
		/// </summary>
		public string? SubmitNewChange { get; }

		/// <summary>
		/// Parameters for the job.
		/// </summary>
		public List<string> Arguments { get; set; }

		/// <summary>
		/// List of parameters for this template
		/// </summary>
		public List<ParameterData> Parameters { get; set; }

		/// <summary>
		/// Parameterless constructor for serialization
		/// </summary>
		protected GetTemplateResponseBase()
		{
			this.Name = null!;
			this.AllowPreflights = true;
			this.Arguments = new List<string>();
			this.Parameters = new List<ParameterData>();
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Template">The template to construct from</param>
		public GetTemplateResponseBase(ITemplate Template)
		{
			this.Name = Template.Name;
			this.Priority = Template.Priority;
			this.AllowPreflights = Template.AllowPreflights;
			this.InitialAgentType = Template.InitialAgentType;
			this.SubmitNewChange = Template.SubmitNewChange;
			this.Arguments = new List<string>(Template.Arguments);
			this.Parameters = Template.Parameters.ConvertAll(x => x.ToData());
		}
	}

	/// <summary>
	/// Response describing a template
	/// </summary>
	public class GetTemplateResponse : GetTemplateResponseBase
	{
		/// <summary>
		/// Unique id of the template
		/// </summary>
		public string Id { get; set; }

		/// <summary>
		/// Parameterless constructor for serialization
		/// </summary>
		protected GetTemplateResponse()
			: base()
		{
			this.Id = null!;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Template">The template to construct from</param>
		public GetTemplateResponse(ITemplate Template)
			: base(Template)
		{
			this.Id = Template.Id.ToString();
		}
	}
}
