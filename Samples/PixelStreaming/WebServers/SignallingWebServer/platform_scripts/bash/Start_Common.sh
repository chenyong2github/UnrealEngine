#!/bin/bash
# Copyright Epic Games, Inc. All Rights Reserved.

function print_usage {
 echo "
 Usage:
  ${0} [--help] [--publicip <IP Address>] [--turn <turn server>] [--stun <stun server>] [cirrus options...]
 Where:
  --help will print this message and stop this script.
  --publicip is used to define public ip address (using default port) for turn server, syntax: --publicip ; it is used for 
    default value: Retrieved from 'curl https://api.ipify.org' or if unsuccessful then set to  127.0.0.1.  It is the IP address of the Cirrus server and the default IP address of the TURN server
  --turn defines what TURN server to be used, syntax: --turn 127.0.0.1:19303
    default value: as above, IP address downloaded from https://api.ipify.org; in case if download failure it is set to 127.0.0.1
  --stun defined what STUN server to be used, syntax: --stun stun.l.google.com:19302
    default value as above
  Other options: stored and passed to the Cirrus server.  All parameters printed once the script values are set.
  Command line options might be omitted to run with defaults and it is a good practice to omit specific ones when just starting the TURN or the STUN server alone, not the whole set of scripts.
 "
 exit 1
}

function print_parameters {
 echo ""
 echo "${0} is running with the following parameters:"
 echo "--------------------------------------"
 if [[ -n "${stunserver}" ]]; then echo "STUN server       : ${stunserver}" ; fi
 if [[ -n "${turnserver}" ]]; then echo "TURN server       : ${turnserver}" ; fi
 echo "Public IP address : ${publicip}"
 echo "Cirrus server command line arguments: ${cirruscmd}"
 echo ""
}

function set_start_default_values {
 # publicip and cirruscmd are always needed
 publicip=$(curl -s https://api.ipify.org > /dev/null)
 if [[ -z $publicip ]]; then
  publicip="127.0.0.1"
 fi
 cirruscmd=""

 if [ "$1" = "y" ]; then
  turnserver="${publicip}:19303"
 fi

 if [ "$2" = "y" ]; then
  stunserver="stun.l.google.com:19302"
 fi
}

function use_args {
 while(($#)) ; do
  case "$1" in
   --stun ) stunserver="$2"; shift 2;;
   --turn ) turnserver="$2"; shift 2;;
   --publicip ) publicip="$2"; turnserver="${publicip}:19303"; shift 2;;
   --help ) print_usage;;
   * ) echo "Unknown command, adding to cirrus command line: $1"; cirruscmd+=" $1"; shift;;
  esac
 done
}

function call_setup_sh {
 bash "setup.sh"
}
