# A stand-in DOOM 3 install: a pak with one level and one material written here. No game data is used.
G="$ASSETS/games/Doom 3"
mkdir -p "$G/base"
: > "$G/Doom3.exe"
python3 - "$G/base/pak000.pk4" "$REPO/content/maps/testmaps/test_room.map" <<'PY'
import sys, zipfile
with zipfile.ZipFile(sys.argv[1], "w") as z:
    z.write(sys.argv[2], "maps/game/stand_in.map")
    z.writestr("materials/stand_in.mtr", "textures/stand_in/wall\n{\n\tdiffusemap\t_white\n}\n")
PY
