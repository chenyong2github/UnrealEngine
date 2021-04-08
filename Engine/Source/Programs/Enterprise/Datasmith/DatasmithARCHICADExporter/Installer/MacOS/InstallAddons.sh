#!/bin/bash
# Copyright Epic Games, Inc. All Rights Reserved.

# $1 is the ARCHICAD version number

ACVERS=$1
ADDONNAME="DatasmithARCHICAD${ACVERS}Exporter.bundle"

echo "--- Installing $ADDONNAME ---"

echo "--- Collect ARCHICAD preferences files ---"

unset PLISTS NBPLISTS
while IFS= read -r -d $'\0' file; do
	PLISTS[NBPLISTS++]="$file"
done < <(find "$HOME/Library/Preferences" -name "com.graphisoft.AC $ACVERS*.plist" -print0)

if [ $NBPLISTS ]
then
	for ((i = 0; i <= $NBPLISTS-1; i++)); do
  		echo "    ${PLISTS[$i]}"
	done
else
	echo "--- No AC plist file found ---"
	exit 1
fi

echo "--- Collect ARCHICAD application path ---"
unset ACAPPS NBACAPPS
for ((i = 0; i <= $NBPLISTS-1; i++)); do
	unset ACFOLDERPATH
	ACFOLDERPATH=$(/usr/libexec/PlistBuddy  -c "Print :'Last Started Path'" "${PLISTS[$i]}") || echo "No path in ${PLISTS[$i]}"
	if [ "$ACFOLDERPATH" ]
	then
		ACAPPS[$NBACAPPS]="$ACFOLDERPATH"
		let NBACAPPS=NBACAPPS+1
    else
        unset EMPTY_AC_KEY
        EMPTY_AC_KEY=$(plutil -convert xml1 "${PLISTS[$i]}" -o - | grep -A1 '<key></key>') || echo "No empty key in ${PLISTS[$i]}"
        ACFOLDERPATH=$(expr "$EMPTY_AC_KEY" : '.*<string>\(.*\)</string>')
        if [ "$ACFOLDERPATH" ]
        then
            echo "Use Install Path \"$ACFOLDERPATH\""
            ACAPPS[$NBACAPPS]="$ACFOLDERPATH"
            let NBACAPPS=NBACAPPS+1
        fi
	fi
done

if [ $NBACAPPS ]
then
	for ((i = 0; i <= $NBACAPPS-1; i++)); do
  		echo "    ${ACAPPS[$i]}"
	done
else
	echo "--- Can't get ARCHICAD application path from plist ---"
	exit 1
fi

echo "--- Validate ARCHICAD application path ---"
unset ACPATHS NBACPATHS
for ((i = 0; i <= $NBACAPPS-1; i++)); do
	if [ -d "${ACAPPS[$i]}" ]
	then	
		FOLDER="$( cd "$( dirname "${ACAPPS[$i]}" )" && pwd )"
		ACPATHS[$NBACPATHS]="$FOLDER"
		echo "    ${ACPATHS[$NBACPATHS]}"
		let NBACPATHS=NBACPATHS+1
	fi
done

if [ $NBACPATHS ]
then
	echo "    ARCHICAD application found: $NBACPATHS"
else
	echo "--- Can't get ARCHICAD folder ---"
	exit 1
fi

echo "--- Install add-on for all ARCHICAD ---"
unset ADDONSPATHS NBADDONSPATHS
for ((i = 0; i <= $NBACPATHS-1; i++)); do
	if [ -d "${ACPATHS[$i]}" ]
	then
        # Get path to "Collada In-Out" add-on
        if [ $ACVERS -gt 22 ]
        then
            ADDONSPATHS[$NBADDONSPATHS]="$(find "${ACPATHS[$i]}" -type d -maxdepth 3 -name "Collada In-Out.bundle" )"
        else
            ADDONSPATHS[$NBADDONSPATHS]="$(find "${ACPATHS[$i]}" -type d -maxdepth 3 -name "Collada In-Out" )"
        fi
		if [ -d "${ADDONSPATHS[$NBADDONSPATHS]}" ]
		then
            # Path to Import-Export folder
            ADDONSPATHS[$NBADDONSPATHS]="`dirname "${ADDONSPATHS[$NBADDONSPATHS]}"`"

            # Add-on bundle path to install
 			TMP="${ADDONSPATHS[$NBADDONSPATHS]}/$ADDONNAME"
            echo "    --- Installing: ${TMP} ---"

            # if add-on was already installed, we remove-it
			if [ -f "${TMP}" ]
			then
                echo sudo rm -f "${TMP}"
				sudo rm -f "${TMP}"
			else
				if [ -d "${TMP}" ]
				then
					echo "        Directory Exist: ${TMP}"
					sudo rm -Rf "${TMP}"
				fi
			fi

            sudo cp -pPR "/var/tmp/$ADDONNAME" "${ADDONSPATHS[$NBADDONSPATHS]}"
            sudo chown -R root:wheel "${ADDONSPATHS[$NBADDONSPATHS]}"
			sudo chmod -R 777 "${ADDONSPATHS[$NBADDONSPATHS]}"
            echo "        Installation done"
			let NBADDONSPATHS=NBADDONSPATHS+1
		fi
	fi
done

# Remove tmp add-on
echo "--- Clean up: /var/tmp/$ADDONNAME ---"
sudo rm -Rf "/var/tmp/$ADDONNAME"

# Report installed add-on
if [ $NBADDONSPATHS ]
then
	for ((i = 0; i <= $NBADDONSPATHS-1; i++)); do
  		echo "--- ARCHICAD Addon Folder found: ${ADDONSPATHS[$i]} ---"
	done
else
	echo "--- Could not find ARCHICAD Addon Folder ---"
	exit 1
fi

# Success
exit 0
