#!/bin/bash
set -e

# First start p4d and wait for it to finish starting
p4d &
sleep 3

# Add P4 Users
cd p4users
./add_perforce_users.sh
cd ..

# Add base P4 depots
cd p4depots
./add_perforce_depots.sh
cd ..

# Continue running perforce server -- tail the log
tail -f $P4LOGS