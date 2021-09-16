# Copyright Epic Games, Inc. All Rights Reserved.

from __future__ import annotations

import base64
from collections import OrderedDict
from datetime import datetime
from functools import wraps
import ipaddress
import os
import pathlib
import re
import threading
from typing import Callable, List, Optional, Set
import uuid

from PySide2 import QtCore
from PySide2 import QtWidgets

from switchboard import config_osc as osc
from switchboard import message_protocol
from switchboard import switchboard_application
from switchboard import switchboard_utils as sb_utils
from switchboard.config import CONFIG, BoolSetting, DirectoryPathSetting, \
    IntSetting, MultiOptionSetting, OptionSetting, StringSetting, SETTINGS, \
    DEFAULT_MAP_TEXT
from switchboard.devices.device_base import Device, DeviceStatus, \
    PluginHeaderWidgets
from switchboard.devices.device_widget_base import DeviceWidget
from switchboard.listener_client import ListenerClient
from switchboard.switchboard_logging import LOGGER
import switchboard.switchboard_widgets as sb_widgets

from .listener_watcher import ListenerWatcher
from .redeploy_dialog import RedeployListenerDialog
from . import version_helpers


class ProgramStartQueueItem:
    '''
    Item that holds the information for a program to start in the future.
    '''
    def __init__(
        self, name: str, puuid_dependency: Optional[uuid.UUID],
        puuid: uuid.UUID, msg_to_unreal_client: bytes, pid: int = 0,
        launch_fn: Callable[[], None] = lambda: None
    ):
        self.name = name
        # So that it can check wait_for_previous_to_end against the correct
        # program
        self.puuid_dependency = puuid_dependency
        # id of the program.
        self.puuid = puuid
        # command for listener/unreal_client
        self.msg_to_unreal_client = msg_to_unreal_client
        self.launch_fn = launch_fn
        self.pid = pid

    @classmethod
    def from_listener_process(cls, process):
        return cls(
            puuid_dependency=None,
            puuid=uuid.UUID(process['uuid']),
            msg_to_unreal_client=b'',
            name=process['name'],
            pid=process['pid'],
            launch_fn=lambda: None,
        )


def use_lock(func):
    '''
    Decorator to ensure the decorated function is executed by a single thread
    at a time.
    '''
    @wraps(func)
    def _use_lock(self, *args, **kwargs):
        self.lock.acquire()
        try:
            return func(self, *args, **kwargs)
        finally:
            self.lock.release()

    return _use_lock


class ProgramStartQueue:
    ''' Queue of programs to launch that may have dependencies '''

    def __init__(self):
        # Needed because these functions will be called from listener thread
        # and main thread.
        self.lock = threading.Lock()

        self.queued_programs: List[ProgramStartQueueItem] = []
        self.starting_programs: OrderedDict[
            uuid.UUID, ProgramStartQueueItem] = OrderedDict()
        # initialized as returned by listener
        self.running_programs: OrderedDict[
            uuid.UUID, ProgramStartQueueItem] = OrderedDict()

    @use_lock
    def reset(self):
        ''' Clears the list and any internal state '''
        self.queued_programs = []
        self.starting_programs = OrderedDict()
        self.running_programs = OrderedDict()

    def _name_from_puuid(self, puuid):
        ''' Returns the name of the specified program id '''
        for prog in self.queued_programs:
            if prog.puuid == puuid:
                return prog.name

        for thedict in [self.starting_programs, self.running_programs]:
            try:
                return thedict[puuid].name
            except KeyError:
                pass

        raise KeyError

    @use_lock
    def puuid_from_name(self, name):
        '''
        Returns the puuid of the specified program name.

        Searches as to return the one most likely to return last - but not
        guaranteed to do so.
        '''
        for prog in self.queued_programs:
            if prog.name == name:
                return prog.puuid

        for thedict in [self.starting_programs, self.running_programs]:
            for prog in thedict.values():
                if prog.name == name:
                    return prog.puuid

        raise KeyError

    @use_lock
    def on_program_started(self, prog):
        ''' Returns the name of the program that started
        Moves the program from starting_programs to running_programs
        '''
        try:
            self.starting_programs.pop(prog.puuid)
        except KeyError:
            LOGGER.error(
                f"on_program_started::starting_programs.pop({prog.puuid}) "
                "KeyError")

        self.running_programs[prog.puuid] = prog

        return prog.name

    @use_lock
    def on_program_ended(self, puuid, unreal_client):
        ''' Returns the name of the program that ended
        Launches any dependent programs in the queue.
        Removes the program from starting_programs or running_programs.
        '''
        # remove from lists
        prog = None

        if puuid is not None:
            # check if it was waiting to start
            try:
                prog = self.starting_programs.pop(puuid)
            except KeyError:
                pass

            # check if it is already running
            if prog is None:
                try:
                    prog = self.running_programs.pop(puuid)
                except KeyError:
                    pass

            # check if it is queued
            if prog is None:
                programs = [
                    program for program in self.queued_programs
                    if program.puuid == puuid]

                for program in programs:
                    prog = program
                    self.queued_programs.remove(program)

            if prog is not None:
                LOGGER.debug(f"Ended {prog.name} {prog.puuid}")

        self._launch_dependents(puuid=puuid, unreal_client=unreal_client)

        return prog.name if prog else "unknown"

    def _launch_dependents(self, puuid, unreal_client):
        ''' Launches programs dependend on given puuid
        Do not call externally because it does not use the thread lock.
        '''
        # see if we need to launch any dependencies
        progs_launched = []

        for prog in self.queued_programs:
            if prog.puuid_dependency is None or puuid == prog.puuid_dependency:

                progs_launched.append(prog)
                self.starting_programs[prog.puuid] = prog

                unreal_client.send_message(prog.msg_to_unreal_client)
                prog.launch_fn()

        for prog in progs_launched:
            self.queued_programs.remove(prog)

    @use_lock
    def add(self, prog, unreal_client):
        ''' Adds a new program to be started in the queue
        Must be of type ProgramStartQueueItem.
        It may start launch it right away if it doesn't have any dependencies.
        '''
        assert isinstance(prog, ProgramStartQueueItem)

        # Ensure that dependance is still possible, and if it isn't, replace
        # with None.
        if prog.puuid_dependency is not None:
            try:
                self._name_from_puuid(prog.puuid_dependency)
            except KeyError:
                LOGGER.debug(
                    f"{prog.name} specified non-existent dependency on puuid "
                    f"{prog.puuid_dependency}")
                prog.puuid_dependency = None

        self.queued_programs.append(prog)

        # This effectively causes a launch if it doesn't have any dependencies
        self._launch_dependents(puuid=None, unreal_client=unreal_client)

    @use_lock
    def running_programs_named(self, name):
        ''' Returns the program ids of running programs named as specified '''
        return [
            prog for puuid, prog in self.running_programs.items()
            if prog.name == name]

    @use_lock
    def running_puuids_named(self, name):
        ''' Returns the program ids of running programs named as specified '''
        return [
            puuid for puuid, prog in self.running_programs.items()
            if prog.name == name]

    @use_lock
    def update_running_program(self, prog):
        self.running_programs[prog.puuid] = prog

    @use_lock
    def clear_running_programs(self):
        self.running_programs.clear()


