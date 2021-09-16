# Copyright Epic Games, Inc. All Rights Reserved.

import datetime
import logging
import os
import threading
from typing import List, Optional, Set

from PySide2 import QtCore
from PySide2 import QtGui
from PySide2 import QtUiTools
from PySide2 import QtWidgets

from switchboard import config
from switchboard import config_osc as osc
from switchboard import p4_utils
from switchboard import recording
from switchboard import resources  # noqa
from switchboard import switchboard_application
from switchboard import switchboard_utils
from switchboard import switchboard_widgets as sb_widgets
from switchboard.add_config_dialog import AddConfigDialog
from switchboard.config import CONFIG, DEFAULT_MAP_TEXT, SETTINGS
from switchboard.device_list_widget import DeviceListWidget, DeviceWidgetHeader
from switchboard.devices.device_base import DeviceStatus
from switchboard.devices.device_manager import DeviceManager
from switchboard.settings_dialog import SettingsDialog
from switchboard.switchboard_logging import ConsoleStream, LOGGER

ENGINE_PATH = "../../../../.."
RELATIVE_PATH = os.path.dirname(__file__)
EMPTY_SYNC_ENTRY = "-- None --"


class SwitchboardDialog(QtCore.QObject):
    STYLESHEET_PATH = os.path.join(RELATIVE_PATH, 'ui/switchboard.qss')

    _stylesheet_watcher: Optional[QtCore.QFileSystemWatcher] = None

    @classmethod
    def init_stylesheet_watcher(cls):
        ''' Load Qt stylesheet, and reload it whenever the file is changed. '''
        if not cls._stylesheet_watcher:
            cls._stylesheet_watcher = QtCore.QFileSystemWatcher()
            cls._stylesheet_watcher.addPath(cls.STYLESHEET_PATH)
            cls._stylesheet_watcher.fileChanged.connect(
                lambda: cls.reload_stylesheet())

            cls.reload_stylesheet()

    @classmethod
    def reload_stylesheet(cls):
        with open(cls.STYLESHEET_PATH, "r") as styling:
            stylesheet = styling.read()
            QtWidgets.QApplication.instance().setStyleSheet(stylesheet)

    def __init__(self):
        super().__init__()

        font_dir = os.path.join(ENGINE_PATH, 'Content/Slate/Fonts')
        font_files = ['Roboto-Regular.ttf', 'Roboto-Bold.ttf',
                      'DroidSansMono.ttf']

        for font_file in font_files:
            font_path = os.path.join(font_dir, font_file)
            QtGui.QFontDatabase.addApplicationFont(font_path)

        self.logger_autoscroll = True
        ConsoleStream.stderr().message_written.connect(self._console_pipe)

        # Set the UI object
        loader = QtUiTools.QUiLoader()
        loader.registerCustomWidget(sb_widgets.FramelessQLineEdit)
        loader.registerCustomWidget(DeviceWidgetHeader)
        loader.registerCustomWidget(DeviceListWidget)
        loader.registerCustomWidget(sb_widgets.ControlQPushButton)

        self.window = loader.load(
            os.path.join(RELATIVE_PATH, "ui/switchboard.ui"))

        # used to shut down services cleanly on exit
        self.window.installEventFilter(self)
        self.close_event_counter = 0

        self.init_stylesheet_watcher()

        self._shoot = None
        self._sequence = None
        self._slate = None
        self._take = None
        self._project_changelist = None
        self._engine_changelist = None
        self._level = None
        self._multiuser_session_name = None
        self._is_recording = False
        self._description = 'description'

        # Recording Manager
        self.recording_manager = recording.RecordingManager(CONFIG.SWITCHBOARD_DIR)
        self.recording_manager.signal_recording_manager_saved.connect(self.recording_manager_saved)

        # DeviceManager
        self.device_manager = DeviceManager()
        self.device_manager.signal_device_added.connect(self.device_added)

        # Transport Manager
        #self.transport_queue = recording.TransportQueue(CONFIG.SWITCHBOARD_DIR)
        #self.transport_queue.signal_transport_queue_job_started.connect(self.transport_queue_job_started)
        #self.transport_queue.signal_transport_queue_job_finished.connect(self.transport_queue_job_finished)

        self.shoot = 'Default'
        self.sequence = SETTINGS.CURRENT_SEQUENCE
        self.slate = SETTINGS.CURRENT_SLATE
        self.take = SETTINGS.CURRENT_TAKE

        # Device List Widget
        self.device_list_widget: DeviceListWidget = self.window.device_list_widget
        # When new widgets are added, register the signal/slots
        self.device_list_widget.signal_register_device_widget.connect(self.register_device_widget)
        self.device_list_widget.signal_connect_all_plugin_devices_toggled.connect(self.on_all_plugin_devices_connect_toggled)
        self.device_list_widget.signal_open_all_plugin_devices_toggled.connect(self.on_all_plugin_devices_open_toggled)

        # forward device(widget) removal between ListWidget and DeviceManager
        self.device_list_widget.signal_remove_device.connect(self.device_manager.remove_device_by_hash)
        self.device_manager.signal_device_removed.connect(self.on_device_removed)

        # DeviceManager initialize with from the config
        #
        CONFIG.push_saving_allowed(False)
        try:
            self.device_manager.reset_plugins_settings(CONFIG)
            self.device_manager.add_devices(CONFIG._device_data_from_config)
        finally:
            CONFIG.pop_saving_allowed()

        # add menu for adding new devices
        self.device_add_menu = QtWidgets.QMenu()
        self.device_add_menu.aboutToShow.connect(lambda: self.show_device_add_menu())
        self.window.device_add_tool_button.setMenu(self.device_add_menu)
        self.window.device_add_tool_button.clicked.connect(lambda: self.show_device_add_menu())
        self.device_add_menu.triggered.connect(self.on_triggered_add_device)

        # Start the OSC server
        self.osc_server = switchboard_application.OscServer()
        self.osc_server.launch(SETTINGS.IP_ADDRESS, CONFIG.OSC_SERVER_PORT.get_value())

        # Register with OSC server
        self.osc_server.dispatcher_map(osc.TAKE, self.osc_take)
        self.osc_server.dispatcher_map(osc.SLATE, self.osc_slate)
        self.osc_server.dispatcher_map(osc.SLATE_DESCRIPTION, self.osc_slate_description)
        self.osc_server.dispatcher_map(osc.RECORD_START, self.osc_record_start)
        self.osc_server.dispatcher_map(osc.RECORD_STOP, self.osc_record_stop)
        self.osc_server.dispatcher_map(osc.RECORD_CANCEL, self.osc_record_cancel)
        self.osc_server.dispatcher_map(osc.RECORD_START_CONFIRM, self.osc_record_start_confirm)
        self.osc_server.dispatcher_map(osc.RECORD_STOP_CONFIRM, self.osc_record_stop_confirm)
        self.osc_server.dispatcher_map(osc.RECORD_CANCEL_CONFIRM, self.osc_record_cancel_confirm)
        self.osc_server.dispatcher_map(osc.UE4_LAUNCH_CONFIRM, self.osc_ue4_launch_confirm)
        self.osc_server.dispatcher.map(osc.OSC_ADD_SEND_TARGET_CONFIRM, self.osc_add_send_target_confirm, 1, needs_reply_address=True)
        self.osc_server.dispatcher.map(osc.ARSESSION_START_CONFIRM, self.osc_arsession_start_confrim, 1, needs_reply_address=True)
        self.osc_server.dispatcher.map(osc.ARSESSION_STOP_CONFIRM, self.osc_arsession_stop_confrim, 1, needs_reply_address=True)
        self.osc_server.dispatcher_map(osc.BATTERY, self.osc_battery)
        self.osc_server.dispatcher_map(osc.DATA, self.osc_data)

        # Connect UI to methods
        self.window.multiuser_session_lineEdit.textChanged.connect(self.set_multiuser_session_name)
        self.window.slate_line_edit.textChanged.connect(self._set_slate)
        self.window.take_spin_box.valueChanged.connect(self._set_take)
        self.window.sequence_line_edit.textChanged.connect(self._set_sequence)
        self.window.level_combo_box.currentTextChanged.connect(self._set_level)
        self.window.refresh_levels_button.clicked.connect(self.refresh_levels_incremental)
        self.window.project_cl_combo_box.currentTextChanged.connect(self._set_project_changelist)
        self.window.engine_cl_combo_box.currentTextChanged.connect(self._set_engine_changelist)
        self.window.logger_level_comboBox.currentTextChanged.connect(self.logger_level_comboBox_currentTextChanged)
        self.window.logger_autoscroll_checkbox.stateChanged.connect(self.logger_autoscroll_stateChanged)
        self.window.logger_wrap_checkbox.stateChanged.connect(self.logger_wrap_stateChanged)
        self.window.record_button.released.connect(self.record_button_released)
        self.window.sync_all_button.clicked.connect(self.sync_all_button_clicked)
        self.window.build_all_button.clicked.connect(self.build_all_button_clicked)
        self.window.sync_and_build_all_button.clicked.connect(self.sync_and_build_all_button_clicked)
        self.window.refresh_project_cl_button.clicked.connect(self.refresh_project_cl_button_clicked)
        self.window.refresh_engine_cl_button.clicked.connect(self.refresh_engine_cl_button_clicked)
        self.window.connect_all_button.clicked.connect(self.connect_all_button_clicked)
        self.window.launch_all_button.clicked.connect(self.launch_all_button_clicked)

        # Stylesheet-related: Object names used for selectors, no focus forcing
        def configure_ctrl_btn(btn: sb_widgets.ControlQPushButton, name: str):
            btn.setObjectName(name)
            btn.hover_focus = False

        configure_ctrl_btn(self.window.refresh_levels_button, 'refresh')
        configure_ctrl_btn(self.window.sync_all_button, 'sync')
        configure_ctrl_btn(self.window.build_all_button, 'build')
        configure_ctrl_btn(self.window.sync_and_build_all_button, 'sync_and_build')
        configure_ctrl_btn(self.window.refresh_project_cl_button, 'refresh')
        configure_ctrl_btn(self.window.refresh_engine_cl_button, 'refresh')
        configure_ctrl_btn(self.window.launch_all_button, 'open')
        configure_ctrl_btn(self.window.connect_all_button, 'connect')

        # TransportQueue Menu
        #self.window.transport_queue_push_button.setContextMenuPolicy(QtCore.Qt.CustomContextMenu)
        #self.transport_queue_menu = QtWidgets.QMenu(self.window.transport_queue_push_button)
        #self.transport_queue_menu.aboutToShow.connect(self.transport_queue_menu_about_to_show)
        #self.window.transport_queue_push_button.setMenu(self.transport_queue_menu)

        # Log pane
        self.window.logger_autoscroll_checkbox.setCheckState(QtCore.Qt.Checked if self.logger_autoscroll else QtCore.Qt.Unchecked)
        self.window.base_console.horizontalScrollBar().sliderPressed.connect(lambda: self.window.logger_autoscroll_checkbox.setCheckState(QtCore.Qt.Unchecked))
        self.window.base_console.verticalScrollBar().sliderPressed.connect(lambda: self.window.logger_autoscroll_checkbox.setCheckState(QtCore.Qt.Unchecked))
        # entries will be removed from the log window after the number of maximumBlockCount entries has been reached
        self.window.base_console.document().setMaximumBlockCount(1000)

        # Menu items
        self.window.menu_new_config.triggered.connect(self.menu_new_config)
        self.window.menu_delete_config.triggered.connect(self.menu_delete_config)
        self.window.update_settings.triggered.connect(self.menu_update_settings)

        # The "Load Config" menu is populated lazily.
        self.window.menu_load_config.aboutToShow.connect(
            self._on_menu_load_config_about_to_show)

        # Plugin UI
        self.device_manager.plug_into_ui(self.window.menu_bar, self.window.tabs_main)

        # If starting up with new config, open the menu to create a new one
        if not CONFIG.file_path:
            self.menu_new_config()
        else:
            self.toggle_p4_controls(CONFIG.P4_ENABLED.get_value())
            self.refresh_levels()

        self.set_config_hooks()

        self.set_multiuser_session_name(f'{SETTINGS.MUSERVER_SESSION_NAME}')

        # Run the transport queue
        #self.transport_queue_resume()

    def set_config_hooks(self):
        CONFIG.P4_PROJECT_PATH.signal_setting_changed.connect(lambda: self.p4_refresh_project_cl())
        CONFIG.P4_ENGINE_PATH.signal_setting_changed.connect(lambda: self.p4_refresh_engine_cl())
        CONFIG.BUILD_ENGINE.signal_setting_changed.connect(lambda: self.p4_refresh_engine_cl())
        CONFIG.P4_ENABLED.signal_setting_changed.connect(lambda _, enabled: self.toggle_p4_controls(enabled))
        CONFIG.MAPS_PATH.signal_setting_changed.connect(lambda: self.refresh_levels())
        CONFIG.MAPS_FILTER.signal_setting_changed.connect(lambda: self.refresh_levels())

    def show_device_add_menu(self):
        self.device_add_menu.clear()
        plugins = sorted(self.device_manager.available_device_plugins(), key=str.lower)
        for plugin in plugins:
            icons = self.device_manager.plugin_icons(plugin)
            icon = icons["enabled"] if "enabled" in icons.keys() else QtGui.QIcon()
            self.device_add_menu.addAction(icon, plugin)
        self.window.device_add_tool_button.showMenu()

    def on_triggered_add_device(self, action):
        device_type = action.text()
        dialog = self.device_manager.get_device_add_dialog(device_type)
        dialog.exec()

        if dialog.result() == QtWidgets.QDialog.Accepted:
            for device in dialog.devices_to_remove():
                # this is pretty specific to nDisplay. It will remove all existing nDisplay devices before the devices of a new nDisplay config are added.
                # this offers a simple way to update nDisplay should the config file have been changed.
                self.device_manager.remove_device(device)

            self.device_manager.add_devices({device_type : dialog.devices_to_add()})
            CONFIG.save()

    def on_device_removed(self, device_hash, device_type, device_name, update_config):
        self.device_list_widget.on_device_removed(device_hash, device_type, device_name, update_config)
        CONFIG.on_device_removed(device_hash, device_type, device_name, update_config)

    def eventFilter(self, obj, event: QtCore.QEvent):
        if obj == self.window and event.type() == QtCore.QEvent.Close:
            self.close_event_counter += 1
            if not self.should_allow_exit(self.close_event_counter):
                event.ignore()
                return True
            else:
                self.on_exit()

        return self.window.eventFilter(obj, event)

    def should_allow_exit(self, close_req_id: int) -> bool:
        return all(device.should_allow_exit(close_req_id)
                   for device in self.device_manager.devices())

    def on_exit(self):
        self.osc_server.close()
        for device in self.device_manager.devices():
            device.disconnect_listener()
        self.window.removeEventFilter(self)

    def transport_queue_menu_about_to_show(self):
        self.transport_queue_menu.clear()

        action = QtWidgets.QWidgetAction(self.transport_queue_menu)
        action.setDefaultWidget(TransportQueueHeaderActionWidget())
        self.transport_queue_menu.addAction(action)

        for job_name in self.transport_queue.transport_jobs.keys():
            action = QtWidgets.QWidgetAction(self.transport_queue_menu)
            action.setDefaultWidget(TransportQueueActionWidget(job_name))
            self.transport_queue_menu.addAction(action)

    def _on_menu_load_config_about_to_show(self):
        from functools import partial

        self.window.menu_load_config.clear()

        # We'll build up a dictionary of directory paths to the submenu for
        # that directory as we go. Config files in the root configs directory
        # will go at the top level.
        menu_hierarchy = {
            config.ROOT_CONFIGS_PATH: self.window.menu_load_config
        }

        def _get_menu_for_path(path):
            # Safe guard to make sure we don't accidentally traverse up past
            # the root configs path.
            if (path != config.ROOT_CONFIGS_PATH and
                    config.ROOT_CONFIGS_PATH not in path.parents):
                return None

            path_menu = menu_hierarchy.get(path)
            if not path_menu:
                parent_menu = _get_menu_for_path(path.parent)
                if not parent_menu:
                    return None

                path_menu = parent_menu.addMenu(path.name)
                menu_hierarchy[path] = path_menu

            return path_menu

        config_paths = config.list_config_paths()

        # Take a first pass through the config paths just creating the
        # submenus. This makes sure that all submenus appear before any configs
        # for any given menu level.
        for config_path in config_paths:
            _get_menu_for_path(config_path.parent)

        # Now the dictionary of menus should be populated, so create actions
        # for each config in the appropriate menu.
        for config_path in config_paths:
            menu = _get_menu_for_path(config_path.parent)
            if not menu:
                continue

            config_action = menu.addAction(config_path.stem,
                partial(self.set_current_config, config_path))

            if config_path == SETTINGS.CONFIG:
                config_action.setEnabled(False)

    def set_current_config(self, config_path):

        # Update to the new config
        CONFIG.init_with_file_path(config_path)

        SETTINGS.CONFIG = config_path
        SETTINGS.save()

        # Disable saving while loading
        CONFIG.push_saving_allowed(False)

        try:
            # Remove all devices
            self.device_manager.clear_device_list()
            self.device_list_widget.clear_widgets()

            # Reset plugin settings
            self.device_manager.reset_plugins_settings(CONFIG)

            # Set hooks to this dialog's UI
            self.set_config_hooks()

            # Add new devices
            self.device_manager.add_devices(CONFIG._device_data_from_config)
        finally:
            # Re-enable saving after loading.
            CONFIG.pop_saving_allowed()

        self.p4_refresh_project_cl()
        self.p4_refresh_engine_cl()
        self.refresh_levels()

    def menu_new_config(self):
        uproject_search_path = os.path.dirname(CONFIG.UPROJECT_PATH.get_value().replace('"',''))

        if not os.path.exists(uproject_search_path):
            uproject_search_path = SETTINGS.LAST_BROWSED_PATH

        dialog = AddConfigDialog(
            uproject_search_path=uproject_search_path,
            previous_engine_dir=CONFIG.ENGINE_DIR.get_value(),
            parent=self.window)
        dialog.exec()

        if dialog.result() == QtWidgets.QDialog.Accepted:

            CONFIG.init_new_config(file_path=dialog.config_path,
                uproject=dialog.uproject, engine_dir=dialog.engine_dir,
                p4_settings=dialog.p4_settings())

            # Disable saving while loading
            CONFIG.push_saving_allowed(False)
            try:
                # Remove all devices
                self.device_manager.clear_device_list()
                self.device_list_widget.clear_widgets()

                # Reset plugin settings
                self.device_manager.reset_plugins_settings(CONFIG)

                # Set hooks to this dialog's UI
                self.set_config_hooks()
            finally:
                # Re-enable saving after loading
                CONFIG.pop_saving_allowed()

            # Update the UI
            self.toggle_p4_controls(CONFIG.P4_ENABLED.get_value())
            self.refresh_levels()

    def menu_delete_config(self):
        """
        Delete the currently loaded config.

        After deleting, it will load the first config found by
        config.list_config_paths(), or it will create a new config.
        """
        # Show the confirmation dialog using a relative path to the config.
        rel_config_path = config.get_relative_config_path(SETTINGS.CONFIG)
        reply = QtWidgets.QMessageBox.question(self.window, 'Delete Config',
            f'Are you sure you would like to delete config "{rel_config_path}"?',
            QtWidgets.QMessageBox.Yes, QtWidgets.QMessageBox.No)

        if reply == QtWidgets.QMessageBox.Yes:
            # Remove the old config
            try:
                SETTINGS.CONFIG.unlink()
                self.set_current_config(None)
            except FileNotFoundError as e:
                LOGGER.error(f'menu_delete_config: {e}')

            # Grab a new config to lead once this one is deleted
            config_paths = config.list_config_paths()

            if config_paths:
                self.set_current_config(config_paths[0])
            else:
                # Create a blank config
                self.menu_new_config()

    def menu_update_settings(self):
        """
        Settings window
        """
        # TODO: VALIDATE RECORD PATH
        settings_dialog = SettingsDialog()

        settings_dialog.set_config_path(SETTINGS.CONFIG)
        settings_dialog.set_ip_address(SETTINGS.IP_ADDRESS)
        settings_dialog.set_transport_path(SETTINGS.TRANSPORT_PATH)
        settings_dialog.set_listener_exe(CONFIG.LISTENER_EXE)
        settings_dialog.set_project_name(CONFIG.PROJECT_NAME)
        settings_dialog.set_uproject(CONFIG.UPROJECT_PATH.get_value())
        settings_dialog.set_engine_dir(CONFIG.ENGINE_DIR.get_value())
        settings_dialog.set_build_engine(CONFIG.BUILD_ENGINE.get_value())
        settings_dialog.set_p4_enabled(bool(CONFIG.P4_ENABLED.get_value()))
        settings_dialog.set_source_control_workspace(CONFIG.SOURCE_CONTROL_WORKSPACE.get_value())
        settings_dialog.set_p4_project_path(CONFIG.P4_PROJECT_PATH.get_value())
        settings_dialog.set_p4_engine_path(CONFIG.P4_ENGINE_PATH.get_value())
        settings_dialog.set_map_path(CONFIG.MAPS_PATH.get_value())
        settings_dialog.set_map_filter(CONFIG.MAPS_FILTER.get_value())
        settings_dialog.set_osc_server_port(CONFIG.OSC_SERVER_PORT.get_value())
        settings_dialog.set_osc_client_port(CONFIG.OSC_CLIENT_PORT.get_value())
        settings_dialog.set_mu_server_name(CONFIG.MUSERVER_SERVER_NAME)
        settings_dialog.set_mu_server_endpoint(CONFIG.MUSERVER_ENDPOINT)
        settings_dialog.set_mu_cmd_line_args(CONFIG.MUSERVER_COMMAND_LINE_ARGUMENTS)
        settings_dialog.set_mu_clean_history(CONFIG.MUSERVER_CLEAN_HISTORY)
        settings_dialog.set_mu_auto_launch(CONFIG.MUSERVER_AUTO_LAUNCH)
        settings_dialog.set_mu_auto_join(CONFIG.MUSERVER_AUTO_JOIN)
        settings_dialog.set_mu_server_exe(CONFIG.MULTIUSER_SERVER_EXE)
        settings_dialog.set_mu_server_auto_build(CONFIG.MUSERVER_AUTO_BUILD)
        settings_dialog.set_mu_server_auto_endpoint(CONFIG.MUSERVER_AUTO_ENDPOINT)

        for plugin_name in sorted(self.device_manager.available_device_plugins(), key=str.lower):
            device_instances = self.device_manager.devices_of_type(plugin_name)
            device_settings = [(device.name, device.device_settings(), device.setting_overrides()) for device in device_instances]
            settings_dialog.add_section_for_plugin(plugin_name, self.device_manager.plugin_settings(plugin_name), device_settings)

        # avoid saving the config all the time while in the settings dialog
        CONFIG.push_saving_allowed(False)
        try:
            # Show the Settings Dialog
            settings_dialog.ui.exec()
        finally:
            # Restore saving, which should happen at the end of this function
            CONFIG.pop_saving_allowed()

        new_config_path = settings_dialog.config_path()
        if new_config_path != SETTINGS.CONFIG and new_config_path is not None:
            CONFIG.replace(new_config_path)

            SETTINGS.CONFIG = new_config_path
            SETTINGS.save()

        ip_address = settings_dialog.ip_address()
        if ip_address != SETTINGS.IP_ADDRESS:
            SETTINGS.IP_ADDRESS = ip_address
            SETTINGS.save()

            # Relaunch the OSC server
            self.osc_server.close()
            self.osc_server.launch(SETTINGS.IP_ADDRESS, CONFIG.OSC_SERVER_PORT.get_value())

        transport_path = settings_dialog.transport_path()
        if transport_path != SETTINGS.TRANSPORT_PATH:
            SETTINGS.TRANSPORT_PATH = transport_path
            SETTINGS.save()

        # todo-dara, when these project settings have been converted into actual Settings
        # these assignments are not needed anymore, as the settings would be directly connected to their widgets

        project_name = settings_dialog.project_name()
        if project_name != CONFIG.PROJECT_NAME:
            CONFIG.PROJECT_NAME = project_name

        listener_exe = settings_dialog.listener_exe()
        if listener_exe != CONFIG.LISTENER_EXE:
            CONFIG.LISTENER_EXE = listener_exe

        # Multi User Settings
        mu_server_name = settings_dialog.mu_server_name()
        if mu_server_name != CONFIG.MUSERVER_SERVER_NAME:
            CONFIG.MUSERVER_SERVER_NAME = mu_server_name

        mu_server_endpoint = settings_dialog.mu_server_endpoint()
        if mu_server_endpoint != CONFIG.MUSERVER_ENDPOINT:
            CONFIG.MUSERVER_ENDPOINT = mu_server_endpoint

        mu_cmd_line_args = settings_dialog.mu_cmd_line_args()
        if mu_cmd_line_args != CONFIG.MUSERVER_COMMAND_LINE_ARGUMENTS:
            CONFIG.MUSERVER_COMMAND_LINE_ARGUMENTS = mu_cmd_line_args

        mu_clean_history = settings_dialog.mu_clean_history()
        if mu_clean_history != CONFIG.MUSERVER_CLEAN_HISTORY:
            CONFIG.MUSERVER_CLEAN_HISTORY = mu_clean_history

        mu_auto_launch = settings_dialog.mu_auto_launch()
        if mu_auto_launch != CONFIG.MUSERVER_AUTO_LAUNCH:
            CONFIG.MUSERVER_AUTO_LAUNCH = mu_auto_launch

        mu_auto_join = settings_dialog.mu_auto_join()
        if mu_auto_join != CONFIG.MUSERVER_AUTO_JOIN:
            CONFIG.MUSERVER_AUTO_JOIN = mu_auto_join

        mu_server_exe = settings_dialog.mu_server_exe()
        if mu_server_exe != CONFIG.MULTIUSER_SERVER_EXE:
            CONFIG.MULTIUSER_SERVER_EXE = mu_server_exe

        mu_auto_build = settings_dialog.mu_server_auto_build()
        if mu_auto_build != CONFIG.MUSERVER_AUTO_BUILD:
            CONFIG.MUSERVER_AUTO_BUILD = mu_auto_build

        mu_auto_endpoint = settings_dialog.mu_server_auto_endpoint()
        if mu_auto_endpoint != CONFIG.MUSERVER_AUTO_ENDPOINT:
            CONFIG.MUSERVER_AUTO_ENDPOINT = mu_auto_endpoint

        CONFIG.P4_ENABLED.update_value(settings_dialog.p4_enabled())

        CONFIG.save()

    def sync_all_button_clicked(self):
        if not CONFIG.P4_ENABLED.get_value():
            return
        device_widgets = self.device_list_widget.device_widgets()

        for device_widget in device_widgets:
            if device_widget.can_sync():
                device_widget.sync_button_clicked()

    def build_all_button_clicked(self):
        device_widgets = self.device_list_widget.device_widgets()

        for device_widget in device_widgets:
            if device_widget.can_build():
                device_widget.build_button_clicked()

    def sync_and_build_all_button_clicked(self):
        if not CONFIG.P4_ENABLED.get_value():
            return
        device_widgets = self.device_list_widget.device_widgets()

        for device_widget in device_widgets:
            if device_widget.can_sync() and device_widget.can_build():
                device_widget.sync_button_clicked()
                device_widget.build_button_clicked()

    def refresh_project_cl_button_clicked(self):
        self.p4_refresh_project_cl()

    def refresh_engine_cl_button_clicked(self):
        self.p4_refresh_engine_cl()

    def connect_all_button_clicked(self, button_state):
        devices = self.device_manager.devices()
        self.set_device_connection_state(devices, button_state)

    def launch_all_button_clicked(self, button_state):
        devices = self.device_manager.devices()
        self.set_device_launch_state(devices, button_state)

    def set_device_launch_state(self, devices, launch_state):
        for device in devices:
            try:
                open_button = device.widget.open_button
                if open_button.isEnabled():
                    if launch_state:
                        device.widget._open()
                    elif open_button.isChecked():
                        device.widget._close()
            except Exception:
                pass

    def set_device_connection_state(self, devices, connection_state):
        for device in devices:
            try:
                if connection_state:
                    device.widget._connect()
                else:
                    device.widget._disconnect()
            except Exception:
                pass

    @QtCore.Slot(object)
    def recording_manager_saved(self, recording):
        """
        When the RecordingManager saves a recording
        """
        pass

    # START HERE
    # TODO
    # If JOB IS ADDED, RESUME
    # If DEVICE CONNECTS, RESUME
    def transport_queue_resume(self):
        # Do not allow transport while recording
        if self._is_recording:
            return

        # Do not transport if the active queue is full
        if self.transport_queue.active_queue_full():
            return

        for _, transport_job in self.transport_queue.transport_jobs.items():
            # Do not transport if the device is disconnected
            device = self.device_manager.device_with_name(transport_job.device_name)
            if device.status < DeviceStatus.OPEN:
                continue

            # Only Transport jobs that are ready
            if transport_job.transport_status != recording.TransportStatus.READY_FOR_TRANSPORT:
                continue

            # Transport the file
            self.transport_queue.run_transport_job(transport_job, device)

            # Bail if active queue is full
            if self.transport_queue.active_queue_full():
                break

    @QtCore.Slot(object)
    def transport_queue_job_finished(self, transport_job):
        """
        When the TransportQueue finished a job
        """
        LOGGER.debug(f'transport_queue_job_finished {transport_job.job_name}')
        '''
        # If the device is connected, set that status as READY_FOR_TRANSPORT
        transport_job.transport_status = recording.TransportStatus.READY_FOR_TRANSPORT
        #transport_queue_job_added
        '''

    @QtCore.Slot(object)
    def transport_queue_job_started(self, transport_job):
        """
        When the TransportQueue is ready to transport a new job
        Grab the device and send it back to the transport queue
        """
        LOGGER.debug('transport_queue_job_started')

    @QtCore.Slot(object)
    def device_added(self, device):
        """
        When a new device is added to the DeviceManger, connect its signals
        """
        device.device_qt_handler.signal_device_connect_failed.connect(self.device_connect_failed, QtCore.Qt.QueuedConnection)
        device.device_qt_handler.signal_device_client_disconnected.connect(self.device_client_disconnected, QtCore.Qt.QueuedConnection)
        device.device_qt_handler.signal_device_project_changelist_changed.connect(self.device_project_changelist_changed, QtCore.Qt.QueuedConnection)
        device.device_qt_handler.signal_device_engine_changelist_changed.connect(self.device_engine_changelist_changed, QtCore.Qt.QueuedConnection)
        device.device_qt_handler.signal_device_status_changed.connect(self.device_status_changed, QtCore.Qt.QueuedConnection)
        device.device_qt_handler.signal_device_sync_failed.connect(self.device_sync_failed, QtCore.Qt.QueuedConnection)
        device.device_qt_handler.signal_device_is_recording_device_changed.connect(self.device_is_recording_device_changed, QtCore.Qt.QueuedConnection)
        device.device_qt_handler.signal_device_build_update.connect(self.device_build_update, QtCore.Qt.QueuedConnection)
        device.device_qt_handler.signal_device_sync_update.connect(self.device_sync_update, QtCore.Qt.QueuedConnection)

        # Add the view
        self.device_list_widget.add_device_widget(device)

    @QtCore.Slot(object)
    def register_device_widget(self, device_widget):
        """
        When a new DeviceWidget is added, connect all the signals
        """
        device_widget.signal_device_widget_connect.connect(self.device_widget_connect)
        device_widget.signal_device_widget_disconnect.connect(self.device_widget_disconnect)
        device_widget.signal_device_widget_open.connect(self.device_widget_open)
        device_widget.signal_device_widget_close.connect(self.device_widget_close)
        device_widget.signal_device_widget_sync.connect(self.device_widget_sync)
        device_widget.signal_device_widget_build.connect(self.device_widget_build)
        device_widget.signal_device_widget_trigger_start_toggled.connect(self.device_widget_trigger_start_toggled)
        device_widget.signal_device_widget_trigger_stop_toggled.connect(self.device_widget_trigger_stop_toggled)

        # KiPro Signal Support
        try:
            device_widget.signal_device_widget_play.connect(self.device_widget_play)
            device_widget.signal_device_widget_stop.connect(self.device_widget_stop)
        except:
            pass

        try:
            device = self.device_manager.device_with_hash(device_widget.device_hash)
            device.device_widget_registered(device_widget)
        except:
            LOGGER.error(f'Could not find device with hash {device_widget.device_hash} when registering its widget')

    def on_all_plugin_devices_connect_toggled(self, plugin_name, button_state):
        devices = self.device_manager.devices_of_type(plugin_name)
        self.set_device_connection_state(devices, button_state)

    def on_all_plugin_devices_open_toggled(self, plugin_name, button_state):
        devices = self.device_manager.devices_of_type(plugin_name)
        self.set_device_launch_state(devices, button_state)

    def multiuser_session_name(self):
        return self._multiuser_session_name

    def set_multiuser_session_name(self, value):
        self._multiuser_session_name = value

        if self.window.multiuser_session_lineEdit.text() != value:
            self.window.multiuser_session_lineEdit.setText(value)
        
        if value !=SETTINGS.MUSERVER_SESSION_NAME:
            SETTINGS.MUSERVER_SESSION_NAME = value
            SETTINGS.save()

    @property
    def shoot(self):
        return self._shoot

    @shoot.setter
    def shoot(self, value):
        self._shoot = value

    @property
    def sequence(self):
        return self._sequence

    @sequence.setter
    def sequence(self, value):
        self._set_sequence(value)

    def _set_sequence(self, value):
        self._sequence = value
        self.window.sequence_line_edit.setText(value)

        # Reset the take number to 1 if setting the sequence
        self.take = 1

    @property
    def slate(self):
        return self._slate

    @slate.setter
    def slate(self, value):
        self._set_slate(value, reset_take=False)

    def _set_slate(self, value, exclude_ip_addresses=[], reset_take=True):
        """
        Internal setter that allows exclusion of ip addresses
        """
        # Protect against blank slates
        if value == '':
            return

        if self._slate == value:
            return

        self._slate = value
        SETTINGS.CURRENT_SLATE = value
        SETTINGS.save()

        # Reset the take number to 1 if setting the slate
        if reset_take:
            self.take = 1

        # UI out of date with control means external message
        if self.window.slate_line_edit.text() != self._slate:
            self.window.slate_line_edit.setText(self._slate)

        thread = threading.Thread(target=self._set_slate_all_devices, args=[value], kwargs={'exclude_ip_addresses':exclude_ip_addresses})
        thread.start()

    def _set_slate_all_devices(self, value, exclude_ip_addresses=[]):
        """
        Tell all devices the new slate
        """
        for device in self.device_manager.devices():
            if device.ip_address in exclude_ip_addresses:
                continue

            # Do not send updates to disconnected devices
            if device.is_disconnected:
                continue

            device.set_slate(self._slate)

    @property
    def take(self):
        return self._take

    @take.setter
    def take(self, value):
        self._set_take(value)

    def _set_take(self, value, exclude_ip_addresses=[]):
        """
        Internal setter that allows exclusion of ip addresses
        """
        requested_take = value

        # TODO: Add feedback in UI
        # Check is that slate/take combo has been used before
        while not self.recording_manager.slate_take_available(self._sequence, self._slate, requested_take):
            requested_take += 1
        
        if requested_take == value == self._take:
            return

        if requested_take != value:
            LOGGER.warning(f'Slate: "{self._slate}" Take: "{value}" have already been used. Auto incremented up to take: "{requested_take}"')
            # Clear the exclude list since Switchboard changed the incoming value
            exclude_ip_addresses = []
        

        self._take = requested_take
        SETTINGS.CURRENT_TAKE = value
        SETTINGS.save()

        if self.window.take_spin_box.value() != self._take:
            self.window.take_spin_box.setValue(self._take)

        thread = threading.Thread(target=self._set_take_all_devices, args=[value], kwargs={'exclude_ip_addresses':exclude_ip_addresses})
        thread.start()

    def _set_take_all_devices(self, value, exclude_ip_addresses=[]):
        """
        Tell all devices the new take
        """
        # Tell all devices the new take
        for device in self.device_manager.devices():
            # Do not send updates to disconnected devices
            if device.is_disconnected:
                continue

            if device.ip_address in exclude_ip_addresses:
                continue

            device.set_take(self._take)

    @property
    def description(self):
        return self._description

    @description.setter
    def description(self, value):
        self._description = f'{self.level} {self.slate} {self.take}\nvalue'

    @property
    def level(self):
        return self._level

    @level.setter
    def level(self, value):
        self._set_level(value)

    def _set_level(self, value):
        ''' Called when level dropdown text changes
        '''
        self._level = value

        if CONFIG.CURRENT_LEVEL != value:
            CONFIG.CURRENT_LEVEL = value
            CONFIG.save()

        if self.window.level_combo_box.currentText() != self._level:
            self.window.level_combo_box.setCurrentText(self._level)

    @property
    def project_changelist(self):
        return self._project_changelist

    @project_changelist.setter
    def project_changelist(self, value):
        self._set_project_changelist(value)

    def _set_project_changelist(self, value):
        self._project_changelist = value

        if self.window.project_cl_combo_box.currentText() != self._project_changelist:
            self.window.project_cl_combo_box.setText(self._project_changelist)

        # Check if all of the devices are on the right changelist
        for device in self.device_manager.devices():
            if not device.project_changelist:
                continue

            device_widget = self.device_list_widget.device_widget_by_hash(device.device_hash)
            if value == EMPTY_SYNC_ENTRY:
                device_widget.project_changelist_display_warning(False)
            else:
                if device.project_changelist == self.project_changelist:
                    device_widget.project_changelist_display_warning(False)
                else:
                    device_widget.project_changelist_display_warning(True)

    @property
    def engine_changelist(self):
        return self._engine_changelist

    @engine_changelist.setter
    def engine_changelist(self, value):
        self._set_engine_changelist(value)

    def _set_engine_changelist(self, value):
        self._engine_changelist = value

        if self.window.engine_cl_combo_box.currentText() != self._engine_changelist:
            self.window.engine_cl_combo_box.setText(self._engine_changelist)

        # Check if all of the devices are on the right changelist
        for device in self.device_manager.devices():
            if not device.engine_changelist:
                continue

            device_widget = self.device_list_widget.device_widget_by_hash(device.device_hash)
            if value == EMPTY_SYNC_ENTRY:
                device_widget.engine_changelist_display_warning(False)
            else:
                if device.engine_changelist == self.engine_changelist:
                    device_widget.engine_changelist_display_warning(False)
                else:
                    device_widget.engine_changelist_display_warning(True)

    @QtCore.Slot(object)
    def device_widget_connect(self, device_widget):
        device = self.device_manager.device_with_hash(device_widget.device_hash)
        if not device:
            return

        if device.device_type == 'LiveLinkFace':
            device.look_for_device = True
        else:
            device.connect_listener()

    @QtCore.Slot(object)
    def device_connect_failed(self, device):
        device_widget = self.device_list_widget.device_widget_by_hash(device.device_hash)

        if not device_widget:
            return

        device_widget._disconnect()

        LOGGER.warning(f'{device.name}: Could not connect to device')

    @QtCore.Slot(object)
    def device_widget_disconnect(self, device_widget):
        device = self.device_manager.device_with_hash(device_widget.device_hash)
        if not device:
            return

        if device.device_type == 'LiveLinkFace':
            device.look_for_device = False
        else:
            device.disconnect_listener()

    @QtCore.Slot(object)
    def device_client_disconnected(self, device):
        device_widget = self.device_list_widget.device_widget_by_hash(device.device_hash)

        device_widget._disconnect()
        LOGGER.warning(f'{device.name}: Client disconnected')

    @QtCore.Slot(object)
    def device_widget_open(self, device_widget):
        device = self.device_manager.device_with_hash(device_widget.device_hash)

        device.launch(self.level)

    @QtCore.Slot(object)
    def device_widget_close(self, device_widget):
        device = self.device_manager.device_with_hash(device_widget.device_hash)
        device.close(force=True)

    @QtCore.Slot(object)
    def device_widget_sync(self, device_widget):
        if not CONFIG.P4_ENABLED.get_value():
            return
        device = self.device_manager.device_with_hash(device_widget.device_hash)
        project_cl = None if self.project_changelist == EMPTY_SYNC_ENTRY else self.project_changelist
        engine_cl = None if self.engine_changelist == EMPTY_SYNC_ENTRY else self.engine_changelist
        device.sync(engine_cl, project_cl)

    @QtCore.Slot(object)
    def device_widget_build(self, device_widget):
        device = self.device_manager.device_with_hash(device_widget.device_hash)
        device.build()

    @QtCore.Slot(object)
    def device_widget_play(self, device_widget):
        device = self.device_manager.device_with_hash(device_widget.device_hash)
        device.play()

    @QtCore.Slot(object)
    def device_widget_stop(self, device_widget):
        device = self.device_manager.device_with_hash(device_widget.device_hash)
        device.stop()

    @QtCore.Slot(object)
    def device_sync_failed(self, device):
        #LOGGER.debug(f'{device.name} device_sync_failed')
        # CHANGE THE SYNC ICON HERE
        pass

    @QtCore.Slot(object)
    def device_build_update(self, device, step, percent):
        device_widget = self.device_list_widget.device_widget_by_hash(device.device_hash)
        device_widget.update_build_status(device, step, percent)

    @QtCore.Slot(object)
    def device_sync_update(self, device, progress):
        device_widget = self.device_list_widget.device_widget_by_hash(device.device_hash)
        device_widget.update_sync_status(device, progress)

    @QtCore.Slot(object)
    def device_project_changelist_changed(self, device):
        device_widget = self.device_list_widget.device_widget_by_hash(device.device_hash)
        device_widget.update_project_changelist(device.project_changelist)

        if self.project_changelist == EMPTY_SYNC_ENTRY:
            device_widget.project_changelist_display_warning(False)
        else:
            if device.project_changelist == self.project_changelist:
                device_widget.project_changelist_display_warning(False)
            else:
                device_widget.project_changelist_display_warning(True)

    @QtCore.Slot(object)
    def device_engine_changelist_changed(self, device):
        device_widget = self.device_list_widget.device_widget_by_hash(device.device_hash)
        device_widget.update_engine_changelist(device.engine_changelist)

        if self.engine_changelist == EMPTY_SYNC_ENTRY:
            device_widget.engine_changelist_display_warning(False)
        else:
            if device.engine_changelist == self.engine_changelist:
                device_widget.engine_changelist_display_warning(False)
            else:
                device_widget.engine_changelist_display_warning(True)

    @QtCore.Slot(object)
    def device_status_changed(self, device, previous_status):
        # Update the device widget
        device.widget.update_status(device.status, previous_status)

        devices = self.device_manager.devices_of_type(device.device_type)
        self.device_list_widget.update_category_status(device.category_name, devices)
        self.update_connect_and_open_button_states()

        if previous_status != device.status:
            LOGGER.debug(f'{device.name}: device status change: {device.status.name}')

        if previous_status == DeviceStatus.RECORDING and device.status >= DeviceStatus.OPEN:
            self.device_record_stop_confirm(device)
        elif previous_status == DeviceStatus.READY and device.status >= DeviceStatus.RECORDING:
            self.device_record_start_confirm(device)

        # Send Slate/Take to the device when it connects
        if previous_status <= DeviceStatus.OPEN and device.status >= DeviceStatus.READY:
            device.set_take(self.take)
            device.set_slate(self.slate)

    def update_connect_and_open_button_states(self):
        """ Refresh states of connect-all and start-all buttons. """
        connect_checked, connect_enabled, open_checked, open_enabled = \
            self.device_list_widget.get_connect_and_open_all_button_states()

        self.update_connect_all_button_state(checked=connect_checked,
                                            enabled=connect_enabled)
        self.update_start_all_button_state(checked=open_checked,
                                        enabled=open_enabled)

    def update_connect_all_button_state(self, checked, enabled):
        """ Refresh state of connect-all button. """
        self.window.connect_all_button.setChecked(checked)
        self.window.connect_all_button.setEnabled(enabled)
        if checked:
            self.window.connect_all_button.setToolTip("Disconnect all connected devices")
        else:
            self.window.connect_all_button.setToolTip("Connect all devices")

    def update_start_all_button_state(self, checked, enabled):
        """ Refresh state of start-all button. """
        self.window.launch_all_button.setChecked(checked)
        self.window.launch_all_button.setEnabled(enabled)
        if checked:
            self.window.launch_all_button.setToolTip("Stop all running devices")
        else:
            self.window.launch_all_button.setToolTip("Start all connected devices")

    @QtCore.Slot(object)
    def device_is_recording_device_changed(self, device, is_recording_device):
        """
        When the is_recording_device bool changes, fresh the device status to force the repositioning
        of the device in the UI
        """
        self.device_status_changed(device, DeviceStatus.OPEN)

    def device_record_start_confirm(self, device):
        """
        Callback when the device has started recording
        """
        LOGGER.info(f'{device.name}: Recording started') # {timecode}

    def device_record_stop_confirm(self, device):
        """
        Callback when the device has stopped recording
        """
        LOGGER.info(f'{device.name}: Recording stopped')

        latest_recording = self.recording_manager.latest_recording
        device_recording = device.get_device_recording()

        # Add the device to the latest recording
        self.recording_manager.add_device_to_recording(device_recording, latest_recording)
        '''
        # TransportJob
        # If the device produces transport paths, create a transport job
        paths = device.transport_paths(device_recording)
        if not paths:
            return

        # If the status is not on device, do not create jobs
        if device_recording.status != recording.RecordingStatus.ON_DEVICE:
            return

        device_name = device_recording.device_name
        slate = latest_recording.slate
        take = latest_recording.take
        date = latest_recording.date
        job_name = self.transport_queue.job_name(slate, take, device_name)

        # Create a transport job
        transport_job = recording.TransportJob(job_name, device_name, slate, take, date, paths)
        self.transport_queue.add_transport_job(transport_job)
        '''

    @QtCore.Slot(object)
    def device_widget_trigger_start_toggled(self, device_widget, value):
        device = self.device_manager.device_with_hash(device_widget.device_hash)
        device.trigger_start = value

    @QtCore.Slot(object)
    def device_widget_trigger_stop_toggled(self, device_widget, value):
        device = self.device_manager.device_with_hash(device_widget.device_hash)
        device.trigger_stop = value

    def _console_pipe(self, msg):
        """
        Pipes the emiting message from the QtHandler to the base_console widget.
        Scrolls on each emit signal.
        :param msg: This is a built in event, QT Related, not given.
        """
        if self.window:
            self.window.base_console.appendHtml(msg)

        if self.logger_autoscroll:
            self.logger_scroll_to_end()

    def logger_scroll_to_end(self):
        # This combination keeps the cursor at the bottom left corner in all cases.
        if self.window:
            self.window.base_console.moveCursor(QtGui.QTextCursor.End)
            self.window.base_console.moveCursor(QtGui.QTextCursor.StartOfLine)

    # Allow user to change logging level
    def logger_level_comboBox_currentTextChanged(self):
        value = self.window.logger_level_comboBox.currentText()

        if value == 'Message':
            LOGGER.setLevel(logging.MESSAGE_LEVEL_NUM)
        elif value == 'OSC':
            LOGGER.setLevel(logging.OSC_LEVEL_NUM)
        elif value == 'Debug':
            LOGGER.setLevel(logging.DEBUG)
        else:
            LOGGER.setLevel(logging.INFO)

    def logger_autoscroll_stateChanged(self, value):
        if value == QtCore.Qt.Checked:
            self.logger_autoscroll = True
            self.logger_scroll_to_end()
        else:
            self.logger_autoscroll = False

    def logger_wrap_stateChanged(self, value):
        if value == QtCore.Qt.Checked:
            self.window.base_console.setLineWrapMode(QtWidgets.QPlainTextEdit.LineWrapMode.WidgetWidth)
        else:
            self.window.base_console.setLineWrapMode(QtWidgets.QPlainTextEdit.LineWrapMode.NoWrap)

        if self.logger_autoscroll:
            self.logger_scroll_to_end()

    # Update UI with latest CLs
    def p4_refresh_project_cl(self):
        if not CONFIG.P4_ENABLED.get_value():
            return
        LOGGER.info("Refreshing p4 project changelists")
        working_dir = os.path.dirname(CONFIG.UPROJECT_PATH.get_value())
        changelists = p4_utils.p4_latest_changelist(CONFIG.P4_PROJECT_PATH.get_value(), working_dir)
        self.window.project_cl_combo_box.clear()

        if changelists:
            self.window.project_cl_combo_box.addItems(changelists)
            self.window.project_cl_combo_box.setCurrentIndex(0)
        self.window.project_cl_combo_box.addItem(EMPTY_SYNC_ENTRY)

    def p4_refresh_engine_cl(self):
        if not CONFIG.P4_ENABLED.get_value():
            return
        self.window.engine_cl_combo_box.clear()
        # if engine is built from source, refresh the engine cl dropdown
        if CONFIG.BUILD_ENGINE.get_value():
            LOGGER.info("Refreshing p4 engine changelists")
            self.window.engine_cl_label.setEnabled(True)
            self.window.engine_cl_combo_box.setEnabled(True)
            self.window.engine_cl_combo_box.setToolTip("Select changelist to sync the engine to")
            self.window.refresh_engine_cl_button.setEnabled(True)
            self.window.refresh_engine_cl_button.setToolTip("Click to refresh changelists")

            engine_p4_path = CONFIG.P4_ENGINE_PATH.get_value()
            if engine_p4_path:
                working_dir = os.path.dirname(CONFIG.UPROJECT_PATH.get_value())
                changelists = p4_utils.p4_latest_changelist(engine_p4_path, working_dir)
                if changelists:
                    self.window.engine_cl_combo_box.addItems(changelists)
                    self.window.engine_cl_combo_box.setCurrentIndex(0)
            else:
                LOGGER.warning('"Build Engine" is enabled in the settings but the engine does not seem to be under perforce control.')
                LOGGER.warning("Please check your perforce settings.")
        else:
            # disable engine cl controls if engine is not built from source
            self.window.engine_cl_label.setEnabled(False)
            self.window.engine_cl_combo_box.setEnabled(False)
            tool_tip = "Engine is not build from source. To use this make sure the Engine is on p4 and 'Build Engine' "
            tool_tip += "is enabled in the Settings."
            self.window.engine_cl_combo_box.setToolTip(tool_tip)
            self.window.refresh_engine_cl_button.setEnabled(False)
            self.window.refresh_engine_cl_button.setToolTip(tool_tip)
        self.window.engine_cl_combo_box.addItem(EMPTY_SYNC_ENTRY)

    def refresh_levels(self, levels: Optional[List[str]] = None):
        if levels is None:
            levels = CONFIG.maps()

        current_level = CONFIG.CURRENT_LEVEL

        self.window.level_combo_box.clear()
        self.window.level_combo_box.addItems([DEFAULT_MAP_TEXT] + levels)

        if current_level and current_level in levels:
            self.level = current_level

    def get_current_level_list(self):
        level_combo = self.window.level_combo_box
        return [level_combo.itemText(i) for i in range(1, level_combo.count())] # skip DEFAULT_MAP_TEXT

    def refresh_levels_incremental(self):
        ''' Wrapper around `refresh_levels` with the following differences:
            - Only resaves config if the selected level was removed (as opposed
              to always generating 2-3 change events/config saves).
            - Logs messages indicating which levels were added/removed.
        '''

        current_level: str = CONFIG.CURRENT_LEVEL
        prev_levels_list: List[str] = self.get_current_level_list()
        updated_levels_list: List[str] = CONFIG.maps()

        prev_levels: Set[str] = set(prev_levels_list)
        updated_levels: Set[str] = set(updated_levels_list)
        levels_added = updated_levels - prev_levels
        levels_removed = prev_levels - updated_levels

        if not (levels_added or levels_removed):
            return

        if levels_added:
            LOGGER.info(f'Levels added: {", ".join(levels_added)}')

        if levels_removed:
            LOGGER.info(f'Levels removed: {", ".join(levels_removed)}')

        if current_level in levels_removed:
            LOGGER.warning(f'Selected level "{current_level}" was removed; reverting to default')
            self.level = DEFAULT_MAP_TEXT

        CONFIG.push_saving_allowed(False)
        self.refresh_levels(updated_levels_list)
        CONFIG.pop_saving_allowed()


    def toggle_p4_controls(self, enabled):
        self.window.engine_cl_label.setEnabled(enabled)
        self.window.engine_cl_combo_box.setEnabled(enabled)
        self.window.refresh_engine_cl_button.setEnabled(enabled)

        self.window.project_cl_label.setEnabled(enabled)
        self.window.project_cl_combo_box.setEnabled(enabled)
        self.window.refresh_project_cl_button.setEnabled(enabled)

        self.window.sync_all_button.setEnabled(enabled)
        self.window.sync_and_build_all_button.setEnabled(enabled)
        if enabled:
            self.p4_refresh_engine_cl()
            self.p4_refresh_project_cl()

    def osc_take(self, ip_address, command, value):
        device = self._device_from_ip_address(ip_address, command, value=value)

        if not device:
            return

        self._set_take(value, exclude_ip_addresses=[device.ip_address])

    # OSC: Set Slate
    def osc_slate(self, ip_address, command, value):
        device = self._device_from_ip_address(ip_address, command, value=value)
        if not device:
            return

        self._set_slate(value, exclude_ip_addresses=[device.ip_address], reset_take=False)

    # OSC: Set Description UPDATE THIS TO MAKE IT WORK
    def osc_slate_description(self, ip_address, command, value):
        self.description = value

    def record_button_released(self):
        """
        User press record button
        """
        if self._is_recording:
            LOGGER.debug('Record stop button pressed')
            self._record_stop(1)
        else:
            LOGGER.debug('Record start button pressed')
            self._record_start(self.slate, self.take, self.description)

    def _device_from_ip_address(self, ip_address, command, value=''):
        device = self.device_manager.device_with_ip_address(ip_address[0])

        if not device:
            LOGGER.warning(f'{ip_address} is not registered with a device in Switchboard')
            return None

        # Do not receive osc from disconnected devices
        if device.is_disconnected:
            LOGGER.warning(f'{device.name}: is sending OSC commands but is not connected. Ignoring "{command} {value}"')
            return None

        LOGGER.osc(f'OSC Server: Received "{command} {value}" from {device.name} ({device.ip_address})')
        return device

    # OSC: Start a recording
    def osc_record_start(self, ip_address, command, slate, take, description):
        '''
        OSC message Recieved /RecordStart
        '''
        device = self._device_from_ip_address(ip_address, command, value=[slate, take, description])
        if not device:
            return

        # There is a bug that causes a slate of None. If this occurs, use the stored slate in control
        # Try to track down this bug in sequencer
        if not slate or slate == 'None':
            LOGGER.critical(f'Slate is None, using {self.slate}')
        else:
            self.slate = slate

        self.take = take
        self.description = description

        self._record_start(self.slate, self.take, self.description, exclude_ip_address=device.ip_address)

    def _record_start(self, slate, take, description, exclude_ip_address=None):
        LOGGER.success(f'Record Start: "{self.slate}" {self.take}')

        # Update the UI button
        pixmap = QtGui.QPixmap(":/icons/images/record_start.png")
        self.window.record_button.setIcon(QtGui.QIcon(pixmap))

        # Pause the TransportQueue
        #self.transport_queue.pause()

        # Start a new recording
        self._is_recording = True

        # TODO: Lock SLATE/TAKE/SESSION/CL

        # Return a Recording object
        new_recording = recording.Recording()
        new_recording.project = CONFIG.PROJECT_NAME
        new_recording.shoot = self.shoot
        new_recording.sequence = self.sequence
        new_recording.slate = self.slate
        new_recording.take = self.take
        new_recording.description = self.description
        new_recording.date = switchboard_utils.date_to_string(datetime.date.today())
        new_recording.map = self.level
        new_recording.multiuser_session = self.multiuser_session_name()
        new_recording.changelist = self.project_changelist

        self.recording_manager.add_recording(new_recording)

        # Sends the message to all recording devices
        devices = self.device_manager.devices()
        for device in devices:
            # Do not send a start record message to whichever device sent it
            if exclude_ip_address and exclude_ip_address == device.ip_address:
                continue

            # Do not send updates to disconnected devices
            if device.is_disconnected:
                continue

            LOGGER.debug(f'Record Start {device.name}')
            device.record_start(slate, take, description)

    def _record_stop(self, exclude_ip_address=None):
        LOGGER.success(f'Record Stop: "{self.slate}" {self.take}')

        pixmap = QtGui.QPixmap(":/icons/images/record_stop.png")
        self.window.record_button.setIcon(QtGui.QIcon(pixmap))

        # Resume the transport queue
        #self.transport_queue.resume()

        # End the recording
        self._is_recording = False

        # Pull the latest recording down
        new_recording = self.recording_manager.latest_recording
        # Start the auto_save timer
        self.recording_manager.auto_save(new_recording)

        # TODO: If no UE4 auto incriment 

        # TODO: Unlock SLATE/TAKE/SESSION/CL

        devices = self.device_manager.devices()

        # Sends the message to all recording devices
        for device in devices:
            # Do not send a start record message to whichever device sent it
            if exclude_ip_address and exclude_ip_address == device.ip_address:
                continue

            # Do not send updates to disconnected devices
            if device.is_disconnected:
                continue

            device.record_stop()

    def _record_cancel(self, exclude_ip_address=None):
        self._record_stop(exclude_ip_address=exclude_ip_address)

        # Incriment Take
        #new_recording = self.recording_manager.latest_recording
        #self.take = new_recording.take + 1

    def osc_record_start_confirm(self, ip_address, command, timecode):
        device = self._device_from_ip_address(ip_address, command, value=timecode)
        if not device:
            return

        device.record_start_confirm(timecode)

    def osc_record_stop(self, ip_address, command):
        device = self._device_from_ip_address(ip_address, command)
        if not device:
            return

        self._record_stop(exclude_ip_address=device.ip_address)

    def osc_record_stop_confirm(self, ip_address, command, timecode, *paths):
        device = self._device_from_ip_address(ip_address, command, value=timecode)
        if not device:
            return

        if not paths:
            paths = None
        device.record_stop_confirm(timecode, paths=paths)

    def osc_record_cancel(self, ip_address, command):
        """
        This is called when record has been pressed and stopped before the countdown in take recorder
        has finished
        """
        device = self._device_from_ip_address(ip_address, command)
        if not device:
            return

        self._record_cancel(exclude_ip_address=device.ip_address)

    def osc_record_cancel_confirm(self, ip_address, command, timecode):
        pass
        #device = self._device_from_ip_address(ip_address, command, value=timecode)
        #if not device:
        #    return

        #self.record_cancel_confirm(device, timecode)

    def osc_ue4_launch_confirm(self, ip_address, command):
        device = self._device_from_ip_address(ip_address, command)
        if not device:
            return

        # If the device is already ready, bail
        if device.status == DeviceStatus.READY:
            return

        # Set the device status to ready
        device.status = DeviceStatus.READY

    def osc_add_send_target_confirm(self, ip_address, command, value):
        device = self.device_manager.device_with_ip_address(ip_address[0])
        if not device:
            return

        device.osc_add_send_target_confirm()

    def osc_arsession_stop_confrim(self, ip_address, command, value):
        LOGGER.debug(f'osc_arsession_stop_confrim {value}')

    def osc_arsession_start_confrim(self, ip_address, command, value):
        device = self._device_from_ip_address(ip_address, command, value=value)
        if not device:
            return

        device.connect_listener()

    def osc_battery(self, ip_address, command, value):
        # The Battery command is used to handshake with LiveLinkFace. Don't reject it if it's not connected 
        device = self.device_manager.device_with_ip_address(ip_address[0])
        
        if not device:
            return

        # Update the device
        device.battery = value

        # Update the UI
        device_widget = self.device_list_widget.device_widget_by_hash(device.device_hash)
        device_widget.set_battery(value)

    def osc_data(self, ip_address, command, value):
        device = self._device_from_ip_address(ip_address, command)
        if not device:
            return


