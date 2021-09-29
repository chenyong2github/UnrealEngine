# Copyright Epic Games, Inc. All Rights Reserved.

import os
import pathlib

from PySide2 import QtCore
from PySide2 import QtGui
from PySide2 import QtUiTools
from PySide2 import QtWidgets

from switchboard import config
from switchboard import switchboard_widgets as sb_widgets
from switchboard.config import CONFIG, SETTINGS

RELATIVE_PATH = os.path.dirname(__file__)


class SettingsDialog(QtCore.QObject):
    def __init__(self):
        super().__init__()

        # Set the UI object
        loader = QtUiTools.QUiLoader()
        self.ui = loader.load(os.path.join(RELATIVE_PATH, "ui/settings.ui"))

        self.ui.setWindowTitle("Settings")

        self.ui.finished.connect(self._on_finished)

        max_port = (1 << 16) - 1
        self.ui.osc_server_port_line_edit.setValidator(
            QtGui.QIntValidator(0, max_port))
        self.ui.osc_client_port_line_edit.setValidator(
            QtGui.QIntValidator(0, max_port))

        # Store the current config paths so we can warn about overwriting an
        # existing config.
        self._config_paths = config.list_config_paths()
        self.set_config_path(SETTINGS.CONFIG)

        self.ui.config_path_line_edit.textChanged.connect(
            self.config_path_text_changed)
        self.ui.uproject_browse_button.clicked.connect(
            self.uproject_browse_button_clicked)
        self.ui.engine_dir_browse_button.clicked.connect(
            self.engine_dir_browse_button_clicked)

        # update settings in CONFIG when they are changed in the SettingsDialog
        self.ui.engine_dir_line_edit.editingFinished.connect(
            lambda widget=self.ui.engine_dir_line_edit:
                CONFIG.ENGINE_DIR.update_value(widget.text()))
        self.ui.build_engine_checkbox.stateChanged.connect(
            lambda state:
                CONFIG.BUILD_ENGINE.update_value(
                    True if state == QtCore.Qt.Checked else False))
        self.ui.uproject_line_edit.editingFinished.connect(
            lambda widget=self.ui.uproject_line_edit:
                CONFIG.UPROJECT_PATH.update_value(widget.text()))
        self.ui.map_path_line_edit.editingFinished.connect(
            lambda widget=self.ui.map_path_line_edit:
                CONFIG.MAPS_PATH.update_value(widget.text()))
        self.ui.map_filter_line_edit.editingFinished.connect(
            lambda widget=self.ui.map_filter_line_edit:
                CONFIG.MAPS_FILTER.update_value(widget.text()))

        self.ui.osc_server_port_line_edit.editingFinished.connect(
            lambda widget=self.ui.osc_server_port_line_edit:
                CONFIG.OSC_SERVER_PORT.update_value(int(widget.text())))
        self.ui.osc_client_port_line_edit.editingFinished.connect(
            lambda widget=self.ui.osc_client_port_line_edit:
                CONFIG.OSC_CLIENT_PORT.update_value(int(widget.text())))

        self.ui.p4_project_path_line_edit.editingFinished.connect(
            lambda widget=self.ui.p4_project_path_line_edit:
                CONFIG.P4_PROJECT_PATH.update_value(widget.text()))
        self.ui.p4_engine_path_line_edit.editingFinished.connect(
            lambda widget=self.ui.p4_engine_path_line_edit:
                CONFIG.P4_ENGINE_PATH.update_value(widget.text()))
        self.ui.source_control_workspace_line_edit.editingFinished.connect(
            lambda widget=self.ui.source_control_workspace_line_edit:
                CONFIG.SOURCE_CONTROL_WORKSPACE.update_value(widget.text()))

        self._device_groupbox = {}

    def _on_finished(self, result: int):
        # Currently, the only way to dismiss the settings dialog is by using
        # the close button as opposed to ok/cancel buttons, so we intercept the
        # close to issue a warning if the config path was changed and we're
        # about to overwrite some other existing config file.
        if (self._changed_config_path and
                self._changed_config_path in self._config_paths and
                self._changed_config_path != self._current_config_path):
            # Show the confirmation dialog using a relative path to the config.
            rel_config_path = config.get_relative_config_path(
                self._changed_config_path)
            reply = QtWidgets.QMessageBox.question(
                self.ui, 'Confirm Overwrite',
                ('Are you sure you would like to change the config path and '
                 f'overwrite the existing config file "{rel_config_path}"?'),
                QtWidgets.QMessageBox.Yes, QtWidgets.QMessageBox.No)

            if reply != QtWidgets.QMessageBox.Yes:
                # Clear the changed config path so that the dialog returns the
                # originally specified path when queried via config_path().
                self._changed_config_path = None

    def config_path(self):
        if self._changed_config_path:
            return self._changed_config_path

        return self._current_config_path

    def set_config_path(self, config_path: pathlib.Path):
        self._current_config_path = config_path
        self._changed_config_path = None

        if not config_path:
            self._changed_config_path = config.Config.DEFAULT_CONFIG_PATH
            rel_config_path_str = self._changed_config_path.stem
        else:
            rel_config_path_str = str(
                config.get_relative_config_path(config_path).with_suffix(''))

        self.ui.config_path_line_edit.setText(rel_config_path_str)

    def config_path_text_changed(self, config_path_str):
        config_path = config.Config.DEFAULT_CONFIG_PATH

        sb_widgets.set_qt_property(
            self.ui.config_path_line_edit, "input_error", False)

        try:
            config_path = config.get_absolute_config_path(config_path_str)
        except Exception as e:
            sb_widgets.set_qt_property(
                self.ui.config_path_line_edit, "input_error", True)

            rect = self.ui.config_path_line_edit.parent().mapToGlobal(
                self.ui.config_path_line_edit.geometry().topRight())
            QtWidgets.QToolTip().showText(rect, str(e))

        self._changed_config_path = config_path

    def uproject_browse_button_clicked(self):
        file_name, _ = QtWidgets.QFileDialog.getOpenFileName(
            parent=self.ui, filter='*.uproject')
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

    def listener_exe(self):
        return self.ui.listener_exe_line_edit.text()

    def set_listener_exe(self, value):
        self.ui.listener_exe_line_edit.setText(value)

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

    def mu_server_endpoint(self):
        return self.ui.mu_server_endpoint_line_edit.text()

    def set_mu_server_endpoint(self, endpoint):
        self.ui.mu_server_endpoint_line_edit.setText(str(endpoint))

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

    def mu_server_exe(self):
        return self.ui.muserver_exe_line_edit.text()

    def set_mu_server_exe(self, value):
        self.ui.muserver_exe_line_edit.setText(value)

    def mu_server_auto_build(self) -> bool:
        return self.ui.muserver_auto_build_check_box.isChecked()

    def set_mu_server_auto_build(self, value: bool):
        self.ui.muserver_auto_build_check_box.setChecked(value)

    def mu_server_auto_endpoint(self) -> bool:
        return self.ui.muserver_auto_endpoint_check_box.isChecked()

    def set_mu_server_auto_endpoint(self, value: bool):
        self.ui.muserver_auto_endpoint_check_box.setChecked(value)

    # Devices
    def add_section_for_plugin(
            self, plugin_name, plugin_settings, device_settings):
        any_device_settings = (
            any([device[1] for device in device_settings]) or
            any([device[2] for device in device_settings]))
        if not any_device_settings:
            return  # no settings to show

        # Create a group box per plugin
        device_override_group_box = self._device_groupbox.setdefault(
            plugin_name, QtWidgets.QGroupBox())
        if device_override_group_box.parent() is None:
            device_override_group_box.setTitle(f'{plugin_name} Settings')
            device_override_group_box.setLayout(QtWidgets.QVBoxLayout())
            self.ui.device_override_layout.addWidget(device_override_group_box)

        plugin_layout = QtWidgets.QFormLayout()
        device_override_group_box.layout().addLayout(plugin_layout)

        # add widgets for settings shared by all devices of a plugin
        for setting in plugin_settings:
            setting.create_ui(form_layout=plugin_layout)

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
                setting.create_ui(form_layout=layout)

            for setting in overrides:
                setting.create_ui(
                    form_layout=layout, override_device_name=device_name)
