# Schematic generators (hardware 2.0)

The first version of the new sheets was written by these scripts, from KiCad's
own symbol libraries, and checked with `kicad-cli sch erc`. Parts connect
through net labels and power symbols placed on the pin ends.

Once a sheet has been edited in KiCad, **KiCad is the master**: running its
script again would overwrite the edits.

```sh
python gen/build_esp.py .                       # ESP32-C6-WROOM-1 symbol + footprint
python gen/power_sheet.py power.kicad_sch <root uuid> <power sheet uuid>
python gen/module_sheet.py module.kicad_sch <root uuid> <module sheet uuid>
kicad-cli sch erc christmas-star-2.kicad_sch
```
