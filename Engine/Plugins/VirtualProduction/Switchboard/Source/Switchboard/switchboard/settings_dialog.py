# Copyright Epic Games, Inc. All Rights Reserved.
from switchboard.config import CONFIG, SETTINGS, list_config_files
import switchboard.switchboard_widgets as sb_widgets

from PySide2 import QtCore, QtGui, QtUiTools, QtWidgets

import os

RELATIVE_PATH = os.path.dirname(__file__)


class SettingsDialog(QtCore.QObject):
    def __init__(self):
        super().__init__()

        # Set the UI object
        loader = QtUiTools.QUiLoader()
        self.ui = loader.load(os.path.join(RELATIVE_PATH, "ui/settings.ui"))

        # Load qss file for dark styling
        qss_file = os.path.join(RELATIVE_PATH, "ui/switchboard.qss")
        with open(qss_file, "r") as styling:
            self.ui.setStyleSheet(styling.read())

        self.ui.setWindowTitle("Settings")

        max_port = (1 << 16) - 1
        self.ui.osc_server_port_line_edit.setValidator(QtGui.QIntValidator(0, max_port))
        self.ui.osc_client_port_line_edit.setValidator(QtGui.QIntValidator(0, max_port))

        # Store the current config names
        self._config_names = [CONFIG.config_file_name_to_name(x) for x in list_config_files()]
        self._current_config_name = CONFIG.config_file_name_to_name(SETTINGS.CONFIG)

        self.ui.uproject_browse_button.clicked.connect(self.uproject_browse_button_clicked)
        self.ui.engine_dir_browse_button.clicked.connect(self.engine_dir_browse_button_clicked)
        self.ui.config_name_line_edit.textChanged.connect(self.config_name_text_changed)

        # update settings in CONFIG when they are changed in the SettingsDialog
        self.ui.engine_dir_line_edit.editingFinished.connect(lambda widget=self.ui.engine_dir_line_edit: CONFIG.ENGINE_DIR.update_value(widget.text()))
        self.ui.build_engine_checkbox.stateChanged.connect(lambda state: CONFIG.BUILD_ENGINE.update_value(True if state == QtCore.Qt.Checked else False))
        self.ui.uproject_line_edit.editingFinished.connect(lambda widget=self.ui.uproject_line_edit: CONFIG.UPROJECT_PATH.update_value(widget.text()))
        self.ui.map_path_line_edit.editingFinished.connect(lambda widget=self.ui.map_path_line_edit: CONFIG.MAPS_PATH.update_value(widget.text()))
        self.ui.map_filter_line_edit.editingFinished.connect(lambda widget=self.ui.map_filter_line_edit: CONFIG.MAPS_FILTER.update_value(widget.text()))

        self.ui.osc_server_port_line_edit.editingFinished.connect(lambda widget=self.ui.osc_server_port_line_edit: CONFIG.OSC_SERVER_PORT.update_value(int(widget.text())))
        self.ui.osc_client_port_line_edit.editingFinished.connect(lambda widget=self.ui.osc_client_port_line_edit: CONFIG.OSC_CLIENT_PORT.update_value(int(widget.text())))

        self.ui.p4_project_path_line_edit.editingFinished.connect(lambda widget=self.ui.p4_project_path_line_edit: CONFIG.P4_PROJECT_PATH.update_value(widget.text()))
        self.ui.p4_engine_path_line_edit.editingFinished.connect(lambda widget=self.ui.p4_engine_path_line_edit: CONFIG.P4_ENGINE_PATH.update_value(widget.text()))
        self.ui.source_control_workspace_line_edit.editingFinished.connect(lambda widget=self.ui.source_control_workspace_line_edit: CONFIG.SOURCE_CONTROL_WORKSPACE.update_value(widget.text()))

        self._device_groupbox = {}

    def config_name(self):
        return self.ui.config_name_line_edit.text()

    def set_config_name(self, value):
        self.ui.config_name_line_edit.setText(value)

    def config_name_text_changed(self, value):
        if not value:
            valid_name = False
        else:
            if value in self._config_names:
                if value == self._current_config_name:
                    valid_name = True
                else:
                    valid_name = False
            else:
                valid_name = True

        if valid_name:
            sb_widgets.set_qt_property(self.ui.config_name_line_edit, "input_error", False)
        else:
            sb_widgets.set_qt_property(self.ui.config_name_line_edit, "input_error", True)

            rect = self.ui.config_name_line_edit.parent().mapToGlobal(self.ui.config_name_line_edit.geometry().topRight())
            QtWidgets.QToolTip().showText(rect, 'Config name must be unique')

    def uproject_browse_button_clicked(self):
        file_name, _ = QtWidgets.QFileDialog.getOpenFileName(parent=self.ui, filter='*.uproject')
        if file_name:
            file_name = os.path.normpath(file_name)
            self.set_uproject(file_name)
            CONFIG.UPROJECT_PATH.update_value(file_name)

    def engine_dir_browse_button_clicked(self):
        dir_name = QtWidgets.QFileDialog.getExistingDirectory(parent=self.ui)
        if dir_name:
            dir_name = os.path.normpath(dir_name)
            self.set_engine_dir(dir_name)
            CONFIG.ENGINE_DIR.update_value(dir_name)

    def ip_address(self):
        return self.ui.ip_address_line_edit.text()

    def set_ip_address(self, value):
        self.ui.ip_address_line_edit.setText(value)

    def transport_path(self):
        return self.ui.transport_path_line_edit.text()

    def set_transport_path(self, value):
        self.ui.transport_path_line_edit.setText(value)

    def project_name(self):
        return self.ui.project_name_line_edit.text()

    def set_project_name(self, value):
        self.ui.project_name_line_edit.setText(value)

    def uproject(self):
        return self.ui.uproject_line_edit.text()

    def set_uproject(self, value):
        self.ui.uproject_line_edit.setText(value)

    def engine_dir(self):
        return self.ui.engine_dir_line_edit.text()

    def set_engine_dir(self, value):
        self.ui.engine_dir_line_edit.setText(value)

    def set_build_engine(self, value):
        self.ui.build_engine_checkbox.setChecked(value)

    def p4_enabled(self):
        return self.ui.p4_group_box.isChecked()

    def set_p4_enabled(self, enabled):
        self.ui.p4_group_box.setChecked(enabled)

    def p4_project_path(self):
        return self.ui.p4_project_path_line_edit.text()

    def set_p4_project_path(self, value):
        self.ui.p4_project_path_line_edit.setText(value)

    def p4_engine_path(self):
        return self.ui.p4_engine_path_line_edit.text()

    def set_p4_engine_path(self, value):
        self.ui.p4_engine_path_line_edit.setText(value)

    def source_control_workspace(self):
        return self.ui.source_control_workspace_line_edit.text()

    def set_source_control_workspace(self, value):
        self.ui.source_control_workspace_line_edit.setText(value)

    def map_path(self):
        return self.ui.map_path_line_edit.text()

    def set_map_path(self, value):
        self.ui.map_path_line_edit.setText(value)

    def map_filter(self):
        return self.ui.map_filter_line_edit.text()

    def set_map_filter(self, value):
        self.ui.map_filter_line_edit.setText(value)

    # OSC Settings
    def set_osc_server_port(self, port):
        self.ui.osc_server_port_line_edit.setText(str(port))

    def set_osc_client_port(self, port):
        self.ui.osc_client_port_line_edit.setText(str(port))

    # MU SERVER Settings
    def mu_server_name(self):
        return self.ui.mu_server_name_line_edit.text()
    
    def set_mu_server_name(self, value):
        self.ui.mu_server_name_line_edit.setText(value)
    
    def mu_cmd_line_args(self):
        return self.ui.mu_cmd_line_args_line_edit.text()
    
    def set_mu_cmd_line_args(self, value):
        self.ui.mu_cmd_line_args_line_edit.setText(value)
    
    def mu_auto_launch(self):
        return self.ui.mu_auto_launch_check_box.isChecked()
    
    def set_mu_auto_launch(self, value):
        self.ui.mu_auto_launch_check_box.setChecked(value)
    
    def mu_auto_join(self):
        return self.ui.mu_auto_join_check_box.isChecked()
    
    def set_mu_auto_join(self, value):
        self.ui.mu_auto_join_check_box.setChecked(value)
    
    def mu_clean_history(self):
        return self.ui.mu_clean_history_check_box.isChecked()
    
    def set_mu_clean_history(self, value):
        self.ui.mu_clean_history_check_box.setChecked(value)

    # Devices
    def add_section_for_plugin(self, plugin_name, plugin_settings, device_settings):
        any_device_settings = any([device[1] for device in device_settings]) or any([device[2] for device in device_settings])
        if not any_device_settings:
            return # no settings to show

        # Create a group box per plugin
        device_override_group_box = self._device_groupbox.setdefault(plugin_name, QtWidgets.QGroupBox())
        if device_override_group_box.parent() is None:
            device_override_group_box.setTitle(f'{plugin_name} Settings')
            device_override_group_box.setLayout(QtWidgets.QVBoxLayout())
            self.ui.device_override_layout.addWidget(device_override_group_box)

        plugin_layout = QtWidgets.QFormLayout()
        device_override_group_box.layout().addLayout(plugin_layout)

        # add widgets for settings shared by all devices of a plugin
        for setting in plugin_settings:

            if not setting.show_ui:
                continue

            value_type = type(setting.get_value())

            if value_type is list:
                self.create_device_setting_multiselect_combobox(setting, plugin_layout)
            elif len(setting.possible_values) > 0:
                self.create_device_setting_combobox(setting, plugin_layout)
            elif value_type in [str, int]:
                self.create_device_setting_line_edit(setting, plugin_layout)
            elif value_type == bool:
                self.create_device_setting_checkbox(setting, plugin_layout)

        # add widgets for settings and overrides of individual devices
        for device_name, settings, overrides in device_settings:
            group_box = QtWidgets.QGroupBox()
            group_box.setTitle(device_name)
            group_box.setLayout(QtWidgets.QVBoxLayout())

            device_override_group_box.layout().addWidget(group_box)

            layout = QtWidgets.QFormLayout()
            group_box.layout().addLayout(layout)

            # regular "instance" settings
            for setting in settings:

                if not setting.show_ui:
                    continue

                value_type = type(setting.get_value(device_name))

                if value_type is list:
                    self.create_device_setting_multiselect_combobox(setting, layout)
                elif len(setting.possible_values) > 0:
                    self.create_device_setting_combobox(setting, layout)
                elif value_type in [str, int]:
                    self.create_device_setting_line_edit(setting, layout)
                elif value_type == bool:
                    self.create_device_setting_checkbox(setting, layout)

            for setting in overrides:

                if not setting.show_ui:
                    continue
                
                value_type = type(setting.get_value(device_name))

                if value_type in [str, int]:
                    self.create_device_setting_override_line_edit(setting, device_name, layout)
                elif value_type == bool:
                    self.create_device_setting_override_checkbox(setting, device_name, layout)

    def create_device_setting_multiselect_combobox(self, setting, layout):
        label = QtWidgets.QLabel()
        label.setText(setting.nice_name)

        combo = sb_widgets.MultiSelectionComboBox()
        selected_values = setting.get_value()
        possible_values = setting.possible_values if len(setting.possible_values) > 0 else selected_values
        combo.add_items(selected_values, possible_values)

        layout.addRow(label, combo)

        if setting.tool_tip:
            label.setToolTip(setting.tool_tip)
            combo.setToolTip(setting.tool_tip)

        combo.signal_selection_changed.connect(lambda entries, setting=setting: setting.update_value(entries))

    def create_device_setting_combobox(self, setting, layout):
        label = QtWidgets.QLabel()
        label.setText(setting.nice_name)

        combo = sb_widgets.NonScrollableComboBox()

        for value in setting.possible_values:
            combo.addItem(str(value), value)

        combo.setCurrentIndex(combo.findData(setting.get_value()))

        # update the widget if the setting changes, but only when the value is actually different (to avoid endless recursion)
        # this is useful for settings that will change their value based on the input of the widget of another setting
        def on_setting_changed(new_value, combo):
            if combo.currentText() != new_value:
                combo.setCurrentIndex(combo.findText(new_value))
        setting.signal_setting_changed.connect(lambda old, new, widget=combo: on_setting_changed(new, widget)) 

        layout.addRow(label, combo)

        if setting.tool_tip:
            label.setToolTip(setting.tool_tip)
            combo.setToolTip(setting.tool_tip)

        combo.currentTextChanged.connect(lambda text, setting=setting: setting.update_value(text))

    def create_device_setting_line_edit(self, setting, layout):
        
        label = QtWidgets.QLabel()
        label.setText(setting.nice_name)
        line_edit = QtWidgets.QLineEdit()

        value = setting.get_value()
        value_type = type(value)

        if value_type == int:
            value = str(value)

        line_edit.setText(value)
        line_edit.setCursorPosition(0)
        layout.addRow(label, line_edit)

        if setting.tool_tip:
            label.setToolTip(setting.tool_tip)
            line_edit.setToolTip(setting.tool_tip)

        if value_type == str:
            line_edit.editingFinished.connect(lambda field=line_edit, setting=setting: setting.update_value(field.text()))

        elif value_type == int:

            def le_int_editingFinished(field, setting):
                try:
                    text = field.text()
                    setting.update_value(int(text))
                except ValueError:
                    field.setText(str(setting.get_value()))

            line_edit.editingFinished.connect(lambda field=line_edit, setting=setting : le_int_editingFinished(field=line_edit, setting=setting))

        def on_setting_changed(new_value, line_edit):
            if line_edit.text() != new_value:
                line_edit.setText(str(new_value))
                line_edit.setCursorPosition(0)

        setting.signal_setting_changed.connect(lambda old, new, widget=line_edit: on_setting_changed(new, widget))

    def create_device_setting_checkbox(self, setting, layout):
        check_box = QtWidgets.QCheckBox()
        check_box.setChecked(setting.get_value())
        layout.addRow(f"{setting.nice_name}", check_box)

        if setting.tool_tip:
            check_box.setToolTip(setting.tool_tip)

        check_box.stateChanged.connect(lambda state, setting=setting: setting.update_value(bool(state)))

    def create_device_setting_override_line_edit(self, setting, device_name, layout):
        label = QtWidgets.QLabel()
        label.setText(setting.nice_name)
        line_edit = QtWidgets.QLineEdit()

        if setting.is_overriden(device_name):
            sb_widgets.set_qt_property(line_edit, "override", True)

        value = setting.get_value(device_name)
        line_edit.setText(str(value))
        line_edit.setCursorPosition(0)
        layout.addRow(label, line_edit)

        if setting.tool_tip:
            label.setToolTip(setting.tool_tip)
            line_edit.setToolTip(setting.tool_tip)

        line_edit.editingFinished.connect(lambda field=line_edit, setting=setting, device_name=device_name: self._on_line_edit_override_editingFinished(field, setting, device_name))

        def on_base_value_changed(setting, device_name, line_edit):
            new_base_value = setting.get_value()
            if not setting.is_overriden(device_name):
                line_edit.setText(str(new_base_value))
                line_edit.setCursorPosition(0)
                if new_base_value == setting.get_value(device_name):
                    # if the setting was overriden but the new base value happens to be the same as the override we can remove the override
                    sb_widgets.set_qt_property(line_edit, "override", False)
                    setting.remove_override(device_name)

        setting.signal_setting_changed.connect(lambda old, new, setting=setting, device_name=device_name, widget=line_edit: on_base_value_changed(setting, device_name, widget))

    def _on_line_edit_override_editingFinished(self, widget, setting, device_name):

        old_value = setting.get_value(device_name)
        new_value = widget.text()

        if type(old_value) == int:
            try:
                new_value = int(new_value)
            except ValueError:
                new_value = setting._original_value

        if new_value != old_value:
            setting.override_value(device_name, new_value)

        if setting.is_overriden(device_name):
            sb_widgets.set_qt_property(widget, "override", True)
        else:
            sb_widgets.set_qt_property(widget, "override", False)
            setting.remove_override(device_name)

    def create_device_setting_override_checkbox(self, setting, device_name, layout):
        check_box = QtWidgets.QCheckBox()
        check_box.setChecked(setting.get_value(device_name))

        if setting.is_overriden(device_name):
            sb_widgets.set_qt_property(check_box, "override", True)

        layout.addRow(f"{setting.nice_name}", check_box)

        if setting.tool_tip:
            check_box.setToolTip(setting.tool_tip)

        check_box.stateChanged.connect(lambda state, widget=check_box, setting=setting, device_name=device_name: self._on_checkbox_override_stateChanged(widget, setting, device_name))

        def on_base_value_changed(new_value, check_box):
            check_box.setChecked(new_value)
            # reset checkbox override state, as the checkbox has only two states there is no override anymore when the base value changes
            sb_widgets.set_qt_property(check_box, "override", False)
            setting.remove_override(device_name)

        setting.signal_setting_changed.connect(lambda old, new, widget=check_box: on_base_value_changed(new, widget))

    def _on_checkbox_override_stateChanged(self, widget, setting, device_name):
        old_value = setting.get_value(device_name)
        new_value = bool(widget.checkState())
        if new_value != old_value:
            setting.override_value(device_name, new_value)

        if setting.is_overriden(device_name):
            sb_widgets.set_qt_property(widget, "override", True)
        else:
            sb_widgets.set_qt_property(widget, "override", False)
            setting.remove_override(device_name)