class DeviceUnreal(Device):

    csettings = {
        'buffer_size': IntSetting(
            attr_name="buffer_size",
            nice_name="Buffer Size",
            value=1024,
            tool_tip=(
                "Buffer size used for communication with SwitchboardListener"),
        ),
        'command_line_arguments': StringSetting(
            attr_name="command_line_arguments",
            nice_name='Command Line Arguments',
            value="",
            tool_tip='Additional command line arguments for the engine',
        ),
        'exec_cmds': StringSetting(
            attr_name="exec_cmds",
            nice_name='ExecCmds',
            value="",
            tool_tip='ExecCmds to be passed. No need for outer double quotes.',
        ),
        'dp_cvars': StringSetting(
            attr_name='dp_cvars',
            nice_name="DPCVars",
            value='',
            tool_tip="Device profile console variables (comma separated)."
        ),
        'port': IntSetting(
            attr_name="port",
            nice_name="Listener Port",
            value=2980,
            tool_tip="Port of SwitchboardListener"
        ),
        'roles_filename': StringSetting(
            attr_name="roles_filename",
            nice_name="Roles Filename",
            value="VPRoles.ini",
            tool_tip=(
                "File that stores VirtualProduction roles. "
                "Default: Config/Tags/VPRoles.ini"),
        ),
        'stage_session_id': IntSetting(
            attr_name="stage_session_id",
            nice_name="Stage Session ID",
            value=0,
            tool_tip=(
                "An ID that groups Stage Monitor providers and monitors. "
                "Instances with different Session IDs are invisible to each "
                "other in Stage Monitor."),
        ),
        'ue4_exe': StringSetting(
            attr_name="editor_exe",
            nice_name="UE4 Editor filename",
            value="UE4Editor.exe",
        ),
        'max_gpu_count': OptionSetting(
            attr_name="max_gpu_count",
            nice_name="Number of GPUs",
            value=1,
            possible_values=list(range(1, 17)),
            tool_tip=(
                "If you have multiple GPUs in the PC, you can specify how "
                "many to use."),
        ),
        'priority_modifier': OptionSetting(
            attr_name='priority_modifier',
            nice_name="Process Priority",
            value=sb_utils.PriorityModifier.Normal.name,
            possible_values=[p.name for p in sb_utils.PriorityModifier],
            tool_tip="Used to override the priority of the process.",
        ),
        'auto_decline_package_recovery': BoolSetting(
            attr_name='auto_decline_package_recovery',
            nice_name='Skip Package Recovery',
            value=False,
            tool_tip=(
                'Automatically DISCARDS auto-saved packages at startup, '
                'skipping the restore prompt. Useful in multi-user '
                'scenarios, where restoring from auto-save may be '
                'undesirable.'),
        ),
        'udpmessaging_unicast_endpoint': StringSetting(
            attr_name='udpmessaging_unicast_endpoint',
            nice_name='Unicast Endpoint',
            value=':0',
            tool_tip=(
                'Local interface binding (-UDPMESSAGING_TRANSPORT_UNICAST) of '
                'the form {ip}:{port}. If {ip} is omitted, the device IP '
                'address is used.'),
        ),
        'udpmessaging_extra_static_endpoints': StringSetting(
            attr_name='udpmessaging_extra_static_endpoints',
            nice_name='Extra Static Endpoints',
            value='',
            tool_tip=(
                'Comma separated. Used to add static endpoints '
                '(-UDPMESSAGING_TRANSPORT_STATIC) in addition to those '
                'managed by Switchboard.'),
        ),
        'udpmessaging_multicast_endpoint': StringSetting(
            attr_name='udpmessaging_multicast_endpoint',
            nice_name='Multicast Endpoint',
            value='230.0.0.1:6666',
            tool_tip=(
                'Multicast group and port (-UDPMESSAGING_TRANSPORT_MULTICAST) '
                'in the {ip}:{port} endpoint format. The multicast group IP '
                'must be in the range 224.0.0.0 to 239.255.255.255.'),
        ),
        'log_download_dir': DirectoryPathSetting(
            attr_name='log_download_dir',
            nice_name='Log Download Dir',
            value='',
            tool_tip=(
                'Directory in which to store logs transferred from devices. '
                'If unset, defaults to $(ProjectDir)/Saved/Logs/Switchboard/'),
        ),
        'rsync_port': IntSetting(
            attr_name='rsync_port',
            nice_name='Rsync Server Port',
            value=switchboard_application.RsyncServer.DEFAULT_PORT,
            tool_tip='Port number on which the rsync server should listen.'
        ),
    }

    unreal_started_signal = QtCore.Signal()

    mu_server = switchboard_application.MultiUserApplication()
    rsync_server = switchboard_application.RsyncServer()

    # Monitors the local listener executable and notifies when the file is
    # changed.
    listener_watcher = ListenerWatcher()

    # Every DeviceUnreal (and derived class, e.g. DevicenDisplay) instance;
    # used for listener updates.
    active_unreal_devices: Set[DeviceUnreal] = set()

    # Flag used to batch together multiple rapid calls to
    # `_queue_notify_redeploy`.
    _pending_notify_redeploy = False

    @classmethod
    def get_designated_local_builder(cls) -> Optional[DeviceUnreal]:
        '''
        Returns first (by IP) local `DeviceUnreal` (or derived class) device.

        This is the device tasked with building the multiuser server and
        listener executables.
        '''
        ip_device_pairs = [
            (ipaddress.ip_address(d.ip_address), d)
            for d in cls.active_unreal_devices]

        switchboard_ip_address = ipaddress.ip_address(SETTINGS.IP_ADDRESS)

        def is_local(pair):
            return pair[0].is_loopback or (pair[0] == switchboard_ip_address)

        ip_device_pairs = filter(is_local, ip_device_pairs)
        ip_device_pairs = sorted(ip_device_pairs, key=lambda pair: pair[0])

        return ip_device_pairs[0][1] if len(ip_device_pairs) > 0 else None

    def is_designated_local_builder(self) -> bool:
        return self is DeviceUnreal.get_designated_local_builder()

    @QtCore.Slot()
    def _queue_notify_redeploy(self):
        # Ensure this code is run from the main thread
        if threading.current_thread() is not threading.main_thread():
            QtCore.QMetaObject.invokeMethod(
                self, '_queue_notify_redeploy', QtCore.Qt.QueuedConnection)
            return

        # Brief debounce in case we're still connecting multiple clients in
        # quick succession
        if not DeviceUnreal._pending_notify_redeploy:
            QtCore.QTimer.singleShot(100, self._notify_redeploy)
            DeviceUnreal._pending_notify_redeploy = True

    def _notify_redeploy(self):
        dlg = RedeployListenerDialog(
            DeviceUnreal.active_unreal_devices, DeviceUnreal.listener_watcher)
        dlg.exec()
        DeviceUnreal._pending_notify_redeploy = False

    def __init__(self, name, ip_address, **kwargs):
        super().__init__(name, ip_address, **kwargs)

        self.unreal_client = ListenerClient(
            ip_address=self.ip_address,
            port=DeviceUnreal.csettings['port'].get_value(self.name),
            buffer_size=DeviceUnreal.csettings['buffer_size'].get_value(
                self.name)
        )

        roles = kwargs.get("roles", [])

        self.setting_roles = MultiOptionSetting(
            attr_name="roles",
            nice_name="Roles",
            value=roles,
            tool_tip="List of roles for this device"
        )

        self.setting_ip_address.signal_setting_changed.connect(
            self.on_setting_ip_address_changed)
        DeviceUnreal.csettings['port'].signal_setting_changed.connect(
            self.on_setting_port_changed)
        CONFIG.BUILD_ENGINE.signal_setting_changed.connect(
            self.on_build_engine_changed)

        self.auto_connect = False

        self.inflight_project_cl = None
        self.inflight_engine_cl = None

        listener_qt_handler = self.unreal_client.listener_qt_handler
        listener_qt_handler.listener_connecting.connect(
            super().connecting_listener, QtCore.Qt.QueuedConnection)
        listener_qt_handler.listener_connected.connect(
            super().connect_listener, QtCore.Qt.QueuedConnection)
        listener_qt_handler.listener_connection_failed.connect(
            self._on_listener_connection_failed, QtCore.Qt.QueuedConnection)

        # Set a delegate method if the device gets a disconnect signal
        self.unreal_client.disconnect_delegate = self.on_listener_disconnect
        self.unreal_client.receive_file_completed_delegate = (
            self.on_file_received)
        self.unreal_client.receive_file_failed_delegate = (
            self.on_file_receive_failed)
        self.unreal_client.delegates["state"] = self.on_listener_state
        self.unreal_client.delegates["kill"] = self.on_program_killed
        self.unreal_client.delegates["programstdout"] = (
            self.on_listener_programstdout)
        self.unreal_client.delegates["program ended"] = (
            self.on_program_ended)
        self.unreal_client.delegates["program started"] = (
            self.on_program_started)
        self.unreal_client.delegates["program killed"] = self.on_program_killed
        # This catches start failures.
        self.unreal_client.delegates["start"] = self.on_program_started

        self.osc_connection_timer = QtCore.QTimer(self)
        self.osc_connection_timer.timeout.connect(self._try_connect_osc)

        # on_program_started is called from inside the connection thread but
        # QTimer can only be used from the main thread.
        # so we connect to a signal sent on the connection threat and can then
        # start the timer on the main thread.
        self.unreal_started_signal.connect(self.on_unreal_started)

        # keeps track of programs to start
        self.program_start_queue = ProgramStartQueue()

        # Launch rsync server
        if not DeviceUnreal.rsync_server.is_running():
            # Set incoming log directory, and update on settings change.
            def update_incoming_logs_path():
                log_dir = DeviceUnreal.get_log_download_dir()
                try:
                    DeviceUnreal.rsync_server.set_incoming_logs_path(log_dir)
                except RuntimeError as exc:
                    LOGGER.error(
                        f'update_incoming_logs_path failed for path {log_dir}',
                        exc_info=exc)

            update_incoming_logs_path()

            log_download_dir = DeviceUnreal.csettings['log_download_dir']
            log_download_dir.signal_setting_changed.connect(
                update_incoming_logs_path)

            # UPROJECT_PATH is only relevant if log_download_dir is left blank.
            def update_log_path_on_project_path_changed():
                if not log_download_dir.get_value().strip():
                    update_incoming_logs_path()

            CONFIG.UPROJECT_PATH.signal_setting_changed.connect(
                update_log_path_on_project_path_changed)

            # Relaunch the server if the port setting is changed.
            def launch_rsync_server():
                if DeviceUnreal.rsync_server.is_running():
                    DeviceUnreal.rsync_server.shutdown()

                rsync_port = DeviceUnreal.csettings['rsync_port'].get_value()
                DeviceUnreal.rsync_server.launch(port=rsync_port)

            launch_rsync_server()
            DeviceUnreal.csettings[
                'rsync_port'].signal_setting_changed.connect(
                    launch_rsync_server)

        app = QtCore.QCoreApplication.instance()
        app.aboutToQuit.connect(self._on_about_to_quit)

        self._widget_classes: Set[str] = set()

        # Notify user of any invalid settings
        self.check_settings_valid()

    def should_allow_exit(self, close_req_id: int) -> bool:
        # Delegate to a class method which surveys all active devices.
        return DeviceUnreal._should_allow_exit(close_req_id)

    _last_close_req_id: Optional[int] = None

    @classmethod
    def _should_allow_exit(cls, close_req_id: int) -> bool:
        # We give each request one opportunity to return False; this serves to
        # deduplicate calls to each device of this class or its subclasses.
        if close_req_id == cls._last_close_req_id:
            return True
        else:
            cls._last_close_req_id = close_req_id

        log_xfer_devices = [
            dev for dev in DeviceUnreal.active_unreal_devices
            if dev.log_transfer_in_progress]

        if len(log_xfer_devices) == 0:
            return True

        log_xfer_names = [device.name for device in log_xfer_devices]

        msg_result = QtWidgets.QMessageBox.warning(
            None, 'Log Transfers In Progress',
            'The following devices are still transferring their log files to '
            f'Switchboard: {", ".join(log_xfer_names)}\n\n'
            'If you exit Switchboard, these transfers will be interrupted. '
            'Do you still want to exit Switchboard?',
            QtWidgets.QMessageBox.Yes | QtWidgets.QMessageBox.No)

        return msg_result == QtWidgets.QMessageBox.Yes

    @classmethod
    def added_device(cls, device: DeviceUnreal):
        '''
        Implementation of base class function. Called when one of our plugin
        devices has been added.
        '''
        assert device not in DeviceUnreal.active_unreal_devices
        DeviceUnreal.active_unreal_devices.add(device)

        DeviceUnreal.listener_watcher.update_listener_path(
            CONFIG.listener_path())

        DeviceUnreal.rsync_server.register_client(
            device, device.name, device.ip_address)
        device.widget.signal_device_name_changed.connect(
            device.reregister_rsync_client)
        device.setting_ip_address.signal_setting_changed.connect(
            device.reregister_rsync_client)

    @classmethod
    def removed_device(cls, device: DeviceUnreal):
        '''
        Implementation of base class function. Called when one of our plugin
        devices has been removed.
        '''
        assert device in DeviceUnreal.active_unreal_devices
        DeviceUnreal.active_unreal_devices.remove(device)

        DeviceUnreal.rsync_server.unregister_client(device)
        device.widget.signal_device_name_changed.disconnect(
            device.reregister_rsync_client)
        device.setting_ip_address.signal_setting_changed.disconnect(
            device.reregister_rsync_client)

        if len(DeviceUnreal.active_unreal_devices) == 0:
            if DeviceUnreal.rsync_server.is_running():
                DeviceUnreal.rsync_server.shutdown()

    @QtCore.Slot()
    def _on_about_to_quit(self):
        if DeviceUnreal.rsync_server.is_running():
            DeviceUnreal.rsync_server.shutdown()

    @classmethod
    def plugin_settings(cls):
        return Device.plugin_settings() + list(DeviceUnreal.csettings.values())

    def setting_overrides(self):
        return super().setting_overrides() + [
            Device.csettings['is_recording_device'],
            DeviceUnreal.csettings['command_line_arguments'],
            DeviceUnreal.csettings['exec_cmds'],
            DeviceUnreal.csettings['dp_cvars'],
            DeviceUnreal.csettings['max_gpu_count'],
            DeviceUnreal.csettings['priority_modifier'],
            DeviceUnreal.csettings['auto_decline_package_recovery'],
            DeviceUnreal.csettings['udpmessaging_unicast_endpoint'],
            DeviceUnreal.csettings['udpmessaging_extra_static_endpoints'],
            CONFIG.ENGINE_DIR,
            CONFIG.SOURCE_CONTROL_WORKSPACE,
            CONFIG.UPROJECT_PATH,
        ]

    def device_settings(self):
        return super().device_settings() + [self.setting_roles]

    def check_settings_valid(self) -> bool:
        valid = True

        # Check this device's settings.
        device_extra_args_lower: str = self.extra_cmdline_args_setting.lower()
        if '-udpmessaging_transport_unicast' in device_extra_args_lower:
            LOGGER.error(
                f'{self.name}: Command line arguments include '
                '-UDPMESSAGING_TRANSPORT_UNICAST; use the "Unicast Endpoint" '
                'setting instead.')
            valid = False

        if '-udpmessaging_transport_static' in device_extra_args_lower:
            LOGGER.error(
                f'{self.name}: Command line arguments include '
                '-UDPMESSAGING_TRANSPORT_STATIC; use the '
                '"Extra Static Endpoints" setting instead.')
            valid = False

        # Also check the multi-user server settings.
        muserver_extra_args_lower: str = (
            CONFIG.MUSERVER_COMMAND_LINE_ARGUMENTS.lower())
        if '-udpmessaging_transport_unicast' in muserver_extra_args_lower:
            LOGGER.error(
                f'{self.name}: Multi-user server command line arguments '
                'include -UDPMESSAGING_TRANSPORT_UNICAST; use the '
                '"Unicast Endpoint" setting instead.')
            valid = False

        return valid

    @classmethod
    def plugin_header_widget_config(cls):
        """
        Combination of widgets that will be visualized in the plugin header.
        """
        return (
            super().plugin_header_widget_config() |
            PluginHeaderWidgets.OPEN_BUTTON |
            PluginHeaderWidgets.CHANGELIST_LABEL)

    def on_setting_ip_address_changed(self, _, new_address):
        LOGGER.info(f"Updating IP address for ListenerClient to {new_address}")
        self.unreal_client.ip_address = new_address

    def on_setting_port_changed(self, _, new_port):
        if not DeviceUnreal.csettings['port'].is_overridden(self.name):
            LOGGER.info(f"Updating Port for ListenerClient to {new_port}")
            self.unreal_client.port = new_port

    def on_build_engine_changed(self, _, build_engine):
        if build_engine:
            self.widget.engine_changelist_label.show()
            if not self.is_disconnected:
                self._request_engine_changelist_number()
        else:
            self.widget.engine_changelist_label.hide()

    def set_slate(self, value):
        if not self.is_recording_device:
            return

        self.send_osc_message(osc.SLATE, value)

    def set_take(self, value):
        if not self.is_recording_device:
            return

        self.send_osc_message(osc.TAKE, value)

    def record_stop(self):
        if not self.is_recording_device:
            return

        self.send_osc_message(osc.RECORD_STOP, 1)

    def _request_roles_file(self):
        uproject_path = CONFIG.UPROJECT_PATH.get_value(
            self.name).replace('"', '')
        roles_filename = DeviceUnreal.csettings["roles_filename"].get_value(
            self.name)
        roles_file_path = os.path.join(
            os.path.dirname(uproject_path), "Config", "Tags", roles_filename)
        _, msg = message_protocol.create_copy_file_from_listener_message(
            roles_file_path)
        self.unreal_client.send_message(msg)

    def _request_project_changelist_number(self):
        if not CONFIG.P4_ENABLED.get_value():
            return

        client_name = CONFIG.SOURCE_CONTROL_WORKSPACE.get_value(self.name)

        if not client_name:
            LOGGER.warning(
                f"{self.name}: Missing workspace name to query the project "
                "changelist")
            return

        p4_path = CONFIG.P4_PROJECT_PATH.get_value()

        if not p4_path:
            LOGGER.warning(
                f"{self.name}: Missing p4 path to query the project "
                "changelist")
            return

        formatstring = "%change%"
        args = f'-F "{formatstring}" -c {client_name} cstat {p4_path}/...#have'

        program_name = "cstat_project"

        working_dir = os.path.dirname(
            CONFIG.UPROJECT_PATH.get_value(self.name))

        puuid, msg = message_protocol.create_start_process_message(
            prog_path="p4",
            prog_args=args,
            prog_name=program_name,
            caller=self.name,
            working_dir=working_dir,
            update_clients_with_stdout=False,
        )

        self.program_start_queue.add(
            ProgramStartQueueItem(
                name=program_name,
                puuid_dependency=None,
                puuid=puuid,
                msg_to_unreal_client=msg,
            ),
            unreal_client=self.unreal_client,
        )

    def _request_engine_changelist_number(self):
        if not CONFIG.P4_ENABLED.get_value():
            return

        client_name = CONFIG.SOURCE_CONTROL_WORKSPACE.get_value(self.name)

        if not client_name:
            LOGGER.warning(
                f"{self.name}: Missing workspace name to query the engine "
                "changelist")
            return

        p4_path = CONFIG.P4_ENGINE_PATH.get_value()

        if not p4_path:
            LOGGER.warning(
                f'{self.name}: Missing p4 path to query the engine changelist')
            return

        formatstring = "%change%"
        args = f'-F "{formatstring}" -c {client_name} cstat {p4_path}/...#have'

        program_name = "cstat_engine"

        working_dir = os.path.dirname(
            CONFIG.UPROJECT_PATH.get_value(self.name))

        puuid, msg = message_protocol.create_start_process_message(
            prog_path="p4",
            prog_args=args,
            prog_name=program_name,
            caller=self.name,
            working_dir=working_dir,
            update_clients_with_stdout=False,
        )

        self.program_start_queue.add(
            ProgramStartQueueItem(
                name=program_name,
                puuid_dependency=None,
                puuid=puuid,
                msg_to_unreal_client=msg,
            ),
            unreal_client=self.unreal_client,
        )

    @QtCore.Slot()
    def connect_listener(self):
        ''' Connects to the listener '''
        # Ensure this code is run from the main thread
        if threading.current_thread() is not threading.main_thread():
            QtCore.QMetaObject.invokeMethod(
                self, 'connect_listener', QtCore.Qt.QueuedConnection)
            return

        # This will start the connection process asynchronously. The base
        # class will be notified by signal whether the connection succeeded or
        # failed and will update the device's status
        # accordingly.
        self.unreal_client.connect()

    @QtCore.Slot()
    def _on_listener_connection_failed(self):
        self.device_qt_handler.signal_device_connect_failed.emit(self)

    @QtCore.Slot()
    def disconnect_listener(self):
        ''' Disconnects from the listener '''
        # Ensure this code is run from the main thread
        if threading.current_thread() is not threading.main_thread():
            QtCore.QMetaObject.invokeMethod(
                self, 'disconnect_listener', QtCore.Qt.QueuedConnection)
            return

        super().disconnect_listener()
        self.unreal_client.disconnect()

    def sync(self, engine_cl: Optional[int], project_cl: Optional[int]):
        if not engine_cl and not project_cl:
            LOGGER.warning(
                "Neither project nor engine changelist is selected. "
                "There is nothing to sync!")
            return

        program_name = 'sync'

        # check if it is already on its way:
        try:
            existing_puuid = self.program_start_queue.puuid_from_name(
                program_name)
            LOGGER.info(
                f"{self.name}: Already syncing with puuid {existing_puuid}")
            return
        except KeyError:
            pass

        project_path = CONFIG.UPROJECT_PATH.get_value(self.name)
        engine_dir = CONFIG.ENGINE_DIR.get_value(self.name)
        workspace = CONFIG.SOURCE_CONTROL_WORKSPACE.get_value(self.name)

        project_name = os.path.basename(os.path.dirname(project_path))
        LOGGER.info(
            f"{self.name}: Queuing sync for project {project_name} "
            f"(revisions: engine={engine_cl}, project={project_cl})")

        python_path = os.path.normpath(os.path.join(
            engine_dir, 'Binaries', 'ThirdParty', 'Python3', 'Win64',
            'python.exe'))
        helper_path = os.path.normpath(os.path.join(
            engine_dir, 'Plugins', 'VirtualProduction', 'Switchboard',
            'Source', 'Switchboard', 'sbl_helper.py'))

        sync_tool = python_path
        sync_args = (
            f'"{helper_path}" '
            # '--log-level=DEBUG '
            'sync '
            f'--project="{project_path}" '
            f'--engine-dir="{engine_dir}" '
            '--generate')

        if workspace:
            sync_args += f' --p4client={workspace}'

        if engine_cl:
            sync_args += f' --engine-cl={engine_cl}'
            self.inflight_engine_cl = engine_cl

        if project_cl:
            sync_args += f' --project-cl={project_cl} --clobber-project'
            self.inflight_project_cl = project_cl

        puuid, msg = message_protocol.create_start_process_message(
            prog_path=sync_tool,
            prog_args=sync_args,
            prog_name=program_name,
            caller=self.name,
            working_dir=os.path.dirname(
                CONFIG.UPROJECT_PATH.get_value(self.name)),
            update_clients_with_stdout=True,
        )

        self.program_start_queue.add(
            ProgramStartQueueItem(
                name=program_name,
                puuid_dependency=None,
                puuid=puuid,
                msg_to_unreal_client=msg,
                launch_fn=lambda: LOGGER.info(
                    f"{self.name}: Sending sync command: "
                    f"{sync_tool} {sync_args}")
            ),
            unreal_client=self.unreal_client,
        )

        if self.status != DeviceStatus.SYNCING:
            self.status = DeviceStatus.SYNCING

    def build(self):
        program_name = 'build_project'

        # check if it is already on its way:
        try:
            existing_puuid = self.program_start_queue.puuid_from_name(
                program_name)
            LOGGER.info(
                f"{self.name}: Already building with puuid {existing_puuid}")
            return
        except KeyError:
            pass

        # check for any sync dependencies
        sync_puuid = None

        try:
            sync_puuid = self.program_start_queue.puuid_from_name('sync')
        except KeyError:
            pass

        if sync_puuid:
            LOGGER.debug(
                f"{self.name} Queuing build after sync with "
                f"puuid {sync_puuid}")

        # Build dependency chain
        puuid_dependency = sync_puuid

        if (self.is_designated_local_builder() and
                CONFIG.BUILD_ENGINE.get_value()):
            # Build multi-user server
            if CONFIG.MUSERVER_AUTO_BUILD:
                if DeviceUnreal.mu_server.is_running():
                    mb_ret = QtWidgets.QMessageBox.question(
                        None, 'Terminate multi-user server?',
                        'The multi-user server is currently running. '
                        'Would you like to terminate it so that it can be '
                        'updated by the build?',
                        QtWidgets.QMessageBox.Yes | QtWidgets.QMessageBox.No)

                    if mb_ret == QtWidgets.QMessageBox.Yes:
                        DeviceUnreal.mu_server.terminate()

                puuid_dependency = self._build_mu_server(
                    puuid_dependency=puuid_dependency)

            # TODO: Build listener
            # - Handle the running listener's exe being locked
            # - Parse SwitchboardListenerVersion.h, skip if unchanged?

            # if CONFIG.LISTENER_AUTO_BUILD:
            #     puuid_dependency = self._build_listener(
            #         puuid_dependency=puuid_dependency)

        puuid_dependency = self._build_shadercompileworker(
            puuid_dependency=puuid_dependency)
        puuid_dependency = self._build_project(
            puuid_dependency=puuid_dependency)

    def _build_project(self, puuid_dependency: Optional[uuid.UUID] = None):
        ubt_args = (
            'Win64 Development '
            f'-project="{CONFIG.UPROJECT_PATH.get_value(self.name)}" '
            '-TargetType=Editor -Progress -NoHotReloadFromIDE')
        return self._queue_build(
            'project', ubt_args=ubt_args, puuid_dependency=puuid_dependency)

    def _build_mu_server(self, puuid_dependency: Optional[uuid.UUID] = None):
        ubt_args = 'UnrealMultiUserServer Win64 Development -Progress'
        return self._queue_build(
            'mu_server', ubt_args=ubt_args, puuid_dependency=puuid_dependency)

    def _build_listener(self, puuid_dependency: Optional[uuid.UUID] = None):
        ubt_args = 'SwitchboardListener Win64 Development -Progress'
        return self._queue_build(
            'listener', ubt_args=ubt_args, puuid_dependency=puuid_dependency)

    def _build_shadercompileworker(
            self, puuid_dependency: Optional[uuid.UUID] = None):
        ubt_args = 'ShaderCompileWorker Win64 Development -Progress'
        return self._queue_build(
            'shadercw', ubt_args=ubt_args, puuid_dependency=puuid_dependency)

    def _queue_build(
            self, program_name_suffix: str, ubt_args: str,
            puuid_dependency: Optional[uuid.UUID] = None):
        program_name = f"build_{program_name_suffix}"

        engine_path = CONFIG.ENGINE_DIR.get_value(self.name)
        ubt_path = os.path.join(
            engine_path, 'Binaries', 'DotNET', 'UnrealBuildTool')

        puuid, msg = message_protocol.create_start_process_message(
            prog_path=ubt_path,
            prog_args=ubt_args,
            prog_name=program_name,
            caller=self.name,
            update_clients_with_stdout=True,
        )

        def launch_fn():
            LOGGER.info(
                f"{self.name}: Sending {program_name} command: "
                f"{ubt_path} {ubt_args}")
            self.status = DeviceStatus.BUILDING

        # Queue the build command
        self.program_start_queue.add(
            ProgramStartQueueItem(
                name=program_name,
                puuid_dependency=puuid_dependency,
                puuid=puuid,
                msg_to_unreal_client=msg,
                launch_fn=launch_fn,
            ),
            unreal_client=self.unreal_client,
        )

        return puuid

    def close(self, force=False):
        # This call only refers to "unreal" programs.
        unreal_puuids = self.program_start_queue.running_puuids_named('unreal')

        for unreal_puuid in unreal_puuids:
            _, msg = message_protocol.create_kill_process_message(unreal_puuid)
            self.unreal_client.send_message(msg)

        if not len(unreal_puuids):
            self.status = DeviceStatus.CLOSED
        else:
            self.status = DeviceStatus.CLOSING

    def fix_exe_flags(self):
        ''' Tries to force the correct UE4Editor.exe flags '''
        unreals = self.program_start_queue.running_programs_named('unreal')

        if not len(unreals):
            return

        puuid = unreals[0].puuid

        if not puuid:
            return

        _, msg = message_protocol.create_fixExeFlags_message(puuid)
        self.unreal_client.send_message(msg)

    def minimize_windows(self):
        ''' Tries to minimize all windows '''

        _, msg = message_protocol.create_minimize_windows_message()
        self.unreal_client.send_message(msg)

    @property
    def executable_filename(self):
        return DeviceUnreal.csettings["ue4_exe"].get_value()

    @property
    def extra_cmdline_args_setting(self) -> str:
        return DeviceUnreal.csettings[
            'command_line_arguments'].get_value(self.name)

    @property
    def udpmessaging_unicast_endpoint_setting(self) -> str:
        return DeviceUnreal.csettings[
            'udpmessaging_unicast_endpoint'].get_value(self.name)

    @property
    def udpmessaging_extra_static_endpoints_setting(self) -> str:
        return DeviceUnreal.csettings[
            'udpmessaging_extra_static_endpoints'].get_value(self.name)

    def generate_unreal_exe_path(self):
        return CONFIG.engine_exe_path(
            CONFIG.ENGINE_DIR.get_value(self.name), self.executable_filename)

    def get_vproles(self):
        ''' Gets selected vp roles that are also present in the ini file
        Also returns any selected vp roles that are not in the ini file.
        '''

        vproles = self.setting_roles.get_value()
        missing_roles = [
            role for role in vproles
            if role not in self.setting_roles.possible_values]
        vproles = [role for role in vproles if role not in missing_roles]

        return vproles, missing_roles

    @classmethod
    def expand_endpoint(
            cls, endpoint: str, default_addr: str = '0.0.0.0',
            default_port: int = 0) -> str:
        '''
        Given an endpoint where either address or port was omitted, use
        the provided defaults.
        '''
        addr_str, _, port_str = endpoint.partition(':')

        if not addr_str:
            addr_str = default_addr

        if not port_str:
            port_str = str(default_port)

        return f'{addr_str}:{port_str}'

    @property
    def udpmessaging_multicast_endpoint(self) -> str:
        return DeviceUnreal.csettings[
            'udpmessaging_multicast_endpoint'].get_value().strip()

    @classmethod
    def get_muserver_endpoint(cls) -> str:
        setting_val = CONFIG.MUSERVER_ENDPOINT.strip()
        if setting_val:
            return cls.expand_endpoint(setting_val,
                                       SETTINGS.IP_ADDRESS.strip())
        else:
            return ''

    @property
    def udpmessaging_unicast_endpoint(self) -> str:
        setting_val = self.udpmessaging_unicast_endpoint_setting.strip()
        if setting_val:
            return self.expand_endpoint(setting_val, self.ip_address)
        else:
            return ''

    def build_udpmessaging_static_endpoint_list(self) -> List[str]:
        endpoints: List[str] = []

        # Multi-user server.
        if CONFIG.MUSERVER_AUTO_JOIN and CONFIG.MUSERVER_AUTO_ENDPOINT:
            endpoints.append(DeviceUnreal.get_muserver_endpoint())

        # Any additional endpoints manually specified via settings.
        extra_endpoints = \
            self.udpmessaging_extra_static_endpoints_setting.split(',')
        endpoints.extend(endpoint.strip() for endpoint in extra_endpoints)

        endpoints = list(filter(None, endpoints))

        return endpoints

    def generate_unreal_command_line_args(self, map_name):
        command_line_args = f'{self.extra_cmdline_args_setting}'

        command_line_args += f' Log={self.log_filename}'

        if CONFIG.MUSERVER_AUTO_JOIN:
            command_line_args += (
                ' -CONCERTRETRYAUTOCONNECTONERROR '
                '-CONCERTAUTOCONNECT '
                f'-CONCERTSERVER={CONFIG.MUSERVER_SERVER_NAME} '
                f'-CONCERTSESSION={SETTINGS.MUSERVER_SESSION_NAME} '
                f'-CONCERTDISPLAYNAME={self.name}')

        exec_cmds = str(
            DeviceUnreal.csettings["exec_cmds"].get_value(self.name)).strip()
        if len(exec_cmds):
            command_line_args += f' -ExecCmds="{exec_cmds}" '

        # DPCVars may need to be appended to, so we don't concatenate them
        # until the end.
        dp_cvars = str(
            DeviceUnreal.csettings["dp_cvars"].get_value(self.name)).strip()

        (supported_roles, unsupported_roles) = self.get_vproles()
        if supported_roles:
            command_line_args += ' -VPRole=' + '|'.join(supported_roles)
        if unsupported_roles:
            LOGGER.error(
                f"{self.name}: Omitted unsupported roles: "
                f"{'|'.join(unsupported_roles)}")

        # Session ID
        session_id = DeviceUnreal.csettings["stage_session_id"].get_value()
        if session_id > 0:
            command_line_args += f" -StageSessionId={session_id}"

        command_line_args += f" -StageFriendlyName={self.name}"

        # Max GPU Count (mGPU)
        max_gpu_count = DeviceUnreal.csettings["max_gpu_count"].get_value(
            self.name)
        try:
            if int(max_gpu_count) > 1:
                command_line_args += f" -MaxGPUCount={max_gpu_count} "
                if len(dp_cvars):
                    dp_cvars += ','
                dp_cvars += 'r.AllowMultiGPUInEditor=1'
        except ValueError:
            LOGGER.warning(f"Invalid Number of GPUs '{max_gpu_count}'")

        if len(dp_cvars):
            command_line_args += f' -DPCVars="{dp_cvars}" '

        if DeviceUnreal.csettings['auto_decline_package_recovery'].get_value(
                self.name):
            command_line_args += ' -AutoDeclinePackageRecovery'

        # UdpMessaging endpoints
        if self.udpmessaging_multicast_endpoint:
            command_line_args += (
                ' -UDPMESSAGING_TRANSPORT_MULTICAST='
                f'"{self.udpmessaging_multicast_endpoint}"'
            )

        if self.udpmessaging_unicast_endpoint:
            command_line_args += (
                ' -UDPMESSAGING_TRANSPORT_UNICAST='
                f'"{self.udpmessaging_unicast_endpoint}"')

        static_endpoints = self.build_udpmessaging_static_endpoint_list()
        if len(static_endpoints) > 0:
            command_line_args += (
                ' -UDPMESSAGING_TRANSPORT_STATIC='
                f'"{",".join(static_endpoints)}"')

        return (
            f'"{CONFIG.UPROJECT_PATH.get_value(self.name)}" {map_name} '
            f'{command_line_args}')

    def generate_unreal_command_line(self, map_name):
        return (
            self.generate_unreal_exe_path(),
            self.generate_unreal_command_line_args(map_name))

    def launch(self, map_name, program_name="unreal"):
        if map_name == DEFAULT_MAP_TEXT:
            map_name = ''

        if not self.check_settings_valid():
            LOGGER.error(f"{self.name}: Not launching due to invalid settings")
            self.widget._close()
            return

        # Launch the MU server
        if CONFIG.MUSERVER_AUTO_LAUNCH:
            mu_args: List[str] = []

            if DeviceUnreal.get_muserver_endpoint():
                mu_args.append('-UDPMESSAGING_TRANSPORT_UNICAST='
                               f'"{DeviceUnreal.get_muserver_endpoint()}"')

            if self.udpmessaging_multicast_endpoint:
                mu_args.append('-UDPMESSAGING_TRANSPORT_MULTICAST='
                               f'"{self.udpmessaging_multicast_endpoint}"')

            DeviceUnreal.mu_server.launch(mu_args)

        engine_path, args = self.generate_unreal_command_line(map_name)
        LOGGER.info(f"Launching UE4: {engine_path} {args}")

        priority_modifier_str = self.csettings['priority_modifier'].get_value(
            self.name)
        try:
            priority_modifier = sb_utils.PriorityModifier[
                priority_modifier_str].value
        except KeyError:
            LOGGER.warning(
                f"Invalid priority_modifier '{priority_modifier_str}', "
                "defaulting to Normal")
            priority_modifier = sb_utils.PriorityModifier.Normal.value

        # TODO: Sanitize these on Qt input? Deserialization?
        args = args.replace('\r', ' ').replace('\n', ' ')

        puuid, msg = message_protocol.create_start_process_message(
            prog_path=engine_path,
            prog_args=args,
            prog_name=program_name,
            caller=self.name,
            update_clients_with_stdout=False,
            priority_modifier=priority_modifier
        )

        self.program_start_queue.add(
            ProgramStartQueueItem(
                name=program_name,
                puuid_dependency=None,
                puuid=puuid,
                msg_to_unreal_client=msg,
            ),
            unreal_client=self.unreal_client,
        )

    def _try_connect_osc(self):
        if self.status == DeviceStatus.OPEN:
            self.send_osc_message(
                osc.OSC_ADD_SEND_TARGET,
                [SETTINGS.IP_ADDRESS, CONFIG.OSC_SERVER_PORT.get_value()])
        else:
            self.osc_connection_timer.stop()

    def on_listener_disconnect(self, unexpected=False, exception=None):
        self.program_start_queue.reset()

        if unexpected:
            self.device_qt_handler.signal_device_client_disconnected.emit(self)

    def do_program_running_update(self, prog):
        if prog.name == 'unreal':
            self.status = DeviceStatus.OPEN
            self.unreal_started_signal.emit()
        elif prog.name.startswith('build_'):
            self.status = DeviceStatus.BUILDING
        elif prog.name == 'sync':
            self.status = DeviceStatus.SYNCING

        self.program_start_queue.update_running_program(prog=prog)

    def on_program_started(self, message):
        ''' Handler of the "start program" command '''
        # check if the operation failed
        if not message['bAck']:
            # When we send the start program command, the listener will use
            # the message uuid as program uuid.
            puuid = uuid.UUID(message['puuid'])

            # Tell the queue that the program ended (even before it started)
            program_name = self.program_start_queue.on_program_ended(
                puuid=puuid, unreal_client=self.unreal_client)

            # log this
            LOGGER.error(f"Could not start {program_name}: {message['error']}")

            if program_name == 'sync' or program_name.startswith('build_'):
                self.status = DeviceStatus.CLOSED
                # Force to show existing project_changelist to hide
                # building/syncing.
                self.project_changelist = self.project_changelist
            elif program_name == 'unreal':
                self.status = DeviceStatus.CLOSED
            elif program_name == 'retrieve_log':
                self.status = DeviceStatus.CLOSED

            return

        # grab the process data
        try:
            process = message['process']
        except KeyError:
            LOGGER.warning(
                f"{self.name} Received 'on_program_started' "
                "but no 'process' in the message")
            return

        if process['caller'] != self.name:
            return

        prog = ProgramStartQueueItem.from_listener_process(process)

        LOGGER.info(
            f"{self.name}: {prog.name} with id {prog.puuid} was "
            "successfully started")

        # Tell the queue that the program started
        self.program_start_queue.on_program_started(prog=prog)

        # Perform any necessary updates to the device
        self.do_program_running_update(prog=prog)

        self._update_widget_classes()

    def on_unreal_started(self):
        if self.is_recording_device:
            sleep_time_in_ms = 1000
            self.osc_connection_timer.start(sleep_time_in_ms)

    def on_program_ended(self, message):
        try:
            process = message['process']
        except KeyError:
            LOGGER.warning(
                "Received 'on_program_ended' but no 'process' in the message")
            return

        if process['caller'] != self.name:
            return

        puuid = uuid.UUID(process['uuid'])
        returncode = message['returncode']

        def get_stdout_str() -> str:
            b64bytes = base64.b64decode(message['stdoutB64'])
            return b64bytes.decode()

        LOGGER.info(
            f"{self.name}: Program with id {puuid} exited with "
            f"returncode {returncode}")

        self._update_widget_classes()

        try:
            program_name = self.program_start_queue.on_program_ended(
                puuid=puuid, unreal_client=self.unreal_client)
        except KeyError:
            LOGGER.error(
                f"{self.name}: on_program_ended with unknown id {puuid}")
            return

        # Check if there are remaining programs named the same but with
        # different ids, which is not normal.
        remaining_homonyms = self.program_start_queue.running_puuids_named(
            program_name)
        for prog_id in remaining_homonyms:
            LOGGER.warning(
                f'{self.name}: But ({prog_id}) with the same name '
                f'"{program_name}" is still in the list, which is unusual')

        if program_name == 'unreal' and not len(remaining_homonyms):
            self.start_retrieve_log(unreal_exit_code=returncode)

        elif program_name == 'retrieve_log':
            self.status = DeviceStatus.CLOSED

        elif program_name == 'sync':
            if returncode == 0:
                LOGGER.info(f"{self.name}: Sync successful")
            else:
                LOGGER.error(f"{self.name}: Sync failed!")
                for line in get_stdout_str().splitlines():
                    LOGGER.error(f"{self.name}: {line}")

                # flag the inflight cl as invalid
                self.inflight_engine_cl = None
                self.inflight_project_cl = None

                # notify of the failure
                self.device_qt_handler.signal_device_sync_failed.emit(self)

            # If you build and sync the engine, update its CL
            if CONFIG.BUILD_ENGINE.get_value():
                self.engine_changelist = (
                    self.inflight_engine_cl
                    if self.inflight_engine_cl is not None
                    else self.engine_changelist)
                self.inflight_engine_cl = None

            # Update CL with the one in flight
            self.project_changelist = self.inflight_project_cl
            self.inflight_project_cl = None

            # refresh project CL
            self.project_changelist = self.project_changelist

            self.status = DeviceStatus.CLOSED

        elif program_name.startswith('build_'):
            if returncode == 0:
                LOGGER.info(f"{self.name}: {program_name} successful!")
            else:
                LOGGER.error(f"{self.name}: {program_name} failed!")

                # MSVC build tools error codes, e.g. 'error C4430' or
                # 'error LNK1104'.
                error_pattern = re.compile(r"error [A-Z]{1,3}[0-9]{4}")
                for line in get_stdout_str().splitlines():
                    if error_pattern.search(line):
                        LOGGER.error(f"{self.name}: {line}")

            if 'build_project' == program_name:
                self.status = DeviceStatus.CLOSED
                # Forces an update to the changelist field (to hide the
                # Building state).
                self.project_changelist = self.project_changelist
                if CONFIG.BUILD_ENGINE.get_value():
                    self.engine_changelist = self.engine_changelist

        elif "cstat" in program_name:
            output = get_stdout_str()
            changelists = [line.strip() for line in output.split()]

            try:
                current_changelist = str(int(changelists[-1]))

                # when not connected to p4, you get a message similar to:
                #  "Perforce client error: Connect to server failed;
                #   check $P4PORT. TCP connect to perforce:1666 failed. No
                #   such host is known."
                if 'Perforce client error:' in output:
                    raise ValueError

            except (ValueError, IndexError):
                LOGGER.error(
                    f"{self.name}: Could not retrieve changelists for "
                    "project. Are the Source Control Settings correctly "
                    "configured?")
                return

            if program_name.endswith("project"):
                project_name = os.path.basename(
                    os.path.dirname(CONFIG.UPROJECT_PATH.get_value(self.name)))
                LOGGER.info(
                    f"{self.name}: Project {project_name} "
                    f"is on revision {current_changelist}")
                self.project_changelist = current_changelist
            elif program_name.endswith("engine"):
                project_name = os.path.basename(
                    os.path.dirname(CONFIG.UPROJECT_PATH.get_value(self.name)))
                LOGGER.info(
                    f"{self.name}: Engine used for project "
                    f"{project_name} is on revision {current_changelist}")
                self.engine_changelist = current_changelist

    def on_program_killed(self, message):
        '''
        Handler of killed program. Expect on_program_ended for anything other
        than a fail.
        '''
        self._update_widget_classes()

        if not message['bAck']:
            # remove from list of puuids (if it exists)
            puuid = uuid.UUID(message['puuid'])
            self.program_start_queue.on_program_ended(
                puuid=puuid, unreal_client=self.unreal_client)

            LOGGER.error(
                f"{self.name} Unable to close program with id {str(puuid)}. "
                f"Error was '{message['error']}''")

    def on_file_received(self, source_path, content):
        if source_path.endswith(
                DeviceUnreal.csettings["roles_filename"].get_value(self.name)):
            decoded_content = base64.b64decode(content).decode()
            tags = parse_unreal_tag_file(decoded_content.splitlines())
            self.setting_roles.possible_values = tags
            LOGGER.info(f"{self.name}: All possible roles: {tags}")
            unsupported_roles = [
                role for role in self.setting_roles.get_value()
                if role not in tags]
            if len(unsupported_roles) > 0:
                LOGGER.error(
                    f"{self.name}: Found unsupported roles: "
                    f"{unsupported_roles}")
                LOGGER.error(
                    f"{self.name}: Please change the roles for this device in "
                    "the settings or in the unreal project settings!")

    def on_file_receive_failed(self, source_path, error):
        roles = self.setting_roles.get_value()
        if len(roles) > 0:
            LOGGER.error(
                f"{self.name}: Error receiving role file from listener and "
                f"device claims to have these roles: {' | '.join(roles)}")
            LOGGER.error(f"Error: {error}")

    def on_listener_programstdout(self, message):
        ''' Handles updates to stdout of programs
        Particularly useful to update build progress
        '''
        process = message['process']

        if process['caller'] != self.name:
            return

        stdoutbytes = base64.b64decode(message['partialStdoutB64'])
        stdout = stdoutbytes.decode()
        lines = list(filter(None, stdout.splitlines()))

        # see if this is an update to the build
        #
        # example lines:
        #
        #    @progress push 5%
        #    @progress 'Generating code...' 0%
        #    @progress 'Generating code...' 67%
        #    @progress 'Generating code...' 100%
        #    @progress pop
        #
        if process['name'].startswith('build_'):
            for line in lines:
                if '@progress' in line:
                    stepparts = line.split("'")

                    if len(stepparts) < 2:
                        break

                    step = stepparts[-2].strip()

                    percent = line.split(' ')[-1].strip()

                    if '%' == percent[-1]:
                        self.device_qt_handler.signal_device_build_update.emit(
                            self, step, percent)

        elif process['name'] == 'sync':
            for line in lines:
                if 'Progress:' in line:
                    match = re.search(r'Progress: (\d{1,3}\.\d\d%)', line)
                    if match:
                        sync_progress = match.group(1)
                        self.device_qt_handler.signal_device_sync_update.emit(
                            self, sync_progress)

        for line in lines:
            LOGGER.debug(f"{self.name} {process['name']}: {line}")

    def on_listener_state(self, message):
        '''
        Message expected to be received upon connection with the listener.

        It contains the state of the listener. Particularly useful when
        Switchboard reconnects.
        '''
        self.program_start_queue.clear_running_programs()

        server_version = version_helpers.listener_ver_from_state_message(
            message)
        if not server_version:
            LOGGER.error('Unable to parse listener version. Disconnecting...')
            self.disconnect_listener()
            return

        redeploy_version = self.listener_watcher.listener_ver

        # If incompatible or newer patch release available, prompt to redeploy.
        if not version_helpers.listener_is_compatible(server_version):
            self._queue_notify_redeploy()
        elif (redeploy_version is not None and
                server_version < redeploy_version):
            self._queue_notify_redeploy()

        if not version_helpers.listener_is_compatible(server_version):
            server_version_str = version_helpers.version_str(server_version)
            compat_version_str = version_helpers.version_str(
                version_helpers.LISTENER_COMPATIBLE_VERSION)
            LOGGER.error(
                f"{self.name}: Listener version {server_version_str} not "
                f"compatible ({compat_version_str}.x required)")
            self.disconnect_listener()
            return

        self.os_version_label = message.get('osVersionLabel', '')
        self.os_version_label_sub = message.get('osVersionLabelSub', '')
        self.os_version_number = message.get('osVersionNumber', '')
        self.total_phys_mem = message.get('totalPhysicalMemory', 0)

        # update list of running processes
        for process in message['runningProcesses']:
            prog = ProgramStartQueueItem.from_listener_process(process)

            if process['caller'] == self.name:
                LOGGER.warning(
                    f"{self.name} already running {prog.name} {prog.puuid}")
                self.do_program_running_update(prog=prog)

        # request roles and changelists
        self._request_roles_file()
        self._request_project_changelist_number()

        if CONFIG.BUILD_ENGINE.get_value():
            self._request_engine_changelist_number()

    def transport_paths(self, device_recording):
        '''
        Do not transport UE4 paths as they will be checked into source control.
        '''
        return []

    @property
    def log_filename(self):
        return f'{self.name}.log'

    @classmethod
    def get_log_download_dir(cls) -> Optional[pathlib.Path]:
        log_download_dir_setting = \
            DeviceUnreal.csettings['log_download_dir'].get_value().strip()

        if log_download_dir_setting:
            log_download_dir = pathlib.Path(log_download_dir_setting)
            if log_download_dir.is_dir() and log_download_dir.is_absolute():
                return log_download_dir
            else:
                LOGGER.error(
                    f'Invalid log download dir: {log_download_dir_setting}')
        else:
            local_project_dir = pathlib.Path(
                CONFIG.UPROJECT_PATH.get_value()).parent
            if local_project_dir.is_dir():
                log_download_dir = \
                    local_project_dir / 'Saved' / 'Logs' / 'Switchboard'
                log_download_dir.mkdir(parents=True, exist_ok=True)
                return log_download_dir

        return None

    def reregister_rsync_client(self):
        DeviceUnreal.rsync_server.unregister_client(self)
        DeviceUnreal.rsync_server.register_client(
            self, self.name, self.ip_address)

    def backup_downloaded_log(self):
        ''' Rotate existing log to a timestamped backup, ala Unreal. '''
        log_download_path = self.get_log_download_dir()
        if not log_download_path:
            return

        src = log_download_path / self.log_filename
        if not src.is_file():
            return

        modtime = datetime.fromtimestamp(src.stat().st_mtime)
        modtime_str = modtime.strftime('%Y.%m.%d-%H.%M.%S')
        dest_filename = f'{src.stem}-backup-{modtime_str}{src.suffix}'

        src.rename(log_download_path / dest_filename)

    def start_retrieve_log(self, unreal_exit_code: int):
        self.backup_downloaded_log()

        program_name = 'retrieve_log'

        rsync_path = pathlib.Path(
            CONFIG.ENGINE_DIR.get_value(self.name),
            'Extras', 'ThirdPartyNotUE', 'cwrsync', 'bin', 'rsync.exe')

        remote_project_path = \
            pathlib.Path(CONFIG.UPROJECT_PATH.get_value(self.name)).parent
        remote_log_path = \
            remote_project_path / 'Saved' / 'Logs' / self.log_filename
        remote_log_cygpath = \
            DeviceUnreal.rsync_server.make_cygdrive_path(remote_log_path)

        dest_endpoint = \
            f'{SETTINGS.IP_ADDRESS}:{DeviceUnreal.rsync_server.port}'
        dest_module = DeviceUnreal.rsync_server.INCOMING_LOGS_MODULE
        dest_path = f'rsync://{dest_endpoint}/{dest_module}/'

        rsync_args = f'{remote_log_cygpath} {dest_path}'

        puuid, msg = message_protocol.create_start_process_message(
            prog_path=str(rsync_path),
            prog_args=rsync_args,
            prog_name=program_name,
            caller=self.name
        )

        self.program_start_queue.add(
            ProgramStartQueueItem(
                name=program_name,
                puuid_dependency=None,
                puuid=puuid,
                msg_to_unreal_client=msg,
            ),
            unreal_client=self.unreal_client,
        )

        # if unreal_exit_code != 0:
        #     TODO: sync crash log?

    @property
    def log_transfer_in_progress(self) -> bool:
        return len(
            self.program_start_queue.running_puuids_named('retrieve_log')) > 0

    def get_widget_classes(self):
        widget_classes = list(self._widget_classes)

        widget_classes.append(f'status_{self.status.name.lower()}')

        if self.log_transfer_in_progress:
            widget_classes.append('download')

        return widget_classes

    @QtCore.Slot()
    def _update_widget_classes(self):
        # Ensure this code is run from the main thread
        if threading.current_thread() is not threading.main_thread():
            QtCore.QMetaObject.invokeMethod(
                self, '_update_widget_classes', QtCore.Qt.QueuedConnection)
            return

        classes = self.get_widget_classes()
        sb_widgets.set_qt_property(self.widget, 'widget_classes', classes,
                                   update_box_model=False,
                                   recursive_refresh=True)

    def add_widget_class(self, widget_class: str):
        if widget_class not in self._widget_classes:
            self._widget_classes.add(widget_class)
            self._update_widget_classes()

    def remove_widget_class(self, widget_class: str):
        if widget_class in self._widget_classes:
            self._widget_classes.remove(widget_class)
            self._update_widget_classes()


