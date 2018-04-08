# batstat
Battery status logger and monitor.
Logs information about capacity, health, charge, voltage, temperature...
Uses a sqlite3 database.
## Requirements
- mounted /sys
- sqlite3 (for compilation)
## Planned features
- No heavier than it has to be
- Dump db to other formats
	- json
	- text file
- Does not rely on systemd
- man page
- packaging for some popular distros
- R scripts for analysis (and to draw nice plots)
## License
batstat is licensed under the MIT License