class TransportQueueHeaderActionWidget(QtWidgets.QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)

        self.layout = QtWidgets.QHBoxLayout()
        self.layout.setContentsMargins(0, 0, 0, 0)
        self.setLayout(self.layout)

        def __label(label_text):
            label = QtWidgets.QLabel()
            label.setText(label_text)
            label.setObjectName('widget_header')
            return label

        self.name_label = __label('Transport Queue')

        self.layout.addWidget(self.name_label)
    
    def paintEvent(self, event):
        opt = QtWidgets.QStyleOption()
        opt.initFrom(self)
        painter = QtGui.QPainter(self)
        self.style().drawPrimitive(QtWidgets.QStyle.PE_Widget, opt, painter, self)


class TransportQueueActionWidget(QtWidgets.QWidget):
    def __init__(self, name, parent=None):
        super().__init__(parent)

        self.layout = QtWidgets.QHBoxLayout()
        self.layout.setContentsMargins(20, 2, 20, 2)
        self.layout.setSpacing(2)
        self.setLayout(self.layout)

        # Job Label
        label = QtWidgets.QLabel(name)
        self.layout.addWidget(label)

        spacer = QtWidgets.QSpacerItem(0, 20, QtWidgets.QSizePolicy.Expanding, QtWidgets.QSizePolicy.Minimum)
        self.layout.addItem(spacer)

        # Remove button
        button = sb_widgets.ControlQPushButton()
        sb_widgets.set_qt_property(button, 'no_background', True)

        icon = QtGui.QIcon()
        pixmap = QtGui.QPixmap(f":/icons/images/icon_close_disabled.png")
        icon.addPixmap(pixmap, QtGui.QIcon.Normal, QtGui.QIcon.Off)
        pixmap = QtGui.QPixmap(f":/icons/images/icon_close.png")
        icon.addPixmap(pixmap, QtGui.QIcon.Normal, QtGui.QIcon.On)
        pixmap = QtGui.QPixmap(f":/icons/images/icon_close_hover.png")
        icon.addPixmap(pixmap, QtGui.QIcon.Active, QtGui.QIcon.Off)

        button.setIcon(icon)
        button.setIconSize(pixmap.rect().size()*0.75)
        button.setMinimumSize(QtCore.QSize(20, 20))

        button.setCheckable(False)
        #button.clicked.connect(self.device_button_clicked)
        self.layout.addWidget(button)

    def test(self):
        LOGGER.debug('BOOM!')
    
    def paintEvent(self, event):
        opt = QtWidgets.QStyleOption()
        opt.initFrom(self)
        painter = QtGui.QPainter(self)
        self.style().drawPrimitive(QtWidgets.QStyle.PE_Widget, opt, painter, self)