def parse_unreal_tag_file(file_content):
    tags = []
    for line in file_content:
        if line.startswith("GameplayTagList"):
            tag = line.split("Tag=")[1]
            tag = tag.split(',', 1)[0]
            tag = tag.strip('"')
            tags.append(tag)
    return tags


class DeviceWidgetUnreal(DeviceWidget):
    def __init__(self, name, device_hash, ip_address, icons, parent=None):
        super().__init__(name, device_hash, ip_address, icons, parent=parent)

        CONFIG.P4_ENABLED.signal_setting_changed.connect(
            lambda _, enabled: self.sync_button.setVisible(enabled))

    def _add_control_buttons(self):
        super()._add_control_buttons()

        changelist_layout = QtWidgets.QVBoxLayout()
        self.engine_changelist_label = QtWidgets.QLabel()
        self.engine_changelist_label.setObjectName('changelist')
        self.engine_changelist_label.setToolTip("Current Engine Changelist")
        changelist_layout.addWidget(self.engine_changelist_label)

        self.project_changelist_label = QtWidgets.QLabel()
        self.project_changelist_label.setObjectName('changelist')
        self.project_changelist_label.setToolTip("Current Project Changelist")
        changelist_layout.addWidget(self.project_changelist_label)

        spacer = QtWidgets.QSpacerItem(
            0, 20, QtWidgets.QSizePolicy.Expanding,
            QtWidgets.QSizePolicy.Minimum)

        self.layout.addLayout(changelist_layout)

        CONTROL_BUTTON_ICON_SIZE = QtCore.QSize(21, 21)

        self.sync_button = self.add_control_button(
            icon_size=CONTROL_BUTTON_ICON_SIZE,
            checkable=False,
            tool_tip='Sync changelist',
            hover_focus=False,
            name='sync')

        self.build_button = self.add_control_button(
            icon_size=CONTROL_BUTTON_ICON_SIZE,
            checkable=False,
            tool_tip='Build changelist',
            hover_focus=False,
            name='build')

        self.layout.addItem(spacer)

        self.open_button = self.add_control_button(
            icon_size=CONTROL_BUTTON_ICON_SIZE,
            tool_tip='Start Unreal',
            hover_focus=False,
            name='open')

        self.connect_button = self.add_control_button(
            icon_size=CONTROL_BUTTON_ICON_SIZE,
            tool_tip='Connect to listener',
            hover_focus=False,
            name='connect')

        self.sync_button.clicked.connect(self.sync_button_clicked)
        self.build_button.clicked.connect(self.build_button_clicked)
        self.connect_button.clicked.connect(self.connect_button_clicked)
        self.open_button.clicked.connect(self.open_button_clicked)

        # Disable UI when not connected
        self.open_button.setDisabled(True)
        self.sync_button.setDisabled(True)
        self.build_button.setDisabled(True)

        self.project_changelist_label.hide()
        self.engine_changelist_label.hide()
        self.sync_button.hide()

    def can_sync(self):
        return self.sync_button.isEnabled()

    def can_build(self):
        return self.build_button.isEnabled()

    def _open(self):
        # Make sure the button is in the correct state
        self.open_button.setChecked(True)
        # Emit Signal to Switchboard
        self.signal_device_widget_open.emit(self)

    def _close(self):
        # Make sure the button is in the correct state
        self.open_button.setChecked(False)
        # Emit Signal to Switchboard
        self.signal_device_widget_close.emit(self)

    def _connect(self):
        self._update_connected_ui()

        # Emit Signal to Switchboard
        self.signal_device_widget_connect.emit(self)

    def _disconnect(self):
        ''' Called when user disconnects '''
        self._update_disconnected_ui()

        # Emit Signal to Switchboard
        self.signal_device_widget_disconnect.emit(self)

    def _update_disconnected_ui(self):
        ''' Updates the UI of the device to reflect disconnected status '''
        # Make sure the button is in the correct state
        self.connect_button.setChecked(False)
        self.connect_button.setToolTip('Connect to listener')

        # Don't show the changelist
        self.project_changelist_label.hide()
        self.engine_changelist_label.hide()
        self.sync_button.hide()

        # Disable the buttons
        self.open_button.setDisabled(True)
        self.sync_button.setDisabled(True)
        self.build_button.setDisabled(True)

    def _update_connected_ui(self):
        ''' Updates the UI of the device to reflect connected status. '''
        # Make sure the button is in the correct state
        self.connect_button.setChecked(True)
        self.connect_button.setToolTip('Disconnect from listener')

        self.open_button.setDisabled(False)
        self.sync_button.setDisabled(False)
        self.build_button.setDisabled(False)

    def update_status(self, status, previous_status):
        super().update_status(status, previous_status)

        if status <= DeviceStatus.CONNECTING:
            self._update_disconnected_ui()
        else:
            self._update_connected_ui()

        # The connect/disconnect button is enabled in all states except for
        # CONNECTING.
        self.connect_button.setDisabled(False)

        if status == DeviceStatus.CONNECTING:
            self.connect_button.setDisabled(True)
            self.connect_button.setToolTip('Connecting to listener...')
        elif status == DeviceStatus.CLOSED:
            self.open_button.setDisabled(False)
            self.open_button.setChecked(False)
            self.sync_button.setDisabled(False)
            self.build_button.setDisabled(False)
        elif status == DeviceStatus.CLOSING:
            self.open_button.setDisabled(True)
            self.open_button.setChecked(True)
            self.sync_button.setDisabled(True)
            self.build_button.setDisabled(True)
        elif status == DeviceStatus.SYNCING:
            self.open_button.setDisabled(True)
            self.sync_button.setDisabled(True)
            self.build_button.setDisabled(True)
            self.engine_changelist_label.hide()
            self.project_changelist_label.setText('Syncing...')
        elif status == DeviceStatus.BUILDING:
            self.open_button.setDisabled(True)
            self.sync_button.setDisabled(True)
            self.build_button.setDisabled(True)
            self.engine_changelist_label.hide()
            self.project_changelist_label.setText('Building...')
        elif status == DeviceStatus.OPEN:
            self.open_button.setDisabled(False)
            self.open_button.setChecked(True)
            self.sync_button.setDisabled(True)
            self.build_button.setDisabled(True)
        elif status == DeviceStatus.READY:
            self.open_button.setDisabled(False)
            self.open_button.setChecked(True)
            self.sync_button.setDisabled(True)
            self.build_button.setDisabled(True)

        if self.open_button.isChecked():
            if self.open_button.isEnabled():
                self.open_button.setToolTip('Stop Unreal')
            else:
                self.open_button.setToolTip('Stopping Unreal...')
        else:
            self.open_button.setToolTip('Start Unreal')

    def update_project_changelist(self, value):
        self.project_changelist_label.setText(f'P: {value}')
        self.project_changelist_label.setToolTip('Project CL')

        self.project_changelist_label.show()
        self.sync_button.show()
        self.build_button.show()

    def update_build_status(self, device, step, percent):
        self.project_changelist_label.setText(f'Building...{percent}')
        self.project_changelist_label.setToolTip(step)

        self.project_changelist_label.show()

    def update_sync_status(self, device, percent):
        self.project_changelist_label.setText(f'Syncing...{percent}')
        self.project_changelist_label.setToolTip(
            'Syncing from Version Control')

        self.project_changelist_label.show()

    def update_engine_changelist(self, value):
        self.engine_changelist_label.setText(f'E: {value}')
        self.engine_changelist_label.show()

        self.sync_button.show()
        self.build_button.show()

    def project_changelist_display_warning(self, b):
        sb_widgets.set_qt_property(self.project_changelist_label, 'error', b)

    def engine_changelist_display_warning(self, b):
        sb_widgets.set_qt_property(self.engine_changelist_label, 'error', b)

    def sync_button_clicked(self):
        self.signal_device_widget_sync.emit(self)

    def build_button_clicked(self):
        self.signal_device_widget_build.emit(self)

    def open_button_clicked(self):
        if self.open_button.isChecked():
            self._open()
        else:
            self._close()

    def connect_button_clicked(self):
        if self.connect_button.isChecked():
            self._connect()
        else:
            self._disconnect()
