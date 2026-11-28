# Pretend installs: the right file names, empty contents. No game data is used or kept.
G="$ASSETS/games"
f() { mkdir -p "$(dirname "$1")"; : > "$1"; }
f "$G/Doom 3/Doom3.exe"
f "$G/Doom 3/base/PAK000.pk4"
f "$G/Doom 3/base/pak008.pk4"
f "$G/Doom 3/d3xp/pak000.pk4"
f "$G/DOOM 3 BFG Edition/base/_common.resources"
f "$G/Quake 4/Quake4.exe"
f "$G/Quake 4/q4base/pak001.pk4"
f "$G/Prey/PREYDed.exe"
f "$G/Prey/base/pak000.pk4"
f "$G/Prey/base/pak040.pk4"
f "$G/Wolfenstein/SP/base/assets.pk4"
f "$G/Notes/readme.txt"
f "$ASSETS/nothing/readme.txt"
# picking the data folder instead of the install still finds the game
f "$ASSETS/solo/q4/q4base/pak001.pk4"
# a Steam install whose library list points at a second library
mkdir -p "$ASSETS/steam/steamapps/common" "$ASSETS/lib2/steamapps/common"
cat > "$ASSETS/steam/steamapps/libraryfolders.vdf" <<VDF
"libraryfolders"
{
	"0"
	{
		"path"		"$ASSETS/steam"
	}
	"1"
	{
		"path"		"$ASSETS/lib2"
		"apps" { "9050" "123" }
	}
}
VDF
f "$ASSETS/lib2/steamapps/common/Doom 3/Doom3.exe"
f "$ASSETS/lib2/steamapps/common/Doom 3/base/pak000.pk4"
