namespace UnrealGameSync.Forms
{
	partial class NetCoreWindow
	{
		/// <summary>
		/// Required designer variable.
		/// </summary>
		private System.ComponentModel.IContainer components = null;

		/// <summary>
		/// Clean up any resources being used.
		/// </summary>
		/// <param name="disposing">true if managed resources should be disposed; otherwise, false.</param>
		protected override void Dispose(bool disposing)
		{
			if (disposing && (components != null))
			{
				components.Dispose();
			}
			base.Dispose(disposing);
		}

		#region Windows Form Designer generated code

		/// <summary>
		/// Required method for Designer support - do not modify
		/// the contents of this method with the code editor.
		/// </summary>
		private void InitializeComponent()
		{
			this.label1 = new System.Windows.Forms.Label();
			this.linkLabel1 = new System.Windows.Forms.LinkLabel();
			this.SnoozeBtn = new System.Windows.Forms.Button();
			this.DismissBtn = new System.Windows.Forms.Button();
			this.SuspendLayout();
			// 
			// label1
			// 
			this.label1.AutoSize = true;
			this.label1.Location = new System.Drawing.Point(13, 13);
			this.label1.Name = "label1";
			this.label1.Size = new System.Drawing.Size(838, 15);
			this.label1.TabIndex = 0;
			this.label1.Text = "An upcoming release of UnrealGameSync will require NET Core 3.1. Please download " +
    "and install the NET Core runtime from this page to avoid upgrade issues.";
			// 
			// linkLabel1
			// 
			this.linkLabel1.AutoSize = true;
			this.linkLabel1.Location = new System.Drawing.Point(13, 41);
			this.linkLabel1.Name = "linkLabel1";
			this.linkLabel1.Size = new System.Drawing.Size(379, 15);
			this.linkLabel1.TabIndex = 1;
			this.linkLabel1.TabStop = true;
			this.linkLabel1.Text = "https://dotnet.microsoft.com/download/dotnet-core/current/runtime";
			this.linkLabel1.LinkClicked += new System.Windows.Forms.LinkLabelLinkClickedEventHandler(this.linkLabel1_LinkClicked);
			// 
			// SnoozeBtn
			// 
			this.SnoozeBtn.Anchor = ((System.Windows.Forms.AnchorStyles)((System.Windows.Forms.AnchorStyles.Bottom | System.Windows.Forms.AnchorStyles.Right)));
			this.SnoozeBtn.DialogResult = System.Windows.Forms.DialogResult.Cancel;
			this.SnoozeBtn.Location = new System.Drawing.Point(671, 74);
			this.SnoozeBtn.Name = "SnoozeBtn";
			this.SnoozeBtn.Size = new System.Drawing.Size(102, 30);
			this.SnoozeBtn.TabIndex = 2;
			this.SnoozeBtn.Text = "Snooze";
			this.SnoozeBtn.UseVisualStyleBackColor = true;
			this.SnoozeBtn.Click += new System.EventHandler(this.SnoozeBtn_Click);
			// 
			// DismissBtn
			// 
			this.DismissBtn.Anchor = ((System.Windows.Forms.AnchorStyles)((System.Windows.Forms.AnchorStyles.Bottom | System.Windows.Forms.AnchorStyles.Right)));
			this.DismissBtn.DialogResult = System.Windows.Forms.DialogResult.OK;
			this.DismissBtn.Location = new System.Drawing.Point(779, 74);
			this.DismissBtn.Name = "DismissBtn";
			this.DismissBtn.Size = new System.Drawing.Size(102, 30);
			this.DismissBtn.TabIndex = 3;
			this.DismissBtn.Text = "Dismiss";
			this.DismissBtn.UseVisualStyleBackColor = true;
			this.DismissBtn.Click += new System.EventHandler(this.DismissBtn_Click);
			// 
			// NetCoreWindow
			// 
			this.AcceptButton = this.DismissBtn;
			this.AutoScaleDimensions = new System.Drawing.SizeF(96F, 96F);
			this.AutoScaleMode = System.Windows.Forms.AutoScaleMode.Dpi;
			this.ClientSize = new System.Drawing.Size(893, 116);
			this.Controls.Add(this.DismissBtn);
			this.Controls.Add(this.SnoozeBtn);
			this.Controls.Add(this.linkLabel1);
			this.Controls.Add(this.label1);
			this.Font = new System.Drawing.Font("Segoe UI", 9F, System.Drawing.FontStyle.Regular, System.Drawing.GraphicsUnit.Point, ((byte)(0)));
			this.MaximizeBox = false;
			this.Name = "NetCoreWindow";
			this.ShowIcon = false;
			this.StartPosition = System.Windows.Forms.FormStartPosition.Manual;
			this.Text = "NET Core";
			this.ResumeLayout(false);
			this.PerformLayout();

		}

		#endregion

		private System.Windows.Forms.Label label1;
		private System.Windows.Forms.LinkLabel linkLabel1;
		private System.Windows.Forms.Button SnoozeBtn;
		private System.Windows.Forms.Button DismissBtn;
	}
}