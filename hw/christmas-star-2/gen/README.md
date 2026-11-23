# Schematic generators (hardware 2.0)

The first version of the new sheets was written by these scripts, from KiCad's
own symbol libraries, and checked with `kicad-cli sch erc`. Parts connect
through net labels and power symbols placed on the pin ends.

Once a sheet has been edited in KiCad, **KiCad is the master**: running its
script again would overwrite the edits.

```sh
python gen/power_sheet.py power.kicad_sch <root uuid> <power sheet uuid>
kicad-cli sch erc christmas-star-2.kicad_sch
```
